#include "routes/FileService.h"
#include "core/TransferTracker.h"
#include <algorithm>
#include <chrono>

namespace fs = std::filesystem;

namespace routes::fileService {

struct SharePerms {
    bool canAccess = false;
    bool canDownload = false;
    bool canUpload = false;
    bool canDelete = false;
    bool canRename = false;
};

SharePerms computeEffectivePerms( const std::string &shareId,
                                  const std::optional<core::auth::UserInfo> &user ) {
    SharePerms p;
    auto rows = App::getInstance()->getDatabase().query(
        "SELECT subject_type,user_id,can_access,can_download,can_upload,can_delete,can_rename "
        "FROM file_share_permissions WHERE share_id='" +
        Database::sqlEscape( shareId ) + "'" );
    for ( auto &r : rows ) {
        std::string st = r["subject_type"];
        bool applies = false;
        if ( st == "anonymous" )
            applies = true;
        else if ( st == "all" && user.has_value() )
            applies = true;
        else if ( st == "user" && user.has_value() && r["user_id"] == user->id )
            applies = true;
        if ( applies ) {
            p.canAccess |= r["can_access"] == "1";
            p.canDownload |= r["can_download"] == "1";
            p.canUpload |= r["can_upload"] == "1";
            p.canDelete |= r["can_delete"] == "1";
            p.canRename |= r.count( "can_rename" ) > 0 && r["can_rename"] == "1";
        }
    }
    return p;
}

Server::json shareToUserJson( const Database::Row &r, const SharePerms &perms ) {
    return {
        { "id", r.at( "id" ) }, { "name", r.at( "name" ) }, { "isDirectory", r.at( "is_directory" ) == "1" }, { "canAccess", perms.canAccess }, { "canDownload", perms.canDownload }, { "canUpload", perms.canUpload }, { "canDelete", perms.canDelete }, { "canRename", perms.canRename } };
}

void fileServiceList( const httplib::Request &req, httplib::Response &res ) {
    auto user = core::auth::getUserFromRequest( req );
    auto rows = App::getInstance()->getDatabase().query( "SELECT * FROM file_shares ORDER BY created_at DESC" );
    Server::json arr = Server::json::array();
    for ( auto &r : rows ) {
        auto perms = computeEffectivePerms( r["id"], user );
        if ( perms.canAccess )
            arr.push_back( shareToUserJson( r, perms ) );
    }
    LOG_DEBUG << "用户可见共享数量: " << arr.size();
    Server::sendJson( res, { { "success", true }, { "shares", arr } } );
}

void fileServiceListDir( const httplib::Request &req, httplib::Response &res ) {
    std::string id = req.matches[1];
    LOG_DEBUG << "文件服务列表目录 shareId=" << id;
    auto rows = App::getInstance()->getDatabase().query( "SELECT * FROM file_shares WHERE id='" + Database::sqlEscape( id ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "共享不存在", 404 );

    auto user = core::auth::getUserFromRequest( req );
    auto perms = computeEffectivePerms( id, user );
    if ( !perms.canAccess )
        return Server::sendError( res, "无访问权限", 403 );

    if ( rows[0]["is_directory"] != "1" )
        return Server::sendError( res, "该共享不是目录", 400 );

    std::string basePath = rows[0]["real_path"];
    std::string relPath = Server::queryParam( req, "path" );
    bool showHidden = Server::queryParam( req, "showHidden" ) == "1" || Server::queryParam( req, "showHidden" ) == "true";
    std::string full = utils::fs::safeJoin( basePath, relPath );
    if ( full.empty() )
        return Server::sendError( res, "非法路径", 400 );

    std::error_code ec;
    if ( !fs::exists( full, ec ) || !fs::is_directory( full, ec ) )
        return Server::sendError( res, "目录不存在", 404 );

    std::string err;
    auto items = utils::fs::listDir( full, &err, showHidden );
    Server::json entries = Server::json::array();
    for ( auto &e : items ) {
        entries.push_back( { { "name", e.name }, { "isDir", e.isDir }, { "size", e.size }, { "modified", e.modified } } );
    }
    Server::json resp = { { "success", true }, { "path", relPath }, { "showHidden", showHidden }, { "canDownload", perms.canDownload }, { "canUpload", perms.canUpload }, { "canDelete", perms.canDelete }, { "canRename", perms.canRename }, { "entries", entries } };
    LOG_DEBUG << "目录列表返回项数: " << entries.size() << " 路径=" << full << " showHidden=" << showHidden;
    Server::sendJson( res, resp );
}

void fileServiceSearch( const httplib::Request &req, httplib::Response &res ) {
    std::string id = req.matches[1];
    LOG_DEBUG << "文件服务搜索 shareId=" << id;
    auto rows = App::getInstance()->getDatabase().query( "SELECT * FROM file_shares WHERE id='" + Database::sqlEscape( id ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "共享不存在", 404 );

    auto user = core::auth::getUserFromRequest( req );
    auto perms = computeEffectivePerms( id, user );
    if ( !perms.canAccess )
        return Server::sendError( res, "无访问权限", 403 );

    if ( rows[0]["is_directory"] != "1" )
        return Server::sendError( res, "该共享不是目录", 400 );

    std::string basePath = rows[0]["real_path"];
    std::string keyword = Server::queryParam( req, "q" );
    bool showHidden = Server::queryParam( req, "showHidden" ) == "1" || Server::queryParam( req, "showHidden" ) == "true";
    if ( keyword.empty() )
        return Server::sendError( res, "搜索关键词不能为空", 400 );

    // 转换为小写用于不区分大小写匹配
    std::string keywordLower = keyword;
    std::transform( keywordLower.begin(), keywordLower.end(), keywordLower.begin(), ::tolower );

    std::error_code ec;
    if ( !fs::exists( basePath, ec ) || !fs::is_directory( basePath, ec ) )
        return Server::sendError( res, "目录不存在", 404 );

    Server::json results = Server::json::array();
    int maxResults = 100;

    // 递归遍历目录
    for ( auto it = fs::recursive_directory_iterator( basePath, fs::directory_options::skip_permission_denied, ec );
          it != fs::recursive_directory_iterator(); it.increment( ec ) ) {
        if ( ec ) {
            ec.clear();
            continue;
        }

        const auto &entry = *it;
        std::string entryName = entry.path().filename().string();
        std::string entryNameLower = entryName;
        std::transform( entryNameLower.begin(), entryNameLower.end(), entryNameLower.begin(), ::tolower );

        if ( !showHidden && utils::fs::isHiddenEntry( entry.path().string(), entryName ) )
            continue;

        if ( entryNameLower.find( keywordLower ) != std::string::npos ) {
            // 计算相对路径
            std::string relPath = fs::relative( entry.path(), basePath ).string();
            // 统一使用正斜杠
            std::replace( relPath.begin(), relPath.end(), '\\', '/' );

            std::error_code sizeEc;
            int64_t size = 0;
            if ( entry.is_regular_file( sizeEc ) ) {
                size = entry.file_size( sizeEc );
            }

            std::error_code timeEc;
            auto ftime = entry.last_write_time( timeEc );
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - decltype( ftime )::clock::now() + std::chrono::system_clock::now() );
            auto modified = std::chrono::system_clock::to_time_t( sctp );

            results.push_back( { { "name", entryName },
                                 { "path", relPath },
                                 { "isDir", entry.is_directory() },
                                 { "size", size },
                                 { "modified", utils::fs::fileTimeFormat( modified ) } } );

            if ( (int)results.size() >= maxResults )
                break;
        }
    }

    Server::json resp = {
        { "success", true },
        { "keyword", keyword },
        { "showHidden", showHidden },
        { "total", results.size() },
        { "results", results } };
    LOG_DEBUG << "搜索结果: " << results.size() << " 条, 关键词=" << keyword << " showHidden=" << showHidden;
    Server::sendJson( res, resp );
}

void fileServiceDownload( const httplib::Request &req, httplib::Response &res ) {
    std::string id = req.matches[1];
    LOG_DEBUG << "文件服务下载 shareId=" << id;
    auto rows = App::getInstance()->getDatabase().query( "SELECT * FROM file_shares WHERE id='" + Database::sqlEscape( id ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "共享不存在", 404 );

    auto user = core::auth::getUserFromRequest( req );
    auto perms = computeEffectivePerms( id, user );
    if ( !perms.canDownload )
        return Server::sendError( res, "无下载权限", 403 );

    std::string basePath = rows[0]["real_path"];
    std::string relPath = Server::queryParam( req, "path" );
    std::string full;

    if ( rows[0]["is_directory"] != "1" ) {
        full = basePath;
    } else {
        full = utils::fs::safeJoin( basePath, relPath );
        if ( full.empty() )
            return Server::sendError( res, "非法路径", 400 );
    }
    LOG_DEBUG << "请求下载文件: " << full;

    std::error_code ec;
    if ( !fs::exists( full, ec ) || !fs::is_regular_file( full, ec ) )
        return Server::sendError( res, "文件不存在", 404 );

    auto fsize = utils::fs::fileSize( full );
    if ( fsize < 0 )
        return Server::sendError( res, "无法获取文件大小", 500 );

    fs::path p( full );
    std::string fileName = p.filename().string();
    std::string mimeType = Server::contentType( fileName );
    std::string urlName = utils::urlEncode( fileName );

    int64_t contentLength = fsize;
    int64_t rangeStart = 0;
    if ( !req.ranges.empty() ) {
        auto &r = req.ranges[0];
        int64_t first = r.first;
        int64_t last = r.second;
        if ( first == -1 && last == -1 ) {
            first = 0;
            last = fsize - 1;
        } else if ( first == -1 ) {
            first = fsize - last;
            last = fsize - 1;
        } else if ( last == -1 || last >= fsize ) {
            last = fsize - 1;
        }
        rangeStart = first;
        contentLength = last - first + 1;
    }

    LOG_INFO << "文件下载"
             << "filename=" << fileName << " size=" << fsize
             << " bytes rangeStart=" << rangeStart
             << " contentLength=" << contentLength
             << " 远端=" << req.remote_addr;

    std::string file = utils::utf8ToLocal( full );
    FILE *fp = std::fopen( file.c_str(), "rb" );
    if ( !fp )
        return Server::sendError( res, "打开文件失败", 500 );

    res.set_header( "Content-Disposition", "attachment; filename=\"" + urlName + "\"" );
    res.set_header( "Accept-Ranges", "bytes" );

    // 注册到传输跟踪器
    std::string tid = TransferTracker::instance().start( "download", fileName, req.remote_addr, contentLength );
    res.set_content_provider(
        fsize, mimeType,
        [fp, tid, rangeStart]( size_t offset, size_t length, httplib::DataSink &sink ) -> bool {
            constexpr size_t CHUNK = 1024 * 1024;
            char buf[CHUNK];
            size_t toRead = std::min( CHUNK, length );
            if ( toRead == 0 ) {
                sink.done();
                return true;
            }
            // httplib 传入的 offset 已是相对于文件开头的绝对偏移，直接定位即可
            fseeko( fp, offset, SEEK_SET );
            size_t n = std::fread( buf, 1, toRead, fp );
            if ( n > 0 ) {
                sink.write( buf, n );
                TransferTracker::instance().update( tid, offset + n - rangeStart );
            } else {
                sink.done();
            }
            return true; },
        [fp, tid]( bool success ) {
            if ( fp )
                std::fclose( fp );
            TransferTracker::instance().end(
                tid, success ? "completed" : "error",
                success ? "" : "客户端中止" );
        } );
}

void fileServiceUpload( const httplib::Request &req, httplib::Response &res,
                        const httplib::ContentReader &content_reader ) {
    std::string id = req.matches[1];
    LOG_DEBUG << "文件服务上传 shareId=" << id;
    auto rows = App::getInstance()->getDatabase().query( "SELECT * FROM file_shares WHERE id='" + Database::sqlEscape( id ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "共享不存在", 404 );

    auto user = core::auth::getUserFromRequest( req );
    auto perms = computeEffectivePerms( id, user );
    if ( !perms.canUpload )
        return Server::sendError( res, "无上传权限", 403 );

    if ( rows[0]["is_directory"] != "1" )
        return Server::sendError( res, "该共享不是目录", 400 );

    if ( !req.is_multipart_form_data() )
        return Server::sendError( res, "需要 multipart/form-data 提交", 400 );

    // 客户端可传入单文件 size（字节），用于更准确的进度/剩余时间展示。
    // 适用于 Content-Length 不可用或不可信的场景（如大文件流式上传）。
    int64_t fileSize = 0;
    {
        auto sp = Server::queryParam( req, "size" );
        if ( !sp.empty() ) {
            try {
                fileSize = std::stoll( sp );
            } catch ( ... ) {
            }
        }
    }

    std::string basePath = rows[0]["real_path"];
    std::string relPath = Server::queryParam( req, "path" );
    std::string dirFull = utils::fs::safeJoin( basePath, relPath );
    if ( dirFull.empty() )
        return Server::sendError( res, "非法路径", 400 );

    std::error_code ec;
    if ( !fs::is_directory( dirFull, ec ) )
        return Server::sendError( res, "目标目录不存在", 404 );

    // 流式接收：每块直接 fwrite 到磁盘，避免 content 累积到内存
    struct FileState {
        std::string filename;
        fs::path outPath;
        FILE *fp = nullptr;
        int64_t written = 0;
        std::string transferId;
        std::shared_ptr<bool> ended = std::make_shared<bool>( false );
    };
    FileState cur;
    std::string errMsg;
    int errStatus = 0;
    bool sawFile = false;

    auto closeAndCleanup = [&]() {
        if ( cur.fp ) {
            std::fclose( cur.fp );
            cur.fp = nullptr;
        }
    };
    auto endTransfer = [&cur]( const std::string &status, const std::string &err ) {
        if ( !cur.transferId.empty() && !*cur.ended ) {
            *cur.ended = true;
            TransferTracker::instance().end( cur.transferId, status, err );
        }
    };

    content_reader(
        [&]( const httplib::FormData &item ) -> bool {
            // 进入新一项：先关闭上一项（若是 file）
            closeAndCleanup();
            endTransfer( "completed", "" );
            if ( item.name != "file" || item.filename.empty() )
                return true; // 跳过非文件字段

            sawFile = true;
            cur.filename = item.filename;
            cur.outPath = fs::path( dirFull ) / cur.filename;
            if ( fs::exists( cur.outPath, ec ) ) {
                errMsg = "文件已存在";
                errStatus = 400;
                return false;
            }

            std::string file = utils::utf8ToLocal( cur.outPath.string() );
            cur.fp = std::fopen( file.c_str(), "wb" );
            if ( !cur.fp ) {
                errMsg = "打开输出文件失败";
                errStatus = 500;
                return false;
            }
            cur.written = 0;
            cur.ended = std::make_shared<bool>( false );
            // 注册到传输跟踪器；优先用客户端传入的文件大小作 total
            int64_t total = fileSize > 0 ? fileSize : 0;
            cur.transferId = TransferTracker::instance().start(
                "upload", cur.filename, req.remote_addr, total );
            return true;
        },
        [&]( const char *data, size_t data_length ) -> bool {
            if ( !cur.fp || errStatus != 0 )
                return true;
            cur.written += (int64_t)data_length;
            size_t n = std::fwrite( data, 1, data_length, cur.fp );
            if ( n != data_length ) {
                errMsg = "写入失败";
                errStatus = 500;
                std::fclose( cur.fp );
                cur.fp = nullptr;
                std::error_code rmec;
                fs::remove( cur.outPath, rmec );
                endTransfer( "error", errMsg );
                return false;
            }
            TransferTracker::instance().update( cur.transferId, cur.written );
            return true;
        } );

    // 收尾：关闭最后一项的句柄
    closeAndCleanup();
    if ( !sawFile )
        return Server::sendError( res, "未提供文件", 400 );

    auto fsize = utils::fs::fileSize( cur.outPath.string() );
    // 用实际文件大小做最终校正（覆盖 Content-Length 估计），保证 100% 进度
    if ( fsize > 0 && !cur.transferId.empty() ) {
        TransferTracker::instance().setTotal( cur.transferId, fsize );
        TransferTracker::instance().update( cur.transferId, fsize );
    }

    if ( errStatus != 0 ) {
        endTransfer( "error", errMsg );
        return Server::sendError( res, errMsg, errStatus );
    }
    endTransfer( "completed", "" );

    auto fmodified = utils::fs::fileModifiedTime( cur.outPath.string() );
    auto finfo = Server::json::object( { { "name", cur.filename },
                                         { "size", fsize },
                                         { "modified", fmodified },
                                         { "isDir", false } } );

    LOG_INFO << "文件上传成功（流式） filename=" << cur.filename
             << " size=" << fsize
             << " bytes 保存路径=" << cur.outPath.string();
    Server::sendJson( res, { { "success", true },
                             { "file", finfo } } );
}

void fileServiceDelete( const httplib::Request &req, httplib::Response &res ) {
    std::string id = req.matches[1];
    LOG_DEBUG << "文件服务删除 shareId=" << id;
    auto rows = App::getInstance()->getDatabase().query( "SELECT * FROM file_shares WHERE id='" + Database::sqlEscape( id ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "共享不存在", 404 );

    auto user = core::auth::getUserFromRequest( req );
    auto perms = computeEffectivePerms( id, user );
    if ( !perms.canDelete )
        return Server::sendError( res, "无删除权限", 403 );

    if ( rows[0]["is_directory"] != "1" )
        return Server::sendError( res, "该共享不是目录", 400 );

    std::string basePath = rows[0]["real_path"];
    std::string relPath = Server::queryParam( req, "path" );
    if ( relPath.empty() )
        return Server::sendError( res, "路径不能为空", 400 );

    std::string full = utils::fs::safeJoin( basePath, relPath );
    if ( full.empty() )
        return Server::sendError( res, "非法路径", 400 );
    LOG_DEBUG << "请求删除路径: " << full;

    std::error_code ec;
    if ( !fs::exists( full, ec ) )
        return Server::sendError( res, "文件不存在", 404 );

    if ( !utils::fs::remove( full ) )
        return Server::sendError( res, "删除失败", 500 );

    LOG_INFO << "文件删除成功 path=" << full;
    Server::sendJson( res, { { "success", true }, { "message", "已删除" } } );
}

void fileServiceRename( const httplib::Request &req, httplib::Response &res ) {
    std::string id = req.matches[1];
    LOG_DEBUG << "文件服务重命名 shareId=" << id;
    auto rows = App::getInstance()->getDatabase().query( "SELECT * FROM file_shares WHERE id='" + Database::sqlEscape( id ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "共享不存在", 404 );

    auto user = core::auth::getUserFromRequest( req );
    auto perms = computeEffectivePerms( id, user );
    if ( !perms.canRename )
        return Server::sendError( res, "无重命名权限", 403 );

    if ( rows[0]["is_directory"] != "1" )
        return Server::sendError( res, "该共享不是目录", 400 );

    auto body = Server::parseBody( req );
    if ( body.is_null() )
        return Server::sendError( res, "无效的请求", 400 );

    std::string oldPath = utils::jsonStringValue( body, "path" );
    std::string newName = utils::jsonStringValue( body, "newName" );
    if ( oldPath.empty() || newName.empty() )
        return Server::sendError( res, "路径和新名称不能为空", 400 );

    // 防止新名称包含路径分隔符
    if ( newName.find( '/' ) != std::string::npos || newName.find( '\\' ) != std::string::npos )
        return Server::sendError( res, "新名称不能包含路径分隔符", 400 );

    std::string basePath = rows[0]["real_path"];
    std::string oldFull = utils::fs::safeJoin( basePath, oldPath );
    if ( oldFull.empty() )
        return Server::sendError( res, "非法路径", 400 );

    // 计算新路径：替换最后一个路径组件为newName
    fs::path oldFsPath( oldFull );
    fs::path parentPath = oldFsPath.parent_path();
    fs::path newFull = parentPath / newName;
    std::string newFullStr = newFull.string();

    LOG_DEBUG << "重命名请求: " << oldFull << " -> " << newFullStr;

    std::error_code ec;
    if ( !fs::exists( oldFull, ec ) )
        return Server::sendError( res, "文件/目录不存在", 404 );
    if ( fs::exists( newFull, ec ) )
        return Server::sendError( res, "目标名称已存在", 409 );

    fs::rename( oldFull, newFull, ec );
    if ( ec ) {
        LOG_ERROR << "重命名失败: " << ec.message();
        return Server::sendError( res, "重命名失败: " + ec.message(), 500 );
    }

    // 计算返回的相对路径
    std::string newRelPath;
    if ( basePath.size() < newFullStr.size() ) {
        newRelPath = newFullStr.substr( basePath.size() );
        // 去掉开头的分隔符
        while ( !newRelPath.empty() && ( newRelPath[0] == '\\' || newRelPath[0] == '/' ) )
            newRelPath.erase( 0, 1 );
        std::replace( newRelPath.begin(), newRelPath.end(), '\\', '/' );
    }

    LOG_INFO << "重命名成功 " << oldFull << " -> " << newFullStr;
    Server::sendJson( res, { { "success", true }, { "newPath", newRelPath }, { "newName", newName } } );
}

void registerFileServiceRoutes( httplib::Server &svr ) {
    svr.Get( "/api/fileservice", fileServiceList );
    svr.Get( R"(/api/fileservice/([^/]+)/list)", fileServiceListDir );
    svr.Get( R"(/api/fileservice/([^/]+)/search)", fileServiceSearch );
    svr.Get( R"(/api/fileservice/([^/]+)/download)", fileServiceDownload );
    svr.Post( R"(/api/fileservice/([^/]+)/upload)", fileServiceUpload );
    svr.Delete( R"(/api/fileservice/([^/]+)/delete)", fileServiceDelete );
    svr.Put( R"(/api/fileservice/([^/]+)/rename)", fileServiceRename );
    LOG_DEBUG << "已注册 8 个文件服务路由";
}

} // namespace routes::fileService
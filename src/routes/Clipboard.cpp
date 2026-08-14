#include "routes/Clipboard.h"
#include "core/Server.h"

namespace routes::clipboard {

void registerClipboardRoutes( httplib::Server &svr ) {
    svr.Get( "/api/clipboard", clipboardList );
    svr.Post( "/api/clipboard", clipboardCreate );
    svr.Delete( R"(/api/clipboard/([^/]+))", clipboardDelete );

    svr.Post( "/api/clipboard/upload", fileUpload );
    svr.Post( R"(/api/clipboard/([^/]+)/download)", fileRequestDownload );
    svr.Get( R"(/api/clipboard/download/([^/]+))", fileDownload );
    LOG_DEBUG << "已注册 6 个剪贴板路由";
}

void clipboardList( const httplib::Request &req, httplib::Response &res ) {
    std::string teamId = Server::queryParam( req, "teamId" );
    std::string since = Server::queryParam( req, "since" );
    bool onlyCount = Server::queryParam( req, "onlyCount" ) == "true";
    if ( teamId.empty() )
        return Server::sendError( res, "团队ID不能为空", 400 );
    LOG_DEBUG << "剪贴板列表查询 teamId=" << teamId << " since=" << since << " onlyCount=" << onlyCount;

    if ( onlyCount ) {
        auto rows = App::getInstance()->getDatabase().query( "SELECT COUNT(*) AS c FROM clipboard_items WHERE team_id='" + Database::sqlEscape( teamId ) + "'" );
        int count = std::stoi( rows[0]["c"] );
        LOG_DEBUG << "剪贴板条目数 teamId=" << teamId << " count=" << count;
        Server::sendJson( res, { { "success", true }, { "count", count } } );
        return;
    }

    std::string sql =
        "SELECT ci.id,ci.team_id,ci.type,ci.content,ci.html_content,ci.mime_type, "
        "ci.file_url,ci.file_name,ci.file_size,ci.created_by_id,ci.created_at, "
        "u.email AS u_email,u.nickname AS u_nickname "
        "FROM clipboard_items ci LEFT JOIN users u ON ci.created_by_id=u.id "
        "WHERE ci.team_id='" +
        Database::sqlEscape( teamId ) + "'";
    if ( !since.empty() )
        sql += " AND ci.created_at>'" + Database::sqlEscape( since ) + "'";
    sql += " ORDER BY ci.created_at DESC";

    auto rows = App::getInstance()->getDatabase().query( sql );
    Server::json items = Server::json::array();
    for ( auto &r : rows )
        items.push_back( clipboardToJson( r ) );

    auto latest = App::getInstance()->getDatabase().query( "SELECT created_at FROM clipboard_items WHERE team_id='" + Database::sqlEscape( teamId ) +
                                                           "' ORDER BY created_at DESC LIMIT 1" );
    std::string latestTime = ( !latest.empty() && latest[0].count( "created_at" ) ) ? latest[0]["created_at"] : "";

    Server::sendJson( res, { { "success", true }, { "items", items }, { "hasMore", !since.empty() }, { "latestTime", latestTime.empty() ? Server::json( nullptr ) : Server::json( latestTime ) } } );
}

void clipboardCreate( const httplib::Request &req, httplib::Response &res ) {
    auto body = Server::parseBody( req );
    if ( body.is_null() )
        return Server::sendError( res, "无效的请求", 400 );
    std::string teamId = utils::jsonStringValue( body, "teamId" );
    std::string type = utils::jsonStringValue( body, "type" );
    if ( teamId.empty() || type.empty() ) {
        LOG_WARN << "剪贴板创建失败: teamId或type为空";
        return Server::sendError( res, "团队ID和内容类型不能为空", 400 );
    }
    LOG_DEBUG << "创建剪贴板 teamId=" << teamId << " type=" << type;

    auto teams = App::getInstance()->getDatabase().query( "SELECT is_default FROM clipboard_teams WHERE id='" + Database::sqlEscape( teamId ) + "'" );
    if ( teams.empty() )
        return Server::sendError( res, "团队不存在", 404 );

    bool isDefault = teams[0]["is_default"] == "1";
    auto user = core::auth::getUserFromRequest( req );
    std::string userId;
    if ( isDefault || user ) {
        userId = user ? user->id : core::auth::ensureAnonymousUser();
    } else {
        return Server::sendError( res, "请登录后操作", 401 );
    }

    std::string content = utils::jsonStringValue( body, "content" );
    std::string htmlContent = utils::jsonStringValue( body, "htmlContent" );
    std::string mimeType = utils::jsonStringValue( body, "mimeType" );
    std::string fileName = utils::jsonStringValue( body, "fileName" );
    std::string fileSizeStr = utils::jsonStringValue( body, "fileSize" );
    std::string fileUrl, fileSize;
    if ( type == "file" ) {
        fileUrl = content;
        fileName = utils::jsonStringValue( body, "fileName" );
        fileSize = fileSizeStr;
    }

    std::string id = utils::generateId();
    std::string now = utils::nowIso();
    App::getInstance()->getDatabase().execParams(
        "INSERT INTO clipboard_items(id,team_id,type,content,html_content,mime_type,"
        "file_url,file_name,file_size,created_by_id,created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)",
        { { 1, id }, { 2, teamId }, { 3, type }, { 4, content }, { 5, htmlContent }, { 6, mimeType }, { 7, fileUrl }, { 8, fileName }, { 9, fileSize }, { 10, userId }, { 11, now } } );

    auto rows = App::getInstance()->getDatabase().query(
        "SELECT ci.id,ci.team_id,ci.type,ci.content,ci.html_content,ci.mime_type, "
        "ci.file_url,ci.file_name,ci.file_size,ci.created_by_id,ci.created_at, "
        "u.email AS u_email,u.nickname AS u_nickname "
        "FROM clipboard_items ci LEFT JOIN users u ON ci.created_by_id=u.id "
        "WHERE ci.id='" +
        id + "'" );
    if ( rows.empty() ) {
        LOG_ERROR << "剪贴板创建后查询失败 id=" << id;
        return Server::sendError( res, "创建失败", 500 );
    }
    LOG_INFO << "剪贴板创建成功 id=" << id << " teamId=" << teamId << " type=" << type;
    Server::sendJson( res, { { "success", true }, { "item", clipboardToJson( rows[0] ) } } );
}

void clipboardDelete( const httplib::Request &req, httplib::Response &res ) {
    std::string id = req.matches[1];
    LOG_DEBUG << "删除剪贴板 id=" << id;
    auto user = core::auth::getUserFromRequest( req );

    auto rows = App::getInstance()->getDatabase().query(
        "SELECT ci.id,ci.team_id,ci.type,ci.file_url,ci.created_by_id,t.is_default "
        "FROM clipboard_items ci JOIN clipboard_teams t ON ci.team_id=t.id WHERE ci.id='" +
        Database::sqlEscape( id ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "内容不存在", 404 );

    bool isDefault = rows[0]["is_default"] == "1";
    if ( !isDefault && !user )
        return Server::sendError( res, "请登录后操作", 401 );

    if ( user ) {
        if ( rows[0]["created_by_id"] != user->id ) {
            auto m = App::getInstance()->getDatabase().query( "SELECT role FROM clipboard_team_members WHERE team_id='" + Database::sqlEscape( rows[0]["team_id"] ) +
                                                              "' AND user_id='" + Database::sqlEscape( user->id ) + "'" );
            if ( m.empty() || m[0]["role"] != "owner" )
                return Server::sendError( res, "只有创建者或团队管理员可以删除", 403 );
        }
    }

    if ( rows[0]["type"] == "file" && !rows[0]["file_url"].empty() ) {
        std::string fileUrl = rows[0]["file_url"];
        std::string fname = fileUrl;
        size_t pos = fname.find( "/uploads/" );
        if ( pos != std::string::npos )
            fname = fname.substr( pos + 9 );
        std::error_code ec;
        fs::remove( fs::path( Config::getUploadFilePath() ) / fname, ec );
    }

    App::getInstance()->getDatabase().execParams( "DELETE FROM clipboard_file_downloads WHERE clipboard_item_id=?", { { 1, id } } );
    App::getInstance()->getDatabase().execParams( "DELETE FROM clipboard_items WHERE id=?", { { 1, id } } );
    LOG_INFO << "剪贴板删除成功 id=" << id;
    Server::sendJson( res, { { "success", true } } );
}

void fileUpload( const httplib::Request &req, httplib::Response &res ) {
    if ( !req.form.has_file( "file" ) || !req.form.has_field( "teamId" ) )
        return Server::sendError( res, "文件和团队ID不能为空", 400 );
    auto file = req.form.get_file( "file" );
    std::string teamId = req.form.get_field( "teamId" );
    LOG_DEBUG << "剪贴板文件上传 teamId=" << teamId << " filename=" << file.filename << " size=" << file.content.size();
    if ( file.content.size() > Config::getMaxUploadFileSize() ) {
        LOG_WARN << "剪贴板文件上传失败: 超过大小限制 size=" << file.content.size() << " max=" << Config::getMaxUploadFileSize();
        return Server::sendError( res, "文件大小超过限制", 400 );
    }

    auto teams = App::getInstance()->getDatabase().query( "SELECT is_default FROM clipboard_teams WHERE id='" + Database::sqlEscape( teamId ) + "'" );
    if ( teams.empty() )
        return Server::sendError( res, "团队不存在", 404 );
    bool isDefault = teams[0]["is_default"] == "1";
    auto user = core::auth::getUserFromRequest( req );
    if ( !isDefault && !user )
        return Server::sendError( res, "请登录后操作", 401 );
    std::string userId = user ? user->id : core::auth::ensureAnonymousUser();

    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch() )
                     .count();
    std::string filename = std::to_string( nowMs ) + "-" + file.filename;
    fs::path outPath = fs::path( Config::getUploadFilePath() ) / filename;
    std::ofstream ofs( outPath, std::ios::binary );
    if ( !ofs )
        return Server::sendError( res, "文件保存失败", 500 );
    ofs.write( file.content.data(), file.content.size() );
    ofs.close();

    std::string fileUrl = "/uploads/" + filename;
    std::string id = utils::generateId();
    std::string now = utils::nowIso();
    std::string fileSize = std::to_string( file.content.size() );

    bool isImage = file.content_type.substr( 0, 6 ) == "image/";
    std::string itemType = isImage ? "image" : "file";
    std::string content;
    if ( isImage ) {
        content = "data:" + file.content_type + ";base64," + utils::base64Encode( file.content );
    } else {
        content = fileUrl;
    }

    App::getInstance()->getDatabase().execParams(
        "INSERT INTO clipboard_items(id,team_id,type,content,html_content,mime_type,"
        "file_url,file_name,file_size,created_by_id,created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)",
        { { 1, id }, { 2, teamId }, { 3, itemType }, { 4, content }, { 5, "" }, { 6, file.content_type }, { 7, fileUrl }, { 8, file.filename }, { 9, fileSize }, { 10, userId }, { 11, now } } );

    auto rows = App::getInstance()->getDatabase().query(
        "SELECT ci.id,ci.team_id,ci.type,ci.content,ci.html_content,ci.mime_type, "
        "ci.file_url,ci.file_name,ci.file_size,ci.created_by_id,ci.created_at, "
        "u.email AS u_email,u.nickname AS u_nickname "
        "FROM clipboard_items ci LEFT JOIN users u ON ci.created_by_id=u.id "
        "WHERE ci.id='" +
        id + "'" );
    LOG_INFO << "剪贴板文件上传成功 id=" << id << " filename=" << file.filename << " size=" << file.content.size() << " itemType=" << itemType;
    Server::sendJson( res,
                      {
                          { "success", true },
                          { "item", clipboardToJson( rows[0] ) },
                          { "fileUrl", fileUrl },
                      } );
}

void fileRequestDownload( const httplib::Request &req, httplib::Response &res ) {
    std::string id = req.matches[1];
    LOG_DEBUG << "请求剪贴板下载链接 id=" << id;
    auto user = core::auth::getUserFromRequest( req );
    auto rows = App::getInstance()->getDatabase().query(
        "SELECT ci.id,ci.team_id,ci.type,ci.file_url,t.is_default "
        "FROM clipboard_items ci JOIN teams t ON ci.team_id=t.id WHERE ci.id='" +
        Database::sqlEscape( id ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "内容不存在", 404 );
    if ( rows[0]["type"] != "file" || rows[0]["file_url"].empty() )
        return Server::sendError( res, "不是文件类型", 400 );
    bool isDefault = rows[0]["is_default"] == "1";
    if ( !isDefault && !user )
        return Server::sendError( res, "请登录后操作", 401 );
    if ( user && !isDefault ) {
        auto m = App::getInstance()->getDatabase().query( "SELECT id FROM clipboard_team_members WHERE team_id='" + Database::sqlEscape( rows[0]["team_id"] ) +
                                                          "' AND user_id='" + Database::sqlEscape( user->id ) + "'" );
        if ( m.empty() )
            return Server::sendError( res, "没有访问权限", 403 );
    }

    std::string token = utils::generateToken();
    std::string did = utils::generateId();
    std::string now = utils::nowIso();
    std::time_t exp = utils::nowTime() + 3600;
    std::tm tm{};
#ifdef _WIN32
    gmtime_s( &tm, &exp );
#else
    gmtime_r( &exp, &tm );
#endif
    char buf[32];
    std::strftime( buf, sizeof( buf ), "%Y-%m-%dT%H:%M:%SZ", &tm );
    App::getInstance()->getDatabase().execParams(
        "INSERT INTO clipboard_file_downloads(id,token,clipboard_item_id,expires_at,created_at) VALUES(?,?,?,?,?)",
        { { 1, did }, { 2, token }, { 3, id }, { 4, std::string( buf ) }, { 5, now } } );

    LOG_INFO << "生成剪贴板下载链接 token前8位=" << ( token.size() >= 8 ? token.substr( 0, 8 ) : "N/A" ) << " clipboard_id=" << id << " 过期时间=" << buf;
    Server::sendJson( res, { { "success", true }, { "downloadUrl", "/api/clipboard/download/" + token } } );
}

void fileDownload( const httplib::Request &req, httplib::Response &res ) {
    std::string token = req.matches[1];
    LOG_DEBUG << "下载剪贴板文件 token前8位=" << ( token.size() >= 8 ? token.substr( 0, 8 ) : "N/A" );
    auto rows = App::getInstance()->getDatabase().query(
        "SELECT fd.expires_at,ci.file_url,ci.file_name,ci.mime_type "
        "FROM clipboard_file_downloads fd JOIN clipboard_items ci ON fd.clipboard_item_id=ci.id "
        "WHERE fd.token='" +
        Database::sqlEscape( token ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "下载链接无效", 404 );
    if ( utils::parseIso( rows[0]["expires_at"] ) < utils::nowTime() )
        return Server::sendError( res, "下载链接已过期", 410 );

    std::string fileUrl = rows[0]["file_url"];
    std::string fname = fileUrl;
    size_t pos = fname.find( "/uploads/" );
    if ( pos != std::string::npos )
        fname = fname.substr( pos + 9 );
    fs::path filePath = fs::path( Config::getUploadFilePath() ) / fname;

    std::ifstream ifs( filePath, std::ios::binary );
    if ( !ifs )
        return Server::sendError( res, "文件不存在", 404 );
    std::string data( ( std::istreambuf_iterator<char>( ifs ) ), std::istreambuf_iterator<char>() );

    std::string mimeType = rows[0]["mime_type"].empty() ? "application/octet-stream" : rows[0]["mime_type"];
    std::string downloadName = rows[0]["file_name"].empty() ? "file" : rows[0]["file_name"];
    LOG_INFO << "剪贴板文件下载成功 filename=" << downloadName << " size=" << data.size() << " bytes";
    res.set_content( data, mimeType.c_str() );
    res.set_header( "Content-Disposition",
                    "attachment; filename=\"" + utils::urlEncode( downloadName ) + "\"" );
}

json clipboardToJson( const Database::Row &r ) {
    json createdBy = json::object();
    createdBy["id"] = r.count( "created_by_id" ) ? r.at( "created_by_id" ) : "";
    createdBy["email"] = r.count( "u_email" ) ? r.at( "u_email" ) : "";
    std::string nick = r.count( "u_nickname" ) ? r.at( "u_nickname" ) : "";
    createdBy["nickname"] = nick.empty() ? json( nullptr ) : json( nick );

    json item = {
        { "id", r.count( "id" ) ? r.at( "id" ) : "" },
        { "teamId", r.count( "team_id" ) ? r.at( "team_id" ) : "" },
        { "type", r.count( "type" ) ? r.at( "type" ) : "" },
        { "createdAt", r.count( "created_at" ) ? r.at( "created_at" ) : "" },
        { "createdBy", createdBy } };
    auto setIf = [&]( const char *dbCol, const char *jsonKey ) {
        if ( r.count( dbCol ) && !r.at( dbCol ).empty() )
            item[jsonKey] = r.at( dbCol );
        else
            item[jsonKey] = nullptr;
    };
    setIf( "content", "content" );
    setIf( "html_content", "htmlContent" );
    setIf( "mime_type", "mimeType" );
    setIf( "file_url", "fileUrl" );
    setIf( "file_name", "fileName" );
    if ( r.count( "file_size" ) && !r.at( "file_size" ).empty() )
        item["fileSize"] = std::stoi( r.at( "file_size" ) );
    else
        item["fileSize"] = nullptr;
    return item;
}

} // namespace routes::clipboard
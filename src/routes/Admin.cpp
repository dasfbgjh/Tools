#include "admin.h"
#include "core/TransferTracker.h"

namespace fs = std::filesystem;

namespace routes::admin {

Server::json shareToJson( const Database::Row &r ) {
    Server::json share = {
        { "id", r.at( "id" ) }, { "name", r.at( "name" ) }, { "realPath", r.at( "real_path" ) }, { "isDirectory", r.at( "is_directory" ) == "1" }, { "createdAt", r.at( "created_at" ) }, { "createdBy", r.at( "created_by" ) } };
    auto perms = App::getInstance()->getDatabase().query(
        "SELECT subject_type,user_id,can_access,can_download,can_upload,can_delete,can_rename "
        "FROM file_share_permissions WHERE share_id='" +
        Database::sqlEscape( r.at( "id" ) ) + "'" );
    Server::json arr = Server::json::array();
    for ( auto &p : perms ) {
        arr.push_back( { { "subjectType", p["subject_type"] },
                         { "userId", p["user_id"].empty() ? Server::json( nullptr ) : Server::json( p["user_id"] ) },
                         { "canAccess", p["can_access"] == "1" },
                         { "canDownload", p["can_download"] == "1" },
                         { "canUpload", p["can_upload"] == "1" },
                         { "canDelete", p["can_delete"] == "1" },
                         { "canRename", p["can_rename"] == "1" } } );
    }
    share["permissions"] = arr;
    return share;
}

void adminUsers( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    auto rows = App::getInstance()->getDatabase().query(
        "SELECT id,email,nickname,created_at FROM users WHERE id != 'system' AND id != 'anonymous' ORDER BY email" );
    Server::json arr = Server::json::array();
    for ( auto &r : rows ) {
        arr.push_back( { { "id", r["id"] },
                         { "email", r["email"] },
                         { "nickname", r["nickname"].empty() ? Server::json( nullptr ) : Server::json( r["nickname"] ) },
                         { "createdAt", r["created_at"] } } );
    }
    LOG_DEBUG << "adminUsers 返回用户数: " << arr.size();
    Server::sendJson( res, { { "success", true }, { "users", arr } } );
}

void adminSharesList( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    auto rows = App::getInstance()->getDatabase().query( "SELECT * FROM file_shares ORDER BY created_at DESC" );
    Server::json arr = Server::json::array();
    for ( auto &r : rows )
        arr.push_back( shareToJson( r ) );
    LOG_DEBUG << "共享列表返回数量: " << arr.size();
    Server::sendJson( res, { { "success", true }, { "shares", arr } } );
}

void adminSharesCreate( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    auto body = Server::parseBody( req );
    if ( body.is_null() )
        return Server::sendError( res, "无效的请求", 400 );
    std::string name = utils::jsonStringValue( body, "name" );
    std::string realPath = utils::jsonStringValue( body, "realPath" );
    if ( name.empty() || realPath.empty() ) {
        LOG_WARN << "创建共享失败: 名称或路径为空";
        return Server::sendError( res, "名称和路径不能为空", 400 );
    }
    LOG_DEBUG << "创建共享 name=" << name << " realPath=" << realPath;

    std::error_code ec;
    bool isDir = fs::is_directory( realPath, ec );
    bool isFile = fs::is_regular_file( realPath, ec );
    if ( !isDir && !isFile ) {
        LOG_WARN << "创建共享失败: 路径不存在 " << realPath;
        return Server::sendError( res, "路径不存在", 400 );
    }

    std::string id = utils::generateId();
    std::string now = utils::nowIso();
    auto user = core::auth::getUserFromRequest( req );
    std::string createdBy = user ? user->id : "admin";

    App::getInstance()->getDatabase().execParams(
        "INSERT INTO file_shares(id,name,real_path,is_directory,created_at,created_by) "
        "VALUES(?,?,?,?,?,?)",
        { { 1, id }, { 2, name }, { 3, realPath }, { 4, isDir ? "1" : "0" }, { 5, now }, { 6, createdBy } } );

    int permCount = 0;
    if ( body.contains( "permissions" ) && body["permissions"].is_array() ) {
        for ( auto &p : body["permissions"] ) {
            std::string pid = utils::generateId();
            std::string st = utils::jsonStringValue( p, "subjectType" );
            std::string uid = utils::jsonStringValue( p, "userId" );
            App::getInstance()->getDatabase().execParams(
                "INSERT INTO file_share_permissions(id,share_id,subject_type,user_id,"
                "can_access,can_download,can_upload,can_delete,can_rename) VALUES(?,?,?,?,?,?,?,?,?)",
                { { 1, pid }, { 2, id }, { 3, st }, { 4, st == "user" ? uid : "" }, { 5, p.value( "canAccess", false ) ? "1" : "0" }, { 6, p.value( "canDownload", false ) ? "1" : "0" }, { 7, p.value( "canUpload", false ) ? "1" : "0" }, { 8, p.value( "canDelete", false ) ? "1" : "0" }, { 9, p.value( "canRename", false ) ? "1" : "0" } } );
            ++permCount;
        }
    }

    LOG_INFO << "共享创建成功 id=" << id << " name=" << name << " 权限数=" << permCount;
    auto rows = App::getInstance()->getDatabase().query( "SELECT * FROM file_shares WHERE id='" + id + "'" );
    Server::sendJson( res, { { "success", true }, { "share", shareToJson( rows[0] ) } } );
}

void adminSharesUpdate( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    std::string id = req.matches[1];
    LOG_DEBUG << "更新共享 id=" << id;
    auto rows = App::getInstance()->getDatabase().query( "SELECT * FROM file_shares WHERE id='" + Database::sqlEscape( id ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "共享不存在", 404 );

    auto body = Server::parseBody( req );
    if ( body.is_null() )
        return Server::sendError( res, "无效的请求", 400 );

    if ( body.contains( "name" ) ) {
        std::string name = utils::jsonStringValue( body, "name" );
        if ( !name.empty() )
            App::getInstance()->getDatabase().execParams( "UPDATE file_shares SET name=? WHERE id=?", { { 1, name }, { 2, id } } );
    }

    if ( body.contains( "permissions" ) && body["permissions"].is_array() ) {
        App::getInstance()->getDatabase().execParams( "DELETE FROM file_share_permissions WHERE share_id=?", { { 1, id } } );
        for ( auto &p : body["permissions"] ) {
            std::string pid = utils::generateId();
            std::string st = utils::jsonStringValue( p, "subjectType" );
            std::string uid = utils::jsonStringValue( p, "userId" );
            App::getInstance()->getDatabase().execParams(
                "INSERT INTO file_share_permissions(id,share_id,subject_type,user_id,"
                "can_access,can_download,can_upload,can_delete,can_rename) VALUES(?,?,?,?,?,?,?,?,?)",
                { { 1, pid }, { 2, id }, { 3, st }, { 4, st == "user" ? uid : "" }, { 5, p.value( "canAccess", false ) ? "1" : "0" }, { 6, p.value( "canDownload", false ) ? "1" : "0" }, { 7, p.value( "canUpload", false ) ? "1" : "0" }, { 8, p.value( "canDelete", false ) ? "1" : "0" }, { 9, p.value( "canRename", false ) ? "1" : "0" } } );
        }
    }

    auto updated = App::getInstance()->getDatabase().query( "SELECT * FROM file_shares WHERE id='" + id + "'" );
    LOG_INFO << "共享更新成功 id=" << id;
    Server::sendJson( res, { { "success", true }, { "share", shareToJson( updated[0] ) } } );
}

void adminSharesDelete( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    std::string id = req.matches[1];
    auto rows = App::getInstance()->getDatabase().query( "SELECT id FROM file_shares WHERE id='" + Database::sqlEscape( id ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "共享不存在", 404 );
    App::getInstance()->getDatabase().execParams( "DELETE FROM file_share_permissions WHERE share_id=?", { { 1, id } } );
    App::getInstance()->getDatabase().execParams( "DELETE FROM file_shares WHERE id=?", { { 1, id } } );
    LOG_INFO << "共享删除成功 id=" << id;
    Server::sendJson( res, { { "success", true }, { "message", "共享已删除" } } );
}

void adminParameterPaths( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    std::string id = Server::queryParam( req, "id" );
    if ( id.empty() )
        return Server::sendError( res, "缺少id参数", 400 );
    LOG_DEBUG << "查询路径参数 id=" << id;
    auto paths = Config::getPathParameter( id );
    Server::json arr = Server::json::array();
    for ( auto &p : paths )
        arr.push_back( p );
    LOG_DEBUG << "adminPaths 返回路径数量: " << arr.size();
    Server::sendJson( res, { { "success", true }, { "paths", arr } } );
}

// 读取应用配置，将 KV 表中的字符串值转换为带类型的 JSON
void adminConfigGet( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    Server::json::array_t config;
    Config::configJson( config );
    Server::sendJson( res, { { "success", true }, { "config", config } } );
}

// 更新应用配置（部分更新，只写入请求体中出现的字段，重启后生效）
// 兼容两种请求体格式：
//   1) JSON 数组 [{name:"xx", value: ...}, ...] —— 前端 settings.js 动态渲染后提交的新格式
//   2) JSON 对象 {key1:val1, key2:val2, ...}      —— 兼容直接调用 API 的客户端
void adminConfigPut( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    auto body = Server::parseBody( req );
    if ( body.is_null() )
        return Server::sendError( res, "无效的请求体", 400 );

    Server::json::array_t arr;
    if ( body.is_array() ) {
        arr = body.get<Server::json::array_t>();
    } else if ( body.is_object() ) {
        for ( auto it = body.begin(); it != body.end(); ++it ) {
            arr.push_back( { { "name", it.key() }, { "value", it.value() } } );
        }
    } else {
        return Server::sendError( res, "无效的请求体", 400 );
    }

    App::getInstance()->getDatabase().saveAppConfig( arr );
    LOG_INFO << "应用配置已更新，字段数: " << arr.size() << "（重启后生效）";
    Server::sendJson( res, { { "success", true }, { "message", "配置已保存，重启后生效" } } );
}

// 创建用户
void adminUserCreate( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    auto body = Server::parseBody( req );
    if ( body.is_null() || !body.is_object() )
        return Server::sendError( res, "无效的请求体", 400 );
    std::string email = utils::jsonStringValue( body, "email" );
    std::string password = utils::jsonStringValue( body, "password" );
    std::string nickname = utils::jsonStringValue( body, "nickname" );
    if ( email.empty() || password.empty() )
        return Server::sendError( res, "邮箱和密码不能为空", 400 );

    auto existing = App::getInstance()->getDatabase().query(
        "SELECT id FROM users WHERE email='" + Database::sqlEscape( email ) + "'" );
    if ( !existing.empty() )
        return Server::sendError( res, "该邮箱已被注册", 400 );

    std::string id = utils::generateId();
    std::string now = utils::nowIso();
    std::string hash = core::auth::hashPassword( password );
    App::getInstance()->getDatabase().execParams(
        "INSERT INTO users(id,email,nickname,password,created_at) VALUES(?,?,?,?,?)",
        { { 1, id }, { 2, email }, { 3, nickname }, { 4, hash }, { 5, now } } );
    LOG_INFO << "管理员创建用户成功,用户ID: " << id << " email: " << email;

    // 加入默认团队
    auto defTeams = App::getInstance()->getDatabase().query(
        "SELECT id FROM clipboard_teams WHERE is_default=1 LIMIT 1" );
    if ( !defTeams.empty() ) {
        std::string mid = utils::generateId();
        App::getInstance()->getDatabase().execParams(
            "INSERT INTO clipboard_team_members(id,team_id,user_id,role,joined_at) VALUES(?,?,?,?,?)",
            { { 1, mid }, { 2, defTeams[0]["id"] }, { 3, id }, { 4, "member" }, { 5, now } } );
    }

    Server::sendJson( res, { { "success", true },
                             { "user", { { "id", id }, { "email", email }, { "nickname", nickname.empty() ? Server::json( nullptr ) : Server::json( nickname ) }, { "createdAt", now } } } } );
}

// 更新用户（修改昵称，可选重置密码）
void adminUserUpdate( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    std::string id = req.matches[1];
    if ( id == "system" || id == "anonymous" )
        return Server::sendError( res, "不能修改系统用户", 400 );
    auto body = Server::parseBody( req );
    if ( body.is_null() || !body.is_object() )
        return Server::sendError( res, "无效的请求体", 400 );

    auto rows = App::getInstance()->getDatabase().query(
        "SELECT id FROM users WHERE id='" + Database::sqlEscape( id ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "用户不存在", 404 );

    std::string nickname = utils::jsonStringValue( body, "nickname" );
    App::getInstance()->getDatabase().execParams( "UPDATE users SET nickname=? WHERE id=?",
                                                  { { 1, nickname }, { 2, id } } );

    // 若提供了 password 字段则重置密码
    if ( body.contains( "password" ) && body["password"].is_string() ) {
        std::string password = body["password"].get<std::string>();
        if ( !password.empty() ) {
            std::string hash = core::auth::hashPassword( password );
            App::getInstance()->getDatabase().execParams( "UPDATE users SET password=? WHERE id=?",
                                                          { { 1, hash }, { 2, id } } );
            LOG_INFO << "管理员重置用户密码,用户ID: " << id;
            // 清除该用户所有会话，强制重新登录
            App::getInstance()->getDatabase().execParams( "DELETE FROM sessions WHERE user_id=?",
                                                          { { 1, id } } );
        }
    }
    LOG_INFO << "管理员更新用户信息,用户ID: " << id;

    auto updated = App::getInstance()->getDatabase().query(
        "SELECT id,email,nickname,created_at FROM users WHERE id='" + Database::sqlEscape( id ) + "'" );
    if ( updated.empty() )
        return Server::sendError( res, "用户不存在", 404 );
    Server::sendJson( res, { { "success", true },
                             { "user", { { "id", updated[0]["id"] }, { "email", updated[0]["email"] }, { "nickname", updated[0]["nickname"].empty() ? Server::json( nullptr ) : Server::json( updated[0]["nickname"] ) }, { "createdAt", updated[0]["created_at"] } } } } );
}

// 删除用户（同时清理其会话和团队成员关系）
void adminUserDelete( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    std::string id = req.matches[1];
    if ( id == "system" || id == "anonymous" )
        return Server::sendError( res, "不能删除系统用户", 400 );

    auto rows = App::getInstance()->getDatabase().query(
        "SELECT id FROM users WHERE id='" + Database::sqlEscape( id ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "用户不存在", 404 );

    App::getInstance()->getDatabase().execParams( "DELETE FROM sessions WHERE user_id=?", { { 1, id } } );
    App::getInstance()->getDatabase().execParams( "DELETE FROM clipboard_team_members WHERE user_id=?", { { 1, id } } );
    App::getInstance()->getDatabase().execParams( "DELETE FROM users WHERE id=?", { { 1, id } } );
    LOG_INFO << "管理员删除用户,用户ID: " << id;
    Server::sendJson( res, { { "success", true } } );
}

// 列出当前活跃的文件传输任务 + 速度历史（本机访问）
void adminTransfers( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    res.set_content(
        TransferTracker::instance().snapshotJson(),
        "application/json; charset=utf-8" );
}

void registerAdminRoutes( httplib::Server &svr ) {
    svr.Get( "/api/admin/shares", adminSharesList );
    svr.Post( "/api/admin/shares", adminSharesCreate );
    svr.Put( R"(/api/admin/shares/([^/]+))", adminSharesUpdate );
    svr.Delete( R"(/api/admin/shares/([^/]+))", adminSharesDelete );
    svr.Get( "/api/admin/shares/parameter/paths", adminParameterPaths );

    svr.Get( "/api/admin/users", adminUsers );
    svr.Post( "/api/admin/users", adminUserCreate );
    svr.Put( R"(/api/admin/users/([^/]+))", adminUserUpdate );
    svr.Delete( R"(/api/admin/users/([^/]+))", adminUserDelete );

    svr.Get( "/api/admin/transfers", adminTransfers );

    svr.Get( "/api/admin/config", adminConfigGet );
    svr.Put( "/api/admin/config", adminConfigPut );

    LOG_DEBUG << "已注册 12 个管理员路由";
}

} // namespace routes::admin
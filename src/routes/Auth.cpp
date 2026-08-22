#include "routes/Auth.h"
#include "core/Server.h"

namespace routes::auth {

void authRegister( const httplib::Request &req, httplib::Response &res ) {
    auto body = Server::parseBody( req );
    if ( body.is_null() )
        return Server::sendError( res, "无效的请求", 400 );
    std::string email = utils::jsonStringValue( body, "email" );
    std::string password = utils::jsonStringValue( body, "password" );
    std::string nickname = utils::jsonStringValue( body, "nickname" );
    if ( email.empty() || password.empty() ) {
        LOG_WARN << "注册失败: 邮箱或密码为空 (email长度: " << email.size() << ")";
        return Server::sendError( res, "邮箱和密码不能为空", 400 );
    }
    LOG_DEBUG << "注册请求: email=" << email << " nickname长度=" << nickname.size();

    auto existing = App::getInstance()->getDatabase().query( "SELECT id FROM users WHERE email='" + Database::sqlEscape( email ) + "'" );
    if ( !existing.empty() ) {
        LOG_WARN << "注册失败: 邮箱已被注册 " << email;
        return Server::sendError( res, "该邮箱已被注册", 400 );
    }

    std::string id = utils::generateId();
    std::string now = utils::nowIso();
    std::string hash = core::auth::hashPassword( password );
    App::getInstance()->getDatabase().execParams(
        "INSERT INTO users(id,email,nickname,password,created_at) VALUES(?,?,?,?,?)",
        { { 1, id }, { 2, email }, { 3, nickname }, { 4, hash }, { 5, now } } );
    LOG_INFO << "用户注册成功,用户ID: " << id << " email: " << email;

    auto defTeams = App::getInstance()->getDatabase().query( "SELECT id FROM clipboard_teams WHERE is_default=1 LIMIT 1" );
    if ( !defTeams.empty() ) {
        std::string mid = utils::generateId();
        App::getInstance()->getDatabase().execParams(
            "INSERT INTO clipboard_team_members(id,team_id,user_id,role,joined_at) VALUES(?,?,?,?,?)",
            { { 1, mid }, { 2, defTeams[0]["id"] }, { 3, id }, { 4, "member" }, { 5, now } } );
        LOG_DEBUG << "新用户已加入默认团队, teamId: " << defTeams[0]["id"];
    }

    std::string token = core::auth::createSession( id );
    core::auth::setAuthCookie( res, token );

    auto users = App::getInstance()->getDatabase().query( "SELECT id,email,nickname,created_at FROM users WHERE id='" + id + "'" );
    Server::sendJson( res, { { "success", true },
                             { "user", core::auth::userToJson( { users[0]["id"], users[0]["email"],
                                                                 users[0]["nickname"], users[0]["created_at"] } ) },
                             { "token", token } } );
}

void authLogin( const httplib::Request &req, httplib::Response &res ) {
    auto body = Server::parseBody( req );
    if ( body.is_null() )
        return Server::sendError( res, "无效的请求", 400 );
    std::string email = utils::jsonStringValue( body, "email" );
    std::string password = utils::jsonStringValue( body, "password" );
    if ( email.empty() || password.empty() ) {
        LOG_WARN << "登录失败: 邮箱或密码为空 (email长度: " << email.size() << ")";
        return Server::sendError( res, "邮箱和密码不能为空", 400 );
    }
    LOG_DEBUG << "登录请求: email=" << email;

    auto rows = App::getInstance()->getDatabase().query( "SELECT id,email,nickname,password,created_at FROM users WHERE email='" + Database::sqlEscape( email ) + "'" );
    if ( rows.empty() ) {
        LOG_WARN << "登录失败: 邮箱不存在 " << email;
        return Server::sendError( res, "邮箱或密码错误", 401 );
    }
    if ( !core::auth::verifyPassword( password, rows[0]["password"] ) ) {
        LOG_WARN << "登录失败: 密码不匹配 (email: " << email << ", user_id: " << rows[0]["id"] << ")";
        return Server::sendError( res, "邮箱或密码错误", 401 );
    }

    std::string token = core::auth::createSession( rows[0]["id"] );
    core::auth::setAuthCookie( res, token );
    LOG_INFO << "用户登录成功,用户ID: " << rows[0]["id"] << " email: " << email;
    Server::sendJson( res, { { "success", true },
                             { "user", core::auth::userToJson( { rows[0]["id"], rows[0]["email"],
                                                                 rows[0]["nickname"], rows[0]["created_at"] } ) },
                             { "token", token } } );
}

void authLogout( const httplib::Request &req, httplib::Response &res ) {
    auto cookies = utils::parseCookies( req.get_header_value( "Cookie" ) );
    auto it = cookies.find( core::auth::COOKIE_NAME );
    if ( it != cookies.end() ) {
        core::auth::deleteSession( it->second );
    }
    core::auth::clearAuthCookie( res );
    LOG_INFO << "用户已登出";
    Server::sendJson( res, { { "success", true } } );
}

void authMe( const httplib::Request &req, httplib::Response &res ) {
    auto user = core::auth::getUserFromRequest( req );
    if ( !user )
        return Server::sendError( res, "未登录", 401 );
    LOG_DEBUG << "authMe 成功,用户ID: " << user->id << " email: " << user->email;
    Server::sendJson( res, { { "success", true }, { "user", core::auth::userToJson( *user ) } } );
}

void authUpdateMe( const httplib::Request &req, httplib::Response &res ) {
    auto user = core::auth::getUserFromRequest( req );
    if ( !user )
        return Server::sendError( res, "未登录", 401 );
    auto body = Server::parseBody( req );
    if ( body.is_null() )
        return Server::sendError( res, "无效的请求", 400 );
    std::string nickname = utils::jsonStringValue( body, "nickname" );
    LOG_INFO << "用户更新个人信息,用户ID: " << user->id << " 新昵称长度: " << nickname.size();
    App::getInstance()->getDatabase().execParams( "UPDATE users SET nickname=? WHERE id=?",
                                                  { { 1, nickname }, { 2, user->id } } );
    auto rows = App::getInstance()->getDatabase().query( "SELECT id,email,nickname,created_at FROM users WHERE id='" + user->id + "'" );
    Server::sendJson( res, { { "success", true },
                             { "user", core::auth::userToJson( { rows[0]["id"], rows[0]["email"],
                                                                 rows[0]["nickname"], rows[0]["created_at"] } ) } } );
}

void registerAuthRoutes( httplib::Server &svr ) {
    svr.Post( "/api/auth/register", authRegister );
    svr.Post( "/api/auth/login", authLogin );
    svr.Post( "/api/auth/logout", authLogout );
    svr.Get( "/api/auth/me", authMe );
    svr.Put( "/api/auth/me", authUpdateMe );
    LOG_DEBUG << "已注册 5 个授权路由";
}

} // namespace routes::auth
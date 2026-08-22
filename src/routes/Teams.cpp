#include "routes/Teams.h"
#include "core/Server.h"

namespace routes::teams {

void teamsList( const httplib::Request &req, httplib::Response &res ) {
    bool defaultOnly = Server::queryParam( req, "defaultOnly" ) == "true";
    LOG_DEBUG << "查询团队列表 defaultOnly=" << ( defaultOnly ? "true" : "false" );
    if ( defaultOnly ) {
        auto rows = App::getInstance()->getDatabase().query(
            "SELECT t.id,t.name,t.owner_id,t.is_default,t.created_at, "
            "(SELECT COUNT(*) FROM clipboard_team_members WHERE team_id=t.id) AS member_count "
            "FROM clipboard_teams t WHERE t.is_default=1 LIMIT 1" );
        if ( rows.empty() )
            return Server::sendError( res, "默认团队不存在", 404 );
        Server::json team = {
            { "id", rows[0]["id"] }, { "name", rows[0]["name"] }, { "ownerId", rows[0]["owner_id"] }, { "isDefault", rows[0]["is_default"] == "1" }, { "createdAt", rows[0]["created_at"] }, { "memberCount", std::stoi( rows[0]["member_count"] ) } };
        return Server::sendJson( res, { { "success", true }, { "teams", Server::json::array( { team } ) } } );
    }
    auto user = core::auth::getUserFromRequest( req );
    if ( !user )
        return Server::sendError( res, "未登录", 401 );

    auto rows = App::getInstance()->getDatabase().query(
        "SELECT t.id,t.name,t.owner_id,t.is_default,t.created_at, tm.role, "
        "(SELECT COUNT(*) FROM clipboard_team_members WHERE team_id=t.id) AS member_count "
        "FROM clipboard_team_members tm JOIN clipboard_teams t ON tm.team_id=t.id "
        "WHERE tm.user_id='" +
        Database::sqlEscape( user->id ) + "'" );
    Server::json teams = Server::json::array();
    for ( auto &r : rows ) {
        teams.push_back( { { "id", r["id"] }, { "name", r["name"] }, { "ownerId", r["owner_id"] }, { "isDefault", r["is_default"] == "1" }, { "createdAt", r["created_at"] }, { "role", r["role"] }, { "memberCount", std::stoi( r["member_count"] ) } } );
    }
    LOG_DEBUG << "用户团队列表 user_id=" << user->id << " 数量=" << teams.size();
    return Server::sendJson( res, { { "success", true }, { "teams", teams } } );
}

void teamsCreate( const httplib::Request &req, httplib::Response &res ) {
    auto user = core::auth::getUserFromRequest( req );
    if ( !user )
        return Server::sendError( res, "未登录", 401 );
    auto body = Server::parseBody( req );
    if ( body.is_null() )
        return Server::sendError( res, "无效的请求", 400 );
    std::string name = utils::jsonStringValue( body, "name" );
    if ( name.empty() )
        return Server::sendError( res, "团队名称不能为空", 400 );

    std::string id = utils::generateId();
    std::string now = utils::nowIso();
    App::getInstance()->getDatabase().execParams(
        "INSERT INTO clipboard_teams(id,name,owner_id,is_default,created_at) VALUES(?,?,?,?,?)",
        { { 1, id }, { 2, name }, { 3, user->id }, { 4, "0" }, { 5, now } } );
    std::string mid = utils::generateId();
    App::getInstance()->getDatabase().execParams(
        "INSERT INTO clipboard_team_members(id,team_id,user_id,role,joined_at) VALUES(?,?,?,?,?)",
        { { 1, mid }, { 2, id }, { 3, user->id }, { 4, "owner" }, { 5, now } } );

    LOG_INFO << "团队创建成功 id=" << id << " name=" << name << " owner_id=" << user->id;
    Server::sendJson( res, { { "success", true }, { "team", { { "id", id }, { "name", name }, { "ownerId", user->id }, { "isDefault", false }, { "createdAt", now }, { "role", "owner" }, { "memberCount", 1 } } } } );
}

void teamsInviteValidate( const httplib::Request &req, httplib::Response &res ) {
    std::string code = req.matches[1];
    LOG_DEBUG << "验证邀请码 code前4位=" << ( code.size() >= 4 ? code.substr( 0, 4 ) : "N/A" );
    auto rows = App::getInstance()->getDatabase().query(
        "SELECT ic.code,ic.used,ic.expires_at,t.id AS team_id,t.name AS team_name "
        "FROM clipboard_team_invite_codes ic JOIN clipboard_teams t ON ic.team_id=t.id WHERE ic.code='" +
        Database::sqlEscape( code ) + "'" );
    if ( rows.empty() )
        return Server::sendJson( res, { { "success", true }, { "valid", false }, { "message", "邀请码无效" } } );
    if ( rows[0]["used"] == "1" )
        return Server::sendJson( res, { { "success", true }, { "valid", false }, { "message", "邀请码已被使用" } } );
    if ( utils::parseIso( rows[0]["expires_at"] ) < utils::nowTime() )
        return Server::sendJson( res, { { "success", true }, { "valid", false }, { "message", "邀请码已过期" } } );
    LOG_DEBUG << "邀请码验证有效 team_id=" << rows[0]["team_id"];
    Server::sendJson( res, { { "success", true }, { "valid", true }, { "team", { { "id", rows[0]["team_id"] }, { "name", rows[0]["team_name"] } } } } );
}

void teamsJoin( const httplib::Request &req, httplib::Response &res ) {
    auto user = core::auth::getUserFromRequest( req );
    if ( !user )
        return Server::sendError( res, "未登录", 401 );
    auto body = Server::parseBody( req );
    if ( body.is_null() )
        return Server::sendError( res, "无效的请求", 400 );
    std::string code = utils::jsonStringValue( body, "code" );
    if ( code.empty() )
        return Server::sendError( res, "邀请码不能为空", 400 );
    LOG_DEBUG << "加入团队 user_id=" << user->id << " code前4位=" << ( code.size() >= 4 ? code.substr( 0, 4 ) : "N/A" );

    auto rows = App::getInstance()->getDatabase().query(
        "SELECT ic.id,ic.team_id,ic.expires_at,ic.used,t.id AS tid,t.name AS tname "
        "FROM clipboard_team_invite_codes ic JOIN clipboard_teams t ON ic.team_id=t.id WHERE ic.code='" +
        Database::sqlEscape( code ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "邀请码无效", 404 );
    if ( utils::parseIso( rows[0]["expires_at"] ) < utils::nowTime() )
        return Server::sendError( res, "邀请码已过期", 400 );

    std::string teamId = rows[0]["team_id"];
    auto exist = App::getInstance()->getDatabase().query( "SELECT id FROM clipboard_team_members WHERE team_id='" + Database::sqlEscape( teamId ) +
                                                          "' AND user_id='" + Database::sqlEscape( user->id ) + "'" );
    if ( !exist.empty() )
        return Server::sendError( res, "您已经是该团队成员", 400 );

    std::string mid = utils::generateId();
    std::string now = utils::nowIso();
    App::getInstance()->getDatabase().execParams(
        "INSERT INTO clipboard_team_members(id,team_id,user_id,role,joined_at) VALUES(?,?,?,?,?)",
        { { 1, mid }, { 2, teamId }, { 3, user->id }, { 4, "member" }, { 5, now } } );

    LOG_INFO << "用户加入团队 user_id=" << user->id << " team_id=" << teamId;
    Server::sendJson( res, { { "success", true }, { "team", { { "id", rows[0]["tid"] }, { "name", rows[0]["tname"] } } } } );
}

void teamsInviteCreate( const httplib::Request &req, httplib::Response &res ) {
    auto user = core::auth::getUserFromRequest( req );
    if ( !user )
        return Server::sendError( res, "未登录", 401 );
    auto body = Server::parseBody( req );
    if ( body.is_null() )
        return Server::sendError( res, "无效的请求", 400 );
    std::string teamId = utils::jsonStringValue( body, "teamId" );
    if ( teamId.empty() )
        return Server::sendError( res, "团队ID不能为空", 400 );
    LOG_DEBUG << "创建邀请码 team_id=" << teamId << " 创建者=" << user->id;

    auto rows = App::getInstance()->getDatabase().query( "SELECT owner_id FROM clipboard_teams WHERE id='" + Database::sqlEscape( teamId ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "团队不存在", 404 );
    if ( rows[0]["owner_id"] != user->id )
        return Server::sendError( res, "只有团队创建者可以生成邀请码", 403 );

    // Invalidate old unused codes
    std::string now = utils::nowIso();
    App::getInstance()->getDatabase().execParams( "UPDATE clipboard_team_invite_codes SET expires_at=? WHERE team_id=? AND used=0",
                                                  { { 1, now }, { 2, teamId } } );

    std::string code = utils::generateInviteCode();
    std::string id = utils::generateId();
    std::time_t exp = utils::nowTime() + Config::getInviteCodeDurationSEC();
    std::tm tm{};
#ifdef _WIN32
    gmtime_s( &tm, &exp );
#else
    gmtime_r( &exp, &tm );
#endif
    char buf[32];
    std::strftime( buf, sizeof( buf ), "%Y-%m-%dT%H:%M:%SZ", &tm );
    App::getInstance()->getDatabase().execParams(
        "INSERT INTO clipboard_team_invite_codes(id,code,team_id,created_by_id,expires_at,used) VALUES(?,?,?,?,?,0)",
        { { 1, id }, { 2, code }, { 3, teamId }, { 4, user->id }, { 5, std::string( buf ) } } );

    LOG_INFO << "创建团队邀请码 team_id=" << teamId << " code前4位=" << ( code.size() >= 4 ? code.substr( 0, 4 ) : "N/A" ) << " 过期时间=" << buf;
    Server::sendJson( res, { { "success", true }, { "inviteCode", code }, { "expiresAt", std::string( buf ) }, { "duration", Config::getInviteCodeDurationSEC() } } );
}

void teamsInviteRefresh( const httplib::Request &req, httplib::Response &res ) {
    auto user = core::auth::getUserFromRequest( req );
    if ( !user )
        return Server::sendError( res, "未登录", 401 );
    auto body = Server::parseBody( req );
    if ( body.is_null() )
        return Server::sendError( res, "无效的请求", 400 );
    std::string teamId = utils::jsonStringValue( body, "teamId" );
    if ( teamId.empty() )
        return Server::sendError( res, "团队ID不能为空", 400 );
    LOG_DEBUG << "刷新邀请码 team_id=" << teamId;

    auto rows = App::getInstance()->getDatabase().query( "SELECT owner_id FROM clipboard_teams WHERE id='" + Database::sqlEscape( teamId ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "团队不存在", 404 );
    if ( rows[0]["owner_id"] != user->id )
        return Server::sendError( res, "只有团队创建者可以刷新邀请码", 403 );

    std::string now = utils::nowIso();
    auto codes = App::getInstance()->getDatabase().query(
        "SELECT id,code FROM clipboard_team_invite_codes WHERE team_id='" + Database::sqlEscape( teamId ) +
        "' AND used=0 AND expires_at>'" + now + "' ORDER BY id DESC LIMIT 1" );
    if ( codes.empty() )
        return Server::sendError( res, "邀请码不存在或已失效", 404 );

    std::time_t exp = utils::nowTime() + Config::getInviteCodeDurationSEC();
    std::tm tm{};
#ifdef _WIN32
    gmtime_s( &tm, &exp );
#else
    gmtime_r( &exp, &tm );
#endif
    char buf[32];
    std::strftime( buf, sizeof( buf ), "%Y-%m-%dT%H:%M:%SZ", &tm );
    App::getInstance()->getDatabase().execParams( "UPDATE clipboard_team_invite_codes SET expires_at=? WHERE id=?",
                                                  { { 1, std::string( buf ) }, { 2, codes[0]["id"] } } );

    LOG_INFO << "刷新邀请码成功 team_id=" << teamId << " 新过期时间=" << buf;
    Server::sendJson( res, { { "success", true }, { "inviteCode", codes[0]["code"] }, { "expiresAt", std::string( buf ) }, { "duration", Config::getInviteCodeDurationSEC() } } );
}

void teamsInviteInvalidate( const httplib::Request &req, httplib::Response &res ) {
    auto user = core::auth::getUserFromRequest( req );
    if ( !user )
        return Server::sendError( res, "未登录", 401 );
    auto body = Server::parseBody( req );
    if ( body.is_null() )
        return Server::sendError( res, "无效的请求", 400 );
    std::string teamId = utils::jsonStringValue( body, "teamId" );
    if ( teamId.empty() )
        return Server::sendError( res, "团队ID不能为空", 400 );
    LOG_DEBUG << "使邀请码失效 team_id=" << teamId;

    auto rows = App::getInstance()->getDatabase().query( "SELECT owner_id FROM clipboard_teams WHERE id='" + Database::sqlEscape( teamId ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "团队不存在", 404 );
    if ( rows[0]["owner_id"] != user->id )
        return Server::sendError( res, "只有团队创建者可以使邀请码失效", 403 );

    std::string now = utils::nowIso();
    App::getInstance()->getDatabase().execParams( "UPDATE clipboard_team_invite_codes SET expires_at=? WHERE team_id=? AND used=0",
                                                  { { 1, now }, { 2, teamId } } );
    LOG_INFO << "邀请码已失效 team_id=" << teamId;
    Server::sendJson( res, { { "success", true }, { "message", "邀请码已失效" } } );
}

void teamsDetail( const httplib::Request &req, httplib::Response &res ) {
    auto user = core::auth::getUserFromRequest( req );
    if ( !user )
        return Server::sendError( res, "未登录", 401 );
    std::string id = req.matches[1];
    LOG_DEBUG << "获取团队详情 team_id=" << id;

    auto mrows = App::getInstance()->getDatabase().query( "SELECT role FROM clipboard_team_members WHERE team_id='" + Database::sqlEscape( id ) +
                                                          "' AND user_id='" + Database::sqlEscape( user->id ) + "'" );
    if ( mrows.empty() )
        return Server::sendError( res, "不是团队成员", 403 );

    auto rows = App::getInstance()->getDatabase().query(
        "SELECT t.id,t.name,t.owner_id,t.is_default,t.created_at, "
        "u.id AS uid,u.email AS uemail,u.nickname AS unick, tm.id AS mid, tm.role AS mrole, tm.joined_at AS mjoined "
        "FROM clipboard_teams t "
        "LEFT JOIN clipboard_team_members tm ON tm.team_id=t.id "
        "LEFT JOIN users u ON tm.user_id=u.id "
        "WHERE t.id='" +
        Database::sqlEscape( id ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "团队不存在", 404 );

    Server::json members = Server::json::array();
    for ( auto &r : rows ) {
        if ( r["mid"].empty() )
            continue;
        std::string nick = r["unick"];
        members.push_back( { { "id", r["mid"] }, { "teamId", id }, { "userId", r["uid"] }, { "role", r["mrole"] }, { "joinedAt", r["mjoined"] }, { "user", { { "id", r["uid"] }, { "email", r["uemail"] }, { "nickname", nick.empty() ? Server::json( nullptr ) : Server::json( nick ) } } } } );
    }
    LOG_DEBUG << "团队详情 team_id=" << id << " 成员数=" << members.size();
    Server::json team = {
        { "id", rows[0]["id"] }, { "name", rows[0]["name"] }, { "ownerId", rows[0]["owner_id"] }, { "isDefault", rows[0]["is_default"] == "1" }, { "createdAt", rows[0]["created_at"] }, { "role", mrows[0]["role"] }, { "memberCount", members.size() }, { "members", members } };
    Server::sendJson( res, { { "success", true }, { "team", team } } );
}

void teamsMembers( const httplib::Request &req, httplib::Response &res ) {
    auto user = core::auth::getUserFromRequest( req );
    if ( !user )
        return Server::sendError( res, "未登录", 401 );
    std::string teamId = req.matches[1];
    LOG_DEBUG << "获取团队成员 team_id=" << teamId;

    auto mrows = App::getInstance()->getDatabase().query( "SELECT role FROM clipboard_team_members WHERE team_id='" + Database::sqlEscape( teamId ) +
                                                          "' AND user_id='" + Database::sqlEscape( user->id ) + "'" );
    if ( mrows.empty() )
        return Server::sendError( res, "不是团队成员", 403 );

    auto rows = App::getInstance()->getDatabase().query(
        "SELECT u.id, u.email, u.nickname, tm.role, tm.joined_at "
        "FROM clipboard_team_members tm "
        "JOIN users u ON tm.user_id=u.id "
        "WHERE tm.team_id='" +
        Database::sqlEscape( teamId ) + "' ORDER BY tm.joined_at" );

    Server::json members = Server::json::array();
    for ( auto &r : rows ) {
        std::string nick = r["nickname"];
        members.push_back( { { "id", r["id"] },
                             { "email", r["email"] },
                             { "nickname", nick.empty() ? Server::json( nullptr ) : Server::json( nick ) },
                             { "role", r["role"] },
                             { "joinedAt", r["joined_at"] } } );
    }
    LOG_DEBUG << "团队成员列表 team_id=" << teamId << " 数量=" << members.size();

    Server::sendJson( res, { { "success", true }, { "members", members } } );
}

void teamsDelete( const httplib::Request &req, httplib::Response &res ) {
    auto user = core::auth::getUserFromRequest( req );
    if ( !user )
        return Server::sendError( res, "未登录", 401 );
    std::string id = req.matches[1];
    LOG_DEBUG << "请求解散团队 team_id=" << id;

    auto rows = App::getInstance()->getDatabase().query( "SELECT owner_id,is_default FROM clipboard_teams WHERE id='" + Database::sqlEscape( id ) + "'" );
    if ( rows.empty() )
        return Server::sendError( res, "团队不存在", 404 );
    if ( rows[0]["owner_id"] != user->id )
        return Server::sendError( res, "只有团队创建者才能解散团队", 403 );
    if ( rows[0]["is_default"] == "1" )
        return Server::sendError( res, "无法解散默认团队", 400 );

    App::getInstance()->getDatabase().execParams( "DELETE FROM clipboard_team_members WHERE team_id=?", { { 1, id } } );
    App::getInstance()->getDatabase().execParams( "DELETE FROM clipboard_items WHERE team_id=?", { { 1, id } } );
    App::getInstance()->getDatabase().execParams( "DELETE FROM clipboard_team_invite_codes WHERE team_id=?", { { 1, id } } );
    App::getInstance()->getDatabase().execParams( "DELETE FROM clipboard_teams WHERE id=?", { { 1, id } } );

    LOG_INFO << "团队已解散 team_id=" << id << " 操作者=" << user->id;
    Server::sendJson( res, { { "success", true }, { "message", "团队已解散" } } );
}

void teamsLeave( const httplib::Request &req, httplib::Response &res ) {
    auto user = core::auth::getUserFromRequest( req );
    if ( !user )
        return Server::sendError( res, "未登录", 401 );
    std::string id = req.matches[1];
    LOG_DEBUG << "用户退出团队 user_id=" << user->id << " team_id=" << id;

    auto mrows = App::getInstance()->getDatabase().query( "SELECT role FROM clipboard_team_members WHERE team_id='" + Database::sqlEscape( id ) +
                                                          "' AND user_id='" + Database::sqlEscape( user->id ) + "'" );
    if ( mrows.empty() )
        return Server::sendError( res, "不是团队成员", 403 );
    if ( mrows[0]["role"] == "owner" )
        return Server::sendError( res, "团队创建者不能退出团队", 400 );

    App::getInstance()->getDatabase().execParams( "DELETE FROM clipboard_team_members WHERE team_id=? AND user_id=?",
                                                  { { 1, id }, { 2, user->id } } );
    LOG_INFO << "用户退出团队成功 user_id=" << user->id << " team_id=" << id;
    Server::sendJson( res, { { "success", true } } );
}

void teamsRemoveMember( const httplib::Request &req, httplib::Response &res ) {
    auto user = core::auth::getUserFromRequest( req );
    if ( !user )
        return Server::sendError( res, "未登录", 401 );
    std::string id = req.matches[1];
    std::string userId = req.matches[2];
    LOG_DEBUG << "移除成员 team_id=" << id << " target_user_id=" << userId << " 操作者=" << user->id;

    auto mrows = App::getInstance()->getDatabase().query( "SELECT role FROM clipboard_team_members WHERE team_id='" + Database::sqlEscape( id ) +
                                                          "' AND user_id='" + Database::sqlEscape( user->id ) + "'" );
    if ( mrows.empty() || mrows[0]["role"] != "owner" )
        return Server::sendError( res, "只有团队创建者才能移除成员", 403 );
    if ( userId == user->id )
        return Server::sendError( res, "不能移除自己", 400 );

    auto trows = App::getInstance()->getDatabase().query( "SELECT id FROM clipboard_team_members WHERE team_id='" + Database::sqlEscape( id ) +
                                                          "' AND user_id='" + Database::sqlEscape( userId ) + "'" );
    if ( trows.empty() )
        return Server::sendError( res, "该成员不存在", 404 );

    App::getInstance()->getDatabase().execParams( "DELETE FROM clipboard_team_members WHERE team_id=? AND user_id=?",
                                                  { { 1, id }, { 2, userId } } );
    LOG_INFO << "成员已移除 team_id=" << id << " target_user_id=" << userId;
    Server::sendJson( res, { { "success", true }, { "message", "成员已移除" } } );
}

void registerTeamRoutes( httplib::Server &svr ) {
    // GET /api/teams  (list user teams, or default team if defaultOnly=true)
    svr.Get( "/api/teams", teamsList );

    // POST /api/teams  (create team)
    svr.Post( "/api/teams", teamsCreate );

    // GET /api/teams/invite/:code  (validate invite code)
    svr.Get( R"(/api/teams/invite/([^/]+))", teamsInviteValidate );

    // POST /api/teams/join  (join via code)
    svr.Post( "/api/teams/join", teamsJoin );

    // POST /api/teams/invite  (generate invite code)
    svr.Post( "/api/teams/invite", teamsInviteCreate );

    // PUT /api/teams/invite  (refresh invite code expiry)
    svr.Put( "/api/teams/invite", teamsInviteRefresh );

    // DELETE /api/teams/invite  (invalidate invite code)
    svr.Delete( "/api/teams/invite", teamsInviteInvalidate );

    // GET /api/teams/:id  (team detail with members)
    svr.Get( R"(/api/teams/([^/]+))", teamsDetail );

    // DELETE /api/teams/:id  (disband team)
    svr.Delete( R"(/api/teams/([^/]+))", teamsDelete );

    // DELETE /api/teams/:id/leave  (leave team)
    svr.Delete( R"(/api/teams/([^/]+)/leave)", teamsLeave );

    // GET /api/teams/:id/members  (list team members)
    svr.Get( R"(/api/teams/([^/]+)/members)", teamsMembers );

    // DELETE /api/teams/:id/members/:userId  (remove member)
    svr.Delete( R"(/api/teams/([^/]+)/members/([^/]+))", teamsRemoveMember );
    LOG_DEBUG << "已注册 13 个团队路由";
}

} // namespace routes::teams
#include "routes/Settings.h"
#include "core/Server.h"

namespace routes::settings {

// 未登录用户共用的公共配置 user_id
static const char *PUBLIC_USER_ID = "__public__";

static std::string getOwnerUserId( const httplib::Request &req ) {
    auto user = core::auth::getUserFromRequest( req );
    return user ? user->id : PUBLIC_USER_ID;
}

void registerSettingsRoutes( httplib::Server &svr ) {
    svr.Get( "/api/settings", settingsGet );
    svr.Put( "/api/settings", settingsPut );
    svr.Delete( R"(/api/settings/(.+))", settingsDelete );
    LOG_DEBUG << "已注册 3 个用户设置路由";
}

void settingsGet( const httplib::Request &req, httplib::Response &res ) {
    auto uid = getOwnerUserId( req );
    auto j = App::getInstance()->getDatabase().getUserSettings( uid );
    Server::sendJson( res, { { "success", true }, { "settings", j } } );
}

void settingsPut( const httplib::Request &req, httplib::Response &res ) {
    auto uid = getOwnerUserId( req );
    auto body = Server::parseBody( req );
    if ( body.is_null() || !body.is_object() ) {
        return Server::sendError( res, "无效的请求体", 400 );
    }
    auto &db = App::getInstance()->getDatabase();
    int count = 0;
    for ( auto it = body.begin(); it != body.end(); ++it ) {
        std::string value;
        if ( it.value().is_string() )
            value = it.value().get<std::string>();
        else
            value = it.value().dump();
        db.setUserSetting( uid, it.key(), value );
        ++count;
    }
    LOG_INFO << "用户设置保存成功 user_id=" << uid << " 键数=" << count;
    Server::sendJson( res, { { "success", true } } );
}

void settingsDelete( const httplib::Request &req, httplib::Response &res ) {
    std::string key = req.matches[1];
    auto uid = getOwnerUserId( req );
    App::getInstance()->getDatabase().deleteUserSetting( uid, key );
    Server::sendJson( res, { { "success", true } } );
}

} // namespace routes::settings

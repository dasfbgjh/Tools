#ifndef ROUTES_FILE_SERVICE_H
#define ROUTES_FILE_SERVICE_H

#include <httplib.h>
#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include "core/Auth.hpp"
#include "core/Utils.h"
#include "core/Server.h"
#include "common/App.h"
#include "common/Config.h"

namespace routes::fileService {

namespace fs = std::filesystem;

struct SharePerms {
    bool canAccess = false;
    bool canDownload = false;
    bool canUpload = false;
    bool canDelete = false;
    bool canRename = false;
};

void registerFileServiceRoutes( httplib::Server &svr );

// Compute effective permissions for a user on a share.
// anonymous permissions apply to everyone; 'all' applies to logged-in users;
// 'user' applies to the specific user.
SharePerms computeEffectivePerms( const std::string &shareId,
                                  const std::optional<core::auth::UserInfo> &user );

// Serialize share with user's effective permissions (user view)
Server::json shareToUserJson( const Database::Row &r, const SharePerms &perms );

void fileServiceList( const httplib::Request &req, httplib::Response &res );

void fileServiceListDir( const httplib::Request &req, httplib::Response &res );

void fileServiceSearch( const httplib::Request &req, httplib::Response &res );

void fileServiceDownload( const httplib::Request &req, httplib::Response &res );

void fileServiceUpload( const httplib::Request &req, httplib::Response &res,
                        const httplib::ContentReader &content_reader );

void fileServiceDelete( const httplib::Request &req, httplib::Response &res );

void fileServiceRename( const httplib::Request &req, httplib::Response &res );
} // namespace routes::fileService

#endif // ROUTES_FILE_SERVICE_H
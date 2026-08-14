#ifndef ROUTES_CLIPBOARD_H
#define ROUTES_CLIPBOARD_H

#include <httplib.h>
#include <filesystem>
#include <system_error>
#include "core/Auth.hpp"
#include "core/Utils.h"
#include "core/Database.h"
#include "common/App.h"
#include "common/Config.h"

namespace routes::clipboard {

namespace fs = std::filesystem;
using json = nlohmann::json;

void registerClipboardRoutes( httplib::Server &svr );

void clipboardList( const httplib::Request &req, httplib::Response &res );

void clipboardCreate( const httplib::Request &req, httplib::Response &res );

void clipboardDelete( const httplib::Request &req, httplib::Response &res );

void fileUpload( const httplib::Request &req, httplib::Response &res );

void fileRequestDownload( const httplib::Request &req, httplib::Response &res );

void fileDownload( const httplib::Request &req, httplib::Response &res );

// 将剪贴板项行转换为JSON
json clipboardToJson( const Database::Row &r );

} // namespace routes::clipboard

#endif // ROUTES_CLIPBOARD_H

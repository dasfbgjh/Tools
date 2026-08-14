#ifndef ROUTES_AUTH_H
#define ROUTES_AUTH_H

#include <httplib.h>
#include "common/App.h"
#include "core/Auth.hpp"
#include "core/Utils.h"

namespace routes::auth {
// Register all authentication-related routes on the given server.
void registerAuthRoutes( httplib::Server &svr );

void authRegister( const httplib::Request &req, httplib::Response &res );

void authLogin( const httplib::Request &req, httplib::Response &res );

void authLogout( const httplib::Request &req, httplib::Response &res );

void authMe( const httplib::Request &req, httplib::Response &res );

void authUpdateMe( const httplib::Request &req, httplib::Response &res );

} // namespace Routes::Auth
#endif // ROUTES_AUTH_H

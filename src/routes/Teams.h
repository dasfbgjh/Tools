#ifndef ROUTES_TEAMS_H
#define ROUTES_TEAMS_H

#include <httplib.h>
#include <ctime>
#include <string>
#include "core/Auth.hpp"
#include "core/Utils.h"
#include "common/App.h"
#include "common/Config.h"

namespace routes::teams {

// Register all team-related routes on the given server.
void registerTeamRoutes( httplib::Server &svr );

void teamsList( const httplib::Request &req, httplib::Response &res );

void teamsCreate( const httplib::Request &req, httplib::Response &res );

void teamsInviteValidate( const httplib::Request &req, httplib::Response &res );

void teamsJoin( const httplib::Request &req, httplib::Response &res );

void teamsInviteCreate( const httplib::Request &req, httplib::Response &res );

void teamsInviteRefresh( const httplib::Request &req, httplib::Response &res );

void teamsInviteInvalidate( const httplib::Request &req, httplib::Response &res );

void teamsDetail( const httplib::Request &req, httplib::Response &res );

void teamsDelete( const httplib::Request &req, httplib::Response &res );

void teamsLeave( const httplib::Request &req, httplib::Response &res );

void teamsMembers( const httplib::Request &req, httplib::Response &res );

void teamsRemoveMember( const httplib::Request &req, httplib::Response &res );
} // namespace routes::teams
#endif // ROUTES_TEAMS_H
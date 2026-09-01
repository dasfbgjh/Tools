#ifndef ROUTES_GAME_TOOL_H
#define ROUTES_GAME_TOOL_H

#include <httplib.h>

namespace routes::game {

void registerGameRoutes( httplib::Server &svr );

void shutdownGameHttpServer();

} // namespace routes::game

#endif // ROUTES_GAME_TOOL_H
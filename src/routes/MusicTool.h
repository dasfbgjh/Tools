#ifndef ROUTES_MUSIC_TOOL_H
#define ROUTES_MUSIC_TOOL_H

#include <httplib.h>

namespace routes::music {

void registerMusicRoutes( httplib::Server &svr );

} // namespace routes::music

#endif // ROUTES_MUSIC_TOOL_H

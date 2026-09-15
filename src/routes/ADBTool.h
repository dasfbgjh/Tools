#ifndef ADBTOOL_H
#define ADBTOOL_H

#include <httplib.h>

namespace routes::adb {

void registerADBRoutes( httplib::Server &svr );

} // namespace routes::adb
#endif
// ADBTOOL_H
#ifndef ROUTES_SYS_MONITOR_H
#define ROUTES_SYS_MONITOR_H

#include <httplib.h>

namespace routes::sysmonitor {

void registerSysMonitorRoutes( httplib::Server &svr );

} // namespace routes::sysmonitor

#endif // ROUTES_SYS_MONITOR_H

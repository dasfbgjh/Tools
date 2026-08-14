#ifndef ROUTES_SYS_MONITOR_H
#define ROUTES_SYS_MONITOR_H

#include <httplib.h>

namespace routes::sysmonitor {

// 注册系统监测工具的 HTTP 路由（仅限本机访问）
//  - GET  /api/sys/info        静态硬件/系统信息（OS/主板/CPU/内存/磁盘/GPU/网络）
//  - GET  /api/sys/cpu         实时 CPU 占用（所有线程平均 + 逐线程 + 频率）
//  - GET  /api/sys/ram         实时内存使用（总/已用/空闲/可用）
//  - GET  /api/sys/disks       实时各挂载点剩余空间
void registerSysMonitorRoutes( httplib::Server &svr );

} // namespace routes::sysmonitor

#endif // ROUTES_SYS_MONITOR_H

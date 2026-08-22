#ifndef ROUTES_DOC_TOOL_H
#define ROUTES_DOC_TOOL_H

#include <httplib.h>

namespace routes::docs {

void registerDocRoutes( httplib::Server &svr );

// 停止内部文档 HTTP 服务(主服务关闭时调用)
void shutdownDocHttpServer();

} // namespace routes::docs

#endif // ROUTES_DOC_TOOL_H

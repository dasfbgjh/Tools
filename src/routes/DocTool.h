#ifndef ROUTES_DOC_TOOL_H
#define ROUTES_DOC_TOOL_H

#include <httplib.h>

namespace routes::docs {

// 注册文档阅读工具的 HTTP 路由（仅限本机访问）
//  - GET    /api/docs/sources              列出全部文档源
//  - POST   /api/docs/sources              新增文档源(name + 本机目录)
//  - PUT    /api/docs/sources/:id          修改文档源(重命名 / 改目录)
//  - DELETE /api/docs/sources/:id          删除文档源
//  - GET    /api/docs/sources/:id          获取目录树(支持 ?path=&depth=)
//  - POST   /api/docs/source/select        选择文档源,启动/重配内部 HTTP 服务
//  - POST   /api/docs/source/deselect      取消选择(停止内部服务)
//  - GET    /api/docs/status               查询内部服务状态(baseUrl / port / source)
void registerDocRoutes( httplib::Server &svr );

// 停止内部文档 HTTP 服务(主服务关闭时调用)
void shutdownDocHttpServer();

} // namespace routes::docs

#endif // ROUTES_DOC_TOOL_H

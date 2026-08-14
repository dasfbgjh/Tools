#ifndef ROUTES_CERT_TOOL_H
#define ROUTES_CERT_TOOL_H

#include <httplib.h>
#include <string>

namespace routes::cert {

// 注册自签名证书生成工具的 HTTP 路由
//  - GET  /api/cert/info         获取 openssl 可用性 / 版本信息
//  - POST /api/cert/generate     生成自签名证书（请求体包含输出目录与证书参数）
void registerCertRoutes( httplib::Server &svr );

} // namespace routes::cert

#endif // ROUTES_CERT_TOOL_H

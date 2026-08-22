#ifndef ROUTES_OCR_TOOLS_H
#define ROUTES_OCR_TOOLS_H

#include <httplib.h>

namespace routes::ocrTools {

// 注册 OCR 相关路由
void registerOcrRoutes( httplib::Server &svr );

} // namespace routes::ocrTools

#endif // ROUTES_OCR_TOOLS_H

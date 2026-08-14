#ifndef ROUTES_OCR_TOOLS_H
#define ROUTES_OCR_TOOLS_H

#include <httplib.h>

namespace routes::ocrTools {

// 注册 OCR 相关路由
void registerOcrRoutes( httplib::Server &svr );

// 图片 OCR 识别接口：POST /api/tools/image/ocr
// multipart 上传 file 字段（图片），可选字段：maxSideLen / boxScoreThresh / unClipRatio / doAngle
void imageOcr( const httplib::Request &req, httplib::Response &res );

} // namespace routes::ocrTools

#endif // ROUTES_OCR_TOOLS_H

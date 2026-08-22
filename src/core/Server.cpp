#include "Server.h"
#include "common/Config.h"
#include "routes/Admin.h"
#include "routes/LocalTools.h"
#include "routes/Auth.h"
#include "routes/Teams.h"
#include "routes/Clipboard.h"
#include "routes/FileService.h"
#include "routes/Tools.h"
#include "routes/PdfTools.h"
#include "routes/Settings.h"
#include "routes/FfmpegTool.h"
#include "routes/OcrTools.h"
#include "routes/DocTool.h"

const char *Server::resourcePrefix = "/html";

Server::Server() {
#ifdef RESOURCE_PATH
    LOG_INFO << "(从本地读取资源仅适用于开发环境, 打包请移除宏对应RESOURCE_PATH, 使用资源文件)";
#endif
}

Server::~Server() {
}

void Server::registerhRoutes( httplib::Server &server ) {
    server.set_default_headers( { { "Cache-Control", "no-cache" } } );

    // 放开httplib默认请求体限制并增加超时：
    //  - 默认 payload 最大仅 100MB，200MB+ 上传会直接断连 (ERR_CONNECTION_RESET)
    //  - 默认 read/write 超时仅 5 秒，大文件慢网卡死会被超时断开

    server.set_payload_max_length( 1024UL * 1024 * 1024 * 4 ); // 4GB 最大上传文件大小
    server.set_read_timeout( 600, 0 );                         // 10 分钟
    server.set_write_timeout( 600, 0 );
    server.set_keep_alive_timeout( 60 );

    LOG_DEBUG << "注册管理员路由...";
    routes::admin::registerAdminRoutes( server ); // 注册管理员路由
    LOG_DEBUG << "注册本机工具路由...";
    routes::registerLocalTools( server ); // 注册本机工具路由（仅限本机访问）
    LOG_DEBUG << "注册授权路由...";
    routes::auth::registerAuthRoutes( server ); // 注册授权路由
    LOG_DEBUG << "注册团队路由...";
    routes::teams::registerTeamRoutes( server ); // 注册团队路由
    LOG_DEBUG << "注册剪贴板路由...";
    routes::clipboard::registerClipboardRoutes( server ); // 注册剪贴板路由
    LOG_DEBUG << "注册文件服务路由...";
    routes::fileService::registerFileServiceRoutes( server ); // 注册文件服务路由
    LOG_DEBUG << "注册工具路由...";
    routes::tools::registerToolRoutes( server ); // 注册工具路由
    LOG_DEBUG << "注册OCR工具路由...";
    routes::ocrTools::registerOcrRoutes( server ); // 注册OCR工具路由
    LOG_DEBUG << "注册PDF工具路由...";
    routes::pdfTools::registerPdfRoutes( server ); // 注册PDF工具路由
    LOG_DEBUG << "注册FFmpeg工具路由...";
    routes::ffmpeg::registerFfmpegRoutes( server ); // 注册FFmpeg工具路由
    LOG_DEBUG << "注册用户设置路由...";
    routes::settings::registerSettingsRoutes( server ); // 注册用户设置路由

    server.Get( "/webview", [this]( const httplib::Request &req, httplib::Response &res ) {
        res.set_content( R"(<!DOCTYPE html>
<html lang="zh-CN">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Webview</title>
    </head>
    <body>
        <script>
             __windowPage().then((page) => {
                document.write(page.content);
            });
        </script>
    </body>
</html>)",
                         "text/html" );
    } );

    server.Get( "/.well-known/appspecific/com.chrome.devtools.json", [this]( const httplib::Request &req, httplib::Response &res ) {
        sendJson( res, { {
                           "workspace",
                           {
                               //    { "root", "" },
                               { "uuid", utils::randomHex( 32 ) },
                           },
                       } } );
    } );

    // 服务静态文件路由  (catch-all, must be last)
    server.Get( R"(/(.*))", [this]( const httplib::Request &req, httplib::Response &res ) {
        serveStatic( req, res, "/" + std::string( req.matches[1] ) );
    } );

    server.set_pre_routing_handler( []( const httplib::Request &req, httplib::Response &res )
                                        -> httplib::Server::HandlerResponse {
        if ( req.params.empty() ) {
            LOG_DEBUG << req.method
                      << req.path
                      << "(来自:" << req.remote_addr << ")";
        } else {
            LOG_DEBUG << req.method
                      << req.path
                      << "(来自:" << req.remote_addr << ")"
                      << "参数:" << req.params;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    } );
}

void Server::startHttp( int port ) {
    m_server = std::make_shared<httplib::Server>();
    LOG_INFO << "注册http路由";
    registerhRoutes( *m_server );
    try {
        LOG_INFO << "HTTP服务已经启动 http://127.0.0.1:" + std::to_string( port );
        m_server->listen( "0.0.0.0", port );
    } catch ( const std::exception &e ) {
        LOG_ERROR << "HTTP服务异常: " << e.what();
    }
    LOG_INFO << "HTTP服务已经停止";
}

void Server::startHttps( int port, const std::string &certPath, const std::string &keyPath ) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    m_sslServer = std::make_shared<httplib::SSLServer>( certPath.c_str(), keyPath.c_str() );

    if ( !m_sslServer->is_valid() ) {
        LOG_ERROR << "HTTPS服务初始化失败: 证书或密钥无效";
        return;
    }

    LOG_INFO << "注册https路由";
    registerhRoutes( *m_sslServer );
    try {
        LOG_INFO << "HTTPS服务已经启动 https://127.0.0.1:" + std::to_string( port );
        m_sslServer->listen( "0.0.0.0", port );
    } catch ( const std::exception &e ) {
        LOG_ERROR << "HTTPS服务异常: " << e.what();
    }
    LOG_INFO << "HTTPS服务已经停止";
#else
    LOG_ERROR << "HTTPS not supported in this build";
#endif
}

std::tuple<std::thread, std::thread> Server::listen() {
    LOG_DEBUG << "启动HTTP服务线程,端口: " << Config::getHttpServerPort();
    std::thread httpServerThread = std::thread(
        std::bind( &Server::startHttp, this, Config::getHttpServerPort() ) );

    std::thread httpsServerThread;
    if ( Config::getEnableHttps() && !Config::getSslCertPath().empty() && !Config::getSslKeyPath().empty() ) {
        LOG_DEBUG << "启动HTTPS服务线程,端口: " << Config::getHttpsServerPort();
        httpsServerThread = std::thread(
            std::bind( &Server::startHttps, this, Config::getHttpsServerPort(), Config::getSslCertPath(), Config::getSslKeyPath() ) );
    } else if ( Config::getEnableHttps() ) {
        LOG_WARN << "已启用HTTPS但缺少证书或私钥路径，HTTPS未启动";
    }

    return std::tuple<std::thread, std::thread>( std::move( httpServerThread ), std::move( httpsServerThread ) );
}

void Server::stop() {
    LOG_INFO << "服务停止中...";
    if ( m_server )
        m_server->stop();
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if ( m_sslServer )
        m_sslServer->stop();
#endif
    // 关闭文档阅读工具的内部 HTTP 服务(若有)
    routes::docs::shutdownDocHttpServer();
}

bool Server::resourceExists( const std::string &resName ) {
#ifdef RESOURCE_PATH
    return std::filesystem::exists( RESOURCE_PATH + resName );
#else
    return resource_exists( resName.c_str() );
#endif
}

bool Server::serveResource( httplib::Response &res, const std::string &resName ) {
#ifdef RESOURCE_PATH
    std::string fullPath = RESOURCE_PATH + resName;
    std::string content;
    if ( !utils::fs::readFile( fullPath, content ) )
        return false;
    res.set_content( content, contentType( resName ).c_str() );
    return true;
#else
    const unsigned char *data = nullptr;
    int size = resource_get( resName.c_str(), &data );
    if ( size < 0 || !data )
        return false;
    res.set_content( reinterpret_cast<const char *>( data ), static_cast<size_t>( size ),
                     contentType( resName ).c_str() );
    return true;
#endif
}

void Server::redirectPage( httplib::Response &res, const std::string &path ) {
    std::string html = "<html>"
                       "<head> <title>重定向页</title> </head>"
                       "<body> <script>";
    html += "var params = window.location.search; window.location.href = '" + path + "' + params;";
    html += "</script> </body>"
            "</html>";
    res.set_content( html, "text/html; charset=utf-8" );
};

void Server::serveStatic( const httplib::Request &req, httplib::Response &res, const std::string &path ) {
    if ( ( utils::startsWith( path, "/admin" ) || utils::startsWith( path, "/local-tools" ) ) && !isLocalhost( req ) ) {
        LOG_WARN << "非本机访问敏感页面" << req.remote_addr;
        res.status = 403;
        res.set_content( "<html> <head> <title>403 Forbidden</title> </head> <body> "
                         "<h1>403 Forbidden</h1> <p>您没有权限访问此页面。</p> "
                         "</body> </html>",
                         "text/html; charset=utf-8" );
        return;
    }
    std::string fullPath = resourcePrefix + path;
    if ( serveResource( res, fullPath ) ) {
        LOG_DEBUG << "静态文件命中: " << fullPath;
        return;
    }
    if ( path.find( '.' ) == std::string::npos ) {
        std::string base = path;
        while ( base.size() > 0 && base.back() == '/' )
            base.pop_back();
        if ( resourceExists( resourcePrefix + base + "/index.html" ) ) {
            redirectPage( res, base + "/index.html" );
            LOG_DEBUG << "重定向:" << path << "=>" << base + "/index.html";
            return;
        }
        if ( resourceExists( resourcePrefix + base + ".html" ) ) {
            redirectPage( res, base + ".html" );
            LOG_DEBUG << "重定向:" << path << "=>" << base + ".html";
            return;
        }
    }
    LOG_WARN << "静态文件404: " << fullPath;
    res.status = 404;
    serveResource( res, resourcePrefix + std::string( "/404.html" ) );
}

/*-------------------------------------------------------*/
void Server::sendJson( httplib::Response &res, const json &j, int status ) {
    try {
        res.status = status;
        res.set_content( j.dump(), "application/json; charset=utf-8" );
    } catch ( const std::exception &e ) {
        json err( { { "success", false }, { "error", e.what() } } );
        res.status = 500;
        res.set_content( err.dump(), "application/json; charset=utf-8" );
        LOG_WARN << "响应失败:" << e.what();
    }
}

void Server::sendError( httplib::Response &res, const std::string &msg, int status ) {
    if ( status >= 400 && status < 500 )
        LOG_WARN << "响应客户端错误 " << status << ": " << msg;
    else if ( status >= 500 )
        LOG_ERROR << "响应服务端错误 " << status << ": " << msg;
    sendJson( res, { { "success", false }, { "error", msg } }, status );
}

Server::json Server::parseBody( const httplib::Request &req ) {
    if ( req.body.empty() )
        return json::object();
    try {
        return json::parse( req.body );
    } catch ( const std::exception &e ) {
        LOG_WARN << "解析请求体JSON失败: " << e.what() << " body前50字节: " << req.body.substr( 0, 50 );
        return json( nullptr );
    }
}

std::string Server::queryParam( const httplib::Request &req, const std::string &name ) {
    if ( req.has_param( name ) )
        return req.get_param_value( name );
    return "";
}

std::string Server::contentType( const std::filesystem::path &path ) {
    std::string ext = utils::toLower( path.extension().string() );
    if ( ext == ".html" || ext == ".htm" )
        return "text/html; charset=utf-8";
    if ( ext == ".css" )
        return "text/css; charset=utf-8";
    if ( ext == ".js" || ext == ".mjs" )
        return "application/javascript; charset=utf-8";
    if ( ext == ".json" )
        return "application/json; charset=utf-8";
    if ( ext == ".txt" || ext == ".log" )
        return "text/plain; charset=utf-8";
    if ( ext == ".csv" )
        return "text/csv; charset=utf-8";
    if ( ext == ".xml" )
        return "application/xml; charset=utf-8";
    if ( ext == ".svg" )
        return "image/svg+xml";
    if ( ext == ".png" )
        return "image/png";
    if ( ext == ".jpg" || ext == ".jpeg" )
        return "image/jpeg";
    if ( ext == ".gif" )
        return "image/gif";
    if ( ext == ".webp" )
        return "image/webp";
    if ( ext == ".ico" )
        return "image/x-icon";
    if ( ext == ".bmp" )
        return "image/bmp";
    if ( ext == ".pdf" )
        return "application/pdf";
    if ( ext == ".woff" )
        return "font/woff";
    if ( ext == ".woff2" )
        return "font/woff2";
    if ( ext == ".ttf" )
        return "font/ttf";
    if ( ext == ".otf" )
        return "font/otf";
    if ( ext == ".mp4" )
        return "video/mp4";
    if ( ext == ".webm" )
        return "video/webm";
    if ( ext == ".mp3" )
        return "audio/mpeg";
    if ( ext == ".wav" )
        return "audio/wav";
    return "application/octet-stream";
}

std::string Server::staticResource( const std::string &resName, const std::string &prefix ) {
#ifdef RESOURCE_PATH
    std::string fullPath = RESOURCE_PATH + prefix + resName;
    std::string content;
    if ( utils::fs::readFile( fullPath, content ) )
        return content;
    return "";
#else
    const std::string name = prefix + resName;
    const unsigned char *data = nullptr;
    int size = resource_get( name.c_str(), &data );
    if ( size < 0 || !data )
        return "";
    return std::string( data, data + size );
#endif
}

bool Server::isLocalhost( const httplib::Request &req ) {
    return req.remote_addr == "127.0.0.1" || req.remote_addr == "::1";
}

bool Server::guardLocalhost( const httplib::Request &req, httplib::Response &res ) {
    if ( !isLocalhost( req ) ) {
        sendError( res, "禁止访问：仅限本机", 403 );
        return true;
    }
    return false;
}
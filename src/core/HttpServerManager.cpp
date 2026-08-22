#include "HttpServerManager.h"
#include "common/App.h"
#include "common/Config.h"
#include "common/Logger.hpp"
#include "core/Database.h"
#include "core/Server.h"
#include <chrono>
#include <sstream>

HttpServerManager &HttpServerManager::instance() {
    static HttpServerManager inst;
    return inst;
}

HttpServerInstance *HttpServerManager::findInstance( const std::string &id ) const {
    auto it = m_instances.find( id );
    return it == m_instances.end() ? nullptr : it->second.get();
}

HttpServerManager::~HttpServerManager() {
    shutdownAll();
}

static httplib::Server::Handler createProxyHandler( const std::string &reqPath, const std::string &targetUrl ) {
    return [reqPath, targetUrl]( const httplib::Request &req, httplib::Response &res ) {
        httplib::Client cli( targetUrl );
        cli.set_connection_timeout( 5, 0 );
        cli.set_read_timeout( 60, 0 );
        cli.set_write_timeout( 60, 0 );
        // 跟随重定向
        cli.set_follow_location( true );

        std::string path = req.path;
        if ( utils::startsWith( path, reqPath ) ) {
            path = path.substr( reqPath.size() );
        }
        if ( path.empty() )
            path = "/";

        // 过滤 hop-by-hop 头
        std::vector<std::string> skipHeaders = {
            "Connection", "Keep-Alive", "Proxy-Authenticate", "Proxy-Authorization",
            "TE", "Trailers", "Transfer-Encoding", "Upgrade", "Host" };
        httplib::Headers fwdHeaders;
        for ( auto &h : req.headers ) {
            bool skip = false;
            for ( auto &s : skipHeaders ) {
                if ( h.first == s ) {
                    skip = true;
                    break;
                }
            }
            if ( !skip )
                fwdHeaders.emplace( h.first, h.second );
        }

        auto contentTypeIt = req.headers.find( "Content-Type" );
        std::string contentType = ( contentTypeIt != req.headers.end() ) ? contentTypeIt->second : "application/octet-stream";

        httplib::Result cliRes;
        if ( req.method == "GET" ) {
            cliRes = cli.Get( path, req.params, fwdHeaders );
        } else if ( req.method == "POST" ) {
            cliRes = cli.Post( path, req.headers, req.body, contentType );
        } else if ( req.method == "PUT" ) {
            cliRes = cli.Put( path, req.headers, req.body, contentType );
        } else if ( req.method == "DELETE" ) {
            cliRes = cli.Delete( path, fwdHeaders );
        } else if ( req.method == "PATCH" ) {
            cliRes = cli.Patch( path, req.headers, req.body, contentType );
        } else if ( req.method == "OPTIONS" ) {
            cliRes = cli.Options( path, fwdHeaders );
        } else {
            res.status = 405;
            res.set_content( "Method not supported by proxy", "text/plain; charset=utf-8" );
            return;
        }

        if ( cliRes ) {
            res.status = cliRes->status;
            for ( auto &h : cliRes->headers ) {
                bool skip = false;
                for ( auto &s : skipHeaders ) {
                    if ( h.first == s ) {
                        skip = true;
                        break;
                    }
                }
                // 不透传 Content-Length / Transfer-Encoding（httplib 会自动处理）
                if ( h.first == "Content-Length" || h.first == "Transfer-Encoding" )
                    skip = true;
                if ( !skip )
                    res.set_header( h.first.c_str(), h.second.c_str() );
            }
            res.body = std::move( cliRes->body );
        } else {
            res.status = 502;
            auto err = httplib::to_string( cliRes.error() );
            res.set_content( "{\"success\":false,\"error\":\"proxy error: " + err + "\"}",
                             "application/json; charset=utf-8" );
        }
    };
}

bool HttpServerManager::start( const std::string &id ) {
    std::lock_guard<std::mutex> lock( m_mtx );

    // 已运行？幂等
    if ( auto *p = findInstance( id ) ) {
        if ( p->running )
            return true;
        // 否则清理死实例
        if ( p->thread.joinable() )
            p->thread.join();
        m_instances.erase( id );
    }

    auto &db = App::getInstance()->getDatabase();
    auto rows = db.query( "SELECT * FROM http_servers WHERE id='" + Database::sqlEscape( id ) + "'" );
    if ( rows.empty() ) {
        LOG_WARN << "HttpServerManager::start 找不到配置 id=" << id;
        return false;
    }
    auto &row = rows[0];
    int port = std::stoi( row["port"] );
    std::string name = row["name"];

    auto mountRows = db.query(
        "SELECT path, source FROM http_server_mounts WHERE server_id='" + Database::sqlEscape( id ) +
        "' ORDER BY sort_order ASC, id ASC" );

    auto inst = std::make_unique<HttpServerInstance>();
    inst->id = id;
    inst->port = port;
    inst->server = std::make_unique<httplib::Server>();

    // 路径日志前缀
    std::string logTag = "[HTTP/" + name + "]";

    for ( auto &m : mountRows ) {
        std::string path = m["path"];
        std::string source = m["source"];
        if ( path.empty() )
            continue;
        if ( source.empty() )
            continue;
        if ( utils::startsWith( source, "http://" ) || utils::startsWith( source, "https://" ) ) {
            // 代理：捕获所有子路径
            inst->server->Get( path + "(.*)", createProxyHandler( path, source ) );
            inst->server->Post( path + "(.*)", createProxyHandler( path, source ) );
            inst->server->Put( path + "(.*)", createProxyHandler( path, source ) );
            inst->server->Delete( path + "(.*)", createProxyHandler( path, source ) );
            inst->server->Patch( path + "(.*)", createProxyHandler( path, source ) );
            inst->server->Options( path + "(.*)", createProxyHandler( path, source ) );
            LOG_INFO << logTag << " 代理: " << path << " -> " << source;
        } else {
            // 静态资源：httplib 要求 mount_point 末尾不含通配符，且内部使用前缀匹配
            inst->server->set_mount_point( path, source );
            LOG_INFO << logTag << " 静态资源: " << path << " -> " << source;
        }
    }

    // 健康检查
    inst->server->Get( "/__health", []( const httplib::Request &req, httplib::Response &res ) {
        res.set_content( "{\"status\":\"ok\"}", "application/json; charset=utf-8" );
    } );

    // 请求日志
    inst->server->set_pre_routing_handler( [logTag]( const httplib::Request &req, httplib::Response &res ) {
        LOG_INFO << logTag << req.method << req.path << " 来自: " << req.remote_addr;
        return httplib::Server::HandlerResponse::Unhandled;
    } );

    // 404 处理
    inst->server->set_error_handler( [logTag, name]( const httplib::Request &req, httplib::Response &res ) {
        if ( res.status == 404 ) {
            res.set_content(
                "<html><head><meta charset=\"UTF-8\"><title>404</title></head>"
                "<body style=\"font-family:Arial,sans-serif;text-align:center;margin-top:80px;\">"
                "<h1>404 - Not Found</h1>"
                "<p>" +
                    logTag + " " + req.path + "</p>"
                                              "</body></html>",
                "text/html; charset=utf-8" );
        }
    } );

    // listen 是阻塞的，需要后台线程
    auto *serverPtr = inst->server.get();
    inst->thread = std::thread( [serverPtr, port, &running = inst->running, &err = inst->lastError, logTag] {
        LOG_INFO << logTag << " 正在监听 0.0.0.0:" << port;
        bool ok = serverPtr->listen( "0.0.0.0", port );
        running = false;
        if ( !ok ) {
            err = "端口 " + std::to_string( port ) + " 监听失败（可能已被占用）";
            LOG_ERROR << logTag << err;
        } else {
            LOG_INFO << logTag << " 已停止监听";
        }
    } );
    inst->running = true;

    // 短暂等待以检测 listen() 是否立即失败（端口占用）
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( 300 );
    while ( std::chrono::steady_clock::now() < deadline ) {
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        if ( !inst->running )
            break; // listen 立即返回 = 失败
    }

    if ( !inst->running ) {
        // 启动失败
        std::string err = inst->lastError.empty() ? ( "端口 " + std::to_string( port ) + " 启动失败" ) : inst->lastError;
        if ( inst->thread.joinable() )
            inst->thread.join();
        // 不放入 m_instances
        // 更新 DB 状态
        try {
            db.execParams( "UPDATE http_servers SET status=?, error_msg=?, updated_at=? WHERE id=?",
                           { { 1, "error" }, { 2, err }, { 3, utils::nowIso() }, { 4, id } } );
        } catch ( ... ) {
        }
        LOG_ERROR << "HttpServerManager::start 启动失败 id=" << id << " err=" << err;
        return false;
    }

    m_instances[id] = std::move( inst );
    try {
        db.execParams( "UPDATE http_servers SET status=?, error_msg='', updated_at=? WHERE id=?",
                       { { 1, "running" }, { 2, utils::nowIso() }, { 3, id } } );
    } catch ( ... ) {
    }
    LOG_INFO << "HttpServerManager::start 成功 id=" << id << " port=" << port;
    return true;
}

bool HttpServerManager::stop( const std::string &id ) {
    HttpServerInstance *p = nullptr;
    {
        std::lock_guard<std::mutex> lock( m_mtx );
        p = findInstance( id );
    }
    if ( !p ) {
        // 未在运行，更新 DB 状态
        try {
            App::getInstance()->getDatabase().execParams(
                "UPDATE http_servers SET status=?, error_msg='' WHERE id=? AND status!='stopped'",
                { { 1, "stopped" }, { 2, id } } );
        } catch ( ... ) {
        }
        return true;
    }
    LOG_INFO << "HttpServerManager::stop id=" << id;
    if ( p->server )
        p->server->stop();
    if ( p->thread.joinable() )
        p->thread.join();
    p->running = false;
    {
        std::lock_guard<std::mutex> lock( m_mtx );
        m_instances.erase( id );
    }
    try {
        App::getInstance()->getDatabase().execParams(
            "UPDATE http_servers SET status=?, error_msg='', updated_at=? WHERE id=?",
            { { 1, "stopped" }, { 2, utils::nowIso() }, { 3, id } } );
    } catch ( ... ) {
    }
    return true;
}

bool HttpServerManager::isRunning( const std::string &id ) const {
    std::lock_guard<std::mutex> lock( m_mtx );
    auto *p = findInstance( id );
    return p && p->running;
}

std::string HttpServerManager::status( const std::string &id ) const {
    std::lock_guard<std::mutex> lock( m_mtx );
    auto *p = findInstance( id );
    if ( !p )
        return "stopped";
    if ( p->running )
        return "running";
    return "error";
}

std::vector<int> HttpServerManager::listeningPorts() const {
    std::lock_guard<std::mutex> lock( m_mtx );
    std::vector<int> out;
    for ( auto &kv : m_instances ) {
        if ( kv.second->running )
            out.push_back( kv.second->port );
    }
    return out;
}

void HttpServerManager::startAutoStart() {
    auto &db = App::getInstance()->getDatabase();
    auto rows = db.query( "SELECT id FROM http_servers WHERE auto_start=1" );
    for ( auto &r : rows ) {
        start( r["id"] );
    }
}

void HttpServerManager::shutdownAll() {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock( m_mtx );
        for ( auto &kv : m_instances )
            ids.push_back( kv.first );
    }
    for ( auto &id : ids ) {
        stop( id );
    }
}

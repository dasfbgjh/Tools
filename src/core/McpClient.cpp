#include "core/McpClient.h"

#include <httplib.h>
#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <future>
#include <sstream>
#include <utility>
#include "common/Logger.hpp"
#include "common/EventLoop.h"

#ifdef _WIN32
#include <windows.h>
#endif

// ===========================================================================
// MCP 客户端实现
//  - Client::Error            : 异常类型
//  - HttpTransport            : 基于 httplib 的 Streamable HTTP 传输
//                               (兼容 legacy SSE: 先 GET /mcp 取 endpoint 再 POST)
//  - Client::Impl             : 客户端状态 (id 自增 / 握手结果 / 回调)
//  - Client 各公开方法
// ===========================================================================

namespace mcp {

// =========================================================================
// Client::Error
// =========================================================================
Client::Error::Error( const std::string &msg, int httpStatus,
                      std::optional<core::RpcError> rpcError )
    : std::runtime_error( msg ), m_httpStatus( httpStatus ), m_rpcError( std::move( rpcError ) ) {
}

int Client::Error::httpStatus() const {
    return m_httpStatus;
}

const std::optional<core::RpcError> &Client::Error::rpcError() const {
    return m_rpcError;
}

// =========================================================================
// 内部辅助: URL 解析 / SSE 事件解析
// =========================================================================
namespace {

struct ParsedUrl {
    std::string scheme = "http";
    std::string host;
    int port = 80;
    std::string path = "/mcp"; // 含前导 '/'
    std::string error;         // 非空 = 解析失败
};

ParsedUrl parseUrl( const std::string &url ) {
    ParsedUrl r;
    if ( url.empty() ) {
        r.error = "endpoint 为空";
        return r;
    }
    std::string s = url;
    while ( !s.empty() && std::isspace( (unsigned char)s.front() ) )
        s.erase( s.begin() );
    while ( !s.empty() && std::isspace( (unsigned char)s.back() ) )
        s.pop_back();

    std::string rest = s;
    auto posScheme = s.find( "://" );
    if ( posScheme != std::string::npos ) {
        r.scheme = s.substr( 0, posScheme );
        rest = s.substr( posScheme + 3 );
        std::transform( r.scheme.begin(), r.scheme.end(), r.scheme.begin(),
                        []( unsigned char c ) { return std::tolower( c ); } );
    }

    auto slash = rest.find( '/' );
    std::string auth = ( slash == std::string::npos ) ? rest : rest.substr( 0, slash );
    std::string p = ( slash == std::string::npos ) ? "" : rest.substr( slash );
    if ( p.empty() )
        p = "/mcp";
    r.path = p;

    if ( auth.empty() ) {
        r.error = "endpoint 缺少 host";
        return r;
    }
    auto colon = auth.find( ':' );
    if ( colon != std::string::npos ) {
        r.host = auth.substr( 0, colon );
        std::string portStr = auth.substr( colon + 1 );
        try {
            int pv = std::stoi( portStr );
            if ( pv <= 0 || pv > 65535 ) {
                r.error = "端口范围无效: " + portStr;
                return r;
            }
            r.port = pv;
        } catch ( ... ) {
            r.error = "端口解析失败: " + portStr;
            return r;
        }
    } else {
        r.host = auth;
        r.port = ( r.scheme == "https" ) ? 443 : 80;
    }
    if ( r.scheme != "http" && r.scheme != "https" ) {
        r.error = "仅支持 http(s) 协议; 传入 scheme=" + r.scheme;
        return r;
    }
    return r;
}

struct SseEvent {
    std::string name;
    std::string data;
};

// 解析 text/event-stream body 为事件列表
// - "event: xxx" 行设定当前事件名 (缺省为 "message")
// - "data: yyy" 行累加 (多行以 \n 连接)
// - 空行 flush 一个事件
std::vector<SseEvent> parseSseEvents( const std::string &body ) {
    std::vector<SseEvent> events;
    std::istringstream stream( body );
    std::string line;
    std::string currentEvent;
    std::string currentData;
    auto trimLeadSpace = []( const std::string &v ) -> std::string {
        size_t s = v.find_first_not_of( ' ' );
        return ( s == std::string::npos ) ? std::string{} : v.substr( s );
    };
    auto flush = [&]() {
        if ( currentEvent.empty() && currentData.empty() )
            return;
        std::string name = currentEvent.empty() ? std::string( "message" ) : currentEvent;
        events.push_back( { std::move( name ), std::move( currentData ) } );
        currentEvent.clear();
        currentData.clear();
    };
    while ( std::getline( stream, line ) ) {
        if ( !line.empty() && line.back() == '\r' )
            line.pop_back();
        if ( line.empty() ) {
            flush();
            continue;
        }
        if ( line[0] == ':' )
            continue; // SSE 注释
        if ( line.substr( 0, 6 ) == "event:" ) {
            currentEvent = trimLeadSpace( line.substr( 6 ) );
        } else if ( line.substr( 0, 5 ) == "data:" ) {
            std::string d = trimLeadSpace( line.substr( 5 ) );
            if ( !currentData.empty() )
                currentData += "\n";
            currentData += d;
        }
        // 忽略 id: / retry: 等其它字段
    }
    flush(); // 末尾未以空行结束的事件
    return events;
}

// 把 endpoint 事件的数据归一化为可 POST 的路径
// - 完整 URL (含 ://) -> 取其 path
// - 相对路径 -> 确保以 '/' 开头
std::string normalizeEndpointPath( const std::string &ep, const std::string &fallback ) {
    if ( ep.empty() )
        return fallback;
    if ( ep.find( "://" ) != std::string::npos ) {
        ParsedUrl u = parseUrl( ep );
        return ( u.error.empty() && !u.path.empty() ) ? u.path : fallback;
    }
    std::string p = ep;
    if ( p[0] != '/' )
        p = "/" + p;
    return p;
}

} // namespace

// =========================================================================
// HttpTransport : 基于 httplib 的传输实现
// =========================================================================
class HttpTransport : public Client::Transport {
public:
    explicit HttpTransport( const Client::HttpOptions &opts ) : m_opts( opts ) {
        m_parsed = parseUrl( opts.url );
        if ( !m_parsed.error.empty() )
            throw Client::Error( "MCP endpoint URL 无效: " + m_parsed.error, 0 );
    }

    core::RpcResponse sendRequest( const core::RpcRequest &req ) override {
        const std::string bodyText = core::json( req ).dump();
        const std::string path = targetPath();
        auto cli = makeClient();
        httplib::Headers h = buildHeaders();
        auto res = cli->Post( path, h, bodyText, "application/json" );
        if ( !res )
            throw Client::Error( "HTTP 请求失败: 无响应 (连接超时或目标未开放)", 0 );
        return processReply( res->status, res->body, res->headers, req.id );
    }

    void sendNotification( const core::RpcNotification &n ) override {
        const std::string bodyText = core::json( n ).dump();
        const std::string path = targetPath();
        auto cli = makeClient();
        httplib::Headers h = buildHeaders();
        auto res = cli->Post( path, h, bodyText, "application/json" );
        if ( !res )
            throw Client::Error( "发送通知失败: 无响应", 0 );
        if ( res->status >= 400 )
            throw Client::Error( "发送通知失败: HTTP " + std::to_string( res->status ), res->status );
        // 2xx (含 202 空响应) 视为成功
    }

    void sendResponse( const core::RpcResponse &resp ) override {
        const std::string bodyText = core::json( resp ).dump();
        std::string path;
        {
            std::lock_guard<std::mutex> lk( m_mtx );
            path = m_sseEndpoint.empty() ? m_parsed.path : m_sseEndpoint;
        }
        auto cli = makeClient();
        httplib::Headers h = buildHeaders();
        auto res = cli->Post( path, h, bodyText, "application/json" );
        if ( !res )
            throw Client::Error( "发送响应失败: 无响应", 0 );
        if ( res->status >= 400 )
            throw Client::Error( "发送响应失败: HTTP " + std::to_string( res->status ), res->status );
    }

    void setMessageHandler( MessageHandler handler ) override {
        std::lock_guard<std::mutex> lk( m_mtx );
        m_messageHandler = std::move( handler );
    }

    void close() override {
        std::lock_guard<std::mutex> lk( m_mtx );
        m_sseEndpoint.clear();
    }

private:
    Client::HttpOptions m_opts;
    ParsedUrl m_parsed;
    mutable std::mutex m_mtx;
    std::string m_sseEndpoint;           // legacy SSE 模式下从 endpoint 事件获取的 POST 路径
    MessageHandler m_messageHandler;

    std::shared_ptr<httplib::Client> makeClient() const {
        const std::string hostUrl =
            m_parsed.scheme + "://" + m_parsed.host + ":" + std::to_string( m_parsed.port );
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
        if ( m_parsed.scheme == "https" )
            throw Client::Error( "HTTPS 未启用: 项目未编译 OpenSSL (CPPHTTPLIB_OPENSSL_SUPPORT)", 0 );
#endif
        auto cli = std::make_shared<httplib::Client>( hostUrl );
        auto sec = m_opts.timeout.count();
        if ( sec < 1 )
            sec = 1;
        cli->set_read_timeout( sec, 0 );
        cli->set_write_timeout( sec, 0 );
        cli->set_connection_timeout( sec, 0 );
        return cli;
    }

    httplib::Headers buildHeaders() const {
        httplib::Headers h;
        h.emplace( "Accept", "application/json, text/event-stream" );
        h.emplace( "Content-Type", "application/json" );
        for ( const auto &kv : m_opts.headers )
            h.emplace( kv.first, kv.second );
        if ( !m_opts.authToken.empty() )
            h.emplace( "Authorization", "Bearer " + m_opts.authToken );
        return h;
    }

    // 当前请求应 POST 的路径
    std::string targetPath() {
        if ( !m_opts.sseLegacyMode )
            return m_parsed.path;
        return ensureSseSession();
    }

    // legacy SSE 模式: 先 GET /mcp 建立 SSE 会话, 从 endpoint 事件拿到 POST 路径
    std::string ensureSseSession() {
        {
            std::lock_guard<std::mutex> lk( m_mtx );
            if ( !m_sseEndpoint.empty() )
                return m_sseEndpoint;
        }
        auto cli = makeClient();
        httplib::Headers gh;
        gh.emplace( "Accept", "text/event-stream" );
        for ( const auto &kv : m_opts.headers )
            gh.emplace( kv.first, kv.second );
        if ( !m_opts.authToken.empty() )
            gh.emplace( "Authorization", "Bearer " + m_opts.authToken );

        auto res = cli->Get( m_parsed.path, gh );
        if ( !res )
            throw Client::Error( "SSE 会话建立失败: GET 无响应", 0 );
        if ( res->body.empty() )
            throw Client::Error( "SSE 会话建立失败: 空 body", res->status );

        std::string ep;
        for ( const auto &ev : parseSseEvents( res->body ) ) {
            if ( ev.name == "endpoint" && !ev.data.empty() ) {
                ep = ev.data;
                break;
            }
        }
        if ( ep.empty() )
            throw Client::Error( "SSE 会话建立失败: 未收到 endpoint 事件", res->status );
        ep = normalizeEndpointPath( ep, m_parsed.path );
        {
            std::lock_guard<std::mutex> lk( m_mtx );
            m_sseEndpoint = ep;
        }
        LOG_DEBUG << "MCP client: SSE 会话已建立, endpoint=" << ep;
        return ep;
    }

    // 把 HTTP 响应归约为匹配 expectedId 的 RpcResponse
    // 过程中收到的服务端通知 / 服务端请求通过 m_messageHandler 分派,
    // 若需要回复则立即 sendResponse
    core::RpcResponse processReply( int status, const std::string &body,
                                    const httplib::Headers &headers,
                                    const core::RequestId &expectedId ) {
        bool isSse = false;
        {
            auto it = headers.find( "Content-Type" );
            if ( it != headers.end() && it->second.find( "text/event-stream" ) != std::string::npos )
                isSse = true;
        }

        if ( isSse ) {
            auto events = parseSseEvents( body );
            std::optional<core::RpcResponse> matched;
            MessageHandler handler;
            {
                std::lock_guard<std::mutex> lk( m_mtx );
                handler = m_messageHandler;
            }
            for ( const auto &ev : events ) {
                if ( ev.name == "endpoint" && !ev.data.empty() ) {
                    std::lock_guard<std::mutex> lk( m_mtx );
                    m_sseEndpoint = normalizeEndpointPath( ev.data, m_parsed.path );
                    continue;
                }
                if ( ev.name != "message" || ev.data.empty() )
                    continue;
                core::Message msg;
                try {
                    msg = core::Message::parseMessage( ev.data );
                } catch ( ... ) {
                    continue;
                }
                if ( msg.type == core::Message::Type::Response && msg.response ) {
                    if ( !matched && msg.response->id == expectedId )
                        matched = *msg.response;
                } else if ( handler ) {
                    auto reply = handler( msg );
                    if ( reply ) {
                        try {
                            sendResponse( *reply );
                        } catch ( const std::exception &e ) {
                            LOG_WARN << "MCP client: 回复服务端请求失败: " << e.what();
                        }
                    }
                }
            }
            if ( matched )
                return *matched;
            throw Client::Error( "SSE 响应中未找到匹配 id 的 JSON-RPC 响应", status );
        }

        // 非 SSE: 直接 JSON 响应
        if ( body.empty() )
            throw Client::Error( "服务端返回空响应 (HTTP " + std::to_string( status ) + ")", status );

        core::Message msg;
        try {
            msg = core::Message::parseMessage( body );
        } catch ( const std::exception &e ) {
            throw Client::Error( std::string( "响应 JSON 解析失败: " ) + e.what(), status );
        }
        if ( msg.type == core::Message::Type::Response && msg.response )
            return *msg.response;

        // 服务端在直接 JSON 里发来请求/通知 (少见), 交给分派
        MessageHandler handler;
        {
            std::lock_guard<std::mutex> lk( m_mtx );
            handler = m_messageHandler;
        }
        if ( handler ) {
            auto reply = handler( msg );
            if ( reply )
                sendResponse( *reply );
        }
        throw Client::Error( "服务端响应不是 JSON-RPC response (HTTP " + std::to_string( status ) + ")", status );
    }
};

// =========================================================================
// StdioTransport : 启动子进程, 经 stdin/stdout 交换换行分隔 JSON-RPC
//
// 线程模型:
//  - io 线程: 跑独立 io_context, 异步读子进程 stdout; 解析后:
//      · Response -> 按 id 唤醒等待中的 sendRequest
//      · Notification/Request -> 投递到回调线程, 不阻塞 io 线程 (避免嵌套调用死锁)
//  - 回调线程: 串行执行用户 handler; 服务端 Request 的返回响应写回 stdin
//  - 调用线程: sendRequest/sendNotification/sendResponse 同步写 stdin (互斥)
// 关闭: cancel 读 -> 退出 io 线程 -> 退出回调线程 -> 终止子进程 -> 失败所有 pending
// =========================================================================
class StdioTransport : public Client::Transport {
public:
    using error_code = boost::system::error_code;

    explicit StdioTransport( const Client::StdioOptions &opts )
        : m_opts( opts ),
          m_work( boost::asio::make_work_guard( m_io ) ) {
        if ( opts.command.empty() )
            throw Client::Error( "stdio command 为空", 0 );

        std::string exe = EventLoop::processPath( opts.command.front() );
        if ( exe.empty() )
            throw Client::Error( "找不到可执行文件: " + opts.command.front(), 0 );
        std::vector<std::string> argv( opts.command.begin() + 1, opts.command.end() );

        auto env = EventLoop::currentEnv();
        for ( const auto &kv : opts.env )
            env[kv.first] = kv.second;
        std::vector<std::string> envVec;
        envVec.reserve( env.size() );
        for ( const auto &e : env )
            envVec.push_back( e.first + "=" + e.second );
        if ( envVec.empty() )
            envVec.push_back( "" );

        m_in = std::make_shared<EventLoop::pipe_write>( m_io );
        m_out = std::make_shared<EventLoop::pipe_read>( m_io );

        EventLoop::process_stdio io;
        io.in = *m_in;   // 创建管道, 子进程 stdin 读端; m_in 拿写端
        io.out = *m_out; // 创建管道, 子进程 stdout 写端; m_out 拿读端
        // err: 默认继承父进程 stderr

        const std::string cwdStr = opts.workingDirectory.empty()
                                       ? std::filesystem::current_path().string()
                                       : opts.workingDirectory.string();
        EventLoop::process_startdir cwd( cwdStr );

        try {
#if defined( _WIN32 )
            namespace bpw = boost::process::v2::windows;
            constexpr auto noWinFlags = bpw::process_creation_flags<CREATE_NO_WINDOW>{};
            m_proc = std::make_shared<EventLoop::process>(
                m_io, exe, argv, cwd,
                EventLoop::process_env( envVec ), io, noWinFlags );
#else
            m_proc = std::make_shared<EventLoop::process>(
                m_io, exe, argv, cwd,
                EventLoop::process_env( envVec ), io );
#endif
        } catch ( const std::exception &e ) {
            throw Client::Error( std::string( "启动 stdio 子进程失败: " ) + e.what(), 0 );
        }

        // 启动 io 线程 / 回调线程 / 读取循环
        startRead();
        m_ioThread = std::thread( [this] {
            m_io.run();
        } );
        m_cbThread = std::thread( [this] { callbackLoop(); } );
    }

    ~StdioTransport() override {
        try {
            close();
        } catch ( const std::exception &e ) {
            LOG_WARN << "MCP stdio transport 关闭异常: " << e.what();
        }
    }

    StdioTransport( const StdioTransport & ) = delete;
    StdioTransport &operator=( const StdioTransport & ) = delete;

    core::RpcResponse sendRequest( const core::RpcRequest &req ) override {
        if ( m_closing )
            throw Client::Error( "stdio 传输已关闭", 0 );

        auto prom = std::make_shared<std::promise<core::RpcResponse>>();
        auto fut = prom->get_future();
        const std::string key = idKey( req.id );
        {
            std::lock_guard<std::mutex> lk( m_pendingMtx );
            m_pending[key] = prom;
        }

        try {
            writeJson( core::json( req ) );
        } catch ( ... ) {
            std::lock_guard<std::mutex> lk( m_pendingMtx );
            m_pending.erase( key );
            throw;
        }

        if ( m_opts.timeout.count() > 0 ) {
            if ( fut.wait_for( m_opts.timeout ) != std::future_status::ready ) {
                std::lock_guard<std::mutex> lk( m_pendingMtx );
                m_pending.erase( key );
                throw Client::Error( "stdio 请求等待响应超时", 0 );
            }
        }
        try {
            return fut.get();
        } catch ( const Client::Error & ) {
            throw;
        } catch ( const std::exception &e ) {
            throw Client::Error( std::string( "stdio 传输失败: " ) + e.what(), 0 );
        }
    }

    void sendNotification( const core::RpcNotification &n ) override {
        if ( m_closing )
            throw Client::Error( "stdio 传输已关闭", 0 );
        writeJson( core::json( n ) );
    }

    void sendResponse( const core::RpcResponse &resp ) override {
        if ( m_closing )
            throw Client::Error( "stdio 传输已关闭", 0 );
        writeJson( core::json( resp ) );
    }

    void setMessageHandler( MessageHandler handler ) override {
        std::lock_guard<std::mutex> lk( m_handlerMtx );
        m_handler = std::move( handler );
    }

    void close() override {
        if ( m_closing.exchange( true ) )
            return;

        // 1. 取消异步读, 释放 work_guard, 等 io 线程退出
        error_code ec;
        if ( m_out )
            m_out->cancel( ec );
        if ( m_in )
            m_in->cancel( ec );
        m_work.reset();
        if ( m_ioThread.joinable() )
            m_ioThread.join();

        // 2. 停止回调线程
        {
            std::lock_guard<std::mutex> lk( m_cbMtx );
            m_cbStop = true;
        }
        m_cbCv.notify_all();
        if ( m_cbThread.joinable() )
            m_cbThread.join();

        // 3. 终止并回收子进程
        if ( m_proc ) {
            error_code e2;
            m_proc->terminate( e2 );
            m_proc->wait( e2 );
        }

        // 4. 关闭管道
        if ( m_out )
            m_out->close( ec );
        if ( m_in )
            m_in->close( ec );

        // 5. 唤醒所有等待中的请求
        failPending( Client::Error( "stdio 传输已关闭", 0 ) );
    }

private:
    Client::StdioOptions m_opts;
    boost::asio::io_context m_io;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> m_work;
    std::shared_ptr<EventLoop::pipe_write> m_in;
    std::shared_ptr<EventLoop::pipe_read> m_out;
    std::shared_ptr<EventLoop::process> m_proc;
    boost::asio::streambuf m_readBuf; // 仅 io 线程访问

    std::thread m_ioThread;
    std::thread m_cbThread;

    std::atomic<bool> m_closing{ false };
    std::mutex m_writeMtx;
    std::mutex m_pendingMtx;
    std::map<std::string, std::shared_ptr<std::promise<core::RpcResponse>>> m_pending;

    std::mutex m_handlerMtx;
    MessageHandler m_handler;

    std::mutex m_cbMtx;
    std::condition_variable m_cbCv;
    std::deque<std::function<void()>> m_cbQueue;
    std::atomic<bool> m_cbStop{ false };

    static std::string idKey( const core::RequestId &id ) {
        if ( id.is_int() )
            return "i:" + std::to_string( id.as_int() );
        if ( id.is_string() )
            return "s:" + id.as_string();
        return "n:";
    }

    void startRead() {
        auto out = m_out;
        boost::asio::async_read_until( *out, m_readBuf, '\n',
            [this, out]( const error_code &ec, std::size_t /*n*/ ) {
                onRead( ec );
            } );
    }

    void onRead( const error_code &ec ) {
        if ( ec ) {
            if ( !m_closing ) {
                LOG_WARN << "MCP stdio: 读取子进程 stdout 结束: " << ec.message();
                failPending( Client::Error( "stdio 子进程 stdout 已关闭", 0 ) );
            }
            return;
        }
        std::istream is( &m_readBuf );
        std::string line;
        std::getline( is, line );
        if ( !line.empty() && line.back() == '\r' )
            line.pop_back();
        if ( !line.empty() )
            processLine( line );
        startRead();
    }

    void processLine( const std::string &line ) {
        core::Message msg;
        try {
            msg = core::Message::parseMessage( line );
        } catch ( const std::exception &e ) {
            LOG_WARN << "MCP stdio: 无法解析 JSON 行: " << e.what();
            return;
        }

        if ( msg.type == core::Message::Type::Response && msg.response ) {
            const std::string key = idKey( msg.response->id );
            std::shared_ptr<std::promise<core::RpcResponse>> prom;
            {
                std::lock_guard<std::mutex> lk( m_pendingMtx );
                auto it = m_pending.find( key );
                if ( it != m_pending.end() ) {
                    prom = it->second;
                    m_pending.erase( it );
                }
            }
            if ( prom ) {
                try {
                    prom->set_value( *msg.response );
                } catch ( const std::exception &e ) {
                    LOG_WARN << "MCP stdio: set_value 失败: " << e.what();
                }
            } else {
                LOG_DEBUG << "MCP stdio: 收到未匹配响应 id=" << key;
            }
            return;
        }

        // 通知 / 服务端发起的请求 -> 回调线程 (不阻塞 io 线程)
        MessageHandler h;
        {
            std::lock_guard<std::mutex> lk( m_handlerMtx );
            h = m_handler;
        }
        if ( !h )
            return;
        enqueueCallback( [this, h, msg]() {
            auto reply = h( msg );
            if ( reply ) {
                try {
                    writeJson( core::json( *reply ) );
                } catch ( const std::exception &e ) {
                    LOG_WARN << "MCP stdio: 回复服务端请求失败: " << e.what();
                }
            }
        } );
    }

    void writeJson( const core::json &j ) {
        if ( m_closing )
            throw Client::Error( "stdio 传输已关闭", 0 );
        std::string s = j.dump();
        s.push_back( '\n' );
        std::lock_guard<std::mutex> lk( m_writeMtx );
        if ( m_closing || !m_in )
            throw Client::Error( "stdio 传输已关闭", 0 );
        error_code ec;
        boost::asio::write( *m_in, boost::asio::buffer( s ), ec );
        if ( ec )
            throw Client::Error( std::string( "写入子进程 stdin 失败: " ) + ec.message(), 0 );
    }

    void enqueueCallback( std::function<void()> task ) {
        {
            std::lock_guard<std::mutex> lk( m_cbMtx );
            if ( m_cbStop )
                return;
            m_cbQueue.push_back( std::move( task ) );
        }
        m_cbCv.notify_one();
    }

    void callbackLoop() {
        for ( ;; ) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk( m_cbMtx );
                m_cbCv.wait( lk, [this] { return m_cbStop || !m_cbQueue.empty(); } );
                if ( m_cbQueue.empty() )
                    return; // m_cbStop 且队列空
                task = std::move( m_cbQueue.front() );
                m_cbQueue.pop_front();
            }
            try {
                task();
            } catch ( const std::exception &e ) {
                LOG_WARN << "MCP stdio callback 异常: " << e.what();
            }
        }
    }

    void failPending( const Client::Error &err ) {
        std::vector<std::shared_ptr<std::promise<core::RpcResponse>>> v;
        {
            std::lock_guard<std::mutex> lk( m_pendingMtx );
            v.reserve( m_pending.size() );
            for ( auto &kv : m_pending )
                v.push_back( kv.second );
            m_pending.clear();
        }
        auto eptr = std::make_exception_ptr( err );
        for ( auto &p : v ) {
            try {
                p->set_exception( eptr );
            } catch ( const std::exception &e ) {
                LOG_WARN << "MCP stdio: set_exception 失败: " << e.what();
            }
        }
    }
};

// =========================================================================
// Client::Impl
// =========================================================================
struct Client::Impl {
    std::mutex mtx;
    std::shared_ptr<Transport> transport;
    std::atomic<int64_t> nextId{ 1 };

    core::ImplementationInfo clientInfo{ "Tools-MCP-Client", "1.0.0" };
    core::ClientCapabilities caps;
    std::atomic<bool> initialized{ false };
    core::InitializeResult serverResult;
    std::string protocolVersion{ core::kMcpProtocolVersion };

    RootsProvider rootsProvider;
    SamplingHandler samplingHandler;
    NotificationHandler notificationHandler;
    ServerRequestHandler serverRequestHandler;

    core::RequestId nextRequestId() {
        return core::RequestId{ nextId.fetch_add( 1 ) };
    }

    void throwIfError( const core::RpcResponse &resp ) {
        if ( resp.isError() ) {
            std::string msg = "MCP RPC 错误";
            if ( resp.error ) {
                msg += " " + std::to_string( resp.error->code ) + ": " + resp.error->message;
            }
            throw Client::Error( msg, 0, resp.error );
        }
    }

    std::optional<core::RpcResponse> onTransportMessage( const core::Message &msg ) {
        if ( msg.type == core::Message::Type::Notification && msg.notification ) {
            NotificationHandler h;
            {
                std::lock_guard<std::mutex> lk( mtx );
                h = notificationHandler;
            }
            if ( h ) {
                try {
                    h( *msg.notification );
                } catch ( const std::exception &e ) {
                    LOG_WARN << "MCP client notification 回调异常: " << e.what();
                }
            }
            return std::nullopt;
        }
        if ( msg.type == core::Message::Type::Request && msg.request ) {
            return handleServerRequest( *msg.request );
        }
        return std::nullopt;
    }

    core::RpcResponse handleServerRequest( const core::RpcRequest &req ) {
        const auto &m = req.method;

        if ( m == core::methods::RootsList ) {
            RootsProvider provider;
            {
                std::lock_guard<std::mutex> lk( mtx );
                provider = rootsProvider;
            }
            core::ListRootsResult result;
            if ( provider ) {
                try {
                    result = provider();
                } catch ( const std::exception &e ) {
                    return core::makeErrorResponse( req.id, core::ErrorCode::InternalError, e.what() );
                }
            }
            core::json payload = core::json::object();
            core::json arr = core::json::array();
            for ( const auto &r : result )
                arr.push_back( core::json( r ) );
            payload["roots"] = std::move( arr );
            return core::makeSuccessResponse( req.id, std::move( payload ) );
        }

        if ( m == core::methods::SamplingCreateMessage ) {
            SamplingHandler handler;
            {
                std::lock_guard<std::mutex> lk( mtx );
                handler = samplingHandler;
            }
            if ( !handler )
                return core::makeErrorResponse( req.id, core::ErrorCode::InternalError,
                                                "client 未提供 sampling 支持" );
            try {
                core::CreateMessageParams params;
                if ( req.params )
                    params = req.params->get<core::CreateMessageParams>();
                auto result = handler( params );
                return core::makeSuccessResponse( req.id, core::json( result ) );
            } catch ( const std::exception &e ) {
                return core::makeErrorResponse( req.id, core::ErrorCode::InternalError, e.what() );
            }
        }

        ServerRequestHandler custom;
        {
            std::lock_guard<std::mutex> lk( mtx );
            custom = serverRequestHandler;
        }
        if ( custom ) {
            try {
                return custom( req );
            } catch ( const std::exception &e ) {
                return core::makeErrorResponse( req.id, core::ErrorCode::InternalError, e.what() );
            }
        }
        return core::makeErrorResponse( req.id, core::ErrorCode::MethodNotFound,
                                        "client 未处理的服务端请求: " + m );
    }
};

// =========================================================================
// Client 构造 / 析构 / 元信息
// =========================================================================
Client::Client() : m_impl( std::make_unique<Impl>() ) {
    m_impl->caps.roots = false;
    m_impl->caps.sampling = false;
}

Client::Client( std::shared_ptr<Transport> transport ) : Client() {
    setTransport( std::move( transport ) );
}

Client::~Client() = default;

void Client::setTransport( std::shared_ptr<Transport> transport ) {
    std::lock_guard<std::mutex> lk( m_impl->mtx );
    m_impl->transport = transport;
    if ( transport ) {
        // 捕获 impl 裸指针: Client 拥有 impl 与 transport, 生命周期一致
        Impl *impl = m_impl.get();
        transport->setMessageHandler( [impl]( const core::Message &msg ) -> std::optional<core::RpcResponse> {
            return impl->onTransportMessage( msg );
        } );
    }
}

std::shared_ptr<Client::Transport> Client::transport() const {
    std::lock_guard<std::mutex> lk( m_impl->mtx );
    return m_impl->transport;
}

void Client::setClientInfo( const std::string &name, const std::string &version ) {
    std::lock_guard<std::mutex> lk( m_impl->mtx );
    m_impl->clientInfo.name = name;
    m_impl->clientInfo.version = version;
}

const core::ImplementationInfo &Client::clientInfo() const {
    return m_impl->clientInfo;
}

core::ClientCapabilities &Client::capabilities() {
    return m_impl->caps;
}

const core::ClientCapabilities &Client::capabilities() const {
    return m_impl->caps;
}

bool Client::isInitialized() const {
    return m_impl->initialized.load();
}

const core::InitializeResult &Client::serverInfo() const {
    return m_impl->serverResult;
}

const std::string &Client::protocolVersion() const {
    return m_impl->protocolVersion;
}

void Client::setRootsProvider( RootsProvider provider ) {
    std::lock_guard<std::mutex> lk( m_impl->mtx );
    m_impl->rootsProvider = std::move( provider );
    m_impl->caps.roots = static_cast<bool>( provider );
}

void Client::setSamplingHandler( SamplingHandler handler ) {
    std::lock_guard<std::mutex> lk( m_impl->mtx );
    m_impl->samplingHandler = std::move( handler );
    m_impl->caps.sampling = static_cast<bool>( handler );
}

void Client::setNotificationHandler( NotificationHandler handler ) {
    std::lock_guard<std::mutex> lk( m_impl->mtx );
    m_impl->notificationHandler = std::move( handler );
}

void Client::setServerRequestHandler( ServerRequestHandler handler ) {
    std::lock_guard<std::mutex> lk( m_impl->mtx );
    m_impl->serverRequestHandler = std::move( handler );
}

std::shared_ptr<Client::Transport> Client::createHttpTransport( const HttpOptions &opts ) {
    return std::make_shared<HttpTransport>( opts );
}

std::shared_ptr<Client::Transport> Client::createStdioTransport( const StdioOptions &opts ) {
    return std::make_shared<StdioTransport>( opts );
}

// =========================================================================
// 低级 API
// =========================================================================
core::RpcResponse Client::sendRequest( const std::string &method, std::optional<json> params ) {
    auto t = m_impl->transport;
    if ( !t )
        throw Client::Error( "未设置 transport", 0 );
    core::RequestId id = m_impl->nextRequestId();
    core::RpcRequest req = core::makeRequest( id, method, std::move( params ) );
    return t->sendRequest( req );
}

void Client::sendNotification( const std::string &method, std::optional<json> params ) {
    auto t = m_impl->transport;
    if ( !t )
        throw Client::Error( "未设置 transport", 0 );
    t->sendNotification( core::makeNotification( method, std::move( params ) ) );
}

void Client::sendResponse( const core::RpcResponse &resp ) {
    auto t = m_impl->transport;
    if ( !t )
        throw Client::Error( "未设置 transport", 0 );
    t->sendResponse( resp );
}

// =========================================================================
// 生命周期
// =========================================================================
core::InitializeResult Client::initialize() {
    auto t = m_impl->transport;
    if ( !t )
        throw Client::Error( "未设置 transport", 0 );

    core::InitializeParams params;
    {
        std::lock_guard<std::mutex> lk( m_impl->mtx );
        params.protocolVersion = m_impl->protocolVersion;
        params.capabilities = m_impl->caps;
        params.clientInfo = m_impl->clientInfo;
    }

    core::RequestId id = m_impl->nextRequestId();
    core::RpcRequest req = core::makeInitializeRequest( id, params );
    core::RpcResponse resp = t->sendRequest( req );
    m_impl->throwIfError( resp );

    core::InitializeResult result;
    try {
        result = core::getResult<core::InitializeResult>( resp );
    } catch ( const std::exception &e ) {
        throw Client::Error( std::string( "解析 initialize 结果失败: " ) + e.what(), 0 );
    }
    {
        std::lock_guard<std::mutex> lk( m_impl->mtx );
        m_impl->serverResult = result;
        m_impl->protocolVersion = result.protocolVersion;
    }
    m_impl->initialized = true;

    // 发送 notifications/initialized
    try {
        t->sendNotification( core::makeInitializedNotification() );
    } catch ( const std::exception &e ) {
        LOG_WARN << "MCP client: 发送 initialized 通知失败: " << e.what();
    }
    return result;
}

void Client::notifyInitialized() {
    auto t = m_impl->transport;
    if ( !t )
        throw Client::Error( "未设置 transport", 0 );
    t->sendNotification( core::makeInitializedNotification() );
}

void Client::ping() {
    auto t = m_impl->transport;
    if ( !t )
        throw Client::Error( "未设置 transport", 0 );
    core::RequestId id = m_impl->nextRequestId();
    core::RpcRequest req = core::makePingRequest( id );
    core::RpcResponse resp = t->sendRequest( req );
    m_impl->throwIfError( resp );
}

// =========================================================================
// 高级 API
// =========================================================================
core::ListToolsResult Client::listTools( std::optional<std::string> cursor ) {
    auto t = m_impl->transport;
    if ( !t )
        throw Client::Error( "未设置 transport", 0 );
    core::RequestId id = m_impl->nextRequestId();
    core::RpcRequest req = core::makeToolsListRequest( id, std::move( cursor ) );
    core::RpcResponse resp = t->sendRequest( req );
    m_impl->throwIfError( resp );
    return core::getResult<core::ListToolsResult>( resp );
}

core::ToolCallResult Client::callTool( const std::string &name, std::optional<json> arguments ) {
    auto t = m_impl->transport;
    if ( !t )
        throw Client::Error( "未设置 transport", 0 );
    core::RequestId id = m_impl->nextRequestId();
    core::RpcRequest req = core::makeToolsCallRequest( id, name, std::move( arguments ) );
    core::RpcResponse resp = t->sendRequest( req );
    m_impl->throwIfError( resp );
    return core::getResult<core::ToolCallResult>( resp );
}

core::ListResourcesResult Client::listResources( std::optional<std::string> cursor ) {
    auto t = m_impl->transport;
    if ( !t )
        throw Client::Error( "未设置 transport", 0 );
    core::RequestId id = m_impl->nextRequestId();
    core::RpcRequest req = core::makeResourcesListRequest( id, std::move( cursor ) );
    core::RpcResponse resp = t->sendRequest( req );
    m_impl->throwIfError( resp );
    return core::getResult<core::ListResourcesResult>( resp );
}

core::ReadResourceResult Client::readResource( const std::string &uri ) {
    auto t = m_impl->transport;
    if ( !t )
        throw Client::Error( "未设置 transport", 0 );
    core::RequestId id = m_impl->nextRequestId();
    core::RpcRequest req = core::makeResourcesReadRequest( id, uri );
    core::RpcResponse resp = t->sendRequest( req );
    m_impl->throwIfError( resp );
    return core::getResult<core::ReadResourceResult>( resp );
}

core::ListPromptsResult Client::listPrompts( std::optional<std::string> cursor ) {
    auto t = m_impl->transport;
    if ( !t )
        throw Client::Error( "未设置 transport", 0 );
    core::RequestId id = m_impl->nextRequestId();
    core::RpcRequest req = core::makePromptsListRequest( id, std::move( cursor ) );
    core::RpcResponse resp = t->sendRequest( req );
    m_impl->throwIfError( resp );
    return core::getResult<core::ListPromptsResult>( resp );
}

core::GetPromptResult Client::getPrompt( const std::string &name, std::optional<json> arguments ) {
    auto t = m_impl->transport;
    if ( !t )
        throw Client::Error( "未设置 transport", 0 );
    core::RequestId id = m_impl->nextRequestId();
    core::RpcRequest req = core::makePromptsGetRequest( id, name, std::move( arguments ) );
    core::RpcResponse resp = t->sendRequest( req );
    m_impl->throwIfError( resp );
    return core::getResult<core::GetPromptResult>( resp );
}

void Client::setLoggingLevel( core::LogLevel level ) {
    auto t = m_impl->transport;
    if ( !t )
        throw Client::Error( "未设置 transport", 0 );
    core::RequestId id = m_impl->nextRequestId();
    core::RpcRequest req = core::makeLoggingSetLevelRequest( id, level );
    core::RpcResponse resp = t->sendRequest( req );
    m_impl->throwIfError( resp );
}

core::Completion Client::complete( const core::CompletionCompleteParams &params ) {
    auto t = m_impl->transport;
    if ( !t )
        throw Client::Error( "未设置 transport", 0 );
    core::RequestId id = m_impl->nextRequestId();
    core::RpcRequest req = core::makeCompletionCompleteRequest( id, params );
    core::RpcResponse resp = t->sendRequest( req );
    m_impl->throwIfError( resp );
    return core::getResult<core::CompletionResult>( resp ).completion;
}

} // namespace mcp

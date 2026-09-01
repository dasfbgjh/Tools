#include "core/McpServer.h"
#include "common/Logger.hpp"

namespace mcp {

McpServer::McpServer() {
    // 默认开启所有我们实现了 handler 的能力（除采样：通常由客户端模型提供，服务端不提供）
    m_caps.tools = true;
    m_caps.resources = true;
    m_caps.prompts = true;
    m_caps.logging = true;
    m_caps.experimental.reset();
    m_caps.completions.reset();
}

McpServer::~McpServer() = default;

void McpServer::setServerInfo( const std::string &name, const std::string &version,
                               std::optional<std::string> instructions ) {
    std::lock_guard<std::mutex> lk( m_mtx );
    m_serverInfo.name = name;
    m_serverInfo.version = version;
    m_instructions = std::move( instructions );
}

const core::ImplementationInfo &McpServer::serverInfo() const {
    return m_serverInfo;
}
const std::optional<std::string> &McpServer::instructions() const {
    return m_instructions;
}

core::ServerCapabilities &McpServer::capabilities() {
    return m_caps;
}
const core::ServerCapabilities &McpServer::capabilities() const {
    return m_caps;
}

void McpServer::setProtocolVersion( const std::string &pv ) {
    m_protocolVersion = pv;
}
const std::string &McpServer::protocolVersion() const {
    return m_protocolVersion;
}

// ===========================================================================
// 注册
// ===========================================================================
void McpServer::registerTool( core::Tool definition, ToolHandler handler ) {
    std::lock_guard<std::mutex> lk( m_mtx );
    std::string key = definition.name; // 先拷贝 key，避免移动后问题
    m_tools[key] = RegisteredTool{ std::move( definition ), std::move( handler ) };
}
void McpServer::unregisterTool( const std::string &name ) {
    std::lock_guard<std::mutex> lk( m_mtx );
    m_tools.erase( name );
}

void McpServer::registerResource( core::Resource definition, ResourceReader reader ) {
    std::lock_guard<std::mutex> lk( m_mtx );
    std::string key = definition.uri;
    m_resources[key] = RegisteredResource{ std::move( definition ), std::move( reader ) };
}
void McpServer::unregisterResource( const std::string &uri ) {
    std::lock_guard<std::mutex> lk( m_mtx );
    m_resources.erase( uri );
}

void McpServer::registerPrompt( core::Prompt definition, PromptGetter getter ) {
    std::lock_guard<std::mutex> lk( m_mtx );
    std::string key = definition.name;
    m_prompts[key] = RegisteredPrompt{ std::move( definition ), std::move( getter ) };
}
void McpServer::unregisterPrompt( const std::string &name ) {
    std::lock_guard<std::mutex> lk( m_mtx );
    m_prompts.erase( name );
}

void McpServer::setRootsProvider( RootsProvider provider ) {
    m_rootsProvider = std::move( provider );
}
void McpServer::setLogLevelSetter( LogLevelSetter setter ) {
    m_logLevelSetter = std::move( setter );
}
void McpServer::setCanceller( Canceller canceller ) {
    m_canceller = std::move( canceller );
}
void McpServer::setProgressReporter( ProgressReporter r ) {
    m_progressReporter = std::move( r );
}
void McpServer::setCompletionHandler( CompletionHandler h ) {
    m_completion = std::move( h );
}

// ===========================================================================
// 顶层消息处理
// ===========================================================================
core::Message McpServer::handleMessage( const core::Message &incoming ) {
    switch ( incoming.type ) {
    case core::Message::Type::Request: {
        core::Message out;
        out.type = core::Message::Type::Response;
        out.response = dispatchRequest( *incoming.request );
        return out;
    }
    case core::Message::Type::Notification:
        dispatchNotification( *incoming.notification );
        return {};
    default:
        return {};
    }
}

core::json McpServer::handleJson( const json &j ) {
    core::Message msg = core::Message::fromJson( j );
    core::Message out = handleMessage( msg );
    return out.toJson();
}

std::string McpServer::handleRaw( const std::string &raw ) {
    try {
        json j = json::parse( raw );
        return handleBatchOrSingle( j ).dump();
    } catch ( const json::parse_error &e ) {
        core::RpcResponse r = core::makeErrorResponse( core::RequestId{}, core::ErrorCode::ParseError,
                                                       std::string( "Parse error: " ) + e.what() );
        return json( r ).dump();
    } catch ( const std::exception &e ) {
        core::RpcResponse r = core::makeErrorResponse( core::RequestId{}, core::ErrorCode::InternalError, e.what() );
        return json( r ).dump();
    }
}

core::json McpServer::handleBatchOrSingle( const json &j ) {
    if ( j.is_array() ) {
        json arr = json::array();
        for ( const auto &item : j ) {
            try {
                json resp = handleJson( item );
                if ( !resp.is_null() && !resp.empty() ) {
                    arr.push_back( std::move( resp ) );
                }
            } catch ( const std::exception &e ) {
                core::RpcResponse r = core::makeErrorResponse( core::RequestId{}, core::ErrorCode::InternalError, e.what() );
                arr.push_back( json( r ) );
            }
        }
        return arr;
    }
    return handleJson( j );
}

// ===========================================================================
// 通知分派（仅副作用，无返回值）
// ===========================================================================
void McpServer::dispatchNotification( const core::RpcNotification &n ) {
    try {
        if ( n.method == core::methods::InitializedNotification ) {
            m_initialized = true;
            return;
        }
        if ( n.method == core::methods::CancelRequestNotify ) {
            if ( m_canceller && n.params && n.params->contains( "requestId" ) ) {
                core::RequestId rid;
                core::from_json( ( *n.params )["requestId"], rid );
                m_canceller( rid );
            }
            return;
        }
        if ( n.method == core::methods::ProgressNotify ) {
            // 客户端->服务端的 progress 通知；通常不处理
            return;
        }
        LOG_DEBUG << "MCP: 收到未处理的通知 method=" << n.method;
    } catch ( const std::exception &e ) {
        LOG_WARN << "MCP notification 处理异常: " << e.what();
    }
}

// ===========================================================================
// 请求分派：根据 method 路由到 handleXxx
// ===========================================================================
core::RpcResponse McpServer::dispatchRequest( const core::RpcRequest &req ) {
    try {
        const auto &m = req.method;
        if ( m == core::methods::Initialize )
            return handleInitialize( req );
        if ( m == core::methods::Ping )
            return handlePing( req );
        if ( m == core::methods::ToolsList )
            return handleToolsList( req );
        if ( m == core::methods::ToolsCall )
            return handleToolsCall( req );
        if ( m == core::methods::ResourcesList )
            return handleResourcesList( req );
        if ( m == core::methods::ResourcesRead )
            return handleResourcesRead( req );
        if ( m == core::methods::PromptsList )
            return handlePromptsList( req );
        if ( m == core::methods::PromptsGet )
            return handlePromptsGet( req );
        if ( m == core::methods::LoggingSetLevel )
            return handleLoggingSetLevel( req );
        if ( m == core::methods::RootsList )
            return handleRootsList( req );
        if ( m == core::methods::SamplingCreateMessage )
            return handleSamplingCreateMessage( req );
        if ( m == core::methods::CompletionComplete )
            return handleCompletionComplete( req );

        return core::makeErrorResponse( req.id, core::ErrorCode::MethodNotFound,
                                        "Method not found: " + m );
    } catch ( const std::invalid_argument &e ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InvalidParams, e.what() );
    } catch ( const std::exception &e ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InternalError, e.what() );
    }
}

// ===========================================================================
// 各方法的具体实现
// ===========================================================================
core::RpcResponse McpServer::handleInitialize( const core::RpcRequest &req ) {
    // params 必须存在（规范要求）
    if ( !req.params ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InvalidParams, "initialize requires params" );
    }
    try {
        core::InitializeParams params = req.params->get<core::InitializeParams>();
        if ( params.protocolVersion != m_protocolVersion ) {
            // 不强制拒绝，至少返回自己支持的版本
            LOG_WARN << "MCP client 协议版本不匹配: client=" << params.protocolVersion
                     << " server=" << m_protocolVersion;
        }
        core::InitializeResult result;
        result.protocolVersion = m_protocolVersion;
        {
            std::lock_guard<std::mutex> lk( m_mtx );
            result.capabilities = m_caps;
            result.serverInfo = m_serverInfo;
            result.instructions = m_instructions;
        }
        m_initialized = true;
        return makeSuccessResponse( req.id, json( result ) );
    } catch ( const std::exception &e ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InvalidParams,
                                        std::string( "Invalid initialize params: " ) + e.what() );
    }
}

core::RpcResponse McpServer::handlePing( const core::RpcRequest &req ) {
    return core::makeSuccessResponse( req.id, json::object() );
}

// ---- Tools ----
core::RpcResponse McpServer::handleToolsList( const core::RpcRequest &req ) {
    std::optional<std::string> cursor;
    if ( req.params && req.params->contains( "cursor" ) ) {
        cursor = ( *req.params )["cursor"].get<std::string>();
    }
    (void)cursor; // 暂不实现真正分页（工具数量通常不多）
    core::ListToolsResult out;
    {
        std::lock_guard<std::mutex> lk( m_mtx );
        out.items.reserve( m_tools.size() );
        for ( const auto &[k, v] : m_tools )
            out.items.push_back( v.definition );
    }
    return core::makeSuccessResponse( req.id, json( out ) );
}

core::RpcResponse McpServer::handleToolsCall( const core::RpcRequest &req ) {
    if ( !req.params ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InvalidParams, "tools/call 需要 params" );
    }
    const auto &p = *req.params;
    if ( !p.contains( "name" ) ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InvalidParams, "缺少 name 字段" );
    }
    const std::string name = p["name"].get<std::string>();
    std::optional<json> arguments;
    if ( p.contains( "arguments" ) && !p["arguments"].is_null() ) {
        arguments = p["arguments"];
    }

    ToolHandler handler;
    {
        std::lock_guard<std::mutex> lk( m_mtx );
        auto it = m_tools.find( name );
        if ( it == m_tools.end() ) {
            return core::makeErrorResponse( req.id, core::ErrorCode::MethodNotFound,
                                            "未注册的 tool: " + name );
        }
        handler = it->second.handler;
    }

    try {
        core::ToolCallResult r = handler( arguments );
        return core::makeSuccessResponse( req.id, json( r ) );
    } catch ( const std::exception &e ) {
        // 工具内部异常：按 MCP 规范以 isError=true 返回内容（而不是 JSON-RPC error）
        core::ToolCallResult err;
        err.isError = true;
        core::Content::Text tc;
        tc.text = std::string( "Tool 执行失败: " ) + e.what();
        err.content.emplace_back( std::move( tc ) );
        return makeSuccessResponse( req.id, json( err ) );
    }
}

// ---- Resources ----
core::RpcResponse McpServer::handleResourcesList( const core::RpcRequest &req ) {
    (void)req;
    core::ListResourcesResult out;
    {
        std::lock_guard<std::mutex> lk( m_mtx );
        out.items.reserve( m_resources.size() );
        for ( const auto &[k, v] : m_resources )
            out.items.push_back( v.definition );
    }
    return core::makeSuccessResponse( req.id, json( out ) );
}

core::RpcResponse McpServer::handleResourcesRead( const core::RpcRequest &req ) {
    if ( !req.params ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InvalidParams, "resources/read 需要 params" );
    }
    const auto &p = *req.params;
    if ( !p.contains( "uri" ) ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InvalidParams, "缺少 uri 字段" );
    }
    const std::string uri = p["uri"].get<std::string>();
    ResourceReader reader;
    {
        std::lock_guard<std::mutex> lk( m_mtx );
        auto it = m_resources.find( uri );
        if ( it == m_resources.end() ) {
            return core::makeErrorResponse( req.id, core::ErrorCode::InvalidParams,
                                            "未注册的 resource uri: " + uri );
        }
        reader = it->second.reader;
    }
    try {
        return makeSuccessResponse( req.id, json( reader( uri ) ) );
    } catch ( const std::exception &e ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InternalError,
                                        std::string( "读取 resource 失败: " ) + e.what() );
    }
}

// ---- Prompts ----
core::RpcResponse McpServer::handlePromptsList( const core::RpcRequest &req ) {
    (void)req;
    core::ListPromptsResult out;
    {
        std::lock_guard<std::mutex> lk( m_mtx );
        out.items.reserve( m_prompts.size() );
        for ( const auto &[k, v] : m_prompts )
            out.items.push_back( v.definition );
    }
    return makeSuccessResponse( req.id, json( out ) );
}

core::RpcResponse McpServer::handlePromptsGet( const core::RpcRequest &req ) {
    if ( !req.params ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InvalidParams, "prompts/get 需要 params" );
    }
    const auto &p = *req.params;
    if ( !p.contains( "name" ) ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InvalidParams, "缺少 name 字段" );
    }
    const std::string name = p["name"].get<std::string>();
    std::optional<json> arguments;
    if ( p.contains( "arguments" ) && !p["arguments"].is_null() )
        arguments = p["arguments"];

    PromptGetter getter;
    {
        std::lock_guard<std::mutex> lk( m_mtx );
        auto it = m_prompts.find( name );
        if ( it == m_prompts.end() ) {
            return core::makeErrorResponse( req.id, core::ErrorCode::MethodNotFound,
                                            "未注册的 prompt: " + name );
        }
        getter = it->second.getter;
    }
    try {
        return makeSuccessResponse( req.id, json( getter( arguments ) ) );
    } catch ( const std::exception &e ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InternalError,
                                        std::string( "获取 prompt 失败: " ) + e.what() );
    }
}

// ---- Logging ----
core::RpcResponse McpServer::handleLoggingSetLevel( const core::RpcRequest &req ) {
    if ( !req.params || !req.params->contains( "level" ) ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InvalidParams, "需要 level 字段" );
    }
    core::LogLevel lv = core::logLevelFromString( ( *req.params )["level"].get<std::string>() );
    if ( m_logLevelSetter )
        m_logLevelSetter( lv );
    return makeSuccessResponse( req.id, json::object() );
}

// ---- Roots ----
core::RpcResponse McpServer::handleRootsList( const core::RpcRequest &req ) {
    core::ListRootsResult result;
    if ( m_rootsProvider )
        result = m_rootsProvider();
    json j = json::array();
    for ( const auto &r : result )
        j.push_back( json( r ) );
    json payload = json::object();
    payload["roots"] = std::move( j );
    return makeSuccessResponse( req.id, std::move( payload ) );
}

// ---- Sampling (服务端一般不实现，返回不支持) ----
core::RpcResponse McpServer::handleSamplingCreateMessage( const core::RpcRequest &req ) {
    return core::makeErrorResponse( req.id, core::ErrorCode::InternalError,
                                    "本 MCP 服务器不支持 sampling/createMessage 代理" );
}

// ---- Completion ----
core::RpcResponse McpServer::handleCompletionComplete( const core::RpcRequest &req ) {
    if ( !m_completion ) {
        core::Completion empty;
        return makeSuccessResponse( req.id, json( core::CompletionResult{ std::move( empty ) } ) );
    }
    if ( !req.params ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InvalidParams, "需要 params" );
    }
    try {
        auto p = req.params->get<core::CompletionCompleteParams>();
        core::Completion c = m_completion( p );
        return makeSuccessResponse( req.id, json( core::CompletionResult{ std::move( c ) } ) );
    } catch ( const std::exception &e ) {
        return core::makeErrorResponse( req.id, core::ErrorCode::InvalidParams, e.what() );
    }
}

} // namespace mcp

#include "McpCore.h"

namespace mcp::core {

Message Message::fromJson( const json &j ) {
    Message m;
    if ( !j.is_object() ) {
        throw std::invalid_argument( "MCP message must be a JSON object" );
    }
    if ( !j.contains( "jsonrpc" ) || j.value( "jsonrpc", std::string{} ) != kJsonRpcVersion ) {
        throw std::invalid_argument( "Invalid or missing jsonrpc field (expected '2.0')" );
    }

    const bool hasId = j.contains( "id" ) && !j["id"].is_null();
    const bool hasMethod = j.contains( "method" );
    const bool hasResult = j.contains( "result" );
    const bool hasError = j.contains( "error" );

    if ( ( hasResult || hasError ) && !( hasResult && hasMethod ) ) {
        // Response
        m.type = Type::Response;
        m.response = j.get<RpcResponse>();
        return m;
    }
    if ( hasMethod ) {
        if ( hasId ) {
            m.type = Type::Request;
            m.request = j.get<RpcRequest>();
        } else {
            m.type = Type::Notification;
            m.notification = j.get<RpcNotification>();
        }
        return m;
    }

    throw std::invalid_argument( "Cannot classify MCP JSON-RPC message (missing method/result/error)" );
}

json Message::toJson() const {
    switch ( type ) {
    case Type::Request:
        return json( *request );
    case Type::Notification:
        return json( *notification );
    case Type::Response:
        return json( *response );
    default:
        return json{};
    }
}

Message Message::parseMessage( const json &j ) {
    return fromJson( j );
}

Message Message::parseMessage( const std::string &raw ) {
    try {
        json j = json::parse( raw );
        return fromJson( j );
    } catch ( const json::parse_error &e ) {
        throw std::invalid_argument( std::string( "MCP JSON parse error: " ) + e.what() );
    }
}

const std::string &Message::methodOrEmpty() const {
    if ( type == Type::Request && request )
        return request->method;
    if ( type == Type::Notification && notification )
        return notification->method;
    static const std::string kEmpty;
    return kEmpty;
}

const RequestId &Message::idOrEmpty() const {
    static const RequestId kEmpty;
    if ( type == Type::Request && request )
        return request->id;
    if ( type == Type::Response && response )
        return response->id;
    return kEmpty;
}

RpcRequest makeRequest( const RequestId &id,
                        const std::string &method,
                        std::optional<json> params ) {
    RpcRequest r;
    r.id = id;
    r.method = method;
    r.params = std::move( params );
    return r;
}

RpcNotification makeNotification( const std::string &method, std::optional<json> params ) {
    RpcNotification n;
    n.method = method;
    n.params = std::move( params );
    return n;
}

RpcResponse makeSuccessResponse( const RequestId &id, json result ) {
    return RpcResponse::makeSuccess( id, std::move( result ) );
}

RpcResponse makeErrorResponse( const RequestId &id, RpcError error ) {
    return RpcResponse::makeError( id, std::move( error ) );
}

RpcResponse makeErrorResponse( const RequestId &id, ErrorCode code,
                               const std::string &msg,
                               std::optional<json> data ) {
    return RpcResponse::makeError( id, RpcError{ code, msg, std::move( data ) } );
}

// ---- MCP 标准请求 ----
RpcRequest makeInitializeRequest( const RequestId &id, const InitializeParams &params ) {
    return makeRequest( id, methods::Initialize, json( params ) );
}

RpcRequest makePingRequest( const RequestId &id ) {
    return makeRequest( id, methods::Ping, std::nullopt );
}

RpcRequest makeToolsListRequest( const RequestId &id, std::optional<std::string> cursor ) {
    std::optional<json> params;
    if ( cursor )
        params = json::object( { { "cursor", *cursor } } );
    return makeRequest( id, methods::ToolsList, std::move( params ) );
}

RpcRequest makeToolsCallRequest( const RequestId &id,
                                 const std::string &name,
                                 std::optional<json> arguments ) {
    json p = json::object();
    p["name"] = name;
    if ( arguments )
        p["arguments"] = *arguments;
    return makeRequest( id, methods::ToolsCall, std::move( p ) );
}

RpcRequest makeResourcesListRequest( const RequestId &id,
                                     std::optional<std::string> cursor ) {
    std::optional<json> params;
    if ( cursor )
        params = json::object( { { "cursor", *cursor } } );
    return makeRequest( id, methods::ResourcesList, std::move( params ) );
}

RpcRequest makeResourcesReadRequest( const RequestId &id, const std::string &uri ) {
    json p = json::object();
    p["uri"] = uri;
    return makeRequest( id, methods::ResourcesRead, std::move( p ) );
}

RpcRequest makePromptsListRequest( const RequestId &id,
                                   std::optional<std::string> cursor ) {
    std::optional<json> params;
    if ( cursor )
        params = json::object( { { "cursor", *cursor } } );
    return makeRequest( id, methods::PromptsList, std::move( params ) );
}

RpcRequest makePromptsGetRequest( const RequestId &id,
                                  const std::string &name,
                                  std::optional<json> arguments ) {
    json p = json::object();
    p["name"] = name;
    if ( arguments )
        p["arguments"] = *arguments;
    return makeRequest( id, methods::PromptsGet, std::move( p ) );
}

RpcRequest makeLoggingSetLevelRequest( const RequestId &id, LogLevel level ) {
    json p = json::object();
    const auto ls = logLevelToString( level );
    if ( !ls.empty() )
        p["level"] = ls;
    return makeRequest( id, methods::LoggingSetLevel, std::move( p ) );
}

RpcRequest makeRootsListRequest( const RequestId &id ) {
    return makeRequest( id, methods::RootsList, std::nullopt );
}

RpcRequest makeSamplingCreateMessageRequest( const RequestId &id,
                                             const CreateMessageParams &params ) {
    return makeRequest( id, methods::SamplingCreateMessage, json( params ) );
}

RpcRequest makeCompletionCompleteRequest( const RequestId &id,
                                          const CompletionCompleteParams &params ) {
    return makeRequest( id, methods::CompletionComplete, json( params ) );
}

// ---- MCP 标准通知 ----
RpcNotification makeInitializedNotification() {
    return makeNotification( methods::InitializedNotification, std::nullopt );
}

RpcNotification makeCancelRequestNotification( const RequestId &requestId ) {
    json p = json::object();
    if ( requestId.has_value() )
        p["requestId"] = json( requestId );
    return makeNotification( methods::CancelRequestNotify, std::move( p ) );
}

RpcNotification makeProgressNotification( const std::string &progressToken,
                                          std::optional<int32_t> progress,
                                          std::optional<int32_t> total,
                                          std::optional<std::string> message ) {
    json p = json::object();
    p["progressToken"] = progressToken;
    if ( progress )
        p["progress"] = *progress;
    if ( total )
        p["total"] = *total;
    if ( message )
        p["message"] = *message;
    return makeNotification( methods::ProgressNotify, std::move( p ) );
}

RpcNotification makeLoggingMessageNotification( const LoggingMessageParams &params ) {
    return makeNotification( methods::LoggingMessageNotify, json( params ) );
}

RpcNotification makeResourcesUpdatedNotification( const std::string &uri ) {
    json p = json::object();
    p["uri"] = uri;
    return makeNotification( methods::ResourcesUpdatedNotify, std::move( p ) );
}

RpcNotification makeResourcesListChangedNotification() {
    return makeNotification( methods::ResourcesListChangedNotify, std::nullopt );
}

RpcNotification makePromptsListChangedNotification() {
    return makeNotification( methods::PromptsListChangedNotify, std::nullopt );
}

RpcNotification makeRootsListChangedNotification() {
    return makeNotification( methods::RootsListChangedNotify, std::nullopt );
}

std::string logLevelToString( LogLevel level ) {
    switch ( level ) {
    case LogLevel::Trace:
        return "trace";
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warning";
    case LogLevel::Error:
        return "error";
    case LogLevel::Fatal:
        return "fatal";
    default:
        return "";
    }
}

LogLevel logLevelFromString( const std::string &s ) {
    if ( s == "trace" )
        return LogLevel::Trace;
    if ( s == "debug" )
        return LogLevel::Debug;
    if ( s == "info" )
        return LogLevel::Info;
    if ( s == "warning" )
        return LogLevel::Warning;
    if ( s == "error" )
        return LogLevel::Error;
    if ( s == "fatal" )
        return LogLevel::Fatal;
    return LogLevel::Unset;
}

std::string roleToString( Role r ) {
    return ( r == Role::User ) ? "user" : "assistant";
}

Role roleFromString( const std::string &s ) {
    if ( s == "assistant" )
        return Role::Assistant;
    return Role::User;
}

std::string stopReasonToString( StopReason r ) {
    switch ( r ) {
    case StopReason::EndTurn:
        return "endTurn";
    case StopReason::StopSequence:
        return "stopSequence";
    case StopReason::MaxTokens:
        return "maxTokens";
    default:
        return "custom";
    }
}

StopReason stopReasonFromString( const std::string &s ) {
    if ( s == "endTurn" )
        return StopReason::EndTurn;
    if ( s == "stopSequence" )
        return StopReason::StopSequence;
    if ( s == "maxTokens" )
        return StopReason::MaxTokens;
    return StopReason::Custom;
}

// ===========================================================================
// JSON-RPC 2.0 基础类型: RequestId / RpcError / RpcRequest / RpcNotification / RpcResponse
// ===========================================================================

void to_json( json &j, const RequestId &id ) {
    if ( !id.has_value() ) {
        j = nullptr;
        return;
    }
    if ( id.is_int() )
        j = id.as_int();
    else
        j = id.as_string();
}

void from_json( const json &j, RequestId &id ) {
    if ( j.is_null() ) {
        id = RequestId{};
        return;
    }
    if ( j.is_number_integer() ) {
        id = RequestId{ j.get<int64_t>() };
        return;
    }
    if ( j.is_string() ) {
        id = RequestId{ j.get<std::string>() };
        return;
    }
    // 允许 fallback 到字符串
    id = RequestId{ j.dump() };
}

// --- RpcError ---
void to_json( json &j, const RpcError &e ) {
    j = json::object();
    j["code"] = e.code;
    j["message"] = e.message;
    if ( e.data )
        j["data"] = *e.data;
}

void from_json( const json &j, RpcError &e ) {
    e.code = j.value( "code", 0 );
    e.message = j.value( "message", std::string{} );
    if ( j.contains( "data" ) && !j["data"].is_null() )
        e.data = j["data"];
}

// --- RpcRequest ---
void to_json( json &j, const RpcRequest &req ) {
    j = json::object();
    j["jsonrpc"] = req.jsonrpc;
    j["id"] = req.id;
    j["method"] = req.method;
    if ( req.params )
        j["params"] = *req.params;
}

void from_json( const json &j, RpcRequest &req ) {
    req.jsonrpc = j.value( "jsonrpc", std::string{ kJsonRpcVersion } );
    if ( j.contains( "id" ) )
        from_json( j["id"], req.id );
    req.method = j.value( "method", std::string{} );
    if ( j.contains( "params" ) && !j["params"].is_null() )
        req.params = j["params"];
}

// --- RpcNotification ---
void to_json( json &j, const RpcNotification &n ) {
    j = json::object();
    j["jsonrpc"] = n.jsonrpc;
    j["method"] = n.method;
    if ( n.params )
        j["params"] = *n.params;
}

void from_json( const json &j, RpcNotification &n ) {
    n.jsonrpc = j.value( "jsonrpc", std::string{ kJsonRpcVersion } );
    n.method = j.value( "method", std::string{} );
    if ( j.contains( "params" ) && !j["params"].is_null() )
        n.params = j["params"];
}

// --- RpcResponse ---
void to_json( json &j, const RpcResponse &resp ) {
    j = json::object();
    j["jsonrpc"] = resp.jsonrpc;
    j["id"] = resp.id;
    if ( resp.error ) {
        j["error"] = *resp.error;
    } else {
        j["result"] = resp.result ? *resp.result : json{};
    }
}

void from_json( const json &j, RpcResponse &resp ) {
    resp.jsonrpc = j.value( "jsonrpc", std::string{ kJsonRpcVersion } );
    if ( j.contains( "id" ) )
        from_json( j["id"], resp.id );

    if ( j.contains( "error" ) && !j["error"].is_null() ) {
        resp.error = j["error"].get<RpcError>();
        resp.result.reset();
    } else if ( j.contains( "result" ) ) {
        resp.result = j["result"];
        resp.error.reset();
    }
}

void to_json( json &j, const Content &c ) {
    std::visit( [&j]( const auto &v ) { to_json( j, v ); }, c.value );
}

void from_json( const json &j, Content &c ) {
    const std::string t = j.value( "type", std::string{ "text" } );
    if ( t == "image" ) {
        Content::Image ic;
        from_json( j, ic );
        c.value = std::move( ic );
    } else if ( t == "resource" ) {
        Content::EmbeddedResource er;
        from_json( j, er );
        c.value = std::move( er );
    } else {
        Content::Text tc;
        from_json( j, tc );
        c.value = std::move( tc );
    }
}

void to_json( json &j, const Content::Text &c ) {
    j = json::object();
    j["type"] = c.type;
    j["text"] = c.text;
}
void from_json( const json &j, Content::Text &c ) {
    c.type = j.value( "type", std::string{ "text" } );
    c.text = j.value( "text", std::string{} );
}

void to_json( json &j, const Content::Image &c ) {
    j = json::object();
    j["type"] = c.type;
    j["data"] = c.data;
    j["mimeType"] = c.mimeType;
}
void from_json( const json &j, Content::Image &c ) {
    c.type = j.value( "type", std::string{ "image" } );
    c.data = j.value( "data", std::string{} );
    c.mimeType = j.value( "mimeType", std::string{} );
}

void to_json( json &j, const Content::EmbeddedResource &c ) {
    j = json::object();
    j["type"] = c.type;
    j["resource"] = c.resource;
    j["mimeType"] = c.mimeType;
    j["uri"] = c.uri;
}
void from_json( const json &j, Content::EmbeddedResource &c ) {
    c.type = j.value( "type", std::string{ "resource" } );
    c.resource = j.value( "resource", std::string{} );
    c.mimeType = j.value( "mimeType", std::string{} );
    c.uri = j.value( "uri", std::string{} );
}

// ===========================================================================
// Tool + ToolInputSchema + JsonSchemaProperty
// ===========================================================================

void to_json( json &j, const JsonSchemaProperty &p ) {
    j = json::object();
    j["type"] = p.type;
    if ( p.description )
        j["description"] = *p.description;
    if ( p.enumValues )
        j["enum"] = *p.enumValues;
    if ( p.extra && p.extra->is_object() ) {
        for ( auto it = p.extra->begin(); it != p.extra->end(); ++it ) {
            if ( !j.contains( it.key() ) )
                j[it.key()] = it.value();
        }
    }
}
void from_json( const json &j, JsonSchemaProperty &p ) {
    p.type = j.value( "type", std::string{ "string" } );
    if ( j.contains( "description" ) )
        p.description = j["description"].get<std::string>();
    if ( j.contains( "enum" ) )
        p.enumValues = j["enum"].get<std::vector<std::string>>();
    // 额外字段
    json extra = json::object();
    for ( auto it = j.begin(); it != j.end(); ++it ) {
        const std::string &k = it.key();
        if ( k != "type" && k != "description" && k != "enum" ) {
            extra[k] = it.value();
        }
    }
    if ( !extra.empty() )
        p.extra = std::move( extra );
}

void to_json( json &j, const ToolInputSchema &s ) {
    j = json::object();
    j["type"] = s.type;
    if ( !s.properties.empty() ) {
        json props = json::object();
        for ( const auto &[k, v] : s.properties )
            props[k] = v;
        j["properties"] = props;
    }
    if ( !s.required.empty() )
        j["required"] = s.required;
    if ( s.extra && s.extra->is_object() ) {
        for ( auto it = s.extra->begin(); it != s.extra->end(); ++it ) {
            if ( !j.contains( it.key() ) )
                j[it.key()] = it.value();
        }
    }
}
void from_json( const json &j, ToolInputSchema &s ) {
    s.type = j.value( "type", std::string{ "object" } );
    s.properties.clear();
    s.required.clear();
    if ( j.contains( "properties" ) && j["properties"].is_object() ) {
        for ( auto it = j["properties"].begin(); it != j["properties"].end(); ++it ) {
            s.properties.emplace( it.key(), it.value().get<JsonSchemaProperty>() );
        }
    }
    if ( j.contains( "required" ) && j["required"].is_array() ) {
        s.required = j["required"].get<std::vector<std::string>>();
    }
    json extra = json::object();
    for ( auto it = j.begin(); it != j.end(); ++it ) {
        const std::string &k = it.key();
        if ( k != "type" && k != "properties" && k != "required" ) {
            extra[k] = it.value();
        }
    }
    if ( !extra.empty() )
        s.extra = std::move( extra );
}

void to_json( json &j, const Tool &t ) {
    j = json::object();
    j["name"] = t.name;
    j["description"] = t.description;
    if ( t.inputSchema )
        j["inputSchema"] = *t.inputSchema;
    else {
        // MCP 客户端要求 inputSchema 必须是 object 类型（至少包含 type="object"）
        // 无参数工具也要提供一个空 schema，防止校验失败
        j["inputSchema"] = { { "type", "object" }, { "properties", json::object() } };
    }
    if ( t.destructive )
        j["destructive"] = *t.destructive;
    if ( t.idempotent )
        j["idempotent"] = *t.idempotent;
    if ( t.openWorldHint )
        j["openWorldHint"] = *t.openWorldHint;
    if ( t.annotations )
        j["annotations"] = *t.annotations;
}
void from_json( const json &j, Tool &t ) {
    t.name = j.value( "name", std::string{} );
    t.description = j.value( "description", std::string{} );
    if ( j.contains( "inputSchema" ) )
        t.inputSchema = j["inputSchema"].get<ToolInputSchema>();
    if ( j.contains( "destructive" ) )
        t.destructive = j["destructive"].get<bool>();
    if ( j.contains( "idempotent" ) )
        t.idempotent = j["idempotent"].get<bool>();
    if ( j.contains( "openWorldHint" ) )
        t.openWorldHint = j["openWorldHint"].get<bool>();
    if ( j.contains( "annotations" ) )
        t.annotations = j["annotations"];
}

void to_json( json &j, const ToolCallResult &r ) {
    j = json::object();
    j["content"] = r.content;
    if ( r.isError )
        j["isError"] = true;
}
void from_json( const json &j, ToolCallResult &r ) {
    r.content.clear();
    if ( j.contains( "content" ) && j["content"].is_array() ) {
        for ( const auto &c : j["content"] )
            r.content.push_back( c.get<Content>() );
    }
    r.isError = j.value( "isError", false );
}

// ===========================================================================
// Resource
// ===========================================================================

void to_json( json &j, const Resource &r ) {
    j = json::object();
    j["uri"] = r.uri;
    j["name"] = r.name;
    if ( r.description )
        j["description"] = *r.description;
    if ( r.mimeType )
        j["mimeType"] = *r.mimeType;
    if ( r.annotations )
        j["annotations"] = *r.annotations;
}
void from_json( const json &j, Resource &r ) {
    r.uri = j.value( "uri", std::string{} );
    r.name = j.value( "name", std::string{} );
    if ( j.contains( "description" ) )
        r.description = j["description"].get<std::string>();
    if ( j.contains( "mimeType" ) )
        r.mimeType = j["mimeType"].get<std::string>();
    if ( j.contains( "annotations" ) )
        r.annotations = j["annotations"];
}

void to_json( json &j, const ResourceContent &r ) {
    j = json::object();
    j["uri"] = r.uri;
    if ( r.mimeType )
        j["mimeType"] = *r.mimeType;
    if ( r.text )
        j["text"] = *r.text;
    if ( r.blob )
        j["blob"] = *r.blob;
}
void from_json( const json &j, ResourceContent &r ) {
    r.uri = j.value( "uri", std::string{} );
    if ( j.contains( "mimeType" ) )
        r.mimeType = j["mimeType"].get<std::string>();
    if ( j.contains( "text" ) )
        r.text = j["text"].get<std::string>();
    if ( j.contains( "blob" ) )
        r.blob = j["blob"].get<std::string>();
}

void to_json( json &j, const ReadResourceResult &r ) {
    j = json::object();
    j["contents"] = r.contents;
}
void from_json( const json &j, ReadResourceResult &r ) {
    r.contents.clear();
    if ( j.contains( "contents" ) && j["contents"].is_array() ) {
        for ( const auto &c : j["contents"] )
            r.contents.push_back( c.get<ResourceContent>() );
    }
}

// ===========================================================================
// Prompt + PromptMessage
// ===========================================================================

void to_json( json &j, const PromptArgument &a ) {
    j = json::object();
    j["name"] = a.name;
    if ( a.description )
        j["description"] = *a.description;
    if ( a.required )
        j["required"] = true;
}
void from_json( const json &j, PromptArgument &a ) {
    a.name = j.value( "name", std::string{} );
    a.required = j.value( "required", false );
    if ( j.contains( "description" ) )
        a.description = j["description"].get<std::string>();
}

void to_json( json &j, const Prompt &p ) {
    j = json::object();
    j["name"] = p.name;
    if ( p.description )
        j["description"] = *p.description;
    if ( !p.arguments.empty() )
        j["arguments"] = p.arguments;
}
void from_json( const json &j, Prompt &p ) {
    p.name = j.value( "name", std::string{} );
    p.arguments.clear();
    if ( j.contains( "description" ) )
        p.description = j["description"].get<std::string>();
    if ( j.contains( "arguments" ) && j["arguments"].is_array() ) {
        for ( const auto &a : j["arguments"] )
            p.arguments.push_back( a.get<PromptArgument>() );
    }
}

void to_json( json &j, const PromptMessage &m ) {
    j = json::object();
    j["role"] = roleToString( m.role );
    j["content"] = m.content;
}
void from_json( const json &j, PromptMessage &m ) {
    m.role = roleFromString( j.value( "role", std::string{ "user" } ) );
    if ( j.contains( "content" ) )
        m.content = j["content"].get<Content>();
}

void to_json( json &j, const GetPromptResult &r ) {
    j = json::object();
    j["description"] = r.description;
    j["messages"] = r.messages;
}
void from_json( const json &j, GetPromptResult &r ) {
    r.description = j.value( "description", std::string{} );
    r.messages.clear();
    if ( j.contains( "messages" ) && j["messages"].is_array() ) {
        for ( const auto &m : j["messages"] )
            r.messages.push_back( m.get<PromptMessage>() );
    }
}

// ===========================================================================
// Root
// ===========================================================================
void to_json( json &j, const Root &r ) {
    j = json::object();
    j["uri"] = r.uri;
    if ( r.name )
        j["name"] = *r.name;
}
void from_json( const json &j, Root &r ) {
    r.uri = j.value( "uri", std::string{} );
    if ( j.contains( "name" ) )
        r.name = j["name"].get<std::string>();
}

// ===========================================================================
// Sampling
// ===========================================================================
void to_json( json &j, const SamplingMessage &m ) {
    j = json::object();
    j["role"] = roleToString( m.role );
    j["content"] = m.content;
}
void from_json( const json &j, SamplingMessage &m ) {
    m.role = roleFromString( j.value( "role", std::string{ "user" } ) );
    if ( j.contains( "content" ) )
        m.content = j["content"].get<Content>();
}

void to_json( json &j, const ModelPreferences &p ) {
    j = json::object();
    if ( p.temperature )
        j["temperature"] = *p.temperature;
    if ( p.topP )
        j["topP"] = *p.topP;
    if ( p.stopSequences )
        j["stopSequences"] = *p.stopSequences;
    if ( p.maxTokens )
        j["maxTokens"] = *p.maxTokens;
    if ( p.costPriority && !p.costPriority->empty() ) {
        json arr = json::array();
        for ( const auto &kv : *p.costPriority ) {
            arr.push_back( { { kv.first, kv.second } } );
        }
        // 实际上 MCP 规范没有强制结构，这里退化为对象映射更通用
        json obj = json::object();
        for ( const auto &kv : *p.costPriority )
            obj[kv.first] = kv.second;
        j["costPriority"] = obj;
    }
}
void from_json( const json &j, ModelPreferences &p ) {
    if ( j.contains( "temperature" ) )
        p.temperature = j["temperature"].get<float>();
    if ( j.contains( "topP" ) )
        p.topP = j["topP"].get<float>();
    if ( j.contains( "stopSequences" ) )
        p.stopSequences = j["stopSequences"].get<std::vector<std::string>>();
    if ( j.contains( "maxTokens" ) )
        p.maxTokens = j["maxTokens"].get<int32_t>();
    if ( j.contains( "costPriority" ) && j["costPriority"].is_object() ) {
        std::vector<std::pair<std::string, float>> cp;
        for ( auto it = j["costPriority"].begin(); it != j["costPriority"].end(); ++it ) {
            cp.emplace_back( it.key(), it.value().get<float>() );
        }
        p.costPriority = std::move( cp );
    }
}

void to_json( json &j, const CreateMessageParams &p ) {
    j = json::object();
    j["messages"] = p.messages;
    j["maxTokens"] = p.maxTokens;
    if ( p.modelPreferences )
        j["modelPreferences"] = *p.modelPreferences;
    if ( p.systemPrompt )
        j["systemPrompt"] = *p.systemPrompt;
    if ( p.stopSequences )
        j["stopSequences"] = *p.stopSequences;
    if ( p.metadata )
        j["metadata"] = *p.metadata;
}
void from_json( const json &j, CreateMessageParams &p ) {
    p.maxTokens = j.value( "maxTokens", 0 );
    p.messages.clear();
    if ( j.contains( "messages" ) && j["messages"].is_array() ) {
        for ( const auto &m : j["messages"] )
            p.messages.push_back( m.get<SamplingMessage>() );
    }
    if ( j.contains( "modelPreferences" ) )
        p.modelPreferences = j["modelPreferences"].get<ModelPreferences>();
    if ( j.contains( "systemPrompt" ) )
        p.systemPrompt = j["systemPrompt"].get<std::string>();
    if ( j.contains( "stopSequences" ) )
        p.stopSequences = j["stopSequences"].get<std::vector<std::string>>();
    if ( j.contains( "metadata" ) )
        p.metadata = j["metadata"];
}

void to_json( json &j, const CreateMessageResult &r ) {
    j = json::object();
    j["role"] = roleToString( r.role );
    j["content"] = r.content;
    j["model"] = r.model;
    if ( r.stopReason )
        j["stopReason"] = stopReasonToString( *r.stopReason );
}
void from_json( const json &j, CreateMessageResult &r ) {
    r.role = roleFromString( j.value( "role", std::string{ "assistant" } ) );
    r.model = j.value( "model", std::string{} );
    if ( j.contains( "content" ) )
        r.content = j["content"].get<Content>();
    if ( j.contains( "stopReason" ) )
        r.stopReason = stopReasonFromString( j["stopReason"].get<std::string>() );
}

// ===========================================================================
// Logging
// ===========================================================================
void to_json( json &j, const LoggingMessageParams &p ) {
    j = json::object();
    const auto ls = logLevelToString( p.level );
    if ( !ls.empty() )
        j["level"] = ls;
    if ( p.logger )
        j["logger"] = *p.logger;
    j["data"] = p.data;
}
void from_json( const json &j, LoggingMessageParams &p ) {
    p.level = logLevelFromString( j.value( "level", std::string{} ) );
    p.logger.reset();
    if ( j.contains( "logger" ) )
        p.logger = j["logger"].get<std::string>();
    p.data = j.contains( "data" ) ? j["data"] : json{};
}

// ===========================================================================
// Completion
// ===========================================================================
static std::string completionRefTypeToString( CompletionReferenceType t ) {
    switch ( t ) {
    case CompletionReferenceType::Prompt:
        return "ref/prompt";
    case CompletionReferenceType::Resource:
        return "ref/resource";
    case CompletionReferenceType::ToolCallArguments:
        return "ref/tool-call-arguments";
    }
    return {};
}
static CompletionReferenceType completionRefTypeFromString( const std::string &s ) {
    if ( s.find( "prompt" ) != std::string::npos )
        return CompletionReferenceType::Prompt;
    if ( s.find( "resource" ) != std::string::npos )
        return CompletionReferenceType::Resource;
    if ( s.find( "tool-call" ) != std::string::npos || s.find( "callArguments" ) != std::string::npos )
        return CompletionReferenceType::ToolCallArguments;
    return CompletionReferenceType::Prompt;
}

void to_json( json &j, const CompletionReference &r ) {
    j = json::object();
    j["type"] = completionRefTypeToString( r.type );
    if ( r.name )
        j["name"] = *r.name;
    if ( r.uri )
        j["uri"] = *r.uri;
    if ( r.toolName )
        j["toolName"] = *r.toolName;
}
void from_json( const json &j, CompletionReference &r ) {
    r.type = completionRefTypeFromString( j.value( "type", std::string{} ) );
    if ( j.contains( "name" ) )
        r.name = j["name"].get<std::string>();
    if ( j.contains( "uri" ) )
        r.uri = j["uri"].get<std::string>();
    if ( j.contains( "toolName" ) )
        r.toolName = j["toolName"].get<std::string>();
}

void to_json( json &j, const CompletionArgumentArgument &a ) {
    j = json::object();
    j["name"] = a.name;
    j["value"] = a.value;
}
void from_json( const json &j, CompletionArgumentArgument &a ) {
    a.name = j.value( "name", std::string{} );
    a.value = j.value( "value", std::string{} );
}

void to_json( json &j, const CompletionCompleteParams &p ) {
    j = json::object();
    j["ref"] = p.ref;
    j["argument"] = p.argument;
}
void from_json( const json &j, CompletionCompleteParams &p ) {
    if ( j.contains( "ref" ) )
        p.ref = j["ref"].get<CompletionReference>();
    if ( j.contains( "argument" ) )
        p.argument = j["argument"].get<CompletionArgumentArgument>();
}

void to_json( json &j, const Completion &c ) {
    j = json::object();
    j["values"] = c.values;
    if ( c.hasMore )
        j["hasMore"] = *c.hasMore;
    if ( c.total )
        j["total"] = *c.total;
}
void from_json( const json &j, Completion &c ) {
    c.values.clear();
    if ( j.contains( "values" ) && j["values"].is_array() ) {
        c.values = j["values"].get<std::vector<std::string>>();
    }
    if ( j.contains( "hasMore" ) )
        c.hasMore = j["hasMore"].get<bool>();
    if ( j.contains( "total" ) )
        c.total = j["total"].get<int32_t>();
}

void to_json( json &j, const CompletionResult &r ) {
    j = json::object();
    j["completion"] = r.completion;
}
void from_json( const json &j, CompletionResult &r ) {
    if ( j.contains( "completion" ) )
        r.completion = j["completion"].get<Completion>();
}

// ===========================================================================
// Capabilities / Initialize
// ===========================================================================
void to_json( json &j, const ClientCapabilities &c ) {
    j = json::object();
    if ( c.roots )
        j["roots"] = json::object( { { "listChanges", *c.roots } } );
    if ( c.sampling )
        j["sampling"] = json{};
    if ( c.experimental )
        j["experimental"] = *c.experimental;
}
void from_json( const json &j, ClientCapabilities &c ) {
    if ( j.contains( "roots" ) ) {
        c.roots = j["roots"].value( "listChanges", false );
    }
    if ( j.contains( "sampling" ) )
        c.sampling = true;
    if ( j.contains( "experimental" ) )
        c.experimental = j["experimental"];
}

void to_json( json &j, const ServerCapabilities &c ) {
    j = json::object();
    if ( c.tools )
        j["tools"] = json::object( { { "listChanges", *c.tools } } );
    if ( c.resources )
        j["resources"] = json::object( { { "subscribe", false }, { "listChanges", *c.resources } } );
    if ( c.prompts )
        j["prompts"] = json::object( { { "listChanges", *c.prompts } } );
    if ( c.logging )
        j["logging"] = json::object();
    if ( c.completions )
        j["completions"] = *c.completions;
    if ( c.experimental )
        j["experimental"] = *c.experimental;
}
void from_json( const json &j, ServerCapabilities &c ) {
    if ( j.contains( "tools" ) ) {
        const auto &t = j["tools"];
        c.tools = t.is_boolean() ? t.get<bool>() : t.value( "listChanges", true );
    }
    if ( j.contains( "resources" ) ) {
        const auto &r = j["resources"];
        c.resources = r.is_boolean() ? r.get<bool>() : r.value( "listChanges", true );
    }
    if ( j.contains( "prompts" ) ) {
        const auto &p = j["prompts"];
        c.prompts = p.is_boolean() ? p.get<bool>() : p.value( "listChanges", true );
    }
    if ( j.contains( "logging" ) )
        c.logging = true;
    if ( j.contains( "completions" ) )
        c.completions = j["completions"];
    if ( j.contains( "experimental" ) )
        c.experimental = j["experimental"];
}

void to_json( json &j, const ImplementationInfo &i ) {
    j = json::object();
    j["name"] = i.name;
    j["version"] = i.version;
}
void from_json( const json &j, ImplementationInfo &i ) {
    i.name = j.value( "name", std::string{} );
    i.version = j.value( "version", std::string{} );
}

void to_json( json &j, const InitializeParams &p ) {
    j = json::object();
    j["protocolVersion"] = p.protocolVersion;
    j["capabilities"] = p.capabilities;
    j["clientInfo"] = p.clientInfo;
}
void from_json( const json &j, InitializeParams &p ) {
    p.protocolVersion = j.value( "protocolVersion", std::string{} );
    if ( j.contains( "capabilities" ) )
        p.capabilities = j["capabilities"].get<ClientCapabilities>();
    if ( j.contains( "clientInfo" ) )
        p.clientInfo = j["clientInfo"].get<ImplementationInfo>();
}

void to_json( json &j, const InitializeResult &r ) {
    j = json::object();
    j["protocolVersion"] = r.protocolVersion;
    j["capabilities"] = r.capabilities;
    j["serverInfo"] = r.serverInfo;
    if ( r.instructions )
        j["instructions"] = *r.instructions;
}
void from_json( const json &j, InitializeResult &r ) {
    r.protocolVersion = j.value( "protocolVersion", std::string{} );
    if ( j.contains( "capabilities" ) )
        r.capabilities = j["capabilities"].get<ServerCapabilities>();
    if ( j.contains( "serverInfo" ) )
        r.serverInfo = j["serverInfo"].get<ImplementationInfo>();
    if ( j.contains( "instructions" ) )
        r.instructions = j["instructions"].get<std::string>();
}

} // namespace mcp::core

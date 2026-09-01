#ifndef MCP_CORE_H
#define MCP_CORE_H

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <optional>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include "nlohmann/json.hpp"

// ---------------------------------------------------------------------------
// MCP (Model Context Protocol) C++ 封装
// 基于 JSON-RPC 2.0 规范，覆盖 MCP 标准方法
// 参考: https://spec.modelcontextprotocol.io/
// ---------------------------------------------------------------------------

namespace mcp::core {

using json = nlohmann::json;

constexpr const char *kJsonRpcVersion = "2.0";
constexpr const char *kMcpProtocolVersion = "2025-03-26";

namespace methods {
// -- Lifecycle --
constexpr const char *Initialize = "initialize";
constexpr const char *InitializedNotification = "notifications/initialized";
constexpr const char *Ping = "ping";

// -- Tools --
constexpr const char *ToolsList = "tools/list";
constexpr const char *ToolsCall = "tools/call";

// -- Resources --
constexpr const char *ResourcesList = "resources/list";
constexpr const char *ResourcesRead = "resources/read";
constexpr const char *ResourcesUpdatedNotify = "notifications/resources/updated";
constexpr const char *ResourcesListChangedNotify = "notifications/resources/listChanged";

// -- Prompts --
constexpr const char *PromptsList = "prompts/list";
constexpr const char *PromptsGet = "prompts/get";
constexpr const char *PromptsListChangedNotify = "notifications/prompts/listChanged";

// -- Logging --
constexpr const char *LoggingSetLevel = "logging/setLevel";
constexpr const char *LoggingMessageNotify = "notifications/logging/message";

// -- Roots --
constexpr const char *RootsList = "roots/list";
constexpr const char *RootsListChangedNotify = "notifications/roots/listChanged";

// -- Sampling --
constexpr const char *SamplingCreateMessage = "sampling/createMessage";

// -- Completion --
constexpr const char *CompletionComplete = "completion/complete";

// -- Progress / Cancellation (JSON-RPC 扩展) --
constexpr const char *ProgressNotify = "$/progress";
constexpr const char *CancelRequestNotify = "$/cancelRequest";
} // namespace methods

enum class ErrorCode : int32_t {
    // JSON-RPC 2.0 标准错误
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,

    // -32000 ~ -32099 保留给服务器端自定义错误
    ServerErrorStart = -32099,
    ServerErrorEnd = -32000,
};

enum class LogLevel : uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Fatal = 5,
    Unset = 255,
};

enum class Role {
    User,
    Assistant,
};

enum class StopReason {
    EndTurn,
    StopSequence,
    MaxTokens,
    Custom,
};

enum class CompletionReferenceType {
    Prompt,
    Resource,
    ToolCallArguments,
};

struct RequestId {
    std::variant<std::monostate, int64_t, std::string> value;

    RequestId() = default;
    RequestId( int64_t v ) : value( v ) {
    }
    RequestId( std::string v ) : value( std::move( v ) ) {
    }

    bool has_value() const {
        return !std::holds_alternative<std::monostate>( value );
    }
    bool is_int() const {
        return std::holds_alternative<int64_t>( value );
    }
    bool is_string() const {
        return std::holds_alternative<std::string>( value );
    }

    int64_t as_int() const {
        return std::get<int64_t>( value );
    }
    const std::string &as_string() const {
        return std::get<std::string>( value );
    }

    bool operator==( const RequestId &other ) const {
        return value == other.value;
    }
    bool operator!=( const RequestId &other ) const {
        return !( *this == other );
    }
};

struct RpcError {
    int32_t code = 0;
    std::string message;
    std::optional<json> data;

    RpcError() = default;
    RpcError( ErrorCode c, std::string msg, std::optional<json> d = std::nullopt )
        : code( static_cast<int32_t>( c ) ), message( std::move( msg ) ), data( std::move( d ) ) {
    }
    RpcError( int32_t c, std::string msg, std::optional<json> d = std::nullopt )
        : code( c ), message( std::move( msg ) ), data( std::move( d ) ) {
    }

    RpcError makeParseError( std::optional<json> d = std::nullopt ) {
        return { ErrorCode::ParseError, "Parse error", std::move( d ) };
    }
    RpcError makeInvalidRequest( std::optional<json> d = std::nullopt ) {
        return { ErrorCode::InvalidRequest, "Invalid Request", std::move( d ) };
    }
    RpcError makeMethodNotFound( std::optional<json> d = std::nullopt ) {
        return { ErrorCode::MethodNotFound, "Method not found", std::move( d ) };
    }
    RpcError makeInvalidParams( std::optional<json> d = std::nullopt ) {
        return { ErrorCode::InvalidParams, "Invalid params", std::move( d ) };
    }
    RpcError makeInternalError( std::optional<json> d = std::nullopt ) {
        return { ErrorCode::InternalError, "Internal error", std::move( d ) };
    }
};

struct RpcRequest {
    std::string method;
    std::optional<json> params;
    RequestId id;
    std::string jsonrpc = kJsonRpcVersion;
};

struct RpcNotification {
    std::string method;
    std::optional<json> params;
    std::string jsonrpc = kJsonRpcVersion;
};

struct RpcResponse {
    RequestId id;
    std::optional<json> result;
    std::optional<RpcError> error;
    std::string jsonrpc = kJsonRpcVersion;

    bool isSuccess() const {
        return result.has_value() && !error.has_value();
    }

    bool isError() const {
        return error.has_value();
    }

    static RpcResponse makeSuccess( const RequestId &id, json res ) {
        RpcResponse r;
        r.id = id;
        r.result = std::move( res );
        return r;
    }

    static RpcResponse makeError( const RequestId &id, RpcError err ) {
        RpcResponse r;
        r.id = id;
        r.error = std::move( err );
        return r;
    }
};

struct Message {
    enum class Type {
        Unknown = 0,
        Request = 1,
        Notification = 2,
        Response = 3,
    };

    Type type = Type::Unknown;
    std::optional<RpcRequest> request;
    std::optional<RpcNotification> notification;
    std::optional<RpcResponse> response;

    // 解析任意 JSON 为 Message；如果 JSON 结构不合法，抛出 std::invalid_argument
    static Message parseMessage( const json &j );
    static Message parseMessage( const std::string &raw );

    static Message fromJson( const json &j );
    json toJson() const;

    const std::string &methodOrEmpty() const;
    const RequestId &idOrEmpty() const;
};

struct Content {
    enum class Type {
        Text,
        Image,
        Audio,
        Resource,
    };

    struct Text {
        std::string type = "text";
        std::string text;
    };

    struct Image {
        std::string type = "image";
        std::string data;     // base64
        std::string mimeType; // e.g. "image/png"
    };

    struct EmbeddedResource {
        std::string type = "resource";
        std::string resource; // resource content text or data (base64 for binary)
        std::string mimeType;
        std::string uri;
    };

    std::variant<Text, Image, EmbeddedResource> value;

    Content() : value( Text{} ) {
    }

    Content( Text t ) : value( std::move( t ) ) {
    }

    Content( Image i ) : value( std::move( i ) ) {
    }

    Content( EmbeddedResource r ) : value( std::move( r ) ) {
    }
};

struct Annotation {
    std::optional<std::string> audience; // "user" / "assistant" / 空 = 全部
    std::optional<float> priority;       // 0.0 - 1.0
};

struct JsonSchemaProperty {
    std::string type;
    std::optional<std::string> description;
    std::optional<std::vector<std::string>> enumValues;
    // 嵌套属性不完整建模，使用 json 存储扩展字段
    std::optional<json> extra;
};

struct ToolInputSchema {
    std::string type = "object";
    std::map<std::string, JsonSchemaProperty> properties;
    std::vector<std::string> required;
    std::optional<json> extra;
};

struct Tool {
    std::string name;
    std::string description;
    std::optional<ToolInputSchema> inputSchema;
    std::optional<bool> destructive;
    std::optional<bool> idempotent;
    std::optional<bool> openWorldHint;
    std::optional<json> annotations;
};

struct ToolCallResult {
    std::vector<Content> content;
    bool isError = false;
};

struct Resource {
    std::string uri;
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> mimeType;
    std::optional<json> annotations;
};

struct ResourceContent {
    std::string uri;
    std::optional<std::string> mimeType;
    // text / blob 取一
    std::optional<std::string> text;
    std::optional<std::string> blob; // base64
};

struct ReadResourceResult {
    std::vector<ResourceContent> contents;
};

struct PromptArgument {
    std::string name;
    std::optional<std::string> description;
    bool required = false;
};

struct Prompt {
    std::string name;
    std::optional<std::string> description;
    std::vector<PromptArgument> arguments;
};

struct PromptMessage {
    Role role;
    Content content;
};

struct GetPromptResult {
    std::string description;
    std::vector<PromptMessage> messages;
};

struct Root {
    std::string uri;
    std::optional<std::string> name;
};

struct SamplingMessage {
    Role role;
    Content content;
};

struct ModelPreferences {
    std::optional<float> temperature;
    std::optional<float> topP;
    std::optional<std::vector<std::string>> stopSequences;
    std::optional<int32_t> maxTokens;
    std::optional<std::vector<std::pair<std::string, float>>> costPriority; // 模型名->权重
};

struct CreateMessageParams {
    std::vector<SamplingMessage> messages;
    std::optional<ModelPreferences> modelPreferences;
    std::optional<std::string> systemPrompt;
    int32_t maxTokens = 0;
    std::optional<std::vector<std::string>> stopSequences;
    std::optional<json> metadata;
};

struct CreateMessageResult {
    Role role;
    Content content;
    std::string model;
    std::optional<StopReason> stopReason;
};

struct LoggingMessageParams {
    LogLevel level = LogLevel::Unset;
    std::optional<std::string> logger;
    json data;
};

struct CompletionReference {
    CompletionReferenceType type;
    std::optional<std::string> name;     // prompt/resource 名称
    std::optional<std::string> uri;      // resource uri
    std::optional<std::string> toolName; // for ToolCallArguments
};

struct CompletionArgumentArgument {
    std::string name;
    std::string value;
};

struct CompletionCompleteParams {
    CompletionReference ref;
    CompletionArgumentArgument argument;
};

struct Completion {
    std::vector<std::string> values;
    std::optional<bool> hasMore;
    std::optional<int32_t> total;
};

struct CompletionResult {
    Completion completion;
};

// ----- Capabilities (initialize 握手) -----
struct ClientCapabilities {
    std::optional<bool> roots;
    std::optional<bool> sampling;
    std::optional<json> experimental;
};

struct ServerCapabilities {
    std::optional<bool> tools;
    std::optional<bool> resources;
    std::optional<bool> prompts;
    std::optional<bool> logging;
    std::optional<json> completions;
    std::optional<json> experimental;
};

struct ImplementationInfo {
    std::string name;
    std::string version;
};

struct InitializeParams {
    std::string protocolVersion;
    ClientCapabilities capabilities;
    ImplementationInfo clientInfo;
};

struct InitializeResult {
    std::string protocolVersion;
    ServerCapabilities capabilities;
    ImplementationInfo serverInfo;
    std::optional<std::string> instructions;
};

// ----- Paginated result helper (tools/list / resources/list / prompts/list) -----
template <typename T>
struct PaginatedResult {
    std::vector<T> items;
    std::optional<std::string> nextCursor;
};

using ListToolsResult = PaginatedResult<Tool>;
using ListResourcesResult = PaginatedResult<Resource>;
using ListPromptsResult = PaginatedResult<Prompt>;
using ListRootsResult = std::vector<Root>;

template <typename T>
void to_json( json &j, const PaginatedResult<T> &r, const char *itemsKey ) {
    j = json::object();
    j[itemsKey] = r.items;
    if ( r.nextCursor )
        j["nextCursor"] = *r.nextCursor;
}

template <typename T>
void from_json( const json &j, PaginatedResult<T> &r, const char *itemsKey ) {
    r.items.clear();
    if ( j.contains( itemsKey ) && j[itemsKey].is_array() ) {
        for ( const auto &item : j[itemsKey] ) {
            r.items.push_back( item.get<T>() );
        }
    }
    if ( j.contains( "nextCursor" ) && !j["nextCursor"].is_null() ) {
        r.nextCursor = j["nextCursor"].get<std::string>();
    }
}

inline void to_json( json &j, const ListToolsResult &r ) {
    to_json( j, r, "tools" );
}
inline void from_json( const json &j, ListToolsResult &r ) {
    from_json( j, r, "tools" );
}

inline void to_json( json &j, const ListResourcesResult &r ) {
    to_json( j, r, "resources" );
}
inline void from_json( const json &j, ListResourcesResult &r ) {
    from_json( j, r, "resources" );
}

inline void to_json( json &j, const ListPromptsResult &r ) {
    to_json( j, r, "prompts" );
}
inline void from_json( const json &j, ListPromptsResult &r ) {
    from_json( j, r, "prompts" );
}

// 把任意 JSON-RPC 对象（Rq/Nf/Rs 或 Message）序列化成字符串
template <typename T>
std::string serializeMessage( const T &obj ) {
    return json( obj ).dump();
}

// 便捷从 params 里提取强类型参数（带默认异常）
template <typename T>
T getParams( const RpcRequest &req ) {
    if ( !req.params )
        throw std::invalid_argument( "MCP request has no params" );
    return req.params->get<T>();
}

template <typename T>
T getParams( const RpcRequest &req, const T &defaultValue ) {
    if ( !req.params )
        return defaultValue;
    try {
        return req.params->get<T>();
    } catch ( ... ) {
        return defaultValue;
    }
}

// 便捷从 response 里提取强类型结果
template <typename T>
T getResult( const RpcResponse &resp ) {
    if ( !resp.result )
        throw std::invalid_argument( "MCP response has no result" );
    return resp.result->get<T>();
}

template <typename T>
T getResult( const RpcNotification &n ) {
    if ( !n.params )
        throw std::invalid_argument( "MCP notification has no params" );
    return n.params->get<T>();
}

RpcRequest makeRequest( const RequestId &id, const std::string &method,
                        std::optional<json> params = std::nullopt );

RpcNotification makeNotification( const std::string &method, std::optional<json> params = std::nullopt );

RpcResponse makeSuccessResponse( const RequestId &id, json result );
RpcResponse makeErrorResponse( const RequestId &id, RpcError error );
RpcResponse makeErrorResponse( const RequestId &id, ErrorCode code, const std::string &msg,
                               std::optional<json> data = std::nullopt );

// ---- MCP 标准请求封装 ----
// initialize
RpcRequest makeInitializeRequest( const RequestId &id, const InitializeParams &params );

// ping
RpcRequest makePingRequest( const RequestId &id );

// tools/list
RpcRequest makeToolsListRequest( const RequestId &id,
                                 std::optional<std::string> cursor = std::nullopt );

// tools/call
RpcRequest makeToolsCallRequest( const RequestId &id, const std::string &name,
                                 std::optional<json> arguments = std::nullopt );

// resources/list
RpcRequest makeResourcesListRequest( const RequestId &id, std::optional<std::string> cursor = std::nullopt );

// resources/read
RpcRequest makeResourcesReadRequest( const RequestId &id, const std::string &uri );

// prompts/list
RpcRequest makePromptsListRequest( const RequestId &id, std::optional<std::string> cursor = std::nullopt );

// prompts/get
RpcRequest makePromptsGetRequest( const RequestId &id, const std::string &name,
                                  std::optional<json> arguments = std::nullopt );

// logging/setLevel
RpcRequest makeLoggingSetLevelRequest( const RequestId &id, LogLevel level );

// roots/list
RpcRequest makeRootsListRequest( const RequestId &id );

// sampling/createMessage
RpcRequest makeSamplingCreateMessageRequest( const RequestId &id, const CreateMessageParams &params );

// completion/complete
RpcRequest makeCompletionCompleteRequest( const RequestId &id, const CompletionCompleteParams &params );

// ---- MCP 标准通知封装 ----
RpcNotification makeInitializedNotification();
RpcNotification makeCancelRequestNotification( const RequestId &requestId );
RpcNotification makeProgressNotification( const std::string &progressToken, std::optional<int32_t> progress = std::nullopt,
                                          std::optional<int32_t> total = std::nullopt, std::optional<std::string> message = std::nullopt );
RpcNotification makeLoggingMessageNotification( const LoggingMessageParams &params );
RpcNotification makeResourcesUpdatedNotification( const std::string &uri );
RpcNotification makeResourcesListChangedNotification();
RpcNotification makePromptsListChangedNotification();
RpcNotification makeRootsListChangedNotification();

std::string roleToString( Role r );
Role roleFromString( const std::string &s );

std::string stopReasonToString( StopReason r );
StopReason stopReasonFromString( const std::string &s );

std::string logLevelToString( LogLevel level );
LogLevel logLevelFromString( const std::string &s );

void to_json( json &j, const RequestId &id );
void from_json( const json &j, RequestId &id );

void to_json( json &j, const RpcError &e );
void from_json( const json &j, RpcError &e );

void to_json( json &j, const RpcRequest &req );
void from_json( const json &j, RpcRequest &req );

void to_json( json &j, const RpcNotification &n );
void from_json( const json &j, RpcNotification &n );

void to_json( json &j, const RpcResponse &resp );
void from_json( const json &j, RpcResponse &resp );

void to_json( json &j, const Content &c );
void from_json( const json &j, Content &c );

void to_json( json &j, const Content::Text &c );
void from_json( const json &j, Content::Text &c );

void to_json( json &j, const Content::Image &c );
void from_json( const json &j, Content::Image &c );

void to_json( json &j, const JsonSchemaProperty &p );
void from_json( const json &j, JsonSchemaProperty &p );

void to_json( json &j, const ToolInputSchema &s );
void from_json( const json &j, ToolInputSchema &s );

void to_json( json &j, const Tool &t );
void from_json( const json &j, Tool &t );

void to_json( json &j, const Resource &r );
void from_json( const json &j, Resource &r );

void to_json( json &j, const ResourceContent &r );
void from_json( const json &j, ResourceContent &r );

void to_json( json &j, const ReadResourceResult &r );
void from_json( const json &j, ReadResourceResult &r );

void to_json( json &j, const Prompt &p );
void from_json( const json &j, Prompt &p );

void to_json( json &j, const PromptMessage &m );
void from_json( const json &j, PromptMessage &m );

void to_json( json &j, const GetPromptResult &r );
void from_json( const json &j, GetPromptResult &r );

void to_json( json &j, const Root &r );
void from_json( const json &j, Root &r );

void to_json( json &j, const SamplingMessage &m );
void from_json( const json &j, SamplingMessage &m );

void to_json( json &j, const ModelPreferences &p );
void from_json( const json &j, ModelPreferences &p );

void to_json( json &j, const CreateMessageParams &p );
void from_json( const json &j, CreateMessageParams &p );

void to_json( json &j, const CreateMessageResult &r );
void from_json( const json &j, CreateMessageResult &r );

void to_json( json &j, const LoggingMessageParams &p );
void from_json( const json &j, LoggingMessageParams &p );

void to_json( json &j, const CompletionReference &r );
void from_json( const json &j, CompletionReference &r );

void to_json( json &j, const CompletionArgumentArgument &a );
void from_json( const json &j, CompletionArgumentArgument &a );

void to_json( json &j, const Completion &c );
void from_json( const json &j, Completion &c );

void to_json( json &j, const ClientCapabilities &c );
void from_json( const json &j, ClientCapabilities &c );

void to_json( json &j, const ServerCapabilities &c );
void from_json( const json &j, ServerCapabilities &c );

void to_json( json &j, const ImplementationInfo &i );
void from_json( const json &j, ImplementationInfo &i );

void to_json( json &j, const InitializeParams &p );
void from_json( const json &j, InitializeParams &p );

void to_json( json &j, const InitializeResult &r );
void from_json( const json &j, InitializeResult &r );

void to_json( json &j, const CompletionResult &r );
void from_json( const json &j, CompletionResult &r );

void to_json( json &j, const CompletionCompleteParams &p );
void from_json( const json &j, CompletionCompleteParams &p );

void to_json( json &j, const Content::EmbeddedResource &c );
void from_json( const json &j, Content::EmbeddedResource &c );

void to_json( json &j, const PromptArgument &a );
void from_json( const json &j, PromptArgument &a );

void to_json( json &j, const ToolCallResult &r );
void from_json( const json &j, ToolCallResult &r );

} // namespace mcp::core

#endif // MCP_CORE_H

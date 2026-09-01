#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <optional>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include "McpCore.h"

// ===========================================================================
// MCP 客户端
// 职责:
//  - 通过 Transport 与远端 MCP 服务端通信 (Streamable HTTP / 兼容 legacy SSE)
//  - 管理 JSON-RPC 请求 id 自增 (线程安全)
//  - 提供强类型的高级 API: initialize / ping / tools / resources / prompts /
//    logging / completion
//  - 维持握手后的服务端能力 (ServerCapabilities) 与协议版本
//  - 可选: 处理服务端发起的请求 (roots/list, sampling/createMessage) 与通知
// 对应服务端实现见 mcp::McpServer
// ===========================================================================

namespace mcp {

class Client {
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

public:
    using json = core::json;

    // -----------------------------------------------------------------
    // 异常: 传输错误 / HTTP 错误 / JSON-RPC 错误均通过此类型抛出
    // -----------------------------------------------------------------
    class Error : public std::runtime_error {
    public:
        Error( const std::string &msg, int httpStatus = 0,
               std::optional<core::RpcError> rpcError = std::nullopt );
        int httpStatus() const;
        const std::optional<core::RpcError> &rpcError() const;

    private:
        int m_httpStatus;
        std::optional<core::RpcError> m_rpcError;
    };

    // -----------------------------------------------------------------
    // 传输层抽象
    // 实现负责把一条 JSON-RPC 请求投递给服务端并取回匹配的响应;
    // 过程中收到的服务端通知 / 服务端发起的请求, 通过 MessageHandler 回调
    // 交由 Client 分派 (Client 可能需要回复服务端请求 -> 返回 RpcResponse)
    // -----------------------------------------------------------------
    class Transport {
    public:
        using MessageHandler = std::function<std::optional<core::RpcResponse>( const core::Message & )>;

        virtual ~Transport() = default;
        // 发送请求, 返回 id 匹配的响应; 传输失败抛 Client::Error
        virtual core::RpcResponse sendRequest( const core::RpcRequest &req ) = 0;
        // 发送通知 (无响应); 失败抛 Client::Error
        virtual void sendNotification( const core::RpcNotification &n ) = 0;
        // 发送响应 (回复服务端发起的请求); 失败抛 Client::Error
        virtual void sendResponse( const core::RpcResponse &resp ) = 0;
        // 安装消息分派回调 (由 Client 在 setTransport 时注入)
        virtual void setMessageHandler( MessageHandler handler ) {
            (void)handler;
        }
        // 关闭传输 (如断开 SSE 会话)
        virtual void close() {
        }
    };

    // HTTP (Streamable HTTP) 传输选项
    struct HttpOptions {
        std::string url;                            // 例: "http://127.0.0.1:8080/mcp"
        std::string authToken;                      // 可选 Bearer token
        std::chrono::seconds timeout{ 30 };         // 连接/读写超时
        std::map<std::string, std::string> headers; // 额外自定义请求头
        bool sseLegacyMode = false;                 // true=先 GET /mcp 建 SSE 会话再 POST
    };

    // stdio 传输选项 (启动子进程, 经 stdin/stdout 交换换行分隔的 JSON-RPC)
    struct StdioOptions {
        std::vector<std::string> command;       // command[0]=可执行文件 (路径或 PATH 名), 其余为参数
        std::map<std::string, std::string> env; // 追加/覆盖环境变量 (默认继承当前进程环境)
        std::filesystem::path workingDirectory; // 工作目录 (空 = 当前目录)
        std::chrono::seconds timeout{ 0 };      // 单次请求等待响应超时; 0 = 无限等待
    };

    // -----------------------------------------------------------------
    // 服务端发起请求 / 通知的回调类型
    // -----------------------------------------------------------------
    using RootsProvider = std::function<core::ListRootsResult()>;
    using SamplingHandler = std::function<core::CreateMessageResult( const core::CreateMessageParams & )>;
    using NotificationHandler = std::function<void( const core::RpcNotification & )>;
    using ServerRequestHandler = std::function<core::RpcResponse( const core::RpcRequest & )>;

    // -----------------------------------------------------------------
    // 构造
    // -----------------------------------------------------------------
    Client();
    explicit Client( std::shared_ptr<Transport> transport );
    ~Client();

    Client( const Client & ) = delete;
    Client &operator=( const Client & ) = delete;

    // 设置/获取传输层 (setTransport 会自动把消息分派回调注入 transport)
    void setTransport( std::shared_ptr<Transport> transport );
    std::shared_ptr<Transport> transport() const;

    // -- 客户端元信息 (initialize 时发送给服务端) --
    void setClientInfo( const std::string &name, const std::string &version );
    const core::ImplementationInfo &clientInfo() const;
    core::ClientCapabilities &capabilities();
    const core::ClientCapabilities &capabilities() const;

    // -- 握手后状态 --
    bool isInitialized() const;
    const core::InitializeResult &serverInfo() const;
    const std::string &protocolVersion() const;

    // -- 回调注册 --
    void setRootsProvider( RootsProvider provider );
    void setSamplingHandler( SamplingHandler handler );
    void setNotificationHandler( NotificationHandler handler );
    void setServerRequestHandler( ServerRequestHandler handler ); // 兜底自定义处理

    // -----------------------------------------------------------------
    // 生命周期
    // -----------------------------------------------------------------
    // 执行 initialize 握手, 并自动发送 notifications/initialized
    core::InitializeResult initialize();
    // 仅发送 initialized 通知 (initialize() 已自动调用, 通常无需手动)
    void notifyInitialized();
    // ping
    void ping();

    // -----------------------------------------------------------------
    // 高级 API: RPC 错误抛出 Client::Error; tools/call 的工具错误
    // 通过返回值的 isError 字段表达, 不抛异常
    // -----------------------------------------------------------------
    core::ListToolsResult listTools( std::optional<std::string> cursor = std::nullopt );
    core::ToolCallResult callTool( const std::string &name,
                                   std::optional<json> arguments = std::nullopt );
    core::ListResourcesResult listResources( std::optional<std::string> cursor = std::nullopt );
    core::ReadResourceResult readResource( const std::string &uri );
    core::ListPromptsResult listPrompts( std::optional<std::string> cursor = std::nullopt );
    core::GetPromptResult getPrompt( const std::string &name,
                                     std::optional<json> arguments = std::nullopt );
    void setLoggingLevel( core::LogLevel level );
    core::Completion complete( const core::CompletionCompleteParams &params );

    // -----------------------------------------------------------------
    // 低级 API
    // -----------------------------------------------------------------
    // 发送任意请求, 返回原始 RpcResponse (不因 RPC error 抛异常, 便于自定义判错)
    // 传输/协议错误仍会抛 Client::Error
    core::RpcResponse sendRequest( const std::string &method,
                                   std::optional<json> params = std::nullopt );
    void sendNotification( const std::string &method,
                           std::optional<json> params = std::nullopt );
    void sendResponse( const core::RpcResponse &resp );

    // -----------------------------------------------------------------
    // 工厂: 创建传输 (实现位于 .cpp, 头文件不依赖 httplib / boost::process)
    // -----------------------------------------------------------------
    static std::shared_ptr<Transport> createHttpTransport( const HttpOptions &opts );
    static std::shared_ptr<Transport> createStdioTransport( const StdioOptions &opts );
};

} // namespace mcp

#endif // MCP_CLIENT_H

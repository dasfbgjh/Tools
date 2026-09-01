#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <optional>
#include "McpCore.h"

// ===========================================================================
// MCP 服务端引擎
// 职责:
//  - 持有能力声明 (ServerCapabilities)
//  - 允许外部注册 Tool / Resource / Prompt / RootsProvider 回调
//  - 接收一条 MCP JSON-RPC Message，返回对应响应 Message
//  - 同步 request/response 模式；对 notification 返回空消息
// ===========================================================================

namespace mcp {

class McpServer {
public:
    using json = core::json;

    // -- 回调类型签名 --
    using ToolHandler = std::function<core::ToolCallResult( const std::optional<json> &arguments )>;
    using ResourceReader = std::function<core::ReadResourceResult( const std::string &uri )>;
    using PromptGetter = std::function<core::GetPromptResult( const std::optional<json> &arguments )>;
    using RootsProvider = std::function<std::vector<core::Root>()>;
    using LogLevelSetter = std::function<void( core::LogLevel )>;
    using Canceller = std::function<void( const core::RequestId &requestId )>;
    using ProgressReporter = std::function<void( const std::string &progressToken, std::optional<int32_t> progress,
                                                 std::optional<int32_t> total, std::optional<std::string> message )>;
    using CompletionHandler = std::function<core::Completion( const core::CompletionCompleteParams & )>;

    struct RegisteredTool {
        core::Tool definition;
        ToolHandler handler;
    };

    struct RegisteredResource {
        core::Resource definition;
        ResourceReader reader;
    };

    struct RegisteredPrompt {
        core::Prompt definition;
        PromptGetter getter;
    };

    // -----------------------------------------------------------------
    McpServer();
    ~McpServer();

    // -- 元信息 --
    void setServerInfo( const std::string &name, const std::string &version, std::optional<std::string> instructions = std::nullopt );
    const core::ImplementationInfo &serverInfo() const;
    const std::optional<std::string> &instructions() const;

    // -- 能力声明（默认都开启） --
    core::ServerCapabilities &capabilities();
    const core::ServerCapabilities &capabilities() const;

    // -- 注册入口 --
    void registerTool( core::Tool definition, ToolHandler handler );
    void unregisterTool( const std::string &name );

    void registerResource( core::Resource definition, ResourceReader reader );
    void unregisterResource( const std::string &uri );

    void registerPrompt( core::Prompt definition, PromptGetter getter );
    void unregisterPrompt( const std::string &name );

    void setRootsProvider( RootsProvider provider );
    void setLogLevelSetter( LogLevelSetter setter );
    void setCanceller( Canceller canceller );
    void setProgressReporter( ProgressReporter reporter ); // unused here, exposed for hooks
    void setCompletionHandler( CompletionHandler handler );

    // -----------------------------------------------------------------
    // 核心: 处理一条 JSON-RPC 消息，返回响应（Notification 返回空 Message）
    // -----------------------------------------------------------------
    core::Message handleMessage( const core::Message &incoming );

    // 便捷重载：接受 json 或原始字符串，返回序列化结果 (json / string)
    json handleJson( const json &j );
    std::string handleRaw( const std::string &raw );
    json handleBatchOrSingle( const json &j );

    // 广播型通知生成器（供 SSE / stdio 发送队列使用者调用）—— 暂存最新值
    // 例如 notifyResourcesListChanged / notifyPromptsListChanged / notifyLoggingMessage
    core::RpcNotification makeResourcesListChanged() const {
        return core::makeResourcesListChangedNotification();
    }

    core::RpcNotification makePromptsListChanged() const {
        return core::makePromptsListChangedNotification();
    }

    core::RpcNotification makeRootsListChanged() const {
        return core::makeRootsListChangedNotification();
    }

    // 允许外部覆盖默认 initialize 结果检查（例如要求特定 protocolVersion）
    void setProtocolVersion( const std::string &pv );
    const std::string &protocolVersion() const;

private:
    mutable std::mutex m_mtx;

    std::string m_protocolVersion = core::kMcpProtocolVersion;
    core::ImplementationInfo m_serverInfo{ "Tools-MCP-Server", "1.0.0" };
    std::optional<std::string> m_instructions;
    core::ServerCapabilities m_caps;

    // 注册表
    std::map<std::string, RegisteredTool> m_tools;         // name -> tool
    std::map<std::string, RegisteredResource> m_resources; // uri  -> resource
    std::map<std::string, RegisteredPrompt> m_prompts;     // name -> prompt

    // 可选回调
    RootsProvider m_rootsProvider;
    LogLevelSetter m_logLevelSetter;
    Canceller m_canceller;
    ProgressReporter m_progressReporter;
    CompletionHandler m_completion;

    // initialize 已成功的 session-id-less 粗略标记 (HTTP POST 模式下用于幂等语义)
    std::atomic<bool> m_initialized{ false };

    // ---- 内部分派 ----
    core::RpcResponse dispatchRequest( const core::RpcRequest &req );
    void dispatchNotification( const core::RpcNotification &n );

    core::RpcResponse handleInitialize( const core::RpcRequest &req );
    core::RpcResponse handlePing( const core::RpcRequest &req );
    core::RpcResponse handleToolsList( const core::RpcRequest &req );
    core::RpcResponse handleToolsCall( const core::RpcRequest &req );
    core::RpcResponse handleResourcesList( const core::RpcRequest &req );
    core::RpcResponse handleResourcesRead( const core::RpcRequest &req );
    core::RpcResponse handlePromptsList( const core::RpcRequest &req );
    core::RpcResponse handlePromptsGet( const core::RpcRequest &req );
    core::RpcResponse handleLoggingSetLevel( const core::RpcRequest &req );
    core::RpcResponse handleRootsList( const core::RpcRequest &req );
    core::RpcResponse handleSamplingCreateMessage( const core::RpcRequest &req );
    core::RpcResponse handleCompletionComplete( const core::RpcRequest &req );
};

} // namespace mcp

#endif // MCP_SERVER_H

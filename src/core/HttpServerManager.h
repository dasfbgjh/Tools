#ifndef HTTPSERVERMANAGER_H
#define HTTPSERVERMANAGER_H

#include <httplib.h>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <vector>

// 单个运行中的 HTTP 服务器实例
struct HttpServerInstance {
    std::string id; // 配置 id
    int port = 0;
    std::unique_ptr<httplib::Server> server;
    std::thread thread;
    std::string lastError; // 启动失败时的错误信息
    bool running = false;  // 线程是否在运行
};

class HttpServerManager {
public:
    static HttpServerManager &instance();

    // 启动指定 id 的服务器（id 必须在 http_servers + http_server_mounts 中存在）
    // 成功返回 true；失败返回 false 且 lastError 设置
    bool start( const std::string &id );

    // 停止指定 id 的服务器；不存在的 id 也返回 true（幂等）
    bool stop( const std::string &id );

    // 启动所有 auto_start=1 的服务器
    void startAutoStart();

    // 停止所有服务器
    void shutdownAll();

    // 是否在运行
    bool isRunning( const std::string &id ) const;

    // 实时状态：'running' | 'stopped' | 'error'
    std::string status( const std::string &id ) const;

    // 当前正在监听的端口集合（用于启动前冲突检测）
    std::vector<int> listeningPorts() const;

private:
    HttpServerManager() = default;
    HttpServerManager( const HttpServerManager & ) = delete;
    HttpServerManager &operator=( const HttpServerManager & ) = delete;

    HttpServerInstance *findInstance( const std::string &id ) const;

    mutable std::mutex m_mtx;
    std::unordered_map<std::string, std::unique_ptr<HttpServerInstance>> m_instances;
};

#endif

#ifndef PROCMANAGER_H
#define PROCMANAGER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/EventLoop.h"

struct ProcLogLine {
    int64_t seq = 0;  // 单调递增
    std::string text; // 单行文本（不含 \n）
    int stream = 0;   // 0=stdout, 1=stderr
    int64_t tsMs = 0; // 时间戳
};

struct ProcInstance {
    std::string id;
    std::string name;
    int pid = 0;
    std::unique_ptr<AsyncProcess> process;
    std::thread waiterThread;

    std::mutex mtx;
    std::deque<ProcLogLine> logs; // 环形缓冲（限长）
    int64_t nextSeq = 1;
    static const size_t kMaxLogLines = 2000;
    bool lastLogTruncated = false; // 截断标记

    std::atomic<bool> running{ false };
    std::atomic<int> exitCode{ 0 };
    std::atomic<int64_t> startTimeMs{ 0 };
    std::string lastError;

    // 调用者须已持 mtx
    void appendLog( const std::string &line, int stream );
};

class ProcManager {

private:
    mutable std::mutex m_instancesMtx;
    std::unordered_map<std::string, std::unique_ptr<ProcInstance>> m_instances;

public:
    static ProcManager &instance();

    // 启动/停止
    bool start( const std::string &id );
    bool stop( const std::string &id, bool force = false );

    // 启动时自动启动所有 auto_start=1 的进程
    void startAutoStart();

    // 状态查询
    bool isRunning( const std::string &id ) const;
    std::string status( const std::string &id ) const; // 'running' | 'stopped' | 'error'

    // 取增量输出
    struct LogPage {
        int64_t lastSeq = 0;
        std::vector<ProcLogLine> lines;
        bool truncated = false;
    };
    LogPage getLogs( const std::string &id, int64_t sinceSeq, int limit );

    // 清空日志（停止时调用）
    void clearLogs( const std::string &id );

    // 关闭全部（应用退出时）
    void shutdownAll();

private:
    ProcManager() = default;

    ProcManager( const ProcManager & ) = delete;

    ProcManager &operator=( const ProcManager & ) = delete;

    std::map<std::string, std::string> buildEnv( bool envInherit, const std::map<std::string, std::string> &userEnv );

    void waitProcess( ProcInstance *instPtr, const std::string &name, const std::string &id );

    ProcInstance *findInstanceNoLock( const std::string &id ) const;

    void handleProcessOutput( std::string &pending, ProcInstance *instPtr, int stream );

    void appendLogNoLock( ProcInstance *inst, const std::string &line, int stream );

    static std::string trimNewlines( const std::string &s );
};

#endif

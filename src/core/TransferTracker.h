#ifndef TRANSFERTRACKER_H
#define TRANSFERTRACKER_H

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
struct Transfer {
    std::string id;   // UUID
    std::string type; // "upload" | "download"
    std::string filename;
    std::string ip;
    int64_t total = 0;           // 总字节（0 表示未知，如流式上传）
    int64_t transferred = 0;     // 已传字节
    int64_t speed = 0;           // 当前瞬时速度 bytes/s
    int64_t remainingMs = -1;    // 剩余时间 ms，-1 表示未知
    double progress = 0.0;       // 0-1
    std::string status;          // "active" | "completed" | "error"
    int64_t startTimeMs = 0;     // 起始时间
    int64_t lastUpdateMs = 0;    // 上次 update 时间
    int64_t prevSampleBytes = 0; // 上次采样时已传字节
    int64_t prevSampleMs = 0;    // 上次采样时间
    std::string errorMessage;    // 出错信息
};

// 全局传输注册表（单例）。
// 线程安全；调用方通常在请求处理线程中操作。
class TransferTracker {
public:
    static TransferTracker &instance();

    // 注册新任务并返回 id。
    std::string start( const std::string &type, const std::string &filename,
                       const std::string &ip, int64_t total );

    // 更新已传输字节数。会重算瞬时速度（节流：距上次 update 至少 250ms 才计算新速度）。
    // bytes 累计值（从 0 开始），由调用方提供。
    void update( const std::string &id, int64_t bytes );

    // 设置/更新总字节数（用于后续才知道总量的情况，如流式 multipart 上传，
    // 可先用 Content-Length 作上界，注册成功后再用文件实际大小校正）。
    void setTotal( const std::string &id, int64_t total );

    // 结束任务。status 为 "completed" 或 "error"，error 时附 errorMessage。
    void end( const std::string &id, const std::string &status,
              const std::string &errorMessage = "" );

    // 返回所有任务（含 ended 但尚未清理的）；以及过去 2 分钟按秒采样的总速度历史（bytes/s）。
    // 形式为 JSON 字符串，路由直接转发。
    std::string snapshotJson();

    // 上次 snapshot 之后到现在的总增量 bytes（用于聚合速度）。
    // 每次调用后内部清零。
    int64_t consumeTotalDelta();

private:
    TransferTracker();

    mutable std::shared_mutex mtx_;
    std::unordered_map<std::string, Transfer> items_;

    // 原始增量样本（最近 120 秒）。snapshot 时按秒聚合。
    struct RawSample {
        int64_t t = 0; // epoch ms
        int64_t upDelta = 0;
        int64_t downDelta = 0;
    };
    std::deque<RawSample> rawSamples_;
};

#endif
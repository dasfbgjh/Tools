#include "TransferTracker.h"
#include "Server.h"
#include "common/Logger.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <thread>

namespace {
int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch() )
        .count();
}

std::string newId() {
    static thread_local std::mt19937_64 rng(
        std::chrono::high_resolution_clock::now().time_since_epoch().count() ^
        (uint64_t)std::hash<std::thread::id>{}(std::this_thread::get_id()) );
    uint64_t a = rng(), b = rng();
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << (a >> 32) << '-'
       << std::setw(4) << ((a >> 16) & 0xffff) << '-'
       << std::setw(4) << (a & 0xffff) << '-'
       << std::setw(4) << (b >> 48) << '-'
       << std::setw(12) << (b & 0xffffffffffffULL);
    return ss.str();
}
} // namespace

TransferTracker::TransferTracker() = default;

TransferTracker &TransferTracker::instance() {
    static TransferTracker t;
    return t;
}

std::string TransferTracker::start( const std::string &type, const std::string &filename,
                                    const std::string &ip, int64_t total ) {
    Transfer t;
    t.id = newId();
    t.type = type;
    t.filename = filename;
    t.ip = ip;
    t.total = total;
    t.status = "active";
    t.startTimeMs = nowMs();
    t.lastUpdateMs = t.startTimeMs;
    t.prevSampleMs = t.startTimeMs;

    {
        std::unique_lock lk( mtx_ );
        items_[t.id] = t;
    }
    LOG_DEBUG << "transfer.start id=" << t.id << " type=" << type
              << " filename=" << filename << " ip=" << ip
              << " total=" << total;
    return t.id;
}

void TransferTracker::update( const std::string &id, int64_t bytes ) {
    int64_t now = nowMs();
    int64_t delta = 0;
    bool isUp = false;
    {
        std::unique_lock lk( mtx_ );
        auto it = items_.find( id );
        if ( it == items_.end() ) return;
        Transfer &t = it->second;
        int64_t prev = t.transferred;
        t.transferred = bytes;
        t.lastUpdateMs = now;
        if ( t.total > 0 ) {
            double p = (double)bytes / (double)t.total;
            t.progress = p > 1.0 ? 1.0 : p;
        }
        delta = bytes - prev;
        if ( delta < 0 ) delta = 0;
        isUp = ( t.type == "upload" );
        // 瞬时速度：节流 250ms。窗口内的真实增量 = bytes - prevSampleBytes
        // （不能直接用本轮 delta，否则更新频繁时速度被严重低估）
        int64_t dt = now - t.prevSampleMs;
        if ( dt >= 250 ) {
            int64_t windowBytes = bytes - t.prevSampleBytes;
            if ( windowBytes < 0 ) windowBytes = 0;
            t.speed = (int64_t)( (double)windowBytes * 1000.0 / (double)dt );
            t.prevSampleBytes = bytes;
            t.prevSampleMs = now;
            if ( t.speed > 0 && t.total > 0 ) {
                int64_t left = t.total - t.transferred;
                if ( left < 0 ) left = 0;
                t.remainingMs = (int64_t)( (double)left * 1000.0 / (double)t.speed );
            } else {
                t.remainingMs = -1;
            }
        }
    }
    // 每次 update 都推一个 raw sample，snapshot 时按秒聚合
    if ( delta > 0 ) {
        std::unique_lock lk( mtx_ );
        RawSample s;
        s.t = now;
        if ( isUp ) s.upDelta = delta;
        else s.downDelta = delta;
        rawSamples_.push_back( s );
        // 截断到 120 秒
        int64_t cutoff = now - 120000;
        while ( !rawSamples_.empty() && rawSamples_.front().t < cutoff ) {
            rawSamples_.pop_front();
        }
    }
}

void TransferTracker::setTotal( const std::string &id, int64_t total ) {
    std::unique_lock lk( mtx_ );
    auto it = items_.find( id );
    if ( it == items_.end() ) return;
    Transfer &t = it->second;
    t.total = total;
    if ( total > 0 && t.transferred > 0 ) {
        double p = (double)t.transferred / (double)total;
        t.progress = p > 1.0 ? 1.0 : p;
    }
}

void TransferTracker::end( const std::string &id, const std::string &status,
                           const std::string &errorMessage ) {
    int64_t now = nowMs();
    {
        std::unique_lock lk( mtx_ );
        auto it = items_.find( id );
        if ( it == items_.end() ) return;
        Transfer &t = it->second;
        t.status = status;
        t.errorMessage = errorMessage;
        t.lastUpdateMs = now;
        if ( status == "completed" ) {
            t.progress = 1.0;
            if ( t.total > 0 ) t.transferred = t.total;
        }
    }
    LOG_DEBUG << "transfer.end id=" << id << " status=" << status
              << " error=" << errorMessage;
    // 完成后保留 30 秒供前端展示，再延迟清理
    auto idCopy = id;
    std::thread( [idCopy]() {
        std::this_thread::sleep_for( std::chrono::seconds( 30 ) );
        std::unique_lock lk( TransferTracker::instance().mtx_ );
        TransferTracker::instance().items_.erase( idCopy );
    } ).detach();
}

std::string TransferTracker::snapshotJson() {
    int64_t now = nowMs();
    Server::json j = Server::json::array();
    {
        std::shared_lock lk( mtx_ );
        for ( auto &kv : items_ ) {
            const Transfer &t = kv.second;
            Server::json o = {
                { "id", t.id },
                { "type", t.type },
                { "filename", t.filename },
                { "ip", t.ip },
                { "total", t.total },
                { "transferred", t.transferred },
                { "speed", t.speed },
                { "remainingMs", t.remainingMs },
                { "progress", t.progress },
                { "status", t.status },
                { "startTime", t.startTimeMs },
                { "elapsedMs", now - t.startTimeMs },
                { "error", t.errorMessage }
            };
            j.push_back( o );
        }
    }
    Server::json hist = Server::json::array();
    {
        std::shared_lock lk( mtx_ );
        // 按秒聚合 raw samples 为 {t, up, down}（bytes/s）
        if ( !rawSamples_.empty() ) {
            int64_t curSec = rawSamples_.front().t / 1000;
            int64_t secStart = curSec * 1000;
            int64_t upSum = 0, downSum = 0;
            for ( const auto &s : rawSamples_ ) {
                int64_t sec = s.t / 1000;
                if ( sec != curSec ) {
                    // 推到上一秒：使用真实经过的毫秒数转换为 bytes/s
                    int64_t durMs = rawSamples_.empty() ? 1000 : 0;
                    // 简化：直接用 1s 窗口
                    hist.push_back( { { "t", secStart },
                                     { "up", upSum },
                                     { "down", downSum } } );
                    curSec = sec;
                    secStart = sec * 1000;
                    upSum = 0;
                    downSum = 0;
                }
                upSum += s.upDelta;
                downSum += s.downDelta;
            }
            // 推最后一秒
            hist.push_back( { { "t", secStart },
                             { "up", upSum },
                             { "down", downSum } } );
        }
    }
    Server::json out = {
        { "success", true },
        { "transfers", j },
        { "speedHistory", hist },
        { "now", now }
    };
    return out.dump();
}


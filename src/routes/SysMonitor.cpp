#include "routes/SysMonitor.h"
#include "common/Logger.hpp"
#include "core/Server.h"

#include <hwinfo/hwinfo.h>
#include <hwinfo/cpu.h>
#include <hwinfo/ram.h>
#include <hwinfo/mainboard.h>
#include <hwinfo/disk.h>
#include <hwinfo/os.h>
#include <hwinfo/gpu.h>
#include <hwinfo/network.h>
#include <hwinfo/battery.h>

#include <hwinfo/monitoring/cpu.h>
#include <hwinfo/monitoring/ram.h>
#include <hwinfo/monitoring/disk.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace routes::sysmonitor {

// 字节数转可读字符串（B / KB / MB / GB / TB）
static std::string formatBytes( uint64_t bytes ) {
    const double KB = 1024.0;
    const double MB = KB * 1024.0;
    const double GB = MB * 1024.0;
    const double TB = GB * 1024.0;
    char buf[64];
    if ( bytes >= (uint64_t)TB )
        snprintf( buf, sizeof( buf ), "%.2f TB", bytes / TB );
    else if ( bytes >= (uint64_t)GB )
        snprintf( buf, sizeof( buf ), "%.2f GB", bytes / GB );
    else if ( bytes >= (uint64_t)MB )
        snprintf( buf, sizeof( buf ), "%.2f MB", bytes / MB );
    else if ( bytes >= (uint64_t)KB )
        snprintf( buf, sizeof( buf ), "%.2f KB", bytes / KB );
    else
        snprintf( buf, sizeof( buf ), "%llu B", (unsigned long long)bytes );
    return buf;
}

// 频率（Hz）→ 可读字符串（GHz / MHz）
static std::string formatHz( int64_t hz ) {
    if ( hz <= 0 )
        return "N/A";
    char buf[64];
    if ( hz >= (int64_t)1e9 )
        snprintf( buf, sizeof( buf ), "%.2f GHz", hz / 1e9 );
    else if ( hz >= (int64_t)1e6 )
        snprintf( buf, sizeof( buf ), "%.0f MHz", hz / 1e6 );
    else if ( hz >= (int64_t)1e3 )
        snprintf( buf, sizeof( buf ), "%.0f KHz", hz / 1e3 );
    else
        snprintf( buf, sizeof( buf ), "%lld Hz", (long long)hz );
    return buf;
}

static std::string diskInterfaceToString( hwinfo::Disk::Interface i ) {
    switch ( i ) {
    case hwinfo::Disk::Interface::NVME:
        return "NVMe";
    case hwinfo::Disk::Interface::SATA:
        return "SATA";
    case hwinfo::Disk::Interface::SCSI:
        return "SCSI";
    case hwinfo::Disk::Interface::USB:
        return "USB";
    case hwinfo::Disk::Interface::USB1:
        return "USB 1.0";
    case hwinfo::Disk::Interface::USB2:
        return "USB 2.0";
    case hwinfo::Disk::Interface::USB3_5GBit:
        return "USB 3.0 (5 Gbit/s)";
    case hwinfo::Disk::Interface::USB3_10GBit:
        return "USB 3.1 (10 Gbit/s)";
    case hwinfo::Disk::Interface::USB3_20GBit:
        return "USB 3.2 (20 Gbit/s)";
    case hwinfo::Disk::Interface::USB4_20GBit:
        return "USB4 (20 Gbit/s)";
    case hwinfo::Disk::Interface::USB4_40GBit:
        return "USB4 (40 Gbit/s)";
    case hwinfo::Disk::Interface::USB4_80GBit:
        return "USB4 (80 Gbit/s)";
    default:
        return "UNKNOWN";
    }
}

// ======================================================
// GET /api/sys/info  静态信息
// ======================================================
static void getSysInfo( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;

    Server::json j = Server::json::object();

    // ---- OS ----
    try {
        hwinfo::OS os;
        Server::json o;
        o["name"] = os.name();
        o["version"] = os.version();
        o["kernel"] = os.kernel();
        o["is32bit"] = os.is32bit();
        o["is64bit"] = os.is64bit();
        o["isLittleEndian"] = os.isLittleEndian();
        o["isBigEndian"] = os.isBigEndian();
        j["os"] = o;
    } catch ( const std::exception &e ) {
        LOG_WARN << "读取 OS 信息失败: " << e.what();
        j["os"] = nullptr;
    }

    // ---- Mainboard ----
    try {
        hwinfo::MainBoard mb;
        Server::json m;
        m["vendor"] = mb.vendor();
        m["name"] = mb.name();
        m["version"] = mb.version();
        m["serial"] = mb.serialNumber();
        j["mainboard"] = m;
    } catch ( const std::exception &e ) {
        LOG_WARN << "读取主板信息失败: " << e.what();
        j["mainboard"] = nullptr;
    }

    // ---- CPU（静态） ----
    try {
        auto cpus = hwinfo::getAllCPUs();
        Server::json arr = Server::json::array();
        for ( const auto &c : cpus ) {
            Server::json ci;
            ci["modelName"] = c.modelName();
            ci["vendor"] = c.vendor();
            ci["numPhysicalCores"] = (uint64_t)c.numPhysicalCores();
            ci["numLogicalCores"] = (uint64_t)c.numLogicalCores();
            // flags
            Server::json flags = Server::json::array();
            for ( const auto &f : c.flags() )
                flags.push_back( f );
            ci["flags"] = flags;
            // 单核参数（首颗 CPU）
            if ( !c.cores().empty() ) {
                const auto &core0 = c.cores().front();
                ci["regularFrequencyHz"] = (uint64_t)core0.regular_frequency_hz;
                ci["maxFrequencyHz"] = (uint64_t)core0.max_frequency_hz;
                ci["regularFrequency"] = formatHz( (int64_t)core0.regular_frequency_hz );
                ci["maxFrequency"] = formatHz( (int64_t)core0.max_frequency_hz );
            }
            arr.push_back( ci );
        }
        j["cpus"] = arr;
    } catch ( const std::exception &e ) {
        LOG_WARN << "读取 CPU 信息失败: " << e.what();
        j["cpus"] = Server::json::array();
    }

    // ---- Memory（静态模块信息） ----
    try {
        hwinfo::Memory mem;
        Server::json arr = Server::json::array();
        for ( const auto &m : mem.modules() ) {
            Server::json mi;
            mi["vendor"] = m.vendor;
            mi["name"] = m.name;
            mi["model"] = m.model;
            mi["serial"] = m.serial_number;
            mi["sizeBytes"] = (uint64_t)m._size_bytes;
            mi["sizeHuman"] = formatBytes( m._size_bytes );
            mi["frequencyHz"] = (uint64_t)m.frequency_hz;
            mi["frequency"] = formatHz( (int64_t)m.frequency_hz );
            arr.push_back( mi );
        }
        Server::json mj;
        mj["totalBytes"] = (uint64_t)mem.size();
        mj["totalHuman"] = formatBytes( (uint64_t)mem.size() );
        mj["modules"] = arr;
        j["memory"] = mj;
    } catch ( const std::exception &e ) {
        LOG_WARN << "读取内存信息失败: " << e.what();
        j["memory"] = nullptr;
    }

    // ---- Disks ----
    try {
        auto disks = hwinfo::getAllDisks();
        Server::json arr = Server::json::array();
        for ( const auto &d : disks ) {
            Server::json di;
            di["vendor"] = d.vendor();
            di["model"] = d.model();
            di["serial"] = d.serial_number();
            di["sizeBytes"] = (uint64_t)d.size();
            di["sizeHuman"] = formatBytes( (uint64_t)d.size() );
            di["interface"] = diskInterfaceToString( d.disk_interface() );
            Server::json mps = Server::json::array();
            for ( const auto &mp : d.mount_points() )
                mps.push_back( mp );
            di["mountPoints"] = mps;
            arr.push_back( di );
        }
        j["disks"] = arr;
    } catch ( const std::exception &e ) {
        LOG_WARN << "读取磁盘信息失败: " << e.what();
        j["disks"] = Server::json::array();
    }

    // ---- GPUs ----
    try {
        auto gpus = hwinfo::getAllGPUs();
        Server::json arr = Server::json::array();
        for ( const auto &g : gpus ) {
            Server::json gi;
            gi["vendor"] = g.vendor();
            gi["name"] = g.name();
            gi["driverVersion"] = g.driverVersion();
            gi["dedicatedMemoryBytes"] = (uint64_t)g.dedicated_memory_Bytes();
            gi["dedicatedMemoryHuman"] = formatBytes( (uint64_t)g.dedicated_memory_Bytes() );
            gi["sharedMemoryBytes"] = (uint64_t)g.shared_memory_Bytes();
            gi["sharedMemoryHuman"] = formatBytes( (uint64_t)g.shared_memory_Bytes() );
            gi["frequencyHz"] = (uint64_t)g.frequency_hz();
            gi["frequency"] = formatHz( (int64_t)g.frequency_hz() );
            gi["numCores"] = (uint64_t)g.num_cores();
            arr.push_back( gi );
        }
        j["gpus"] = arr;
    } catch ( const std::exception &e ) {
        LOG_WARN << "读取 GPU 信息失败: " << e.what();
        j["gpus"] = Server::json::array();
    }

    // ---- Networks ----
    try {
        auto nets = hwinfo::getAllNetworks();
        Server::json arr = Server::json::array();
        for ( const auto &n : nets ) {
            Server::json ni;
            ni["index"] = n.interfaceIndex();
            ni["description"] = n.description();
            ni["mac"] = n.mac();
            ni["ip4"] = n.ip4();
            ni["ip6"] = n.ip6();
            arr.insert( arr.begin(), ni );
        }
        j["networks"] = arr;
    } catch ( const std::exception &e ) {
        LOG_WARN << "读取网络信息失败: " << e.what();
        j["networks"] = Server::json::array();
    }

    // ---- Battery ----
    try {
        auto bats = hwinfo::getAllBatteries();
        Server::json arr = Server::json::array();
        for ( const auto &bat : bats ) {
            Server::json bj;
            bj["vendor"] = bat.vendor();
            bj["model"] = bat.model();
            bj["serial"] = bat.serialNumber();
            bj["technology"] = bat.technology();
            bj["energyFull"] = (uint64_t)bat.energyFull();
            bj["energyNow"] = (uint64_t)bat.energyNow();
            bj["capacity"] = bat.capacity(); // 0..1
            bj["charging"] = bat.charging();
            bj["discharging"] = bat.discharging();
            switch ( bat.state() ) {
            case hwinfo::Battery::State::CHARGING:
                bj["state"] = "charging";
                break;
            case hwinfo::Battery::State::DISCHARGING:
                bj["state"] = "discharging";
                break;
            default:
                bj["state"] = "unknown";
                break;
            }
            arr.push_back( bj );
        }
        j["batteries"] = arr;
    } catch ( const std::exception &e ) {
        // 桌面 PC 无电池属正常情况
        LOG_WARN << "读取电池信息失败: " << e.what();
        j["batteries"] = Server::json::array();
    }

    j["success"] = true;
    Server::sendJson( res, j );
}

// ======================================================
// GET /api/sys/cpu  实时 CPU 占用
//   ?sleep=200   测量间隔（毫秒），范围 50~2000
// ======================================================
static void getSysCpu( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;

    int sleepMs = 200;
    if ( req.has_param( "sleep" ) ) {
        try {
            sleepMs = std::stoi( req.get_param_value( "sleep" ) );
        } catch ( ... ) {
        }
    }
    if ( sleepMs < 50 )
        sleepMs = 50;
    if ( sleepMs > 2000 )
        sleepMs = 2000;

    try {
        auto data = hwinfo::monitoring::cpu::fetch( std::chrono::milliseconds( sleepMs ) );
        Server::json j;
        j["success"] = true;
        j["sleepMs"] = sleepMs;
        j["overall"] = data.utilization; // [0, 1]
        Server::json tu = Server::json::array();
        for ( double v : data.thread_utilization )
            tu.push_back( v );
        j["threadUtilization"] = tu;
        Server::json tf = Server::json::array();
        for ( int64_t hz : data.thread_frequency_hz )
            tf.push_back( (uint64_t)hz );
        j["threadFrequencyHz"] = tf;
        Server::sendJson( res, j );
    } catch ( const std::exception &e ) {
        LOG_ERROR << "读取 CPU 实时数据失败: " << e.what();
        Server::sendError( res, std::string( "读取 CPU 数据失败: " ) + e.what(), 500 );
    }
}

// ======================================================
// GET /api/sys/ram  实时内存使用
// ======================================================
static void getSysRam( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;

    try {
        hwinfo::Memory mem;
        uint64_t total = (uint64_t)mem.size();
        uint64_t freeBytes = (uint64_t)mem.free();
        uint64_t availBytes = (uint64_t)mem.available();
        uint64_t used = ( total > freeBytes && total > availBytes )
                            ? ( total - availBytes )
                            : ( total > freeBytes ? total - freeBytes : 0 );

        Server::json j;
        j["success"] = true;
        j["totalBytes"] = total;
        j["totalHuman"] = formatBytes( total );
        j["usedBytes"] = used;
        j["usedHuman"] = formatBytes( used );
        j["freeBytes"] = freeBytes;
        j["freeHuman"] = formatBytes( freeBytes );
        j["availableBytes"] = availBytes;
        j["availableHuman"] = formatBytes( availBytes );
        j["usedRatio"] = total > 0 ? (double)used / (double)total : 0.0;
        Server::sendJson( res, j );
    } catch ( const std::exception &e ) {
        LOG_ERROR << "读取内存数据失败: " << e.what();
        Server::sendError( res, std::string( "读取内存数据失败: " ) + e.what(), 500 );
    }
}

// ======================================================
// GET /api/sys/disks  各挂载点剩余空间
// ======================================================
static void getSysDisks( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;

    try {
        auto disks = hwinfo::getAllDisks();
        Server::json arr = Server::json::array();
        for ( const auto &d : disks ) {
            Server::json di;
            di["model"] = d.model();
            di["vendor"] = d.vendor();
            di["interface"] = diskInterfaceToString( d.disk_interface() );
            di["sizeBytes"] = (uint64_t)d.size();
            di["sizeHuman"] = formatBytes( (uint64_t)d.size() );

            Server::json mps = Server::json::array();
            for ( const auto &mp : d.mount_points() ) {
                Server::json mpj;
                mpj["mountPoint"] = mp;
                try {
                    auto data = hwinfo::monitoring::disk::fetch( mp );
                    uint64_t free = (uint64_t)data.free_bytes;
                    mpj["freeBytes"] = free;
                    mpj["freeHuman"] = formatBytes( free );
                    if ( d.size() > 0 )
                        mpj["usedRatio"] = (double)( d.size() - free ) / (double)d.size();
                    else
                        mpj["usedRatio"] = 0.0;
                } catch ( const std::exception &e ) {
                    mpj["error"] = e.what();
                }
                mps.push_back( mpj );
            }
            di["mounts"] = mps;
            arr.push_back( di );
        }
        Server::json j;
        j["success"] = true;
        j["disks"] = arr;
        Server::sendJson( res, j );
    } catch ( const std::exception &e ) {
        LOG_ERROR << "读取磁盘数据失败: " << e.what();
        Server::sendError( res, std::string( "读取磁盘数据失败: " ) + e.what(), 500 );
    }
}

void registerSysMonitorRoutes( httplib::Server &svr ) {
    svr.Get( "/api/sys/info", getSysInfo );
    svr.Get( "/api/sys/cpu", getSysCpu );
    svr.Get( "/api/sys/ram", getSysRam );
    svr.Get( "/api/sys/disks", getSysDisks );
    LOG_DEBUG << "已注册系统监测工具路由";
}

} // namespace routes::sysmonitor

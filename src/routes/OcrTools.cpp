#include "routes/OcrTools.h"
#include "common/Config.h"
#include "common/Logger.hpp"
#include "core/Server.h"
#include "core/Utils.h"

#include "stb_image.h"
#include "stb_image_write.h"

#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace routes::ocrTools {

struct CropRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool valid() const {
        return w > 0 && h > 0;
    }
};

// 前置声明（TaskManager::runTask 中使用）
static std::string cropImageToTemp( const std::string &srcPath, const CropRect &crop, std::string &errMsg );
static void applyCropOffsetToResult( Server::json &data, int dx, int dy );

// ========================================================================
// RapidOcr C API 类型定义（动态加载，不链接 .lib，不包含 OcrLiteCApi.h）
// ========================================================================
typedef void *OCR_HANDLE;
typedef char OCR_BOOL;

typedef struct {
    int padding;
    int maxSideLen;
    float boxScoreThresh;
    float boxThresh;
    float unClipRatio;
    int doAngle;
    int mostAngle;
} OCR_PARAM;

typedef struct {
    double x;
    double y;
} OCR_POINT;

typedef struct {
    OCR_POINT *boxPoint;
    float boxScore;
    int angleIndex;
    float angleScore;
    double angleTime;
    uint8_t *text;
    float *charScores;
    unsigned long long charScoresLength;
    unsigned long long boxPointLength;
    unsigned long long textLength;
    double crnnTime;
    double blockTime;
} TEXT_BLOCK;

typedef struct {
    double dbNetTime;
    TEXT_BLOCK *textBlocks;
    unsigned long long textBlocksLength;
    double detectTime;
} OCR_RESULT;

typedef OCR_HANDLE ( *OcrInitFn )( const char *, const char *, const char *, const char *, int );
typedef OCR_BOOL ( *OcrInitLoggerFn )( OCR_HANDLE, bool, bool, bool );
typedef OCR_BOOL ( *OcrDetectPathFn )( OCR_HANDLE, const char *, const char *, OCR_PARAM *, OCR_RESULT * );
typedef OCR_BOOL ( *OcrFreeResultFn )( OCR_RESULT * );
typedef void ( *OcrDestroyFn )( OCR_HANDLE );

// ========================================================================
// 模型元信息
// ========================================================================
struct OcrModelInfo {
    std::string id;      // 子目录名，如 ch_mobile_v4
    std::string name;    // 显示名
    std::string dirPath; // 模型目录绝对路径
    std::string detPath;
    std::string clsPath;
    std::string recPath;
    std::string keyPath;
};

// 扫描 RapidOCR 目录下符合条件的模型子目录
static std::vector<OcrModelInfo> scanOcrModels() {
    std::vector<OcrModelInfo> result;
    std::string rapidDir = utils::fs::toNative( Config::getAppPath() + "/RapidOCR" );
    if ( !fs::exists( rapidDir ) ) {
        LOG_WARN << "OCR RapidOCR 目录不存在: " << rapidDir;
        return result;
    }
    std::error_code ec;
    for ( const auto &entry : fs::directory_iterator( rapidDir, ec ) ) {
        if ( ec ) {
            LOG_WARN << "扫描 RapidOCR 目录出错: " << ec.message();
            break;
        }
        if ( !entry.is_directory() )
            continue;
        auto dirPath = entry.path();
        std::string id = dirPath.filename().string();
        // 跳过以点开头的隐藏目录
        if ( id.empty() || id[0] == '.' )
            continue;

        std::string detPath = utils::fs::toNative( dirPath.string() + "/det.onnx" );
        std::string clsPath = utils::fs::toNative( dirPath.string() + "/cls.onnx" );
        std::string recPath = utils::fs::toNative( dirPath.string() + "/rec.onnx" );
        std::string keyPath = utils::fs::toNative( dirPath.string() + "/keys.txt" );

        if ( fs::exists( detPath ) && fs::exists( clsPath ) &&
             fs::exists( recPath ) && fs::exists( keyPath ) ) {
            OcrModelInfo info;
            info.id = id;
            info.name = id;
            info.dirPath = utils::fs::toNative( dirPath.string() );
            info.detPath = detPath;
            info.clsPath = clsPath;
            info.recPath = recPath;
            info.keyPath = keyPath;
            result.push_back( std::move( info ) );
            LOG_INFO << "发现 OCR 模型: " << id;
        }
    }
    // 按目录名稳定排序
    std::sort( result.begin(), result.end(),
               []( const OcrModelInfo &a, const OcrModelInfo &b ) { return a.id < b.id; } );
    return result;
}

static const std::vector<OcrModelInfo> &getModelList() {
    static std::vector<OcrModelInfo> s_list = scanOcrModels();
    return s_list;
}

static const OcrModelInfo *findModelById( const std::string &id ) {
    const auto &list = getModelList();
    // 优先精确匹配
    for ( const auto &m : list )
        if ( m.id == id )
            return &m;
    // 回退：取第一个
    if ( !list.empty() )
        return &list[0];
    return nullptr;
}

// ========================================================================
// OCR 引擎：懒加载 DLL + 模型，串行化调用，支持多模型切换
// ========================================================================
class OcrEngine {
private:
#ifdef _WIN32
    HMODULE m_dll = nullptr;
#endif
    std::mutex m_mutex;
    OcrInitFn m_init = nullptr;
    OcrInitLoggerFn m_initLogger = nullptr;
    OcrDetectPathFn m_detectPath = nullptr;
    OcrFreeResultFn m_freeResult = nullptr;
    OcrDestroyFn m_destroy = nullptr;
    OCR_HANDLE m_handle = nullptr;
    std::string m_currentModelId;
    bool m_libraryLoadFailed = false;
    std::string m_lastError;

public:
    static OcrEngine &instance() {
        static OcrEngine inst;
        return inst;
    }

    // 确保 DLL + 指定模型均已加载；成功返回 true
    bool ensureLoaded( const std::string &modelId, std::string &err ) {
        std::lock_guard<std::mutex> lk( m_mutex );
        // 先确保 DLL 加载
        if ( !ensureDllLoadedInternal( err ) )
            return false;
        // 查找模型信息
        const OcrModelInfo *info = findModelById( modelId );
        if ( !info ) {
            err = "未找到有效的 OCR 模型目录（RapidOCR 下无 det.onnx/cls.onnx/rec.onnx/keys.txt 子目录）";
            LOG_ERROR << err;
            return false;
        }
        // 已加载且模型匹配
        if ( m_handle && m_currentModelId == info->id )
            return true;
        // 若已加载了别的模型，先销毁旧的
        if ( m_handle && m_destroy ) {
            m_destroy( m_handle );
            m_handle = nullptr;
            m_currentModelId.clear();
        }
        return initModelInternal( *info, err );
    }

    // 将非法 UTF-8 字节替换为 U+FFFD（UTF-8: EF BF BD）
    std::string sanitizeUtf8( const std::string &in ) {
        size_t len = in.size();
        while ( len > 0 && in[len - 1] == '\0' )
            len--;
        std::string out;
        out.reserve( len + 8 );
        size_t i = 0;
        const char *data = in.data();
        while ( i < len ) {
            unsigned char c = static_cast<unsigned char>( data[i] );
            if ( c < 0x80 ) {
                out.push_back( data[i] );
                i++;
                continue;
            }
            size_t follow = 0;
            bool invalid = false;
            if ( ( c & 0xE0 ) == 0xC0 ) {
                if ( c < 0xC2 )
                    invalid = true;
                else
                    follow = 1;
            } else if ( ( c & 0xF0 ) == 0xE0 ) {
                follow = 2;
            } else if ( ( c & 0xF8 ) == 0xF0 ) {
                if ( c > 0xF4 )
                    invalid = true;
                else
                    follow = 3;
            } else {
                invalid = true;
            }
            if ( !invalid && i + follow > len ) {
                // 截断序列，跳过开头字节
                invalid = true;
            }
            if ( !invalid && follow >= 1 ) {
                unsigned char b2 = static_cast<unsigned char>( data[i + 1] );
                if ( ( b2 & 0xC0 ) != 0x80 )
                    invalid = true;
                if ( follow == 1 && c == 0xC0 )
                    invalid = true;
                if ( follow == 2 ) {
                    unsigned char b3 = static_cast<unsigned char>( data[i + 2] );
                    if ( ( b3 & 0xC0 ) != 0x80 )
                        invalid = true;
                    if ( c == 0xED && b2 >= 0xA0 )
                        invalid = true; // surrogates
                    if ( c == 0xE0 && b2 < 0xA0 )
                        invalid = true;
                }
                if ( follow == 3 ) {
                    unsigned char b3 = static_cast<unsigned char>( data[i + 2] );
                    unsigned char b4 = static_cast<unsigned char>( data[i + 3] );
                    if ( ( b3 & 0xC0 ) != 0x80 || ( b4 & 0xC0 ) != 0x80 )
                        invalid = true;
                    if ( c == 0xF0 && b2 < 0x90 )
                        invalid = true;
                    if ( c == 0xF4 && b2 > 0x8F )
                        invalid = true;
                }
            }
            if ( invalid ) {
                out.append( "\xEF\xBF\xBD", 3 );
                i += 1;
                continue;
            }
            out.append( data + i, 1 + follow );
            i += 1 + follow;
        }
        return out;
    }

    bool detect( const std::string &imgPath, const OCR_PARAM &param,
                 Server::json &out, std::string &err ) {
        std::lock_guard<std::mutex> lk( m_mutex );
        if ( !m_handle ) {
            err = "OCR引擎未初始化";
            return false;
        }

        OCR_RESULT result;
        memset( &result, 0, sizeof( result ) );

        OCR_PARAM p = param;
        OCR_BOOL ok = m_detectPath( m_handle, imgPath.c_str(), "", &p, &result );
        if ( !ok ) {
            err = "OcrDetectPath 执行失败";
            LOG_ERROR << "OCR识别失败 imgPath=" << imgPath;
            return false;
        }

        Server::json blocks = Server::json::array();
        std::string fullText;
        for ( unsigned long long i = 0; i < result.textBlocksLength; i++ ) {
            const TEXT_BLOCK &tb = result.textBlocks[i];
            std::string text = sanitizeUtf8( std::string( tb.text, tb.text + tb.textLength ) );
            Server::json box = Server::json::array();
            for ( unsigned long long j = 0; j < tb.boxPointLength; j++ )
                box.push_back( { { "x", tb.boxPoint[j].x }, { "y", tb.boxPoint[j].y } } );
            blocks.push_back( {
                { "text", text },
                { "score", tb.boxScore },
                { "box", box },
            } );
            if ( !fullText.empty() )
                fullText += "\n";
            fullText += text;
        }

        out = {
            { "text", fullText },
            { "blocks", blocks },
            { "stats", { { "dbNetTime", result.dbNetTime }, { "detectTime", result.detectTime }, { "blockCount", result.textBlocksLength } } } };

        m_freeResult( &result );
        LOG_DEBUG << "OCR识别成功 文本块数=" << result.textBlocksLength
                  << " 检测耗时=" << result.detectTime << "ms";
        return true;
    }

    ~OcrEngine() {
        if ( m_handle && m_destroy ) {
            m_destroy( m_handle );
            m_handle = nullptr;
        }
#ifdef _WIN32
        if ( m_dll ) {
            FreeLibrary( m_dll );
            m_dll = nullptr;
        }
#endif
    }

private:
    OcrEngine() = default;
    OcrEngine( const OcrEngine & ) = delete;
    OcrEngine &operator=( const OcrEngine & ) = delete;

    // 只负责加载 DLL + 获取函数指针；成功或已加载返回 true
    bool ensureDllLoadedInternal( std::string &err ) {
#ifdef _WIN32
        if ( m_dll )
            return true;
        if ( m_libraryLoadFailed ) {
            err = m_lastError;
            return false;
        }

        // 先在 RapidOCR 子目录查找，再在程序根目录查找
        std::vector<std::string> candidates = {
            utils::fs::toNative( Config::getAppPath() + "/RapidOCR/RapidOcrOnnx.dll" ),
            utils::fs::toNative( Config::getAppPath() + "/RapidOcrOnnx.dll" ),
        };
        for ( const auto &c : candidates ) {
            if ( fs::exists( c ) ) {
                m_dll = LoadLibraryA( c.c_str() );
                if ( m_dll ) {
                    LOG_INFO << "已加载 RapidOCR DLL: " << c;
                    break;
                }
                LOG_WARN << "LoadLibrary 失败 (" << c << "): " << GetLastError();
            }
        }
        if ( !m_dll ) {
            m_lastError = "无法加载 RapidOcrOnnx.dll，请确认该文件存在于 RapidOCR 目录或程序目录";
            m_libraryLoadFailed = true;
            LOG_ERROR << m_lastError;
            err = m_lastError;
            return false;
        }

        m_init = (OcrInitFn)GetProcAddress( m_dll, "OcrInit" );
        m_initLogger = (OcrInitLoggerFn)GetProcAddress( m_dll, "OcrInitLogger" );
        m_detectPath = (OcrDetectPathFn)GetProcAddress( m_dll, "OcrDetectPath" );
        m_freeResult = (OcrFreeResultFn)GetProcAddress( m_dll, "OcrFreeResult" );
        m_destroy = (OcrDestroyFn)GetProcAddress( m_dll, "OcrDestroy" );

        if ( !m_init || !m_initLogger || !m_detectPath || !m_freeResult || !m_destroy ) {
            m_lastError = "DLL函数指针获取失败，RapidOcrOnnx.dll版本可能不兼容";
            LOG_ERROR << m_lastError;
            if ( m_dll ) {
                FreeLibrary( m_dll );
                m_dll = nullptr;
            }
            m_libraryLoadFailed = true;
            err = m_lastError;
            return false;
        }
        return true;
#else
        m_libraryLoadFailed = true;
        return false;
#endif
    }

    // 初始化指定模型（DLL 必须已加载）
    bool initModelInternal( const OcrModelInfo &info, std::string &err ) {
        m_handle = m_init( info.detPath.c_str(), info.clsPath.c_str(),
                           info.recPath.c_str(), info.keyPath.c_str(), 4 );
        if ( !m_handle ) {
            m_lastError = "OcrInit 初始化失败 (模型: " + info.id + ")";
            LOG_ERROR << m_lastError;
            err = m_lastError;
            return false;
        }
        m_initLogger( m_handle, false, false, false );
        m_currentModelId = info.id;
        LOG_INFO << "OCR 引擎初始化成功，模型: " << info.id;
        return true;
    }
};

// ========================================================================
// 异步任务系统：单 worker 串行处理 OCR（detect 内部已串行，没必要并发）
// ========================================================================
enum class TaskStatus : int {
    Queued = 0,
    Running = 1,
    Done = 2,
    Failed = 3,
};

struct OcrTask {
    std::string id;
    std::string imgPath; // 临时文件路径
    std::string modelId; // 使用的模型ID
    OCR_PARAM param{};
    CropRect crop; // 用户选区（原图坐标），无效=全图识别
    TaskStatus status = TaskStatus::Queued;
    std::string error;
    Server::json result; // 成功时填充
    int64_t createdAtMs = 0;
    int64_t startedAtMs = 0;
    int64_t finishedAtMs = 0;
};

class TaskManager {
public:
    static TaskManager &instance() {
        static TaskManager inst;
        return inst;
    }

    TaskManager() {
        m_worker = std::thread( [this] { workerLoop(); } );
    }

    ~TaskManager() {
        {
            std::lock_guard<std::mutex> lk( m_mutex );
            m_shutdown = true;
        }
        m_cv.notify_all();
        if ( m_worker.joinable() )
            m_worker.join();
    }

    std::string submit( std::string imgPath, std::string modelId,
                        const OCR_PARAM &param, const CropRect &crop = {} ) {
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch() )
                            .count();
        std::string id = "ocr_" + std::to_string( nowMs ) + "_" + utils::generateId();

        auto t = std::make_shared<OcrTask>();
        t->id = id;
        t->imgPath = std::move( imgPath );
        t->modelId = std::move( modelId );
        t->param = param;
        t->crop = crop;
        t->createdAtMs = nowMs;

        {
            std::lock_guard<std::mutex> lk( m_mutex );
            m_tasks[id] = t;
            m_queue.push( t );
        }
        m_cv.notify_one();

        // 启动清理线程（首次）
        std::call_once( m_cleanupOnce, [this] {
            m_cleanup = std::thread( [this] { cleanupLoop(); } );
        } );

        return id;
    }

    bool getStatus( const std::string &id, Server::json &out ) {
        std::lock_guard<std::mutex> lk( m_mutex );
        auto it = m_tasks.find( id );
        if ( it == m_tasks.end() )
            return false;
        const auto &t = *it->second;
        const char *status = "queued";
        if ( t.status == TaskStatus::Running )
            status = "running";
        else if ( t.status == TaskStatus::Done )
            status = "done";
        else if ( t.status == TaskStatus::Failed )
            status = "failed";

        out = {
            { "id", t.id },
            { "status", status },
            { "createdAt", t.createdAtMs },
            { "startedAt", t.startedAtMs ? Server::json( t.startedAtMs ) : Server::json() },
            { "finishedAt", t.finishedAtMs ? Server::json( t.finishedAtMs ) : Server::json() },
        };

        if ( t.status == TaskStatus::Failed )
            out["error"] = t.error;
        if ( t.status == TaskStatus::Done )
            out["data"] = t.result;

        return true;
    }

    // 读取完结果后，可立刻释放结果占用和临时文件（但仍在 m_tasks 里保留一小段让重复 status 查询返回 404/410 或失败态）
    void dismiss( const std::string &id ) {
        std::lock_guard<std::mutex> lk( m_mutex );
        auto it = m_tasks.find( id );
        if ( it == m_tasks.end() )
            return;
        cleanupTask( it->second );
        m_tasks.erase( it );
    }

private:
    void workerLoop() {
        for ( ;; ) {
            std::shared_ptr<OcrTask> task;
            {
                std::unique_lock<std::mutex> lk( m_mutex );
                m_cv.wait( lk, [this] { return m_shutdown || !m_queue.empty(); } );
                if ( m_shutdown && m_queue.empty() )
                    return;
                task = m_queue.front();
                m_queue.pop();
            }

            runTask( task );
        }
    }

    void runTask( std::shared_ptr<OcrTask> t ) {
        int64_t startMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch() )
                              .count();

        {
            std::lock_guard<std::mutex> lk( m_mutex );
            t->status = TaskStatus::Running;
            t->startedAtMs = startMs;
        }

        // 首次请求才初始化引擎（避免懒加载阻塞 worker 启动）
        std::string err;
        if ( !OcrEngine::instance().ensureLoaded( t->modelId, err ) ) {
            finishTask( t, TaskStatus::Failed, {}, "OCR引擎不可用: " + err );
            return;
        }

        // 若提供选区：先裁剪到临时 PNG
        std::string usePath = t->imgPath;
        std::string croppedPath;
        CropRect appliedCrop; // 实际生效的 crop（夹紧后）
        if ( t->crop.valid() && !t->imgPath.empty() ) {
            std::string cropErr;
            croppedPath = cropImageToTemp( t->imgPath, t->crop, cropErr );
            if ( !croppedPath.empty() ) {
                usePath = croppedPath;
                appliedCrop = t->crop;
                // 记录实际裁剪窗口（此处未读取真实夹紧值，采用用户传入的；若有差异不影响偏移计算，因为 offset=x,y 是用户原图坐标）
                LOG_DEBUG << "OCR 使用裁剪区 " << appliedCrop.w << "x" << appliedCrop.h
                          << " @(" << appliedCrop.x << "," << appliedCrop.y << ")";
            } else {
                LOG_WARN << "OCR 裁剪失败，回退为全图识别: " << cropErr;
            }
        }

        Server::json out;
        bool ok = OcrEngine::instance().detect( usePath, t->param, out, err );

        // 清理裁剪临时文件（不管成功与否都释放）
        if ( !croppedPath.empty() && fs::exists( croppedPath ) ) {
            std::error_code ec;
            fs::remove( croppedPath, ec );
        }

        // OCR 成功后：坐标回写偏移 + 塞入 crop 信息
        if ( ok && appliedCrop.valid() ) {
            applyCropOffsetToResult( out, appliedCrop.x, appliedCrop.y );
            // 裁剪区信息附带给前端显示
            out["crop"] = {
                { "x", appliedCrop.x },
                { "y", appliedCrop.y },
                { "w", appliedCrop.w },
                { "h", appliedCrop.h },
            };
        }

        finishTask( t, ok ? TaskStatus::Done : TaskStatus::Failed, out, err );
    }

    void finishTask( std::shared_ptr<OcrTask> t, TaskStatus s, Server::json data, std::string errMsg ) {
        // OCR 完成即释放临时文件（结果已在 data 里），仅保留元信息一段时间
        std::error_code ec;
        if ( !t->imgPath.empty() && fs::exists( t->imgPath ) )
            fs::remove( t->imgPath, ec );
        t->imgPath.clear();

        int64_t finishMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch() )
                               .count();
        {
            std::lock_guard<std::mutex> lk( m_mutex );
            t->status = s;
            t->finishedAtMs = finishMs;
            if ( s == TaskStatus::Done )
                t->result = std::move( data );
            else
                t->error = std::move( errMsg );
        }
    }

    // 任务对象和内存资源清理
    void cleanupTask( std::shared_ptr<OcrTask> &t ) {
        if ( !t->imgPath.empty() ) {
            std::error_code ec;
            fs::remove( t->imgPath, ec );
            t->imgPath.clear();
        }
        t->result = Server::json(); // 释放大 JSON
    }

    void cleanupLoop() {
        // 每 30s 扫描一次：完成/失败的结果保留 5 分钟，超时则丢弃元信息
        using namespace std::chrono_literals;
        for ( ;; ) {
            {
                std::unique_lock<std::mutex> lk( m_mutex );
                if ( m_shutdown )
                    return;
            }
            std::this_thread::sleep_for( 30s );
            int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch() )
                                .count();

            std::lock_guard<std::mutex> lk( m_mutex );
            if ( m_shutdown )
                return;

            std::vector<std::string> toErase;
            for ( auto &kv : m_tasks ) {
                auto &t = kv.second;
                if ( t->status == TaskStatus::Done || t->status == TaskStatus::Failed ) {
                    if ( t->finishedAtMs && ( nowMs - t->finishedAtMs ) > 5 * 60 * 1000 ) {
                        cleanupTask( t );
                        toErase.push_back( kv.first );
                    }
                } else if ( !t->finishedAtMs && ( nowMs - t->createdAtMs ) > 20 * 60 * 1000 ) {
                    // 排队/运行超过 20 分钟的异常任务也清理
                    cleanupTask( t );
                    toErase.push_back( kv.first );
                }
            }
            for ( const auto &id : toErase )
                m_tasks.erase( id );
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::map<std::string, std::shared_ptr<OcrTask>> m_tasks;
    std::queue<std::shared_ptr<OcrTask>> m_queue;
    std::thread m_worker;
    std::thread m_cleanup;
    std::once_flag m_cleanupOnce;
    bool m_shutdown = false;
};

// ========================================================================
// 公共辅助：从 req.form（httplib 的 MultipartFormData，含 fields/files）抽参数/文件/写临时文件
// ========================================================================
static OCR_PARAM parseParamsFromForm( const httplib::MultipartFormData &form ) {
    OCR_PARAM param;
    param.padding = 10;
    param.maxSideLen = 1024;
    param.boxScoreThresh = 0.5f;
    param.boxThresh = 0.3f;
    param.unClipRatio = 1.6f;
    param.doAngle = 1;
    param.mostAngle = 0;

    auto parseInt = [&]( const char *field, int minV, int maxV, int defV ) -> int {
        if ( !form.has_field( field ) )
            return defV;
        try {
            int v = std::stoi( form.get_field( field ) );
            return v < minV ? minV : ( v > maxV ? maxV : v );
        } catch ( ... ) {
            return defV;
        }
    };
    auto parseFloat = [&]( const char *field, float minV, float maxV, float defV ) -> float {
        if ( !form.has_field( field ) )
            return defV;
        try {
            float v = std::stof( form.get_field( field ) );
            return v < minV ? minV : ( v > maxV ? maxV : v );
        } catch ( ... ) {
            return defV;
        }
    };

    param.maxSideLen = parseInt( "maxSideLen", 32, 4096, 1024 );
    param.boxScoreThresh = parseFloat( "boxScoreThresh", 0.1f, 0.9f, 0.5f );
    param.unClipRatio = parseFloat( "unClipRatio", 0.5f, 4.0f, 1.6f );
    if ( form.has_field( "doAngle" ) ) {
        std::string v = form.get_field( "doAngle" );
        param.doAngle = ( v == "1" || v == "true" ) ? 1 : 0;
    }
    return param;
}

static CropRect parseCropFromForm( const httplib::MultipartFormData &form ) {
    CropRect c;
    auto parseInt = [&]( const char *field, int defV ) -> int {
        if ( !form.has_field( field ) )
            return defV;
        try {
            return std::stoi( form.get_field( field ) );
        } catch ( ... ) {
            return defV;
        }
    };
    c.x = parseInt( "cropX", 0 );
    c.y = parseInt( "cropY", 0 );
    c.w = parseInt( "cropW", 0 );
    c.h = parseInt( "cropH", 0 );
    if ( c.x < 0 )
        c.x = 0;
    if ( c.y < 0 )
        c.y = 0;
    if ( c.w < 0 )
        c.w = 0;
    if ( c.h < 0 )
        c.h = 0;
    return c;
}

// 使用 stb_image 加载原图，按 crop 裁剪后写临时 PNG 文件；失败返回空串
static std::string cropImageToTemp( const std::string &srcPath, const CropRect &crop, std::string &errMsg ) {
    if ( !crop.valid() ) {
        errMsg = "invalid crop rect";
        return {};
    }
    int w = 0, h = 0, channels = 0;
    stbi_uc *pixels = stbi_load( srcPath.c_str(), &w, &h, &channels, 0 );
    if ( !pixels ) {
        errMsg = std::string( "stbi_load 失败: " ) + ( stbi_failure_reason() ? stbi_failure_reason() : "unknown" );
        return {};
    }
    // 夹紧裁剪区
    int cx = std::max( 0, std::min( w - 1, crop.x ) );
    int cy = std::max( 0, std::min( h - 1, crop.y ) );
    int cw = std::max( 1, std::min( w - cx, crop.w ) );
    int ch = std::max( 1, std::min( h - cy, crop.h ) );
    if ( cw != crop.w || ch != crop.h ) {
        LOG_WARN << "OCR 裁剪区被夹紧 原=" << crop.w << "x" << crop.h << " 实际=" << cw << "x" << ch;
    }

    // 复制矩形区域（通道数保持不变）
    std::vector<unsigned char> buf( size_t( cw ) * ch * channels );
    for ( int y = 0; y < ch; y++ ) {
        const stbi_uc *srcRow = pixels + ( ( cy + y ) * w + cx ) * channels;
        unsigned char *dstRow = buf.data() + size_t( y ) * cw * channels;
        std::memcpy( dstRow, srcRow, size_t( cw ) * channels );
    }
    stbi_image_free( pixels );
    pixels = nullptr;

    // 写 PNG 临时文件
    std::string tempDir = utils::fs::toNative( Config::getTempPath() + "/ocr" );
    std::error_code ec;
    fs::create_directories( tempDir, ec );
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch() )
                     .count();
    std::string tempPath = utils::fs::toNative(
        tempDir + "/crop_" + std::to_string( nowMs ) + "_" + utils::generateId() + ".png" );

    // 写为 PNG (lossless)
    int strideBytes = cw * channels;
    int writeOk = stbi_write_png( tempPath.c_str(), cw, ch, channels, buf.data(), strideBytes );
    if ( !writeOk ) {
        errMsg = "stbi_write_png 失败";
        return {};
    }
    return tempPath;
}

// 把 OCR 结果 JSON 中每个 block 的 box 点加上 (dx, dy) 偏移
static void applyCropOffsetToResult( Server::json &data, int dx, int dy ) {
    if ( !dx && !dy )
        return;
    auto itBlocks = data.find( "blocks" );
    if ( itBlocks == data.end() || !itBlocks->is_array() )
        return;
    for ( auto &b : *itBlocks ) {
        auto itBox = b.find( "box" );
        if ( itBox == b.end() || !itBox->is_array() )
            continue;
        for ( auto &pt : *itBox ) {
            auto itX = pt.find( "x" ), itY = pt.find( "y" );
            if ( itX != pt.end() && itX->is_number() )
                *itX = itX->get<double>() + dx;
            if ( itY != pt.end() && itY->is_number() )
                *itY = itY->get<double>() + dy;
        }
    }
}

static bool findFileFromForm( const httplib::MultipartFormData &form,
                              httplib::FormData *outFile ) {
    if ( !form.has_file( "file" ) )
        return false;
    *outFile = form.get_file( "file" );
    return true;
}

static std::string writeTempFile( const httplib::FormData &file ) {
    std::string tempDir = utils::fs::toNative( Config::getTempPath() + "/ocr" );
    std::error_code ec;
    fs::create_directories( tempDir, ec );

    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch() )
                     .count();

    std::string ext = ".png";
    size_t dotPos = file.filename.rfind( '.' );
    if ( dotPos != std::string::npos ) {
        std::string origExt = file.filename.substr( dotPos );
        std::string lower = utils::toLower( origExt );
        if ( lower == ".png" || lower == ".jpg" || lower == ".jpeg" ||
             lower == ".bmp" || lower == ".webp" || lower == ".tif" || lower == ".tiff" )
            ext = origExt;
    }

    std::string tempPath = utils::fs::toNative(
        tempDir + "/" + std::to_string( nowMs ) + "_" + utils::generateId() + ext );

    std::ofstream ofs( tempPath, std::ios::binary );
    if ( !ofs )
        return "";
    ofs.write( file.content.data(), file.content.size() );
    return tempPath;
}

// 从 form 中读取模型 ID 字段（空串时由 findModelById 回退到第一个）
static std::string parseModelIdFromForm( const httplib::MultipartFormData &form ) {
    if ( !form.has_field( "model" ) )
        return {};
    return form.get_field( "model" );
}

// ========================================================================
// 路由
// ========================================================================

// 模型列表接口：GET /api/tools/image/ocr/models
static void imageOcrModels( const httplib::Request &req, httplib::Response &res ) {
    (void)req;
    const auto &list = getModelList();
    Server::json arr = Server::json::array();
    for ( const auto &m : list ) {
        arr.push_back( {
            { "id", m.id },
            { "name", m.name },
        } );
    }
    Server::sendJson( res, { { "success", true }, { "models", arr } } );
}

// 同步接口（保留兼容，httplib 超时本身很大；若被反向代理/浏览器过早断开，前端会尝试异步模式）
void imageOcr( const httplib::Request &req, httplib::Response &res ) {
    if ( !req.is_multipart_form_data() )
        return Server::sendError( res, "需要multipart上传", 400 );

    httplib::FormData file;
    if ( !findFileFromForm( req.form, &file ) )
        return Server::sendError( res, "缺少file字段", 400 );

    LOG_DEBUG << "OCR识别(sync) filename=" << file.filename << " size=" << file.content.size();
    if ( file.content.size() > 20 * 1024 * 1024 )
        return Server::sendError( res, "图片超过20MB限制", 400 );

    std::string modelId = parseModelIdFromForm( req.form );
    OCR_PARAM param = parseParamsFromForm( req.form );
    CropRect crop = parseCropFromForm( req.form );

    std::string err;
    if ( !OcrEngine::instance().ensureLoaded( modelId, err ) )
        return Server::sendError( res, "OCR引擎不可用: " + err, 503 );

    std::string tempPath = writeTempFile( file );
    if ( tempPath.empty() )
        return Server::sendError( res, "临时文件创建失败", 500 );

    // 裁剪（若指定了选区）
    std::string usePath = tempPath;
    std::string croppedPath;
    CropRect appliedCrop;
    if ( crop.valid() ) {
        std::string cropErr;
        croppedPath = cropImageToTemp( tempPath, crop, cropErr );
        if ( !croppedPath.empty() ) {
            usePath = croppedPath;
            appliedCrop = crop;
        } else {
            LOG_WARN << "OCR 裁剪失败（同步），回退为全图识别: " << cropErr;
        }
    }

    Server::json result;
    bool ok = OcrEngine::instance().detect( usePath, param, result, err );

    // 清理临时文件
    {
        std::error_code ec;
        if ( !croppedPath.empty() && fs::exists( croppedPath ) )
            fs::remove( croppedPath, ec );
        if ( fs::exists( tempPath ) )
            fs::remove( tempPath, ec );
    }

    if ( !ok )
        return Server::sendError( res, "OCR识别失败: " + err, 500 );

    if ( appliedCrop.valid() ) {
        applyCropOffsetToResult( result, appliedCrop.x, appliedCrop.y );
        result["crop"] = { { "x", appliedCrop.x }, { "y", appliedCrop.y }, { "w", appliedCrop.w }, { "h", appliedCrop.h } };
    }

    Server::sendJson( res, { { "success", true }, { "data", result } } );
}

// 异步：提交任务 → 返回 taskId
static void imageOcrSubmit( const httplib::Request &req, httplib::Response &res ) {
    if ( !req.is_multipart_form_data() )
        return Server::sendError( res, "需要multipart上传", 400 );

    httplib::FormData file;
    if ( !findFileFromForm( req.form, &file ) )
        return Server::sendError( res, "缺少file字段", 400 );

    LOG_DEBUG << "OCR识别(submit) filename=" << file.filename << " size=" << file.content.size();
    if ( file.content.size() > 20 * 1024 * 1024 )
        return Server::sendError( res, "图片超过20MB限制", 400 );

    std::string modelId = parseModelIdFromForm( req.form );
    OCR_PARAM param = parseParamsFromForm( req.form );
    CropRect crop = parseCropFromForm( req.form );

    std::string err;
    // 提交时只做基础校验，真实加载延迟到 worker 中；此处只是尽早提示 Dll 缺失
    if ( !OcrEngine::instance().ensureLoaded( modelId, err ) ) {
        // 只给出警告性提示，但仍提交（worker 里会再次尝试）
        LOG_WARN << "OCR引擎尚未就绪，将在后台线程重试: " << err;
    }

    std::string tempPath = writeTempFile( file );
    if ( tempPath.empty() )
        return Server::sendError( res, "临时文件创建失败", 500 );

    std::string id = TaskManager::instance().submit( std::move( tempPath ), std::move( modelId ), param, crop );
    Server::sendJson( res, { { "success", true }, { "taskId", id } } );
}

// 异步：查询状态
static void imageOcrStatus( const httplib::Request &req, httplib::Response &res ) {
    std::string id = req.path_params.at( "id" );
    Server::json status;
    if ( !TaskManager::instance().getStatus( id, status ) )
        return Server::sendError( res, "任务不存在或已过期", 404 );
    Server::sendJson( res, { { "success", true }, { "data", status } } );
}

// 异步：手动 dismiss（释放结果和记录）
static void imageOcrDismiss( const httplib::Request &req, httplib::Response &res ) {
    std::string id = req.path_params.at( "id" );
    TaskManager::instance().dismiss( id );
    Server::sendJson( res, { { "success", true } } );
}

void registerOcrRoutes( httplib::Server &svr ) {
    svr.Get( "/api/tools/image/ocr/models", imageOcrModels );
    svr.Post( "/api/tools/image/ocr", imageOcr );
    svr.Post( "/api/tools/image/ocr/submit", imageOcrSubmit );
    svr.Get( "/api/tools/image/ocr/status/:id", imageOcrStatus );
    svr.Delete( "/api/tools/image/ocr/status/:id", imageOcrDismiss );
    LOG_DEBUG << "已注册 5 个OCR工具路由";
}

} // namespace routes::ocrTools

#include "routes/OcrTools.h"
#include "common/Config.h"
#include "common/Logger.hpp"
#include "core/Server.h"
#include "core/Utils.h"

#include "ocr_dynamic_loader.h"
#include "stb_image.h"
#include "stb_image_write.h"

#include <atomic>
#include <chrono>
#include <cmath>
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

#define OnnxOCR_Dir Config::getAppPath() + "/OnnxOCR"

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

static std::string cropImageToTemp( const std::string &srcPath, const CropRect &crop, std::string &errMsg );
static void applyCropOffsetToResult( Server::json &data, int dx, int dy );

// ========================================================================
// OnnxOCR 模型元信息
// ========================================================================
struct OcrModelInfo {
    std::string id;
    std::string name;
    std::string dirPath;
    std::string detPath;
    std::string clsPath;
    std::string recPath;
    std::string keyPath;
};

struct LayoutModelInfo {
    std::string id;
    std::string name;
    std::string modelPath;
    int modelType = 0;
};

struct TableModelInfo {
    std::string id;
    std::string name;
    std::string modelPath;
    int modelType = 0;
    std::string clsModelPath;
    std::string unetModelPath;
};

struct ClsModelInfo {
    std::string id;
    std::string name;
    std::string modelPath;
    int clsModelType = 0;
};

static std::vector<OcrModelInfo> scanOcrModels() {
    std::vector<OcrModelInfo> result;
    std::string onnxDir = utils::fs::toNative( OnnxOCR_Dir );
    std::string textDir = utils::fs::toNative( onnxDir + "/text" );
    if ( !fs::exists( textDir ) ) {
        LOG_WARN << "OCR OnnxOCR/text 目录不存在: " << textDir;
        return result;
    }
    std::error_code ec;
    for ( const auto &entry : fs::directory_iterator( textDir, ec ) ) {
        if ( ec )
            break;
        if ( !entry.is_directory() )
            continue;
        auto dirPath = entry.path();
        std::string id = dirPath.filename().string();
        if ( id.empty() || id[0] == '.' )
            continue;

        std::string detPath = utils::fs::toNative( dirPath.string() + "/det.onnx" );
        std::string recPath = utils::fs::toNative( dirPath.string() + "/rec.onnx" );
        std::string keyPath = utils::fs::toNative( dirPath.string() + "/keys.txt" );

        if ( fs::exists( detPath ) && fs::exists( recPath ) && fs::exists( keyPath ) ) {
            OcrModelInfo info;
            info.id = id;
            info.name = id;
            info.dirPath = utils::fs::toNative( dirPath.string() );
            info.detPath = detPath;
            info.clsPath = utils::fs::toNative( dirPath.string() + "/cls.onnx" );
            info.recPath = recPath;
            info.keyPath = keyPath;
            result.push_back( std::move( info ) );
            LOG_INFO << "发现 OCR 模型:" << id;
        }
    }
    std::sort( result.begin(), result.end(),
               []( const OcrModelInfo &a, const OcrModelInfo &b ) { return a.id < b.id; } );
    return result;
}

static std::vector<LayoutModelInfo> scanLayoutModels() {
    std::vector<LayoutModelInfo> result;
    std::string onnxDir = utils::fs::toNative( OnnxOCR_Dir );
    std::string layoutDir = utils::fs::toNative( onnxDir + "/layout" );
    if ( !fs::exists( layoutDir ) )
        return result;
    std::error_code ec;
    for ( const auto &entry : fs::directory_iterator( layoutDir, ec ) ) {
        if ( ec )
            break;
        if ( !entry.is_directory() )
            continue;
        auto dirPath = entry.path();
        std::string id = dirPath.filename().string();
        if ( id.empty() || id[0] == '.' )
            continue;
        std::error_code ec2;
        for ( const auto &f : fs::directory_iterator( dirPath, ec2 ) ) {
            if ( ec2 )
                break;
            if ( !f.is_regular_file() )
                continue;
            std::string ext = f.path().extension().string();
            if ( ext != ".onnx" )
                continue;
            std::string modelFile = f.path().filename().string();
            std::string modelPath = utils::fs::toNative( f.path().string() );
            LayoutModelInfo info;
            info.id = id + "/" + modelFile;
            info.name = id + " / " + modelFile;
            info.modelPath = modelPath;
            if ( id.find( "pp_doclayout" ) != std::string::npos )
                info.modelType = PP_DOCLAYOUT_V2;
            else if ( id.find( "pp_layout" ) != std::string::npos || id.find( "cdla" ) != std::string::npos )
                info.modelType = PP_LAYOUT_CDLA;
            else if ( id.find( "doclayout_yolo" ) != std::string::npos || id.find( "doclayout-yolo" ) != std::string::npos )
                info.modelType = DOCLAYOUT_YOLO_DOCSTRUCTBENCH;
            else if ( id.find( "yolov8" ) != std::string::npos || id.find( "yolo" ) != std::string::npos )
                info.modelType = YOLO_LAYOUT_PAPER;
            else
                info.modelType = PP_DOCLAYOUT_V2;
            LOG_INFO << "发现版面模型:" << info.id;
            result.push_back( std::move( info ) );
        }
    }
    std::sort( result.begin(), result.end(),
               []( const LayoutModelInfo &a, const LayoutModelInfo &b ) { return a.id < b.id; } );
    return result;
}

static std::vector<TableModelInfo> scanTableModels() {
    std::vector<TableModelInfo> result;
    std::string onnxDir = utils::fs::toNative( OnnxOCR_Dir );
    std::string tableDir = utils::fs::toNative( onnxDir + "/table" );
    if ( !fs::exists( tableDir ) )
        return result;
    std::error_code ec;
    for ( const auto &entry : fs::directory_iterator( tableDir, ec ) ) {
        if ( ec )
            break;
        if ( !entry.is_directory() )
            continue;
        auto dirPath = entry.path();
        std::string id = dirPath.filename().string();
        if ( id.empty() || id[0] == '.' )
            continue;
        if ( id == "cls" )
            continue;
        std::error_code ec2;
        for ( const auto &f : fs::directory_iterator( dirPath, ec2 ) ) {
            if ( ec2 )
                break;
            if ( !f.is_regular_file() )
                continue;
            std::string ext = f.path().extension().string();
            if ( ext != ".onnx" )
                continue;
            std::string modelFile = f.path().filename().string();
            std::string lowerFile = utils::toLower( modelFile );
            if ( lowerFile.find( "cls" ) != std::string::npos )
                continue;
            std::string modelPath = utils::fs::toNative( f.path().string() );
            TableModelInfo info;
            info.id = id + "/" + modelFile;
            info.name = id + " / " + modelFile;
            info.modelPath = modelPath;
            if ( lowerFile.find( "unet" ) != std::string::npos )
                info.modelType = TABLE_MODEL_UNET;
            else if ( lowerFile.find( "slanet" ) != std::string::npos )
                info.modelType = TABLE_MODEL_SLANET_PLUS;
            else
                info.modelType = TABLE_MODEL_SLANET_PLUS;
            LOG_INFO << "发现表格模型:" << info.id;
            result.push_back( std::move( info ) );
        }
    }
    std::sort( result.begin(), result.end(),
               []( const TableModelInfo &a, const TableModelInfo &b ) { return a.id < b.id; } );
    return result;
}

static std::vector<std::string> scanTableClsModels() {
    std::vector<std::string> result;
    std::string onnxDir = utils::fs::toNative( OnnxOCR_Dir );
    std::string clsDir = utils::fs::toNative( onnxDir + "/table/cls" );
    if ( !fs::exists( clsDir ) )
        return result;
    std::error_code ec;
    for ( const auto &f : fs::directory_iterator( clsDir, ec ) ) {
        if ( ec )
            break;
        if ( !f.is_regular_file() )
            continue;
        std::string ext = f.path().extension().string();
        if ( ext != ".onnx" )
            continue;
        result.push_back( utils::fs::toNative( f.path().string() ) );
        LOG_INFO << "发现表格类型检测模型:" << f.path().filename().string();
    }
    std::sort( result.begin(), result.end() );
    return result;
}

static std::vector<ClsModelInfo> scanClsModels() {
    std::vector<ClsModelInfo> result;
    std::string onnxDir = utils::fs::toNative( OnnxOCR_Dir );
    std::string clsDir = utils::fs::toNative( onnxDir + "/text_cls" );
    if ( !fs::exists( clsDir ) )
        return result;
    std::error_code ec;
    for ( const auto &entry : fs::directory_iterator( clsDir, ec ) ) {
        if ( ec )
            break;
        if ( !entry.is_directory() )
            continue;
        auto dirPath = entry.path();
        std::string id = dirPath.filename().string();
        if ( id.empty() || id[0] == '.' )
            continue;
        std::error_code ec2;
        for ( const auto &f : fs::directory_iterator( dirPath, ec2 ) ) {
            if ( ec2 )
                break;
            if ( !f.is_regular_file() )
                continue;
            std::string ext = f.path().extension().string();
            if ( ext != ".onnx" )
                continue;
            std::string modelFile = f.path().filename().string();
            std::string modelPath = utils::fs::toNative( f.path().string() );
            ClsModelInfo info;
            info.id = id + "/" + modelFile;
            info.name = id + " / " + modelFile;
            info.modelPath = modelPath;
            info.clsModelType = ( id.find( "orientation" ) != std::string::npos )
                                    ? CLS_MODEL_ORIENTATION
                                    : CLS_MODEL_ANGLE;
            LOG_INFO << "发现分类模型:" << info.id;
            result.push_back( std::move( info ) );
        }
    }
    std::sort( result.begin(), result.end(),
               []( const ClsModelInfo &a, const ClsModelInfo &b ) { return a.id < b.id; } );
    return result;
}

static const std::vector<OcrModelInfo> &getOcrModelList() {
    static std::vector<OcrModelInfo> s_list = scanOcrModels();
    return s_list;
}

static const std::vector<LayoutModelInfo> &getLayoutModelList() {
    static std::vector<LayoutModelInfo> s_list = scanLayoutModels();
    return s_list;
}

static const std::vector<TableModelInfo> &getTableModelList() {
    static std::vector<TableModelInfo> s_list = scanTableModels();
    return s_list;
}

static const std::vector<ClsModelInfo> &getClsModelList() {
    static std::vector<ClsModelInfo> s_list = scanClsModels();
    return s_list;
}

static const std::vector<std::string> &getTableClsModelList() {
    static std::vector<std::string> s_list = scanTableClsModels();
    return s_list;
}

static const OcrModelInfo *findOcrModelById( const std::string &id ) {
    const auto &list = getOcrModelList();
    for ( const auto &m : list )
        if ( m.id == id )
            return &m;
    if ( !list.empty() )
        return &list[0];
    return nullptr;
}

static const LayoutModelInfo *findLayoutModelById( const std::string &id ) {
    const auto &list = getLayoutModelList();
    for ( const auto &m : list )
        if ( m.id == id )
            return &m;
    if ( !list.empty() )
        return &list[0];
    return nullptr;
}

static const TableModelInfo *findTableModelById( const std::string &id ) {
    const auto &list = getTableModelList();
    for ( const auto &m : list )
        if ( m.id == id )
            return &m;
    if ( !list.empty() )
        return &list[0];
    return nullptr;
}

static const ClsModelInfo *findClsModelById( const std::string &id ) {
    const auto &list = getClsModelList();
    for ( const auto &m : list )
        if ( m.id == id )
            return &m;
    return nullptr;
}

// ========================================================================
// OCR 引擎：基于 OnnxOCR OcrDynamicLoader，懒加载 DLL + 模型，串行化调用
// ========================================================================
struct OcrRunParam {
    int maxSideLen = 1024;
    float boxScoreThresh = 0.5f;
    float unClipRatio = 1.6f;
};

class OcrEngine {
private:
    std::mutex m_mutex;
    std::unique_ptr<ocr::OcrDynamicLoader> m_loader;
    OcrHandle *m_handle = nullptr;
    DocLayoutYOLOHandle *m_layoutHandle = nullptr;
    TableHandle *m_tableHandle = nullptr;
    std::string m_currentModelId;
    std::string m_currentClsModelId;
    std::string m_currentLayoutId;
    std::string m_currentTableId;
    bool m_libraryLoadFailed = false;
    std::string m_lastError;

public:
    static OcrEngine &instance() {
        static OcrEngine inst;
        return inst;
    }

    bool ensureLoaded( const std::string &modelId,
                       const std::string &clsModelId,
                       const std::string &layoutId,
                       const std::string &tableId,
                       const std::string &tableAlgo,
                       const std::string &tableClsModelId,
                       std::string &err ) {
        std::lock_guard<std::mutex> lk( m_mutex );
        if ( !ensureDllLoadedInternal( err ) )
            return false;
        const OcrModelInfo *info = findOcrModelById( modelId );
        if ( !info ) {
            err = "未找到有效的 OCR 模型: " + modelId;
            LOG_ERROR << err;
            return false;
        }
        if ( m_handle && m_currentModelId == info->id ) {
            bool clsChanged = !clsModelId.empty() && m_currentClsModelId != clsModelId;
            bool layoutChanged = !layoutId.empty() && m_currentLayoutId != layoutId;
            bool tableChanged = !tableId.empty() && m_currentTableId != tableId;
            if ( !clsChanged && !layoutChanged && !tableChanged )
                return true;
            if ( clsChanged ) {
                m_loader->destroy( m_handle );
                m_handle = nullptr;
                m_currentModelId.clear();
                m_currentClsModelId.clear();
                if ( !initModelInternal( *info, clsModelId, err ) )
                    return false;
                m_currentModelId = info->id;
                m_currentClsModelId = clsModelId;
            }
            if ( layoutChanged && m_layoutHandle ) {
                m_loader->doclayout_yolo_destroy( m_layoutHandle );
                m_layoutHandle = nullptr;
                m_currentLayoutId.clear();
            }
            if ( tableChanged && m_tableHandle ) {
                m_loader->table_destroy( m_tableHandle );
                m_tableHandle = nullptr;
                m_currentTableId.clear();
            }
            if ( layoutChanged ) {
                if ( !layoutId.empty() )
                    initLayoutInternal( layoutId, err );
                else
                    initLayoutDefault( err );
            }
            if ( tableChanged ) {
                if ( !tableId.empty() )
                    initTableInternal( tableId, tableAlgo, tableClsModelId, err );
                else
                    initTableDefault( err );
            }
            return true;
        }
        if ( m_handle ) {
            m_loader->destroy( m_handle );
            m_handle = nullptr;
        }
        if ( m_layoutHandle ) {
            m_loader->doclayout_yolo_destroy( m_layoutHandle );
            m_layoutHandle = nullptr;
        }
        if ( m_tableHandle ) {
            m_loader->table_destroy( m_tableHandle );
            m_tableHandle = nullptr;
        }
        m_currentModelId.clear();
        m_currentLayoutId.clear();
        m_currentTableId.clear();
        m_currentClsModelId.clear();
        if ( !initModelInternal( *info, clsModelId, err ) )
            return false;
        m_currentClsModelId = clsModelId;
        if ( !layoutId.empty() )
            initLayoutInternal( layoutId, err );
        else
            initLayoutDefault( err );
        if ( !tableId.empty() )
            initTableInternal( tableId, tableAlgo, tableClsModelId, err );
        else
            initTableDefault( err );
        return true;
    }

    bool detect( const std::string &imgPath, const std::string &mode,
                 int cropX1, int cropY1, int cropX2, int cropY2,
                 Server::json &out, std::string &err ) {
        std::lock_guard<std::mutex> lk( m_mutex );
        if ( !m_handle || !m_loader ) {
            err = "OCR引擎未初始化";
            return false;
        }

        if ( mode == "table" && m_tableHandle ) {
            return detectTableOnly( imgPath, cropX1, cropY1, cropX2, cropY2, out, err );
        }

        if ( m_layoutHandle ) {
            return detectWithLayout( imgPath, cropX1, cropY1, cropX2, cropY2, out, err );
        }

        OcrResultList results;
        memset( &results, 0, sizeof( results ) );

        int rc = m_loader->run_file( m_handle, imgPath.c_str(),
                                     cropX1, cropY1, cropX2, cropY2, &results );
        if ( rc != 0 ) {
            const char *libErr = m_loader->last_error();
            err = libErr ? std::string( "OCR识别失败: " ) + libErr : "OCR识别失败";
            LOG_ERROR << "OCR识别失败 imgPath=" << imgPath << " err=" << err;
            return false;
        }

        Server::json blocks = Server::json::array();
        std::string fullText;
        for ( int i = 0; i < results.count; i++ ) {
            const OcrResult &r = results.items[i];
            Server::json box = Server::json::array();
            for ( int j = 0; j < 4; j++ )
                box.push_back( { { "x", r.box[j].x }, { "y", r.box[j].y } } );
            std::string text = r.text ? r.text : "";
            blocks.push_back( {
                { "type", std::string( "text" ) },
                { "text", text },
                { "score", r.score },
                { "box", box },
            } );
            if ( !fullText.empty() )
                fullText += "\n";
            fullText += text;
        }

        out = {
            { "text", fullText },
            { "blocks", blocks },
            { "stats", { { "blockCount", results.count } } } };

        m_loader->free_results( &results );
        LOG_DEBUG << "OCR识别成功 文本块数=" << results.count;
        return true;
    }

    ~OcrEngine() {
        if ( m_tableHandle && m_loader ) {
            m_loader->table_destroy( m_tableHandle );
            m_tableHandle = nullptr;
        }
        if ( m_layoutHandle && m_loader ) {
            m_loader->doclayout_yolo_destroy( m_layoutHandle );
            m_layoutHandle = nullptr;
        }
        if ( m_handle && m_loader ) {
            m_loader->destroy( m_handle );
            m_handle = nullptr;
        }
    }

private:
    OcrEngine() = default;
    OcrEngine( const OcrEngine & ) = delete;
    OcrEngine &operator=( const OcrEngine & ) = delete;

    bool ensureDllLoadedInternal( std::string &err ) {
        if ( m_loader )
            return true;
        if ( m_libraryLoadFailed ) {
            err = m_lastError;
            return false;
        }

        std::string dllPath = utils::fs::toNative( OnnxOCR_Dir + "/OnnxOCR.dll" );
        if ( !fs::exists( dllPath ) ) {
            m_lastError = "无法加载 OnnxOCR.dll，请确认该文件存在于 " + OnnxOCR_Dir + " 目录";
            m_libraryLoadFailed = true;
            LOG_ERROR << m_lastError << " path=" << dllPath;
            err = m_lastError;
            return false;
        }

        try {
            m_loader = std::make_unique<ocr::OcrDynamicLoader>( dllPath );
            LOG_INFO << "已加载 OnnxOCR DLL: " << dllPath;
        } catch ( const ocr::DynamicLoaderError &e ) {
            m_lastError = std::string( "加载 OnnxOCR.dll 失败: " ) + e.what();
            m_libraryLoadFailed = true;
            LOG_ERROR << m_lastError;
            err = m_lastError;
            return false;
        }
        return true;
    }

    bool initModelInternal( const OcrModelInfo &info, const std::string &clsModelId, std::string &err ) {
        OcrConfig cfg = {};
        cfg.det_model_path = info.detPath.c_str();
        cfg.rec_model_path = info.recPath.c_str();
        cfg.rec_char_dict_path = info.keyPath.c_str();

        const ClsModelInfo *clsInfo = nullptr;
        if ( !clsModelId.empty() )
            clsInfo = findClsModelById( clsModelId );

        if ( clsInfo && !clsInfo->modelPath.empty() && fs::exists( clsInfo->modelPath ) ) {
            cfg.cls_model_path = clsInfo->modelPath.c_str();
            cfg.cls_model_type = clsInfo->clsModelType;
        } else {
            cfg.cls_model_path = nullptr;
            cfg.cls_model_type = CLS_MODEL_ANGLE;
        }
        cfg.use_gpu = 0;
        cfg.gpu_id = 0;

        cfg.det_limit_side_len = 960.f;
        cfg.det_limit_type = "max";
        cfg.det_db_thresh = 0.3f;
        cfg.det_db_box_thresh = 0.6f;
        cfg.det_db_unclip_ratio = 1.5f;
        cfg.use_dilation = 0;
        cfg.det_db_score_mode = "fast";
        cfg.det_box_type = "quad";

        cfg.rec_batch_num = 6;
        cfg.rec_image_c = 3;
        cfg.rec_image_h = 48;
        cfg.rec_image_w = 320;
        cfg.use_space_char = 1;
        cfg.drop_score = 0.5f;

        cfg.cls_batch_num = 6;
        cfg.cls_image_c = 3;
        cfg.cls_image_h = 48;
        cfg.cls_image_w = 192;
        cfg.cls_thresh = 0.9f;

        m_handle = m_loader->create( &cfg );
        if ( !m_handle ) {
            const char *libErr = m_loader->last_error();
            m_lastError = libErr ? std::string( "OnnxOCR 初始化失败: " ) + libErr
                                 : "OnnxOCR 初始化失败 (模型: " + info.id + ")";
            LOG_ERROR << m_lastError;
            err = m_lastError;
            return false;
        }
        m_currentModelId = info.id;
        LOG_INFO << "OCR 引擎初始化成功，模型: " << info.id;
        return true;
    }

    void initLayoutInternal( const std::string &layoutId, std::string &err ) {
        if ( m_layoutHandle )
            return;
        const LayoutModelInfo *info = findLayoutModelById( layoutId );
        if ( !info ) {
            LOG_WARN << "未找到版面模型: " << layoutId;
            return;
        }
        DocLayoutYOLOConfig cfg = {};
        cfg.model_path = info->modelPath.c_str();
        cfg.model_type = info->modelType;
        cfg.use_gpu = 0;
        cfg.gpu_id = 0;
        cfg.conf_thresh = 0.5f;
        cfg.iou_thresh = 0.5f;

        m_layoutHandle = m_loader->doclayout_yolo_create( &cfg );
        if ( !m_layoutHandle ) {
            const char *libErr = m_loader->last_error();
            LOG_WARN << "版面分析引擎初始化失败（将回退为纯OCR）: "
                     << ( libErr ? libErr : "unknown" );
        } else {
            m_currentLayoutId = layoutId;
            LOG_INFO << "版面分析引擎初始化成功，模型: " << layoutId;
        }
    }

    void initLayoutDefault( std::string &err ) {
        const auto &list = getLayoutModelList();
        if ( list.empty() ) {
            LOG_WARN << "无可用版面模型，跳过版面分析";
            return;
        }
        initLayoutInternal( list[0].id, err );
    }

    void initTableInternal( const std::string &tableId,
                            const std::string &tableAlgo,
                            const std::string &tableClsModelId,
                            std::string &err ) {
        if ( m_tableHandle )
            return;
        const TableModelInfo *info = findTableModelById( tableId );
        if ( !info ) {
            LOG_WARN << "未找到表格模型: " << tableId;
            return;
        }
        TableConfig cfg = {};
        cfg.model_path = info->modelPath.c_str();
        cfg.model_type = info->modelType;

        if ( tableAlgo == "combined" ) {
            if ( !tableClsModelId.empty() && fs::exists( tableClsModelId ) )
                cfg.cls_model_path = tableClsModelId.c_str();
            else {
                const auto &clsList = getTableClsModelList();
                if ( !clsList.empty() )
                    cfg.cls_model_path = clsList[0].c_str();
                else
                    cfg.cls_model_path = nullptr;
            }
            const auto &allModels = getTableModelList();
            for ( const auto &m : allModels ) {
                if ( m.modelType == TABLE_MODEL_UNET && !m.modelPath.empty() ) {
                    cfg.unet_model_path = m.modelPath.c_str();
                    break;
                }
            }
        } else {
            cfg.cls_model_path = nullptr;
            cfg.unet_model_path = nullptr;
        }

        cfg.use_gpu = 0;
        cfg.gpu_id = 0;

        m_tableHandle = m_loader->table_create( &cfg );
        if ( !m_tableHandle ) {
            const char *libErr = m_loader->last_error();
            LOG_WARN << "表格识别引擎初始化失败（表格将降级为OCR）: "
                     << ( libErr ? libErr : "unknown" );
        } else {
            m_currentTableId = tableId;
            LOG_INFO << "表格识别引擎初始化成功，模型: " << tableId
                     << " 算法: " << tableAlgo;
        }
    }

    void initTableDefault( std::string &err ) {
        const auto &list = getTableModelList();
        if ( list.empty() ) {
            LOG_WARN << "无可用表格模型，跳过表格识别";
            return;
        }
        initTableInternal( list[0].id, {}, {}, err );
    }

    static std::string layoutClassToType( const std::string &className ) {
        if ( className == "title" || className == "Title" )
            return "title";
        if ( className == "text" || className == "Text" || className == "paragraph" )
            return "text";
        if ( className == "figure" || className == "Figure" || className == "image" )
            return "figure";
        if ( className == "figure_caption" || className == "Figure Caption" )
            return "figure_caption";
        if ( className == "table" || className == "Table" )
            return "table";
        if ( className == "table_caption" || className == "Table Caption" )
            return "table_caption";
        if ( className == "header" || className == "Header" )
            return "header";
        if ( className == "footer" || className == "Footer" )
            return "footer";
        if ( className == "reference" || className == "Reference" )
            return "reference";
        if ( className == "equation" || className == "Equation" )
            return "equation";
        if ( className == "list" || className == "List" )
            return "list";
        if ( className == "abandon" )
            return "abandon";
        return "text";
    }

    bool detectWithLayout( const std::string &imgPath,
                           int cropX1, int cropY1, int cropX2, int cropY2,
                           Server::json &out, std::string &err ) {
        DocLayoutItemList layoutResults;
        memset( &layoutResults, 0, sizeof( layoutResults ) );

        int layoutRc = m_loader->doclayout_yolo_run_file( m_layoutHandle, imgPath.c_str(), &layoutResults );
        if ( layoutRc != 0 ) {
            const char *libErr = m_loader->last_error();
            LOG_WARN << "版面分析失败，回退为纯OCR: " << ( libErr ? libErr : "unknown" );
            m_loader->doc_layout_free_results( &layoutResults );
            OcrResultList ocrResults;
            memset( &ocrResults, 0, sizeof( ocrResults ) );
            int rc = m_loader->run_file( m_handle, imgPath.c_str(),
                                         cropX1, cropY1, cropX2, cropY2, &ocrResults );
            if ( rc != 0 ) {
                libErr = m_loader->last_error();
                err = libErr ? std::string( "OCR识别失败: " ) + libErr : "OCR识别失败";
                return false;
            }
            Server::json blocks = Server::json::array();
            std::string fullText;
            for ( int i = 0; i < ocrResults.count; i++ ) {
                const OcrResult &r = ocrResults.items[i];
                Server::json box = Server::json::array();
                for ( int j = 0; j < 4; j++ )
                    box.push_back( { { "x", r.box[j].x }, { "y", r.box[j].y } } );
                std::string text = r.text ? r.text : "";
                blocks.push_back( { { "type", std::string( "text" ) }, { "text", text }, { "score", r.score }, { "box", box } } );
                if ( !fullText.empty() )
                    fullText += "\n";
                fullText += text;
            }
            out = { { "text", fullText }, { "blocks", blocks }, { "stats", { { "blockCount", ocrResults.count } } } };
            m_loader->free_results( &ocrResults );
            return true;
        }

        LOG_DEBUG << "版面分析完成 区域数=" << layoutResults.count;

        std::vector<std::pair<int, DocLayoutItem>> sortedItems;
        for ( int i = 0; i < layoutResults.count; i++ )
            sortedItems.emplace_back( layoutResults.items[i].order, layoutResults.items[i] );
        std::sort( sortedItems.begin(), sortedItems.end(),
                   []( const auto &a, const auto &b ) { return a.first < b.first; } );

        Server::json blocks = Server::json::array();
        std::string fullText;
        int textBlockCount = 0;

        for ( auto &[order, item] : sortedItems ) {
            std::string className = item.class_name ? item.class_name : "";
            std::string type = layoutClassToType( className );

            if ( type == "abandon" )
                continue;

            Server::json boxJson = Server::json::array();
            float x1 = item.box[0], y1 = item.box[1], x2 = item.box[2], y2 = item.box[3];
            boxJson.push_back( { { "x", x1 }, { "y", y1 } } );
            boxJson.push_back( { { "x", x2 }, { "y", y1 } } );
            boxJson.push_back( { { "x", x2 }, { "y", y2 } } );
            boxJson.push_back( { { "x", x1 }, { "y", y2 } } );

            if ( type == "figure" ) {
                blocks.push_back( {
                    { "type", std::string( "figure" ) },
                    { "text", std::string( "" ) },
                    { "score", item.score },
                    { "box", boxJson },
                    { "class", className },
                } );
                continue;
            }

            int cx1 = static_cast<int>( std::floor( x1 ) );
            int cy1 = static_cast<int>( std::floor( y1 ) );
            int cx2 = static_cast<int>( std::ceil( x2 ) );
            int cy2 = static_cast<int>( std::ceil( y2 ) );

            if ( type == "table" && m_tableHandle ) {
                Server::json tableJson = recognizeTable( imgPath, cx1, cy1, cx2, cy2 );
                if ( !tableJson.is_null() ) {
                    blocks.push_back( {
                        { "type", std::string( "table" ) },
                        { "text", std::string( "" ) },
                        { "score", item.score },
                        { "box", boxJson },
                        { "class", className },
                        { "table", tableJson },
                    } );
                    textBlockCount++;
                    continue;
                }
            }

            OcrResultList ocrResults;
            memset( &ocrResults, 0, sizeof( ocrResults ) );
            int rc = m_loader->run_file( m_handle, imgPath.c_str(),
                                         cx1, cy1, cx2, cy2, &ocrResults );
            if ( rc != 0 ) {
                m_loader->free_results( &ocrResults );
                continue;
            }

            std::string regionText;
            for ( int i = 0; i < ocrResults.count; i++ ) {
                const OcrResult &r = ocrResults.items[i];
                std::string t = r.text ? r.text : "";
                if ( !regionText.empty() )
                    regionText += "\n";
                regionText += t;
            }
            m_loader->free_results( &ocrResults );

            if ( regionText.empty() )
                continue;

            blocks.push_back( {
                { "type", type },
                { "text", regionText },
                { "score", item.score },
                { "box", boxJson },
                { "class", className },
            } );
            textBlockCount++;
            if ( !fullText.empty() )
                fullText += "\n";
            fullText += regionText;
        }

        m_loader->doc_layout_free_results( &layoutResults );

        out = {
            { "text", fullText },
            { "blocks", blocks },
            { "stats", { { "blockCount", textBlockCount }, { "layoutCount", layoutResults.count } } } };

        LOG_DEBUG << "文档识别完成 版面区域=" << layoutResults.count << " 文本块=" << textBlockCount;
        return true;
    }

    bool detectTableOnly( const std::string &imgPath,
                          int cropX1, int cropY1, int cropX2, int cropY2,
                          Server::json &out, std::string &err ) {
        Server::json tableJson = recognizeTable( imgPath, cropX1, cropY1, cropX2, cropY2 );
        if ( tableJson.is_null() ) {
            err = "表格识别失败";
            return false;
        }

        Server::json blocks = Server::json::array();

        Server::json cellBoxes = tableJson.value( "cellBoxes", Server::json::array() );
        if ( cellBoxes.is_array() && !cellBoxes.empty() ) {
            float overallMinX = 1e9f, overallMinY = 1e9f;
            float overallMaxX = 0, overallMaxY = 0;
            for ( const auto &cb : cellBoxes ) {
                if ( !cb.is_array() || cb.size() < 4 )
                    continue;
                float bx1 = cb[0].get<float>(), by1 = cb[1].get<float>();
                float bx2 = cb[2].get<float>(), by2 = cb[3].get<float>();
                if ( bx1 < overallMinX )
                    overallMinX = bx1;
                if ( by1 < overallMinY )
                    overallMinY = by1;
                if ( bx2 > overallMaxX )
                    overallMaxX = bx2;
                if ( by2 > overallMaxY )
                    overallMaxY = by2;
            }
            if ( overallMinX < 1e9f ) {
                Server::json tableBox = Server::json::array();
                tableBox.push_back( { { "x", overallMinX }, { "y", overallMinY } } );
                tableBox.push_back( { { "x", overallMaxX }, { "y", overallMinY } } );
                tableBox.push_back( { { "x", overallMaxX }, { "y", overallMaxY } } );
                tableBox.push_back( { { "x", overallMinX }, { "y", overallMaxY } } );
                blocks.push_back( {
                    { "type", std::string( "table" ) },
                    { "text", std::string( "" ) },
                    { "score", 1.0 },
                    { "box", tableBox },
                    { "table", tableJson },
                } );
            } else {
                blocks.push_back( {
                    { "type", std::string( "table" ) },
                    { "text", std::string( "" ) },
                    { "score", 1.0 },
                    { "box", Server::json::array() },
                    { "table", tableJson },
                } );
            }
        } else {
            Server::json boxJson = Server::json::array();
            if ( cropX1 > 0 || cropY1 > 0 || cropX2 > 0 || cropY2 > 0 ) {
                boxJson.push_back( { { "x", cropX1 }, { "y", cropY1 } } );
                boxJson.push_back( { { "x", cropX2 }, { "y", cropY1 } } );
                boxJson.push_back( { { "x", cropX2 }, { "y", cropY2 } } );
                boxJson.push_back( { { "x", cropX1 }, { "y", cropY2 } } );
            }
            blocks.push_back( {
                { "type", std::string( "table" ) },
                { "text", std::string( "" ) },
                { "score", 1.0 },
                { "box", boxJson },
                { "table", tableJson },
            } );
        }

        out = {
            { "text", std::string( "" ) },
            { "blocks", blocks },
            { "stats", { { "blockCount", 1 }, { "tableRows", tableJson.value( "rowCount", 0 ) }, { "tableCols", tableJson.value( "colCount", 0 ) } } } };

        LOG_DEBUG << "表格识别完成 行=" << tableJson.value( "rowCount", 0 )
                  << " 列=" << tableJson.value( "colCount", 0 );
        return true;
    }

    Server::json recognizeTable( const std::string &imgPath,
                                 int cropX1, int cropY1, int cropX2, int cropY2 ) {
        if ( !m_tableHandle )
            return {};

        TableResult tableResult;
        memset( &tableResult, 0, sizeof( tableResult ) );

        int rc = m_loader->table_run_file( m_tableHandle, imgPath.c_str(), &tableResult );
        if ( rc != 0 || tableResult.cell_count <= 0 || !tableResult.logic_points ) {
            m_loader->table_free_result( &tableResult );
            LOG_DEBUG << "表格结构识别失败或无单元格";
            return {};
        }

        int maxRow = 0, maxCol = 0;
        for ( int i = 0; i < tableResult.cell_count; ++i ) {
            const RectBox &lp = tableResult.logic_points[i];
            if ( lp.y1 + 1 > maxRow )
                maxRow = lp.y1 + 1;
            if ( lp.y2 + 1 > maxCol )
                maxCol = lp.y2 + 1;
        }
        if ( maxRow <= 0 || maxCol <= 0 ) {
            m_loader->table_free_result( &tableResult );
            return {};
        }

        std::vector<std::vector<std::string>> grid(
            maxRow, std::vector<std::string>( maxCol ) );
        Server::json cellBoxes = Server::json::array();

        for ( int i = 0; i < tableResult.cell_count; ++i ) {
            const TableCell &cell = tableResult.cells[i];
            const RectBox &lp = tableResult.logic_points[i];

            float minX = cell.bbox[0].x, minY = cell.bbox[0].y;
            float maxX = cell.bbox[0].x, maxY = cell.bbox[0].y;
            for ( int k = 1; k < 4; ++k ) {
                float cx = cell.bbox[k].x;
                float cy = cell.bbox[k].y;
                if ( cx < minX )
                    minX = cx;
                if ( cx > maxX )
                    maxX = cx;
                if ( cy < minY )
                    minY = cy;
                if ( cy > maxY )
                    maxY = cy;
            }

            cellBoxes.push_back( { minX, minY, maxX, maxY } );

            int x1 = static_cast<int>( minX );
            int y1 = static_cast<int>( minY );
            int x2 = static_cast<int>( maxX );
            int y2 = static_cast<int>( maxY );

            OcrResultList ocrResults;
            memset( &ocrResults, 0, sizeof( ocrResults ) );
            int ocrRc = m_loader->run_file( m_handle, imgPath.c_str(),
                                            x1, y1, x2, y2, &ocrResults );
            if ( ocrRc == 0 ) {
                std::string cellText;
                for ( int j = 0; j < ocrResults.count; ++j ) {
                    const char *t = ocrResults.items[j].text;
                    if ( t && t[0] != '\0' ) {
                        if ( !cellText.empty() )
                            cellText += " ";
                        cellText += t;
                    }
                }
                for ( int r = lp.x1; r <= lp.y1 && r < maxRow; ++r )
                    for ( int c = lp.x2; c <= lp.y2 && c < maxCol; ++c )
                        grid[r][c] = cellText;
            }
            m_loader->free_results( &ocrResults );
        }

        m_loader->table_free_result( &tableResult );

        Server::json rows = Server::json::array();
        for ( int r = 0; r < maxRow; ++r ) {
            Server::json row = Server::json::array();
            for ( int c = 0; c < maxCol; ++c )
                row.push_back( grid[r][c] );
            rows.push_back( row );
        }

        LOG_DEBUG << "表格识别成功 行=" << maxRow << " 列=" << maxCol << " 单元格=" << tableResult.cell_count;
        return { { "rows", rows }, { "rowCount", maxRow }, { "colCount", maxCol }, { "cellBoxes", cellBoxes } };
    }
};

// ========================================================================
// 异步任务系统
// ========================================================================
enum class TaskStatus : int {
    Queued = 0,
    Running = 1,
    Done = 2,
    Failed = 3,
};

struct OcrTask {
    std::string id;
    std::string imgPath;
    std::string modelId;
    std::string clsModelId;
    std::string layoutId;
    std::string tableId;
    std::string tableAlgo;
    std::string tableClsModelId;
    std::string mode;
    OcrRunParam param{};
    CropRect crop;
    TaskStatus status = TaskStatus::Queued;
    std::string error;
    Server::json result;
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

    void shutdown() {
        {
            std::lock_guard<std::mutex> lk( m_mutex );
            if ( m_shutdown )
                return;
            m_shutdown = true;
        }
        m_cv.notify_all();
        if ( m_worker.joinable() )
            m_worker.join();
        if ( m_cleanup.joinable() )
            m_cleanup.join();
    }

    ~TaskManager() {
        shutdown();
    }

    std::string submit( std::string imgPath, std::string modelId, std::string clsModelId,
                        std::string layoutId, std::string tableId,
                        std::string tableAlgo, std::string tableClsModelId,
                        std::string mode,
                        const OcrRunParam &param, const CropRect &crop = {} ) {
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch() )
                            .count();
        std::string id = "ocr_" + std::to_string( nowMs ) + "_" + utils::generateId();

        auto t = std::make_shared<OcrTask>();
        t->id = id;
        t->imgPath = std::move( imgPath );
        t->modelId = std::move( modelId );
        t->clsModelId = std::move( clsModelId );
        t->layoutId = std::move( layoutId );
        t->tableId = std::move( tableId );
        t->tableAlgo = std::move( tableAlgo );
        t->tableClsModelId = std::move( tableClsModelId );
        t->mode = std::move( mode );
        t->param = param;
        t->crop = crop;
        t->createdAtMs = nowMs;

        {
            std::lock_guard<std::mutex> lk( m_mutex );
            m_tasks[id] = t;
            m_queue.push( t );
        }
        m_cv.notify_all();

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
            try {
                runTask( task );
            } catch ( const std::exception &e ) {
                LOG_ERROR << "OCR任务异常: " << e.what();
                finishTask( task, TaskStatus::Failed, {}, std::string( "任务异常: " ) + e.what() );
            } catch ( ... ) {
                LOG_ERROR << "OCR任务未知异常";
                finishTask( task, TaskStatus::Failed, {}, "任务未知异常" );
            }
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

        std::string err;
        if ( !OcrEngine::instance().ensureLoaded( t->modelId, t->clsModelId, t->layoutId, t->tableId, t->tableAlgo, t->tableClsModelId, err ) ) {
            finishTask( t, TaskStatus::Failed, {}, "OCR引擎不可用: " + err );
            return;
        }

        std::string usePath = t->imgPath;
        std::string croppedPath;
        CropRect appliedCrop;
        if ( t->crop.valid() && !t->imgPath.empty() ) {
            std::string cropErr;
            croppedPath = cropImageToTemp( t->imgPath, t->crop, cropErr );
            if ( !croppedPath.empty() ) {
                usePath = croppedPath;
                appliedCrop = t->crop;
                LOG_DEBUG << "OCR 使用裁剪区 " << appliedCrop.w << "x" << appliedCrop.h
                          << " @(" << appliedCrop.x << "," << appliedCrop.y << ")";
            } else {
                LOG_WARN << "OCR 裁剪失败，回退为全图识别: " << cropErr;
            }
        }

        int cx1 = -1, cy1 = -1, cx2 = -1, cy2 = -1;

        Server::json out;
        bool ok = OcrEngine::instance().detect( usePath, t->mode, cx1, cy1, cx2, cy2, out, err );

        if ( !croppedPath.empty() && fs::exists( croppedPath ) ) {
            std::error_code ec;
            fs::remove( croppedPath, ec );
        }

        if ( ok && appliedCrop.valid() ) {
            applyCropOffsetToResult( out, appliedCrop.x, appliedCrop.y );
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

    void cleanupTask( std::shared_ptr<OcrTask> &t ) {
        if ( !t->imgPath.empty() ) {
            std::error_code ec;
            fs::remove( t->imgPath, ec );
            t->imgPath.clear();
        }
        t->result = Server::json();
    }

    void cleanupLoop() {
        using namespace std::chrono_literals;
        for ( ;; ) {
            {
                std::unique_lock<std::mutex> lk( m_mutex );
                if ( m_cv.wait_for( lk, 30s, [this] { return m_shutdown; } ) )
                    return;
            }
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
// 辅助函数
// ========================================================================
static OcrRunParam parseParamsFromForm( const httplib::MultipartFormData &form ) {
    OcrRunParam param;
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
    int cx = std::max( 0, std::min( w - 1, crop.x ) );
    int cy = std::max( 0, std::min( h - 1, crop.y ) );
    int cw = std::max( 1, std::min( w - cx, crop.w ) );
    int ch = std::max( 1, std::min( h - cy, crop.h ) );

    std::vector<unsigned char> buf( size_t( cw ) * ch * channels );
    for ( int y = 0; y < ch; y++ ) {
        const stbi_uc *srcRow = pixels + ( ( cy + y ) * w + cx ) * channels;
        unsigned char *dstRow = buf.data() + size_t( y ) * cw * channels;
        std::memcpy( dstRow, srcRow, size_t( cw ) * channels );
    }
    stbi_image_free( pixels );

    std::string tempDir = utils::fs::toNative( Config::getTempPath() + "/ocr" );
    std::error_code ec;
    fs::create_directories( tempDir, ec );
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch() )
                     .count();
    std::string tempPath = utils::fs::toNative(
        tempDir + "/crop_" + std::to_string( nowMs ) + "_" + utils::generateId() + ".png" );

    int strideBytes = cw * channels;
    int writeOk = stbi_write_png( tempPath.c_str(), cw, ch, channels, buf.data(), strideBytes );
    if ( !writeOk ) {
        errMsg = "stbi_write_png 失败";
        return {};
    }
    return tempPath;
}

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

static std::string parseModelIdFromForm( const httplib::MultipartFormData &form ) {
    if ( !form.has_field( "model" ) )
        return {};
    return form.get_field( "model" );
}

static std::string parseLayoutIdFromForm( const httplib::MultipartFormData &form ) {
    if ( !form.has_field( "layout" ) )
        return {};
    return form.get_field( "layout" );
}

static std::string parseClsModelIdFromForm( const httplib::MultipartFormData &form ) {
    if ( !form.has_field( "clsModel" ) )
        return {};
    return form.get_field( "clsModel" );
}

static std::string parseTableIdFromForm( const httplib::MultipartFormData &form ) {
    if ( !form.has_field( "tableModel" ) )
        return {};
    return form.get_field( "tableModel" );
}

static std::string parseTableAlgoFromForm( const httplib::MultipartFormData &form ) {
    if ( !form.has_field( "tableAlgo" ) )
        return {};
    return form.get_field( "tableAlgo" );
}

static std::string parseTableClsModelIdFromForm( const httplib::MultipartFormData &form ) {
    if ( !form.has_field( "tableClsModel" ) )
        return {};
    return form.get_field( "tableClsModel" );
}

static std::string parseModeFromForm( const httplib::MultipartFormData &form ) {
    if ( !form.has_field( "mode" ) )
        return "doc";
    std::string m = form.get_field( "mode" );
    if ( m != "doc" && m != "table" )
        return "doc";
    return m;
}

// ========================================================================
// 路由
// ========================================================================
static void imageOcrModels( const httplib::Request &req, httplib::Response &res ) {
    (void)req;

    Server::json textArr = Server::json::array();
    for ( const auto &m : getOcrModelList() )
        textArr.push_back( { { "id", m.id }, { "name", m.name } } );

    Server::json clsModes = Server::json::array();
    {
        std::map<std::string, Server::json> clsGrouped;
        for ( const auto &m : getClsModelList() ) {
            std::string mode = ( m.clsModelType == CLS_MODEL_ORIENTATION ) ? "orientation" : "angle";
            if ( clsGrouped.find( mode ) == clsGrouped.end() )
                clsGrouped[mode] = Server::json::array();
            clsGrouped[mode].push_back( { { "id", m.id }, { "name", m.name }, { "path", m.modelPath } } );
        }
        for ( auto &[k, v] : clsGrouped )
            clsModes.push_back( { { "mode", k }, { "models", v } } );
    }

    Server::json layoutAlgos = Server::json::array();
    {
        std::map<std::string, Server::json> layoutGrouped;
        for ( const auto &m : getLayoutModelList() ) {
            auto slashPos = m.id.find( '/' );
            std::string algo = ( slashPos != std::string::npos ) ? m.id.substr( 0, slashPos ) : m.id;
            if ( layoutGrouped.find( algo ) == layoutGrouped.end() )
                layoutGrouped[algo] = Server::json::array();
            layoutGrouped[algo].push_back( { { "id", m.id }, { "name", m.name }, { "path", m.modelPath }, { "modelType", m.modelType } } );
        }
        for ( auto &[k, v] : layoutGrouped )
            layoutAlgos.push_back( { { "algo", k }, { "models", v } } );
    }

    Server::json tableAlgos = Server::json::array();
    {
        Server::json wirelessModels = Server::json::array();
        Server::json wiredModels = Server::json::array();
        for ( const auto &m : getTableModelList() ) {
            Server::json item = { { "id", m.id }, { "name", m.name }, { "path", m.modelPath }, { "modelType", m.modelType } };
            if ( m.modelType == TABLE_MODEL_UNET )
                wiredModels.push_back( item );
            else
                wirelessModels.push_back( item );
        }
        if ( !wirelessModels.empty() )
            tableAlgos.push_back( { { "algo", "wireless" }, { "label", "无线" }, { "models", wirelessModels } } );
        if ( !wiredModels.empty() )
            tableAlgos.push_back( { { "algo", "wired" }, { "label", "有线" }, { "models", wiredModels } } );
        if ( !wirelessModels.empty() && !wiredModels.empty() )
            tableAlgos.push_back( { { "algo", "combined" }, { "label", "组合" }, { "models", wirelessModels } } );
    }

    Server::json tableClsArr = Server::json::array();
    for ( const auto &p : getTableClsModelList() ) {
        auto fn = fs::path( p ).filename().string();
        tableClsArr.push_back( { { "id", p }, { "name", fn } } );
    }

    Server::sendJson( res, {
                               { "success", true },
                               { "textModels", textArr },
                               { "clsModes", clsModes },
                               { "layoutAlgos", layoutAlgos },
                               { "tableAlgos", tableAlgos },
                               { "tableClsModels", tableClsArr },
                           } );
}

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
    std::string clsModelId = parseClsModelIdFromForm( req.form );
    std::string layoutId = parseLayoutIdFromForm( req.form );
    std::string tableId = parseTableIdFromForm( req.form );
    std::string tableAlgo = parseTableAlgoFromForm( req.form );
    std::string tableClsModelId = parseTableClsModelIdFromForm( req.form );
    std::string mode = parseModeFromForm( req.form );
    OcrRunParam param = parseParamsFromForm( req.form );
    CropRect crop = parseCropFromForm( req.form );

    std::string err;
    if ( !OcrEngine::instance().ensureLoaded( modelId, clsModelId, layoutId, tableId, tableAlgo, tableClsModelId, err ) ) {
        LOG_WARN << "OCR引擎尚未就绪，将在后台线程重试: " << err;
    }

    std::string tempPath = writeTempFile( file );
    if ( tempPath.empty() )
        return Server::sendError( res, "临时文件创建失败", 500 );

    std::string id = TaskManager::instance().submit(
        std::move( tempPath ), std::move( modelId ), std::move( clsModelId ),
        std::move( layoutId ), std::move( tableId ),
        std::move( tableAlgo ), std::move( tableClsModelId ),
        std::move( mode ), param, crop );
    Server::sendJson( res, { { "success", true }, { "taskId", id } } );
}

static void imageOcrStatus( const httplib::Request &req, httplib::Response &res ) {
    std::string id = req.path_params.at( "id" );
    Server::json status;
    if ( !TaskManager::instance().getStatus( id, status ) )
        return Server::sendError( res, "任务不存在或已过期", 404 );
    Server::sendJson( res, { { "success", true }, { "data", status } } );
}

static void imageOcrDismiss( const httplib::Request &req, httplib::Response &res ) {
    std::string id = req.path_params.at( "id" );
    TaskManager::instance().dismiss( id );
    Server::sendJson( res, { { "success", true } } );
}

void shutdown() {
    TaskManager::instance().shutdown();
}

void registerOcrRoutes( httplib::Server &svr ) {
    svr.Get( "/api/tools/image/ocr/models", imageOcrModels );
    svr.Post( "/api/tools/image/ocr/submit", imageOcrSubmit );
    svr.Get( "/api/tools/image/ocr/status/:id", imageOcrStatus );
    svr.Delete( "/api/tools/image/ocr/status/:id", imageOcrDismiss );
    LOG_DEBUG << "已注册 5 个OCR工具路由";
}

} // namespace routes::ocrTools
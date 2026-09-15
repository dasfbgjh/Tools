#include "ocr_dynamic_loader.h"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#include <cstring>

namespace ocr {

// ---- 平台辅助函数 ----

#ifdef _WIN32
static void* platform_load(const std::string& path) {
    // Windows: LoadLibraryA / LoadLibraryW
    std::string p = path.empty() ? std::string("OnnxOCR.dll") : path;
    // 使用LoadLibraryA
    HMODULE h = LoadLibraryA(p.c_str());
    return reinterpret_cast<void*>(h);
}

static void platform_unload(void* handle) {
    if (handle) FreeLibrary(reinterpret_cast<HMODULE>(handle));
}

static void* platform_resolve(void* handle, const char* name) {
    if (!handle) return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
}

static std::string platform_error() {
    DWORD err = GetLastError();
    if (err == 0) return "no error";
    LPSTR buf = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buf), 0, nullptr);
    std::string msg(buf ? buf : "unknown error");
    if (buf) LocalFree(buf);
    return msg;
}
#else
static void* platform_load(const std::string& path) {
    std::string p;
    if (path.empty()) {
#if defined(__APPLE__)
        p = "libOnnxOCR.dylib";
#else
        p = "libOnnxOCR.so";
#endif
    } else {
        p = path;
    }
    return dlopen(p.c_str(), RTLD_NOW | RTLD_GLOBAL);
}

static void platform_unload(void* handle) {
    if (handle) dlclose(handle);
}

static void* platform_resolve(void* handle, const char* name) {
    if (!handle) return nullptr;
    return dlsym(handle, name);
}

static std::string platform_error() {
    const char* err = dlerror();
    return err ? err : "no error";
}
#endif

// ---- OcrDynamicLoader ----

OcrDynamicLoader::OcrDynamicLoader(const std::string& lib_path) {
    lib_handle_ = platform_load(lib_path);
    if (!lib_handle_) {
        throw DynamicLoaderError(
            "failed to load OnnxOCR library '" +
            (lib_path.empty() ? std::string("default") : lib_path) +
            "': " + platform_error());
    }

    try {
        resolve_symbols();
    } catch (...) {
        platform_unload(lib_handle_);
        lib_handle_ = nullptr;
        throw;
    }
}

OcrDynamicLoader::~OcrDynamicLoader() {
    if (lib_handle_) {
        platform_unload(lib_handle_);
        lib_handle_ = nullptr;
    }
}

void OcrDynamicLoader::resolve_symbols() {
    struct SymEntry { const char* name; void** target; };
    SymEntry entries[] = {
        // OCR API
        { "ocr_create",        reinterpret_cast<void**>(&ocr_create_fn_) },
        { "ocr_destroy",       reinterpret_cast<void**>(&ocr_destroy_fn_) },
        { "ocr_run",           reinterpret_cast<void**>(&ocr_run_fn_) },
        { "ocr_run_file",      reinterpret_cast<void**>(&ocr_run_file_fn_) },
        { "ocr_free_results",  reinterpret_cast<void**>(&ocr_free_results_fn_) },
        { "ocr_last_error",    reinterpret_cast<void**>(&ocr_last_error_fn_) },

        // 车牌识别API
        { "plate_create",       reinterpret_cast<void**>(&plate_create_fn_) },
        { "plate_destroy",      reinterpret_cast<void**>(&plate_destroy_fn_) },
        { "plate_run",          reinterpret_cast<void**>(&plate_run_fn_) },
        { "plate_run_file",     reinterpret_cast<void**>(&plate_run_file_fn_) },
        { "plate_free_results", reinterpret_cast<void**>(&plate_free_results_fn_) },

        // 版面分析API
        { "layout_create",       reinterpret_cast<void**>(&layout_create_fn_) },
        { "layout_destroy",      reinterpret_cast<void**>(&layout_destroy_fn_) },
        { "layout_run",          reinterpret_cast<void**>(&layout_run_fn_) },
        { "layout_run_file",     reinterpret_cast<void**>(&layout_run_file_fn_) },
        { "layout_free_results", reinterpret_cast<void**>(&layout_free_results_fn_) },

        // 表格识别API
        { "table_create",      reinterpret_cast<void**>(&table_create_fn_) },
        { "table_destroy",     reinterpret_cast<void**>(&table_destroy_fn_) },
        { "table_run",         reinterpret_cast<void**>(&table_run_fn_) },
        { "table_run_file",    reinterpret_cast<void**>(&table_run_file_fn_) },
        { "table_free_result", reinterpret_cast<void**>(&table_free_result_fn_) },

        // 文档版面分析API
        { "doc_layout_create",       reinterpret_cast<void**>(&doc_layout_create_fn_) },
        { "doc_layout_destroy",      reinterpret_cast<void**>(&doc_layout_destroy_fn_) },
        { "doc_layout_run",          reinterpret_cast<void**>(&doc_layout_run_fn_) },
        { "doc_layout_run_file",     reinterpret_cast<void**>(&doc_layout_run_file_fn_) },
        { "doc_layout_free_results", reinterpret_cast<void**>(&doc_layout_free_results_fn_) },

        // YOLOv8版面分析API
        { "yolov8_layout_create",    reinterpret_cast<void**>(&yolov8_layout_create_fn_) },
        { "yolov8_layout_destroy",   reinterpret_cast<void**>(&yolov8_layout_destroy_fn_) },
        { "yolov8_layout_run",       reinterpret_cast<void**>(&yolov8_layout_run_fn_) },
        { "yolov8_layout_run_file",  reinterpret_cast<void**>(&yolov8_layout_run_file_fn_) },

        // DocLayout YOLO版面分析API
        { "doclayout_yolo_create",    reinterpret_cast<void**>(&doclayout_yolo_create_fn_) },
        { "doclayout_yolo_destroy",   reinterpret_cast<void**>(&doclayout_yolo_destroy_fn_) },
        { "doclayout_yolo_run",       reinterpret_cast<void**>(&doclayout_yolo_run_fn_) },
        { "doclayout_yolo_run_file",  reinterpret_cast<void**>(&doclayout_yolo_run_file_fn_) },

        // 图像显示API
        { "image_show_rects",      reinterpret_cast<void**>(&image_show_rects_fn_) },
        { "image_show_rects_file", reinterpret_cast<void**>(&image_show_rects_file_fn_) },

    };

    for (const auto& e : entries) {
        *e.target = platform_resolve(lib_handle_, e.name);
        if (!*e.target) {
            throw DynamicLoaderError(
                std::string("failed to resolve symbol '") + e.name +
                "': " + platform_error());
        }
    }
}

} // namespace ocr
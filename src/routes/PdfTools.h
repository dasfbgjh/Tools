#ifndef ROUTES_PDF_TOOLS_H
#define ROUTES_PDF_TOOLS_H

#include <httplib.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <map>
#include <mutex>
#include "core/Utils.h"
#include "common/Logger.hpp"

namespace fs = std::filesystem;

namespace routes::pdfTools {

struct TempSession {
    fs::path dir;
    std::time_t createdAt;
};

std::string runPdfTool( const std::vector<std::string> &args,
                        const fs::path &workDir,
                        std::string &error );

std::string saveUploadedFile( const httplib::FormData &file,
                              const fs::path &dir );

fs::path createTempDir();

void cleanupTempDir( const fs::path &dir );

void cleanupExpiredSessions();

std::string registerTempSession( const fs::path &dir );

void registerPdfRoutes( httplib::Server &svr );

void pdfTempServe( const httplib::Request &req, httplib::Response &res );

void pdfCompress( const httplib::Request &req, httplib::Response &res );

void pdfMerge( const httplib::Request &req, httplib::Response &res );

void pdfSplit( const httplib::Request &req, httplib::Response &res );

void pdfExtract( const httplib::Request &req, httplib::Response &res );

void pdfRotate( const httplib::Request &req, httplib::Response &res );

void pdfWatermark( const httplib::Request &req, httplib::Response &res );

} // namespace routes::pdfTools

#endif // ROUTES_PDF_TOOLS_H

#include "routes/PdfTools.h"
#include "common/Config.h"
#include "common/EventLoop.h"
#include "core/Server.h"

namespace fs = std::filesystem;

namespace routes::pdfTools {

struct TempSession {
    fs::path dir;
    std::time_t createdAt;
};

static std::map<std::string, TempSession> g_tempSessions;
static std::mutex g_tempMutex;

std::string runPdfTool( const std::vector<std::string> &args,
                        const fs::path &workDir,
                        std::string &error ) {
    std::vector<std::string> cmdArgs;
    cmdArgs.push_back( Config::getPdfToolPath() );
    cmdArgs.insert( cmdArgs.end(), args.begin(), args.end() );

    {
        LOG_INFO << "执行PDF工具命令: " << Config::getPdfToolPath();
        std::string argsStr;
        for ( auto &a : args ) {
            if ( !argsStr.empty() )
                argsStr.append( " " );
            argsStr.append( a );
        }
        LOG_DEBUG << "  参数:" << argsStr;
    }

    EventLoop::ProcessResult result = EventLoop::runProcessSync( cmdArgs, workDir );
    result.output = utils::localToUtf8( result.output );
    result.error = utils::localToUtf8( result.error );

    if ( !result.started ) {
        error = "启动进程失败";
        LOG_ERROR << error;
        return "";
    }

    if ( !result.error.empty() )
        LOG_DEBUG << "PDF工具错误输出前100字符 =" << ( result.error.size() > 100 ? result.error.substr( 0, 100 ) : result.error );
    if ( result.exitCode != 0 ) {
        LOG_ERROR << "PDF工具执行失败 exitCode =" << result.exitCode << " 输出前200字符 =" << ( result.output.size() > 200 ? result.output.substr( 0, 200 ) : result.output );
        error = "pdf_tool exited with code " + std::to_string( result.exitCode ) + ": " + result.output;
        return "";
    }
    LOG_DEBUG << "PDF工具执行成功输出前100字符 =" << ( result.output.size() > 100 ? result.output.substr( 0, 100 ) : result.output );
    return result.output;
}

std::string saveUploadedFile( const httplib::FormData &file,
                              const fs::path &dir ) {
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch() )
                     .count();
    std::string safeName = std::to_string( nowMs ) + "_" + file.filename;
    fs::path outPath = dir / safeName;
    std::ofstream ofs( outPath, std::ios::binary );
    if ( !ofs )
        return "";
    ofs.write( file.content.data(), file.content.size() );
    return outPath.string();
}

fs::path createTempDir() {
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch() )
                     .count();
    fs::path dir = fs::path( Config::getTempPath() + "/pdf_" ) / std::to_string( nowMs );
    fs::create_directories( dir );
    return fs::absolute( dir );
}

void cleanupTempDir( const fs::path &dir ) {
    try {
        if ( fs::exists( dir ) )
            fs::remove_all( dir );
    } catch ( ... ) {
    }
}

void cleanupExpiredSessions() {
    std::lock_guard<std::mutex> lk( g_tempMutex );
    std::time_t now = std::time( nullptr );
    auto it = g_tempSessions.begin();
    while ( it != g_tempSessions.end() ) {
        if ( now - it->second.createdAt > 3600 ) {
            cleanupTempDir( it->second.dir );
            it = g_tempSessions.erase( it );
        } else {
            ++it;
        }
    }
}

std::string registerTempSession( const fs::path &dir ) {
    cleanupExpiredSessions();
    std::string token = utils::generateToken();
    std::lock_guard<std::mutex> lk( g_tempMutex );
    g_tempSessions[token] = { dir, std::time( nullptr ) };
    return token;
}

void pdfTempServe( const httplib::Request &req, httplib::Response &res ) {
    std::string token = req.matches[1];
    std::string filename = req.matches[2];
    LOG_DEBUG << "提供临时PDF token前8位=" << ( token.size() >= 8 ? token.substr( 0, 8 ) : "N/A" ) << " filename=" << filename;

    std::lock_guard<std::mutex> lk( g_tempMutex );
    auto it = g_tempSessions.find( token );
    if ( it == g_tempSessions.end() ) {
        return Server::sendError( res, "会话不存在或已过期", 404 );
    }

    fs::path filePath = it->second.dir / "split_pages" / filename;
    if ( !fs::exists( filePath ) || !fs::is_regular_file( filePath ) ) {
        return Server::sendError( res, "文件不存在", 404 );
    }

    std::ifstream ifs( filePath, std::ios::binary );
    std::string data( ( std::istreambuf_iterator<char>( ifs ) ), std::istreambuf_iterator<char>() );
    LOG_DEBUG << "提供临时PDF文件 filename=" << filename << " size=" << data.size() << " bytes";
    res.set_content( data, "application/pdf" );
    res.set_header( "Content-Disposition", "attachment; filename=\"" + filename + "\"" );
}

void pdfCompress( const httplib::Request &req, httplib::Response &res ) {
    if ( !req.is_multipart_form_data() )
        return Server::sendError( res, "需要multipart上传", 400 );
    if ( !req.form.has_file( "file" ) )
        return Server::sendError( res, "缺少file字段", 400 );

    auto file = req.form.get_file( "file" );
    LOG_DEBUG << "PDF压缩请求 filename=" << file.filename << " size=" << file.content.size();
    if ( file.content.size() > Config::getMaxPdfSize() )
        return Server::sendError( res, "文件超过100MB限制", 400 );

    fs::path workDir = createTempDir();
    std::string inputPath = saveUploadedFile( file, workDir );
    if ( inputPath.empty() ) {
        cleanupTempDir( workDir );
        return Server::sendError( res, "文件保存失败", 500 );
    }

    std::string outName = "compressed.pdf";
    std::string error;
    std::vector<std::string> args = { "compress", "-o", outName, fs::path( inputPath ).filename().string() };
    runPdfTool( args, workDir, error );

    fs::path outPath = workDir / outName;
    if ( !fs::exists( outPath ) ) {
        cleanupTempDir( workDir );
        LOG_ERROR << "PDF压缩失败 filename=" << file.filename << ": " << error;
        return Server::sendError( res, "压缩失败: " + error, 500 );
    }

    std::ifstream ifs( outPath, std::ios::binary );
    std::string data( ( std::istreambuf_iterator<char>( ifs ) ), std::istreambuf_iterator<char>() );
    cleanupTempDir( workDir );
    LOG_INFO << "PDF压缩成功 filename=" << file.filename << " 原始大小=" << file.content.size() << " 压缩后大小=" << data.size() << " bytes";

    res.set_content( data, "application/pdf" );
    res.set_header( "Content-Disposition",
                    "attachment; filename=\"compressed.pdf\"" );
}

void pdfMerge( const httplib::Request &req, httplib::Response &res ) {
    if ( !req.is_multipart_form_data() )
        return Server::sendError( res, "需要multipart上传", 400 );

    std::vector<std::string> inputPaths;
    fs::path workDir = createTempDir();

    for ( auto &kv : req.form.files ) {
        if ( kv.second.content.size() > Config::getMaxPdfSize() ) {
            cleanupTempDir( workDir );
            return Server::sendError( res, "文件超过100MB限制: " + kv.second.filename, 400 );
        }
        std::string p = saveUploadedFile( kv.second, workDir );
        if ( p.empty() ) {
            cleanupTempDir( workDir );
            return Server::sendError( res, "文件保存失败: " + kv.second.filename, 500 );
        }
        inputPaths.push_back( fs::path( p ).filename().string() );
    }

    if ( inputPaths.size() < 2 ) {
        cleanupTempDir( workDir );
        return Server::sendError( res, "至少需要2个PDF文件进行合并", 400 );
    }
    LOG_DEBUG << "PDF合并请求 文件数=" << inputPaths.size();

    std::string outName = "merged.pdf";
    std::string error;
    std::vector<std::string> args = { "merge", "-o", outName };
    for ( auto &p : inputPaths )
        args.push_back( p );
    runPdfTool( args, workDir, error );

    fs::path outPath = workDir / outName;
    if ( !fs::exists( outPath ) ) {
        cleanupTempDir( workDir );
        LOG_ERROR << "PDF合并失败 文件数=" << inputPaths.size() << ": " << error;
        return Server::sendError( res, "合并失败: " + error, 500 );
    }

    std::ifstream ifs( outPath, std::ios::binary );
    std::string data( ( std::istreambuf_iterator<char>( ifs ) ), std::istreambuf_iterator<char>() );
    cleanupTempDir( workDir );
    LOG_INFO << "PDF合并成功 文件数=" << inputPaths.size() << " 合并后大小=" << data.size() << " bytes";

    res.set_content( data, "application/pdf" );
    res.set_header( "Content-Disposition",
                    "attachment; filename=\"merged.pdf\"" );
}

void pdfSplit( const httplib::Request &req, httplib::Response &res ) {
    if ( !req.is_multipart_form_data() )
        return Server::sendError( res, "需要multipart上传", 400 );
    if ( !req.form.has_file( "file" ) )
        return Server::sendError( res, "缺少file字段", 400 );

    auto file = req.form.get_file( "file" );
    LOG_DEBUG << "PDF分割请求 filename=" << file.filename << " size=" << file.content.size();
    if ( file.content.size() > Config::getMaxPdfSize() )
        return Server::sendError( res, "文件超过100MB限制", 400 );

    fs::path workDir = createTempDir();
    std::string inputPath = saveUploadedFile( file, workDir );
    if ( inputPath.empty() ) {
        cleanupTempDir( workDir );
        return Server::sendError( res, "文件保存失败", 500 );
    }

    std::string outDir = "split_pages";
    std::string error;
    std::vector<std::string> args = { "split", "-d", outDir, fs::path( inputPath ).filename().string() };
    runPdfTool( args, workDir, error );

    fs::path outDirPath = workDir / outDir;
    if ( !fs::exists( outDirPath ) || !fs::is_directory( outDirPath ) ) {
        cleanupTempDir( workDir );
        LOG_ERROR << "PDF分割失败 filename=" << file.filename << ": " << error;
        return Server::sendError( res, "分割失败: " + error, 500 );
    }

    std::string token = registerTempSession( workDir );

    Server::json files = Server::json::array();
    std::vector<fs::path> sorted;
    for ( auto &entry : fs::directory_iterator( outDirPath ) ) {
        if ( entry.is_regular_file() && entry.path().extension() == ".pdf" ) {
            sorted.push_back( entry.path() );
        }
    }
    std::sort( sorted.begin(), sorted.end(), []( const fs::path &a, const fs::path &b ) {
        return a.filename().string() < b.filename().string();
    } );
    for ( auto &p : sorted ) {
        files.push_back( { { "name", p.filename().string() },
                           { "size", static_cast<long long>( fs::file_size( p ) ) },
                           { "url", "/api/pdf/temp/" + token + "/" + p.filename().string() } } );
    }
    LOG_INFO << "PDF分割成功 filename=" << file.filename << " 得到文件数=" << files.size() << " token前8位=" << ( token.size() >= 8 ? token.substr( 0, 8 ) : "N/A" );

    Server::sendJson( res, { { "success", true },
                             { "token", token },
                             { "count", files.size() },
                             { "files", files } } );
}

void pdfExtract( const httplib::Request &req, httplib::Response &res ) {
    if ( !req.is_multipart_form_data() )
        return Server::sendError( res, "需要multipart上传", 400 );
    if ( !req.form.has_file( "file" ) )
        return Server::sendError( res, "缺少file字段", 400 );

    auto file = req.form.get_file( "file" );
    LOG_DEBUG << "PDF提取请求 filename=" << file.filename << " size=" << file.content.size();
    if ( file.content.size() > Config::getMaxPdfSize() )
        return Server::sendError( res, "文件超过100MB限制", 400 );

    std::string pages = "";
    if ( req.form.has_field( "pages" ) )
        pages = req.form.get_field( "pages" );
    if ( pages.empty() )
        return Server::sendError( res, "缺少pages参数", 400 );
    LOG_DEBUG << "PDF提取 pages=" << pages;

    fs::path workDir = createTempDir();
    std::string inputPath = saveUploadedFile( file, workDir );
    if ( inputPath.empty() ) {
        cleanupTempDir( workDir );
        return Server::sendError( res, "文件保存失败", 500 );
    }

    std::string outName = "extracted.pdf";
    std::string error;
    std::vector<std::string> args = { "extract", "-p", pages, "-o", outName, fs::path( inputPath ).filename().string() };
    runPdfTool( args, workDir, error );

    fs::path outPath = workDir / outName;
    if ( !fs::exists( outPath ) ) {
        cleanupTempDir( workDir );
        LOG_ERROR << "PDF提取失败 filename=" << file.filename << " pages=" << pages << ": " << error;
        return Server::sendError( res, "提取失败: " + error, 500 );
    }

    std::ifstream ifs( outPath, std::ios::binary );
    std::string data( ( std::istreambuf_iterator<char>( ifs ) ), std::istreambuf_iterator<char>() );
    cleanupTempDir( workDir );
    LOG_INFO << "PDF提取成功 filename=" << file.filename << " pages=" << pages << " 提取后大小=" << data.size() << " bytes";

    res.set_content( data, "application/pdf" );
    res.set_header( "Content-Disposition",
                    "attachment; filename=\"extracted.pdf\"" );
}

void pdfRotate( const httplib::Request &req, httplib::Response &res ) {
    if ( !req.is_multipart_form_data() )
        return Server::sendError( res, "需要multipart上传", 400 );
    if ( !req.form.has_file( "file" ) )
        return Server::sendError( res, "缺少file字段", 400 );

    auto file = req.form.get_file( "file" );
    LOG_DEBUG << "PDF旋转请求 filename=" << file.filename << " size=" << file.content.size();
    if ( file.content.size() > Config::getMaxPdfSize() )
        return Server::sendError( res, "文件超过100MB限制", 400 );

    std::string rotation = "90";
    if ( req.form.has_field( "rotation" ) )
        rotation = req.form.get_field( "rotation" );
    if ( rotation != "90" && rotation != "180" && rotation != "270" )
        return Server::sendError( res, "rotation必须是90/180/270", 400 );

    std::string pages = "";
    if ( req.form.has_field( "pages" ) )
        pages = req.form.get_field( "pages" );
    LOG_DEBUG << "PDF旋转 rotation=" << rotation << ( pages.empty() ? " 全部页面" : " pages=" + pages );

    fs::path workDir = createTempDir();
    std::string inputPath = saveUploadedFile( file, workDir );
    if ( inputPath.empty() ) {
        cleanupTempDir( workDir );
        return Server::sendError( res, "文件保存失败", 500 );
    }

    std::string outName = "rotated.pdf";
    std::string error;
    std::vector<std::string> args = { "rotate", "-r", rotation };
    if ( !pages.empty() ) {
        args.push_back( "-p" );
        args.push_back( pages );
    }
    args.push_back( "-o" );
    args.push_back( outName );
    args.push_back( fs::path( inputPath ).filename().string() );
    runPdfTool( args, workDir, error );

    fs::path outPath = workDir / outName;
    if ( !fs::exists( outPath ) ) {
        cleanupTempDir( workDir );
        LOG_ERROR << "PDF旋转失败 filename=" << file.filename << " rotation=" << rotation << ": " << error;
        return Server::sendError( res, "旋转失败: " + error, 500 );
    }

    std::ifstream ifs( outPath, std::ios::binary );
    std::string data( ( std::istreambuf_iterator<char>( ifs ) ), std::istreambuf_iterator<char>() );
    cleanupTempDir( workDir );
    LOG_INFO << "PDF旋转成功 filename=" << file.filename << " rotation=" << rotation << " 旋转后大小=" << data.size() << " bytes";

    res.set_content( data, "application/pdf" );
    res.set_header( "Content-Disposition",
                    "attachment; filename=\"rotated.pdf\"" );
}

void pdfWatermark( const httplib::Request &req, httplib::Response &res ) {
    if ( !req.is_multipart_form_data() )
        return Server::sendError( res, "需要multipart上传", 400 );
    if ( !req.form.has_file( "file" ) )
        return Server::sendError( res, "缺少file字段", 400 );

    auto file = req.form.get_file( "file" );
    LOG_DEBUG << "PDF水印请求 filename=" << file.filename << " size=" << file.content.size();
    if ( file.content.size() > Config::getMaxPdfSize() )
        return Server::sendError( res, "文件超过100MB限制", 400 );

    std::string text = "";
    if ( req.form.has_field( "text" ) )
        text = req.form.get_field( "text" );
    if ( text.empty() )
        return Server::sendError( res, "缺少text参数", 400 );

    std::string position = "center";
    if ( req.form.has_field( "position" ) )
        position = req.form.get_field( "position" );
    if ( position != "center" && position != "top-right" && position != "bottom-left" )
        return Server::sendError( res, "position必须是center/top-right/bottom-left", 400 );
    LOG_DEBUG << "PDF水印 text前20字符=" << ( text.size() > 20 ? text.substr( 0, 20 ) : text ) << " position=" << position;

    fs::path workDir = createTempDir();
    std::string inputPath = saveUploadedFile( file, workDir );
    if ( inputPath.empty() ) {
        cleanupTempDir( workDir );
        return Server::sendError( res, "文件保存失败", 500 );
    }

    std::string outName = "watermarked.pdf";
    std::string error;
    std::vector<std::string> args = { "watermark", "-t", text, "-pos", position, "-o", outName, fs::path( inputPath ).filename().string() };
    runPdfTool( args, workDir, error );

    fs::path outPath = workDir / outName;
    if ( !fs::exists( outPath ) ) {
        cleanupTempDir( workDir );
        LOG_ERROR << "PDF水印失败 filename=" << file.filename << ": " << error;
        return Server::sendError( res, "水印失败: " + error, 500 );
    }

    std::ifstream ifs( outPath, std::ios::binary );
    std::string data( ( std::istreambuf_iterator<char>( ifs ) ), std::istreambuf_iterator<char>() );
    cleanupTempDir( workDir );
    LOG_INFO << "PDF水印成功 filename=" << file.filename << " 水印后大小=" << data.size() << " bytes";

    res.set_content( data, "application/pdf" );
    res.set_header( "Content-Disposition",
                    "attachment; filename=\"watermarked.pdf\"" );
}

void registerPdfRoutes( httplib::Server &svr ) {
    svr.Get( R"(/api/pdf/temp/([^/]+)/([^/]+))", pdfTempServe );
    svr.Post( "/api/pdf/compress", pdfCompress );
    svr.Post( "/api/pdf/merge", pdfMerge );
    svr.Post( "/api/pdf/split", pdfSplit );
    svr.Post( "/api/pdf/extract", pdfExtract );
    svr.Post( "/api/pdf/rotate", pdfRotate );
    svr.Post( "/api/pdf/watermark", pdfWatermark );
    LOG_DEBUG << "已注册 7 个PDF工具路由";
}

} // namespace routes::pdfTools
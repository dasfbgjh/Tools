#include "routes/Tools.h"
#include "qrcodegen.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE2_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize2.h"

namespace routes::tools {
struct ParsedUrl {
    std::string scheme;
    std::string host;
    int port;
    std::string path;
};

struct IpCacheEntry {
    Server::json data;
    std::time_t ts;
    std::string source;
};

struct IpApiSource {
    const char *name;
    const char *urlTpl;
};

static std::map<std::string, IpCacheEntry> g_ipCache;
static std::mutex g_ipCacheMutex;

namespace ico {

struct DibImage {
    int width;
    int height;
    std::vector<unsigned char> data;
};

static void writeU32LE( std::vector<unsigned char> &buf, size_t offset, uint32_t val ) {
    buf[offset] = static_cast<unsigned char>( val & 0xFF );
    buf[offset + 1] = static_cast<unsigned char>( ( val >> 8 ) & 0xFF );
    buf[offset + 2] = static_cast<unsigned char>( ( val >> 16 ) & 0xFF );
    buf[offset + 3] = static_cast<unsigned char>( ( val >> 24 ) & 0xFF );
}

static void writeU16LE( std::vector<unsigned char> &buf, size_t offset, uint16_t val ) {
    buf[offset] = static_cast<unsigned char>( val & 0xFF );
    buf[offset + 1] = static_cast<unsigned char>( ( val >> 8 ) & 0xFF );
}

std::vector<DibImage> createDibImages( const unsigned char *srcPixels,
                                       int srcWidth, int srcHeight,
                                       const std::vector<int> &sizes ) {
    std::vector<DibImage> result;
    for ( int sz : sizes ) {
        if ( sz <= 0 || sz > 256 )
            sz = 256;
        std::vector<unsigned char> scaledBuf( sz * sz * 4 );
        unsigned char *scaled = stbir_resize_uint8_linear(
            srcPixels, srcWidth, srcHeight, srcWidth * 4,
            scaledBuf.data(), sz, sz, sz * 4,
            STBIR_RGBA );
        if ( !scaled )
            continue;

        // ICO DIB: BITMAPINFOHEADER(40) + 颜色数据(BGRA, 自下而上) + AND掩码(1bpp, 每行按4字节对齐)
        size_t andMaskRowSize = ( ( sz + 31 ) / 32 ) * 4;
        size_t andMaskSize = andMaskRowSize * sz;
        size_t colorSize = static_cast<size_t>( sz ) * sz * 4;
        size_t dibSize = 40 + colorSize + andMaskSize;
        std::vector<unsigned char> dib( dibSize );
        memset( dib.data(), 0, dibSize );

        writeU32LE( dib, 0, 40 );
        writeU32LE( dib, 4, static_cast<uint32_t>( sz ) );
        writeU32LE( dib, 8, static_cast<uint32_t>( sz * 2 ) ); // biHeight翻倍: XOR掩码 + AND掩码
        writeU16LE( dib, 12, 1 );
        writeU16LE( dib, 14, 32 );
        writeU32LE( dib, 20, static_cast<uint32_t>( colorSize + andMaskSize ) );

        unsigned char *pixels = dib.data() + 40;
        for ( int y = 0; y < sz; ++y ) {
            unsigned char *dstRow = pixels + ( sz - 1 - y ) * sz * 4;
            const unsigned char *srcRow = scaledBuf.data() + y * sz * 4;
            for ( int x = 0; x < sz; ++x ) {
                dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
                dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
                dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
                dstRow[x * 4 + 3] = srcRow[x * 4 + 3];
            }
        }
        result.push_back( { sz, sz, std::move( dib ) } );
    }
    return result;
}

std::vector<unsigned char> buildIcoFile( const std::vector<DibImage> &images ) {
    if ( images.empty() )
        return {};

    size_t headerSize = 6 + images.size() * 16;
    size_t totalSize = headerSize;
    for ( const auto &img : images ) {
        totalSize += img.data.size();
    }

    std::vector<unsigned char> ico;
    ico.reserve( totalSize );
    ico.resize( headerSize );

    size_t offset = headerSize;

    writeU16LE( ico, 0, 0 );
    writeU16LE( ico, 2, 1 );
    writeU16LE( ico, 4, static_cast<uint16_t>( images.size() ) );

    for ( size_t i = 0; i < images.size(); ++i ) {
        const auto &img = images[i];
        size_t entryOffset = 6 + i * 16;

        ico[entryOffset + 0] = ( img.width >= 256 ) ? 0 : static_cast<unsigned char>( img.width );
        ico[entryOffset + 1] = ( img.height >= 256 ) ? 0 : static_cast<unsigned char>( img.height );
        ico[entryOffset + 2] = 0;
        ico[entryOffset + 3] = 0;
        writeU16LE( ico, entryOffset + 4, 1 );
        writeU16LE( ico, entryOffset + 6, 32 );
        writeU32LE( ico, entryOffset + 8, static_cast<uint32_t>( img.data.size() ) );
        writeU32LE( ico, entryOffset + 12, static_cast<uint32_t>( offset ) );

        offset += img.data.size();
    }

    for ( const auto &img : images ) {
        ico.insert( ico.end(), img.data.begin(), img.data.end() );
    }

    return ico;
}

} // namespace ico

bool parseUrl( const std::string &url, ParsedUrl &out ) {
    static const std::regex re( R"(^([a-zA-Z][a-zA-Z0-9+.-]*)://([^/:]+)(?::(\d+))?(/.*)?$)" );
    std::smatch m;
    if ( !std::regex_match( url, m, re ) ) {
        static const std::regex re2( R"(^([^/:]+)(?::(\d+))?(/.*)?$)" );
        if ( !std::regex_match( url, m, re2 ) )
            return false;
        out.scheme = "http";
        out.host = m[1].str();
        out.port = m[2].matched ? std::stoi( m[2].str() ) : 80;
        out.path = m[3].matched ? m[3].str() : "/";
        return true;
    }
    out.scheme = m[1].str();
    std::transform( out.scheme.begin(), out.scheme.end(), out.scheme.begin(), ::tolower );
    out.host = m[2].str();
    if ( m[3].matched )
        out.port = std::stoi( m[3].str() );
    else
        out.port = ( out.scheme == "https" ) ? 443 : 80;
    out.path = m[4].matched ? m[4].str() : "/";
    return true;
}

bool isSensitivePort( int port ) {
    static const std::vector<int> blocked = {
        21, 22, 23, 25, 53, 69, 110, 111, 119, 123, 135, 137, 138, 139, 143,
        161, 162, 389, 445, 465, 514, 515, 587, 631, 636, 989, 990, 993, 995,
        1433, 1434, 1521, 3306, 3389, 5000, 5432, 5900, 6379, 8080, 8443, 8888,
        9200, 9300, 11211, 27017 };
    for ( int p : blocked )
        if ( port == p )
            return true;
    return false;
}

bool isUrlSafe( const std::string &url, std::string &err ) {
    ParsedUrl pu;
    if ( !parseUrl( url, pu ) ) {
        err = "URL格式无效";
        return false;
    }
    if ( pu.scheme != "http" && pu.scheme != "https" ) {
        err = "仅支持HTTP/HTTPS协议";
        return false;
    }
    if ( isSensitivePort( pu.port ) ) {
        err = "目标端口不被允许";
        return false;
    }
    return true;
}

void cacheIpInfo( const std::string &ip, const Server::json &data, const std::string &source ) {
    std::lock_guard<std::mutex> lk( g_ipCacheMutex );
    g_ipCache[ip] = { data, utils::nowTime(), source };
    if ( g_ipCache.size() > 200 ) {
        std::vector<std::pair<std::string, std::time_t>> v;
        for ( auto &kv : g_ipCache )
            v.push_back( { kv.first, kv.second.ts } );
        std::sort( v.begin(), v.end(),
                   []( auto &a, auto &b ) { return a.second < b.second; } );
        for ( size_t i = 0; i < 20 && i < v.size(); i++ )
            g_ipCache.erase( v[i].first );
    }
}

bool getCachedIpInfo( const std::string &ip, IpCacheEntry &out ) {
    std::lock_guard<std::mutex> lk( g_ipCacheMutex );
    auto it = g_ipCache.find( ip );
    if ( it == g_ipCache.end() )
        return false;
    if ( utils::nowTime() - it->second.ts > 3600 )
        return false;
    out = it->second;
    return true;
}

bool isValidIpv4( const std::string &ip ) {
    static const std::regex re( R"(^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$)" );
    return std::regex_match( ip, re );
}

bool fetchIpSource( const std::string &url, Server::json &result, std::string &err ) {
    ParsedUrl pu;
    if ( !parseUrl( url, pu ) ) {
        err = "URL解析失败";
        return false;
    }
    std::string schemeHostPort = pu.scheme + "://" + pu.host + ":" + std::to_string( pu.port );
    httplib::Client cli( schemeHostPort );
    cli.set_connection_timeout( 5, 0 );
    cli.set_read_timeout( 5, 0 );
    cli.set_default_headers( { { "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36" },
                               { "Accept", "application/json, text/plain, */*" } } );
    auto res = cli.Get( pu.path.c_str() );
    if ( !res ) {
        err = "连接失败";
        return false;
    }
    if ( res->status >= 400 ) {
        err = "HTTP " + std::to_string( res->status );
        return false;
    }
    try {
        result = Server::json::parse( res->body );
        return true;
    } catch ( ... ) {
        std::string body = res->body;
        size_t s = body.find( '{' );
        size_t e = body.rfind( '}' );
        if ( s != std::string::npos && e != std::string::npos && e > s ) {
            try {
                result = Server::json::parse( body.substr( s, e - s + 1 ) );
                return true;
            } catch ( ... ) {
            }
        }
        err = "响应不是有效的JSON";
        return false;
    }
}

// L/M/Q/H 字符串 -> QrCode::Ecc；未识别回退 MEDIUM
qrcodegen::QrCode::Ecc parseEcc( const std::string &s ) {
    if ( s.size() == 1 ) {
        switch ( s[0] ) {
        case 'L':
        case 'l':
            return qrcodegen::QrCode::Ecc::LOW;
        case 'Q':
        case 'q':
            return qrcodegen::QrCode::Ecc::QUARTILE;
        case 'H':
        case 'h':
            return qrcodegen::QrCode::Ecc::HIGH;
        default:
            break;
        }
    }
    return qrcodegen::QrCode::Ecc::MEDIUM;
}

std::string generateQrSvg( const std::string &text, int scale, int border, qrcodegen::QrCode::Ecc ecc ) {
    using namespace qrcodegen;
    QrCode qr = QrCode::encodeText( text.c_str(), ecc );
    const int dim = ( qr.getSize() + border * 2 ) * scale;
    std::ostringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\""
       << " width=\"" << dim << "\" height=\"" << dim << "\""
       << " viewBox=\"0 0 " << dim << " " << dim << "\""
       << " shape-rendering=\"crispEdges\">"
       << "<rect width=\"" << dim << "\" height=\"" << dim << "\" fill=\"#ffffff\"/>"
       << "<path fill=\"#000000\" d=\"";
    for ( int y = 0; y < qr.getSize(); y++ ) {
        for ( int x = 0; x < qr.getSize(); x++ ) {
            if ( qr.getModule( x, y ) ) {
                const int px = ( x + border ) * scale;
                const int py = ( y + border ) * scale;
                ss << "M" << px << "," << py << "h" << scale << "v" << scale << "h-" << scale << "z";
            }
        }
    }
    ss << "\"/></svg>";
    return ss.str();
}

std::string generateQrSvg( const std::string &text, int scale, int border ) {
    using namespace qrcodegen;
    return generateQrSvg( text, scale, border, QrCode::Ecc::MEDIUM );
}

void ipLookup( const httplib::Request &req, httplib::Response &res ) {
    std::string ip = Server::queryParam( req, "ip" );
    std::string source = Server::queryParam( req, "source" );
    bool checkSelf = ip.empty() || ip == "self";
    LOG_DEBUG << "IP查询 请求IP=" << ip << " source=" << ( source.empty() ? "auto" : source ) << " requestType=" << ( checkSelf ? "self" : "query" );

    std::string clientIp = req.remote_addr;
    if ( checkSelf ) {
        ip = clientIp;
        if ( ip == "127.0.0.1" || ip == "::1" || ip.empty() ) {
            Server::sendJson( res, {
                                       { "data",
                                         { { "ip", "无法获取客户端IP" },
                                           { "country", "未知" },
                                           { "region", "未知" },
                                           { "city", "未知" },
                                           { "isp", "未知" } } },
                                       { "source", "本地分析" },
                                       { "requestType", "self" },
                                   } );
            return;
        }
    } else {
        if ( !isValidIpv4( ip ) ) {
            Server::sendError( res, "无效的IP地址格式", 400 );
            return;
        }
    }

    if ( ip == "127.0.0.1" || ip.rfind( "192.168.", 0 ) == 0 ||
         ip.rfind( "10.", 0 ) == 0 || ip.rfind( "172.", 0 ) == 0 ) {
        int b = 0;
        if ( ip.rfind( "172.", 0 ) == 0 ) {
            try {
                b = std::stoi( ip.substr( 4, ip.find( '.', 4 ) - 4 ) );
            } catch ( ... ) {
            }
            if ( b >= 16 && b <= 31 ) {
                Server::sendJson( res, { { "data", { { "ip", ip }, { "country", "本地网络" }, { "region", "私有网络(B类)" }, { "city", "内部网络" }, { "isp", "本地连接" } } },
                                         { "source", "本地分析" },
                                         { "requestType", checkSelf ? "self" : "query" } } );
                return;
            }
        }
        std::string region = "私有网络";
        if ( ip == "127.0.0.1" )
            region = "环回地址";
        else if ( ip.rfind( "192.168.", 0 ) == 0 )
            region = "私有网络(C类)";
        else if ( ip.rfind( "10.", 0 ) == 0 )
            region = "私有网络(A类)";
        Server::sendJson( res, { { "data", { { "ip", ip }, { "country", "本地网络" }, { "region", region }, { "city", "内部网络" }, { "isp", "本地连接" } } },
                                 { "source", "本地分析" },
                                 { "requestType", checkSelf ? "self" : "query" } } );
        return;
    }

    IpCacheEntry cached;
    if ( getCachedIpInfo( ip, cached ) ) {
        LOG_DEBUG << "IP查询命中缓存 ip=" << ip << " source=" << cached.source;
        Server::sendJson( res, { { "data", cached.data },
                                 { "source", cached.source },
                                 { "requestType", checkSelf ? "self" : "query" },
                                 { "cached", true } } );
        return;
    }

    static const std::vector<IpApiSource> sources = {
        { "ip-api.com", "http://ip-api.com/json/{ip}?lang=zh-CN&fields=status,country,regionName,city,isp,org,as,query,lat,lon,timezone" },
        { "ip-api.cn", "http://ip-api.com/json/{ip}?lang=zh-CN" } };

    std::vector<IpApiSource> trySources;
    if ( !source.empty() && source != "auto" ) {
        for ( auto &s : sources )
            if ( source == s.name ) {
                trySources.push_back( s );
                break;
            }
        if ( trySources.empty() ) {
            Server::sendError( res, "指定的API源不存在或当前不可用", 400 );
            return;
        }
    } else {
        trySources = sources;
    }

    for ( auto &s : trySources ) {
        std::string url = s.urlTpl;
        size_t pos = url.find( "{ip}" );
        if ( pos != std::string::npos )
            url.replace( pos, 4, ip );
        Server::json data;
        std::string err;
        if ( fetchIpSource( url, data, err ) ) {
            cacheIpInfo( ip, data, s.name );
            LOG_INFO << "IP查询成功 ip=" << ip << " source=" << s.name;
            Server::sendJson( res, { { "data", data },
                                     { "source", s.name },
                                     { "requestType", checkSelf ? "self" : "query" } } );
            return;
        } else {
            LOG_WARN << "IP查询源 " << s.name << " 失败: " << err;
        }
    }

    LOG_WARN << "所有IP查询源均失败 ip=" << ip << "，使用回退数据";
    Server::json fallback = { { "ip", ip }, { "country", "未知" }, { "region", "未知" }, { "city", "未知" }, { "isp", "未知" } };
    cacheIpInfo( ip, fallback, "解析失败" );
    Server::sendJson( res, { { "data", fallback },
                             { "source", "解析失败" },
                             { "requestType", checkSelf ? "self" : "query" } } );
}

void httpProxy( const httplib::Request &req, httplib::Response &res ) {
    Server::json body = Server::parseBody( req );
    if ( body.is_null() ) {
        Server::sendError( res, "请求体不是有效的JSON", 400 );
        return;
    }
    std::string url = utils::jsonStringValue( body, "url" );
    std::string method = utils::jsonStringValue( body, "method" );
    if ( url.empty() ) {
        Server::sendError( res, "缺少url参数", 400 );
        return;
    }
    if ( method.empty() )
        method = "GET";
    std::transform( method.begin(), method.end(), method.begin(), ::toupper );

    static const std::vector<std::string> allowed = {
        "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS" };
    bool ok = false;
    for ( auto &m : allowed )
        if ( method == m ) {
            ok = true;
            break;
        }
    if ( !ok ) {
        Server::sendError( res, "不支持的HTTP方法", 400 );
        return;
    }

    std::string err;
    if ( !isUrlSafe( url, err ) ) {
        LOG_WARN << "代理请求被阻止 url=" << url << " reason=" << err;
        Server::sendError( res, err, 403 );
        return;
    }

    ParsedUrl pu;
    parseUrl( url, pu );
    LOG_DEBUG << "HTTP代理请求 method=" << method << " scheme=" << pu.scheme << " host=" << pu.host << " port=" << pu.port << " path=" << pu.path;

    std::string reqBody;
    if ( method != "GET" && method != "HEAD" ) {
        if ( body.contains( "body" ) && body["body"].is_string() ) {
            reqBody = body["body"].get<std::string>();
            if ( reqBody.size() > 2 * 1024 * 1024 ) {
                Server::sendError( res, "请求体超过2MB限制", 413 );
                return;
            }
        }
    }

    httplib::Headers headers;
    headers.emplace( "User-Agent", "shared-clipboard-proxy/1.0" );
    if ( body.contains( "headers" ) && body["headers"].is_object() ) {
        for ( auto it = body["headers"].begin(); it != body["headers"].end(); ++it ) {
            std::string k = it.key();
            std::string lower = k;
            std::transform( lower.begin(), lower.end(), lower.begin(), ::tolower );
            if ( lower == "host" || lower == "origin" || lower == "referer" ||
                 lower == "cookie" || lower == "authorization" )
                continue;
            if ( it.value().is_string() )
                headers.emplace( k, it.value().get<std::string>() );
        }
    }

    std::string schemeHostPort = pu.scheme + "://" + pu.host + ":" + std::to_string( pu.port );
    httplib::Client cli( schemeHostPort );
    cli.set_connection_timeout( 10, 0 );
    cli.set_read_timeout( 15, 0 );
    cli.set_default_headers( headers );

    auto start = std::chrono::steady_clock::now();
    httplib::Result result;
    if ( method == "GET" )
        result = cli.Get( pu.path.c_str() );
    else if ( method == "HEAD" )
        result = cli.Head( pu.path.c_str() );
    else if ( method == "POST" )
        result = cli.Post( pu.path.c_str(), reqBody, "application/json" );
    else if ( method == "PUT" )
        result = cli.Put( pu.path.c_str(), reqBody, "application/json" );
    else if ( method == "DELETE" ) {
        if ( reqBody.empty() )
            result = cli.Delete( pu.path.c_str() );
        else
            result = cli.Delete( pu.path.c_str(), reqBody, "application/json" );
    } else if ( method == "PATCH" )
        result = cli.Patch( pu.path.c_str(), reqBody, "application/json" );
    else if ( method == "OPTIONS" )
        result = cli.Options( pu.path.c_str() );

    auto end = std::chrono::steady_clock::now();
    int ms = static_cast<int>( std::chrono::duration_cast<std::chrono::milliseconds>( end - start ).count() );

    if ( !result ) {
        LOG_WARN << "代理请求失败 method=" << method << " url=" << url << " 耗时=" << ms << "ms reason=连接超时或失败";
        Server::sendError( res, "无法连接到目标服务器或请求超时", 502 );
        return;
    }

    std::string respBody = result->body;
    if ( respBody.size() > 5 * 1024 * 1024 ) {
        respBody = respBody.substr( 0, 5 * 1024 * 1024 );
    }

    Server::json respHeaders = Server::json::object();
    for ( auto &h : result->headers ) {
        if ( respHeaders.contains( h.first ) ) {
            respHeaders[h.first] = respHeaders[h.first].get<std::string>() + ", " + h.second;
        } else {
            respHeaders[h.first] = h.second;
        }
    }

    Server::json respData;
    std::string ct;
    auto ctIt = result->headers.find( "content-type" );
    if ( ctIt != result->headers.end() )
        ct = ctIt->second;
    bool isJson = ct.find( "application/json" ) != std::string::npos || ct.find( "text/json" ) != std::string::npos;
    if ( isJson ) {
        try {
            respData = Server::json::parse( respBody );
        } catch ( ... ) {
            respData = respBody;
        }
    } else {
        respData = respBody;
    }

    LOG_INFO << "代理请求完成 method=" << method << " status=" << result->status << " 耗时=" << ms << "ms 响应大小=" << respBody.size() << " bytes url=" << url;
    Server::json resp = {
        { "status", result->status },
        { "statusText", httplib::status_message( result->status ) },
        { "headers", respHeaders },
        { "data", respData },
        { "time", ms },
        { "size", static_cast<long long>( respBody.size() ) } };
    Server::sendJson( res, resp );
}

void qrcode( const httplib::Request &req, httplib::Response &res ) {
    std::string text = req.get_param_value( "text" );
    if ( text.empty() ) {
        Server::sendJson( res, { { "success", false }, { "error", "Missing text parameter" } }, 400 );
        return;
    }
    std::string format = req.has_param( "format" ) ? req.get_param_value( "format" ) : "svg";
    std::string eccStr = req.has_param( "ecc" ) ? req.get_param_value( "ecc" ) : "M";
    qrcodegen::QrCode::Ecc ecc = parseEcc( eccStr );
    LOG_DEBUG << "生成二维码 format=" << format << " ecc=" << eccStr << " 文本长度=" << text.size();
    try {
        using namespace qrcodegen;
        QrCode qr = QrCode::encodeText( text.c_str(), ecc );
        if ( format == "matrix" ) {
            Server::json matrix = Server::json::array();
            for ( int y = 0; y < qr.getSize(); y++ ) {
                Server::json row = Server::json::array();
                for ( int x = 0; x < qr.getSize(); x++ ) {
                    row.push_back( qr.getModule( x, y ) );
                }
                matrix.push_back( row );
            }
            LOG_DEBUG << "二维码生成成功 format=matrix size=" << qr.getSize();
            Server::sendJson( res, {
                                       { "success", true },
                                       { "size", qr.getSize() },
                                       { "matrix", matrix },
                                   } );
        } else {
            std::string svg = generateQrSvg( text, 10, 4, ecc );
            LOG_DEBUG << "二维码生成成功 format=svg size=" << svg.size() << " bytes";
            res.set_content( svg, "image/svg+xml" );
        }
    } catch ( const std::exception &e ) {
        LOG_ERROR << "二维码生成异常: " << e.what();
        Server::sendJson( res, {
                                   { "success", false },
                                   { "error", e.what() },
                               },
                          500 );
    }
}

void imageConvert( const httplib::Request &req, httplib::Response &res ) {
    if ( !req.is_multipart_form_data() ) {
        Server::sendError( res, "需要multipart上传", 400 );
        return;
    }
    if ( !req.form.has_file( "file" ) ) {
        Server::sendError( res, "缺少file字段", 400 );
        return;
    }
    auto file = req.form.get_file( "file" );
    std::string filename = file.filename;
    LOG_DEBUG << "图片转换请求 filename=" << filename << " size=" << file.content.size();

    std::string targetFormat = "png";
    if ( req.form.has_field( "format" ) ) {
        targetFormat = req.form.get_field( "format" );
        std::transform( targetFormat.begin(), targetFormat.end(), targetFormat.begin(), ::tolower );
    }

    int quality = 90;
    if ( req.form.has_field( "quality" ) ) {
        try {
            quality = std::stoi( req.form.get_field( "quality" ) );
            quality = std::max( 1, std::min( 100, quality ) );
        } catch ( ... ) {
            quality = 90;
        }
    }

    std::vector<int> icoSizes;
    if ( req.form.has_field( "ico_sizes" ) ) {
        std::string sizesStr = req.form.get_field( "ico_sizes" );
        std::stringstream ss( sizesStr );
        std::string token;
        while ( std::getline( ss, token, ',' ) ) {
            try {
                icoSizes.push_back( std::stoi( token ) );
            } catch ( ... ) {
            }
        }
    }

    static const std::vector<std::string> supported = { "png", "jpg", "bmp", "ico" };
    if ( std::find( supported.begin(), supported.end(), targetFormat ) == supported.end() ) {
        Server::sendError( res, "不支持的目标格式，仅支持: png, jpg, bmp, ico", 400 );
        return;
    }

    int width, height, channels;
    unsigned char *imageData = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc *>( file.content.data() ),
        static_cast<int>( file.content.size() ),
        &width, &height, &channels, 4 );

    if ( !imageData ) {
        LOG_WARN << "图片解码失败 filename=" << filename;
        Server::sendError( res, "无法解码图片文件，可能格式不支持或文件已损坏", 400 );
        return;
    }

    LOG_DEBUG << "图片解码成功 width=" << width << " height=" << height << " channels=" << channels << " 目标格式=" << targetFormat;

    std::vector<unsigned char> outputBuffer;
    int outputLen = 0;

    if ( targetFormat == "png" ) {
        int len = 0;
        unsigned char *pngData = stbi_write_png_to_mem(
            imageData, width * 4, width, height, 4, &len );
        if ( pngData && len > 0 ) {
            outputBuffer.assign( pngData, pngData + len );
            outputLen = len;
            STBIW_FREE( pngData );
        }
    } else if ( targetFormat == "jpg" ) {
        auto callback = []( void *context, void *data, int size ) {
            std::vector<unsigned char> *buff = static_cast<std::vector<unsigned char> *>( context );
            buff->insert( buff->end(),
                          static_cast<unsigned char *>( data ),
                          static_cast<unsigned char *>( data ) + size );
        };
        outputLen = stbi_write_jpg_to_func( callback, &outputBuffer, width, height, 4, imageData, quality );
    } else if ( targetFormat == "bmp" ) {
        auto callback = []( void *context, void *data, int size ) {
            std::vector<unsigned char> *buff = static_cast<std::vector<unsigned char> *>( context );
            buff->insert( buff->end(),
                          static_cast<unsigned char *>( data ),
                          static_cast<unsigned char *>( data ) + size );
        };
        outputLen = stbi_write_bmp_to_func( callback, &outputBuffer, width, height, 4, imageData );
    } else if ( targetFormat == "ico" ) {
        if ( icoSizes.empty() ) {
            icoSizes = { 256, 128, 96, 72, 64, 48, 32, 16 };
        }
        for ( auto &s : icoSizes ) {
            if ( s <= 0 )
                s = 256;
            else if ( s > 256 )
                s = 256;
        }
        std::sort( icoSizes.begin(), icoSizes.end(), std::greater<int>() );
        icoSizes.erase( std::unique( icoSizes.begin(), icoSizes.end() ), icoSizes.end() );

        auto dibImages = ico::createDibImages( imageData, width, height, icoSizes );
        if ( dibImages.empty() ) {
            Server::sendError( res, "ICO编码失败：无法生成任何尺寸", 500 );
            stbi_image_free( imageData );
            return;
        }

        outputBuffer = ico::buildIcoFile( dibImages );
        outputLen = static_cast<int>( outputBuffer.size() );
    }

    stbi_image_free( imageData );

    if ( outputLen <= 0 ) {
        LOG_ERROR << "图片编码失败 targetFormat=" << targetFormat;
        Server::sendError( res, "图片编码失败", 500 );
        return;
    }

    std::string mimeType;
    if ( targetFormat == "png" )
        mimeType = "image/png";
    else if ( targetFormat == "jpg" )
        mimeType = "image/jpeg";
    else if ( targetFormat == "ico" )
        mimeType = "image/x-icon";
    else
        mimeType = "image/bmp";

    std::string ext = targetFormat;

    size_t dotPos = filename.rfind( '.' );
    std::string baseName = ( dotPos != std::string::npos ) ? filename.substr( 0, dotPos ) : filename;
    std::string newFilename = baseName + "." + ext;

    LOG_INFO << "图片转换完成 original=" << filename << " size=" << file.content.size()
             << " converted=" << newFilename << " size=" << outputBuffer.size();

    res.set_content( std::string( reinterpret_cast<char *>( outputBuffer.data() ), outputBuffer.size() ), mimeType );
    res.set_header( "Content-Disposition", "attachment; filename=\"" + newFilename + "\"" );
}

void registerToolRoutes( httplib::Server &svr ) {
    svr.Get( "/api/tools/ip", ipLookup );
    svr.Post( "/api/tools/proxy", httpProxy );
    svr.Get( "/api/tools/qrcode", qrcode );
    svr.Post( "/api/tools/image/convert", imageConvert );
    LOG_DEBUG << "已注册 4 个工具路由";
}

} // namespace routes::tools
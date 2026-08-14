#include "routes/CertTool.h"
#include "common/Config.h"
#include "common/EventLoop.h"
#include "common/Logger.hpp"
#include "core/Server.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace routes::cert {

// 获取 openssl 信息
static Server::json getOpensslInfo() {
    Server::json j = Server::json::object();
    std::string openssl = Config::getOpensslPath();
    j["opensslPath"] = openssl;
    j["available"] = false;
    j["version"] = "";
    j["error"] = "";

    if ( openssl.empty() ) {
        j["error"] = "未配置 openssl 路径(--openssl 或配置 opensslPath)";
        return j;
    }

    std::error_code ec;
    if ( !fs::exists( openssl, ec ) ) {
        j["error"] = "openssl 不存在: " + openssl;
        return j;
    }

    auto ver = EventLoop::runProcessSync( { openssl, "version" }, fs::current_path() );
    if ( !ver.started ) {
        j["error"] = "启动 openssl 失败";
        return j;
    }
    if ( !ver.output.empty() ) {
        // 取首行作为版本号
        size_t nl = ver.output.find( '\n' );
        j["version"] = ( nl != std::string::npos ) ? ver.output.substr( 0, nl ) : ver.output;
        j["available"] = true;
    }
    return j;
}

static void certInfo( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    Server::json j = getOpensslInfo();
    j["success"] = true;
    Server::sendJson( res, j );
}

// 字符串去除首尾空白
static std::string trim( const std::string &s ) {
    size_t a = 0, b = s.size();
    while ( a < b && ( s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n' ) )
        ++a;
    while ( b > a && ( s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n' ) )
        --b;
    return s.substr( a, b - a );
}

static std::string jsonString( const Server::json &j, const char *key, const std::string &def = "" ) {
    if ( !j.is_object() || !j.contains( key ) || !j[key].is_string() )
        return def;
    return j[key].get<std::string>();
}
static int jsonInt( const Server::json &j, const char *key, int def ) {
    if ( !j.is_object() || !j.contains( key ) || !j[key].is_number_integer() )
        return def;
    return j[key].get<int>();
}
static bool jsonBool( const Server::json &j, const char *key, bool def ) {
    if ( !j.is_object() || !j.contains( key ) || !j[key].is_boolean() )
        return def;
    return j[key].get<bool>();
}

// 转义 OpenSSL 配置项中的特殊字符（仅处理换行与双引号）
static std::string escapeConf( const std::string &s ) {
    std::string out;
    out.reserve( s.size() );
    for ( char c : s ) {
        switch ( c ) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

// 通过临时 openssl.cnf 注入 subjectAltName（IP / DNS）
// 注意：subjectAltName 不能放在 [v3_req]（CSR 扩展），必须只放在 X.509 扩展段
// altNamesIp: IP 条目数组；altNamesDns: DNS 条目数组
static bool writeOpensslConf( const std::string &confPath,
                              const std::string &commonName,
                              const std::vector<std::string> &altNamesIp,
                              const std::vector<std::string> &altNamesDns ) {
    std::ostringstream ss;
    ss << "[req]\n";
    ss << "default_bits       = 2048\n";
    ss << "prompt             = no\n";
    ss << "default_md         = sha256\n";
    ss << "distinguished_name = dn\n";
    // CSR 不带 subjectAltName —— SAN 只能放在证书的 X.509 扩展里
    ss << "x509_extensions    = v3_ca\n";
    ss << "\n";
    ss << "[dn]\n";
    ss << "CN = " << escapeConf( commonName ) << "\n";
    ss << "\n";
    // [v3_req]：仅放 CSR 允许的扩展（不含 subjectAltName）
    ss << "[v3_req]\n";
    ss << "basicConstraints       = CA:FALSE\n";
    ss << "keyUsage               = digitalSignature, keyEncipherment\n";
    ss << "extendedKeyUsage       = serverAuth, clientAuth\n";
    ss << "\n";
    // [v3_ca]：X.509 证书扩展，subjectAltName 放在这里
    ss << "[v3_ca]\n";
    ss << "basicConstraints       = critical, CA:FALSE\n";
    ss << "keyUsage               = critical, digitalSignature, keyEncipherment\n";
    ss << "extendedKeyUsage       = serverAuth, clientAuth\n";
    ss << "subjectAltName         = @alt_names\n";
    ss << "\n";
    ss << "[alt_names]\n";
    int idx = 1;
    for ( const auto &ip : altNamesIp ) {
        if ( ip.empty() )
            continue;
        ss << "IP." << idx++ << " = " << escapeConf( ip ) << "\n";
    }
    for ( const auto &dns : altNamesDns ) {
        if ( dns.empty() )
            continue;
        ss << "DNS." << idx++ << " = " << escapeConf( dns ) << "\n";
    }

    std::ofstream f( confPath, std::ios::binary | std::ios::trunc );
    if ( !f )
        return false;
    f << ss.str();
    return f.good();
}

// 构造 openssl x509 颁发命令
static std::vector<std::string> buildX509Args( const std::string &openssl,
                                               const std::string &csrPath,
                                               const std::string &keyPath,
                                               const std::string &certPath,
                                               const std::string &confPath,
                                               int days,
                                               const std::string &digest ) {
    std::vector<std::string> args = {
        openssl, "x509", "-req",
        "-in", csrPath,
        "-signkey", keyPath, // 自签：用私钥作为签发者（注意是 key，不是 csr）
        "-out", certPath,
        "-days", std::to_string( days > 0 ? days : 365 ),
        "-sha256" };
    if ( !digest.empty() ) {
        // 覆盖默认 sha256
        for ( auto it = args.begin(); it != args.end(); ++it ) {
            if ( *it == "-sha256" ) {
                args.erase( it );
                break;
            }
        }
        args.push_back( "-" + digest );
    }
    args.push_back( "-extfile" );
    args.push_back( confPath );
    return args;
}

// 构造 openssl req 命令生成 CSR + 私钥
static std::vector<std::string> buildReqArgs( const std::string &openssl,
                                              const std::string &keyPath,
                                              const std::string &csrPath,
                                              const std::string &confPath,
                                              int keyBits,
                                              const std::string &digest ) {
    std::vector<std::string> args = {
        openssl, "req", "-new",
        "-newkey", "rsa:" + std::to_string( keyBits > 0 ? keyBits : 2048 ),
        "-nodes",
        "-keyout", keyPath,
        "-out", csrPath,
        "-config", confPath };
    if ( !digest.empty() ) {
        args.push_back( "-" + digest );
    }
    return args;
}

// POST /api/cert/generate
// body: {
//   outputDir,                 // 输出目录（必填）
//   baseName,                  // 文件前缀（默认 server）
//   commonName,                // CN（必填）
//   organization,              // O
//   organizationalUnit,        // OU
//   country,                   // C
//   state,                     // ST
//   locality,                  // L
//   days,                      // 有效天数（默认 365）
//   keyBits,                   // 私钥位数（默认 2048）
//   digest,                    // 摘要算法（默认 sha256）
//   altNamesIp, altNamesDns,   // SAN 数组
//   overwrite                  // 存在时是否覆盖
// }
static void certGenerate( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    auto body = Server::parseBody( req );
    if ( body.is_null() || !body.is_object() ) {
        return Server::sendError( res, "无效的请求体", 400 );
    }

    std::string openssl = Config::getOpensslPath();
    if ( openssl.empty() ) {
        return Server::sendError( res, "未配置 openssl 路径", 400 );
    }
    std::error_code ec;
    if ( !fs::exists( openssl, ec ) ) {
        return Server::sendError( res, "openssl 不存在: " + openssl, 400 );
    }

    std::string outputDir = trim( jsonString( body, "outputDir" ) );
    std::string baseName = trim( jsonString( body, "baseName", "server" ) );
    std::string commonName = trim( jsonString( body, "commonName" ) );
    if ( outputDir.empty() ) {
        return Server::sendError( res, "缺少输出目录(outputDir)", 400 );
    }
    if ( commonName.empty() ) {
        return Server::sendError( res, "缺少 commonName(CN)", 400 );
    }
    if ( baseName.empty() ) {
        return Server::sendError( res, "baseName 不能为空", 400 );
    }
    // 简单校验 baseName 防止路径穿越
    if ( baseName.find( '/' ) != std::string::npos || baseName.find( '\\' ) != std::string::npos ||
         baseName.find( ".." ) != std::string::npos ) {
        return Server::sendError( res, "baseName 非法", 400 );
    }

    // 输出目录不存在则创建
    std::error_code ecDir;
    if ( !fs::exists( outputDir, ecDir ) ) {
        std::error_code ec2;
        fs::create_directories( outputDir, ec2 );
        if ( ec2 ) {
            return Server::sendError( res, "无法创建输出目录: " + ec2.message(), 400 );
        }
    } else if ( !fs::is_directory( outputDir, ecDir ) ) {
        return Server::sendError( res, "输出路径不是目录", 400 );
    }

    int days = jsonInt( body, "days", 365 );
    int keyBits = jsonInt( body, "keyBits", 2048 );
    std::string digest = trim( jsonString( body, "digest", "sha256" ) );
    bool overwrite = jsonBool( body, "overwrite", false );

    std::vector<std::string> altNamesIp;
    std::vector<std::string> altNamesDns;
    if ( body.contains( "altNamesIp" ) && body["altNamesIp"].is_array() ) {
        for ( const auto &v : body["altNamesIp"] ) {
            if ( v.is_string() ) {
                std::string s = trim( v.get<std::string>() );
                if ( !s.empty() )
                    altNamesIp.push_back( s );
            }
        }
    }
    if ( body.contains( "altNamesDns" ) && body["altNamesDns"].is_array() ) {
        for ( const auto &v : body["altNamesDns"] ) {
            if ( v.is_string() ) {
                std::string s = trim( v.get<std::string>() );
                if ( !s.empty() )
                    altNamesDns.push_back( s );
            }
        }
    }

    // 拼装目标文件路径
    fs::path outDir( outputDir );
    fs::path keyPath = outDir / ( baseName + ".key" );
    fs::path certPath = outDir / ( baseName + ".crt" );
    fs::path csrPath = outDir / ( baseName + ".csr" );
    fs::path confPath = outDir / ( baseName + ".cnf" );

    if ( !overwrite ) {
        if ( fs::exists( keyPath, ec ) || fs::exists( certPath, ec ) || fs::exists( csrPath, ec ) ) {
            return Server::sendError( res, "目标已存在，开启「覆盖」后可重新生成", 400 );
        }
    } else {
        // 允许覆盖：删除旧文件
        std::error_code rmec;
        fs::remove( keyPath, rmec );
        fs::remove( certPath, rmec );
        fs::remove( csrPath, rmec );
        fs::remove( confPath, rmec );
    }

    // 生成 openssl 配置文件（包含 SAN）
    if ( !writeOpensslConf( confPath.string(), commonName, altNamesIp, altNamesDns ) ) {
        return Server::sendError( res, "写入 openssl 配置失败", 500 );
    }

    // 在 openssl.cnf 中以环境变量的方式附加主体（O/OU/C/ST/L）
    // 为简单起见，使用额外 -subj 参数
    std::string subject = "/CN=" + commonName;
    std::string country = trim( jsonString( body, "country" ) );
    std::string state = trim( jsonString( body, "state" ) );
    std::string locality = trim( jsonString( body, "locality" ) );
    std::string organization = trim( jsonString( body, "organization" ) );
    std::string organizationalUnit = trim( jsonString( body, "organizationalUnit" ) );
    if ( !country.empty() )
        subject = "/C=" + country + subject;
    if ( !state.empty() )
        subject = "/ST=" + state + subject;
    if ( !locality.empty() )
        subject = "/L=" + locality + subject;
    if ( !organization.empty() )
        subject = "/O=" + organization + subject;
    if ( !organizationalUnit.empty() )
        subject = "/OU=" + organizationalUnit + subject;

    // 1) 生成 CSR 与私钥
    auto reqArgs = buildReqArgs( openssl, keyPath.string(), csrPath.string(), confPath.string(), keyBits, digest );
    reqArgs.insert( reqArgs.begin() + 3, "-subj" );
    reqArgs.insert( reqArgs.begin() + 4, subject );
    auto reqRes = EventLoop::runProcessSync( reqArgs, fs::current_path() );
    if ( !reqRes.started ) {
        std::error_code rmec;
        fs::remove( confPath, rmec );
        return Server::sendError( res, "启动 openssl req 失败", 500 );
    }
    if ( reqRes.exitCode != 0 ) {
        std::string err = reqRes.error.empty() ? reqRes.output : reqRes.error;
        std::error_code rmec;
        fs::remove( confPath, rmec );
        fs::remove( keyPath, rmec );
        fs::remove( csrPath, rmec );
        return Server::sendError( res, "openssl req 失败(exit=" + std::to_string( reqRes.exitCode ) + "): " + err, 500 );
    }

    // 2) 用自己的私钥对 CSR 自签，生成证书
    auto x509Args = buildX509Args( openssl, csrPath.string(), keyPath.string(), certPath.string(), confPath.string(), days, digest );
    auto x509Res = EventLoop::runProcessSync( x509Args, fs::current_path() );
    // 清理临时文件（CSR 与 conf）
    {
        std::error_code rmec;
        fs::remove( csrPath, rmec );
        fs::remove( confPath, rmec );
    }
    if ( !x509Res.started ) {
        std::error_code rmec;
        fs::remove( keyPath, rmec );
        return Server::sendError( res, "启动 openssl x509 失败", 500 );
    }
    if ( x509Res.exitCode != 0 ) {
        std::string err = x509Res.error.empty() ? x509Res.output : x509Res.error;
        std::error_code rmec;
        fs::remove( keyPath, rmec );
        return Server::sendError( res, "openssl x509 失败(exit=" + std::to_string( x509Res.exitCode ) + "): " + err, 500 );
    }

    // 收集输出文件信息
    Server::json files = Server::json::array();
    auto pushFile = [&]( const fs::path &p, const std::string &kind ) {
        if ( !fs::exists( p ) )
            return;
        std::error_code ec3;
        auto sz = fs::file_size( p, ec3 );
        Server::json fj;
        fj["kind"] = kind;
        fj["name"] = p.filename().string();
        fj["path"] = p.string();
        fj["size"] = static_cast<int64_t>( sz );
        files.push_back( fj );
    };
    pushFile( certPath, "cert" );
    pushFile( keyPath, "key" );

    LOG_INFO << "自签名证书生成成功 dir=" << outputDir << " cn=" << commonName;

    Server::sendJson( res, { { "success", true },
                             { "commonName", commonName },
                             { "outputDir", outputDir },
                             { "files", files },
                             { "days", days },
                             { "keyBits", keyBits },
                             { "digest", digest.empty() ? std::string( "sha256" ) : digest } } );
}

void registerCertRoutes( httplib::Server &svr ) {
    svr.Get( "/api/cert/info", certInfo );
    svr.Post( "/api/cert/generate", certGenerate );
    LOG_DEBUG << "已注册自签名证书工具路由";
}

} // namespace routes::cert

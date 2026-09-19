#include "routes/CertTool.h"
#include "common/Logger.hpp"
#include "core/CertGenerator.h"
#include "core/Server.h"

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace routes::cert {

static Server::json getOpensslInfo() {
    Server::json j = Server::json::object();
    j["opensslPath"] = "builtin";
    j["available"] = CertGenerator::isOpensslAvailable();
    j["version"] = CertGenerator::getOpensslVersion();
    j["error"] = "";
    return j;
}

static void certInfo( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    Server::json j = getOpensslInfo();
    j["success"] = true;
    Server::sendJson( res, j );
}

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

static void certGenerate( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;
    auto body = Server::parseBody( req );
    if ( body.is_null() || !body.is_object() ) {
        return Server::sendError( res, "无效的请求体", 400 );
    }

    if ( !CertGenerator::isOpensslAvailable() ) {
        return Server::sendError( res, "OpenSSL 库不可用", 400 );
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

    CertGenerator::CertParams params;
    params.commonName = commonName;
    params.country = trim( jsonString( body, "country" ) );
    params.state = trim( jsonString( body, "state" ) );
    params.locality = trim( jsonString( body, "locality" ) );
    params.organization = trim( jsonString( body, "organization" ) );
    params.organizationalUnit = trim( jsonString( body, "organizationalUnit" ) );
    params.days = jsonInt( body, "days", 365 );
    params.keyBits = jsonInt( body, "keyBits", 2048 );

    if ( body.contains( "altNamesIp" ) && body["altNamesIp"].is_array() ) {
        for ( const auto &v : body["altNamesIp"] ) {
            if ( v.is_string() ) {
                std::string s = trim( v.get<std::string>() );
                if ( !s.empty() )
                    params.altNamesIp.push_back( s );
            }
        }
    }
    if ( body.contains( "altNamesDns" ) && body["altNamesDns"].is_array() ) {
        for ( const auto &v : body["altNamesDns"] ) {
            if ( v.is_string() ) {
                std::string s = trim( v.get<std::string>() );
                if ( !s.empty() )
                    params.altNamesDns.push_back( s );
            }
        }
    }

    bool overwrite = jsonBool( body, "overwrite", false );
    std::string digest = trim( jsonString( body, "digest", "sha256" ) );

    auto result = CertGenerator::generate( params, outputDir, baseName, overwrite );
    if ( !result.success ) {
        return Server::sendError( res, result.error, 500 );
    }

    Server::json files = Server::json::array();
    auto pushFile = [&]( const std::string &path, const std::string &kind ) {
        fs::path p( path );
        std::error_code ec;
        if ( !fs::exists( p, ec ) )
            return;
        auto sz = fs::file_size( p, ec );
        Server::json fj;
        fj["kind"] = kind;
        fj["name"] = p.filename().string();
        fj["path"] = p.string();
        fj["size"] = static_cast<int64_t>( sz );
        files.push_back( fj );
    };
    pushFile( result.certPath, "cert" );
    pushFile( result.keyPath, "key" );

    Server::sendJson( res, { { "success", true },
                             { "commonName", commonName },
                             { "outputDir", outputDir },
                             { "files", files },
                             { "days", params.days },
                             { "keyBits", params.keyBits },
                             { "digest", digest.empty() ? std::string( "sha256" ) : digest } } );
}

void registerCertRoutes( httplib::Server &svr ) {
    svr.Get( "/api/cert/info", certInfo );
    svr.Post( "/api/cert/generate", certGenerate );
    LOG_DEBUG << "已注册自签名证书工具路由";
}

} // namespace routes::cert
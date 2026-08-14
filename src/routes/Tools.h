#ifndef ROUTES_TOOLS_H
#define ROUTES_TOOLS_H

#include <httplib.h>
#include <string>
#include <vector>
#include <regex>
#include <chrono>
#include <map>
#include <mutex>
#include <algorithm>
#include "core/Utils.h"
#include "core/Server.h"

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

void registerToolRoutes( httplib::Server &svr );

bool parseUrl( const std::string &url, ParsedUrl &out );

bool isSensitivePort( int port );

bool isUrlSafe( const std::string &url, std::string &err );

void cacheIpInfo( const std::string &ip, const Server::json &data, const std::string &source );

bool getCachedIpInfo( const std::string &ip, IpCacheEntry &out );

bool isValidIpv4( const std::string &ip );

bool fetchIpSource( const std::string &url, Server::json &result, std::string &err );

std::string generateQrSvg( const std::string &text, int scale, int border );

void ipLookup( const httplib::Request &req, httplib::Response &res );

void httpProxy( const httplib::Request &req, httplib::Response &res );

void qrcode( const httplib::Request &req, httplib::Response &res );

void imageConvert( const httplib::Request &req, httplib::Response &res );

namespace ico {

struct DibImage {
    int width;
    int height;
    std::vector<unsigned char> data;
};

std::vector<DibImage> createDibImages(
    const unsigned char *srcPixels, int srcWidth, int srcHeight,
    const std::vector<int> &sizes );

std::vector<unsigned char> buildIcoFile( const std::vector<DibImage> &images );

}

} // namespace routes::tools

#endif // ROUTES_TOOLS_H
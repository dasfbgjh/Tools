#ifndef SERVER_H
#define SERVER_H

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <tuple>

class Server {

private:
    std::shared_ptr<httplib::Server> m_server;

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    std::shared_ptr<httplib::SSLServer> m_sslServer;
#endif
    static const char *resourcePrefix;

private:
    bool resourceExists( const std::string &resName );
    bool serveResource( httplib::Response &res, const std::string &resName );
    void redirectPage( httplib::Response &res, const std::string &path );
    void serveStatic( const httplib::Request &req, httplib::Response &res, const std::string &path );
    void registerhRoutes( httplib::Server &server );

public:
    using json = nlohmann::json;

    Server();
    ~Server();

    void startHttp( int port );
    void startHttps( int port, const std::string &certPath, const std::string &keyPath );
    std::tuple<std::thread, std::thread> listen();
    void stop();

    static void sendJson( httplib::Response &res, const json &j, int status = 200 );
    static void sendError( httplib::Response &res, const std::string &msg, int status );
    static json parseBody( const httplib::Request &req );
    static std::string queryParam( const httplib::Request &req, const std::string &name );
    static std::string contentType( const std::filesystem::path &path );
    static std::string staticResource( const std::string &resName );
    static bool isLocalhost( const httplib::Request &req );
    static bool guardLocalhost( const httplib::Request &req, httplib::Response &res );
};

#endif
// SERVER_H
#include "routes/GameTool.h"
#include "common/App.h"
#include "common/Logger.hpp"
#include "core/Server.h"
#include "core/Utils.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace routes::game {

// ===== 游戏目录扫描 =====

struct GameEntry {
    std::string id;
    std::string name;
    std::string entryFile;
};

static std::string gameDataDir() {
#ifdef RESOURCE_PATH
    return std::string( RESOURCE_PATH ) + "/html/game";
#else
    return "";
#endif
}

static std::string guessGameName( const std::string &id ) {
    std::string name;
    bool upper = true;
    for ( char c : id ) {
        if ( c == '_' || c == '-' || c == ' ' ) {
            name += ' ';
            upper = true;
        } else if ( upper ) {
            name += static_cast<char>( std::toupper( static_cast<unsigned char>( c ) ) );
            upper = false;
        } else {
            name += c;
        }
    }
    return name;
}

static std::vector<GameEntry> scanGames() {
    std::string base = gameDataDir();
    if ( base.empty() )
        return {};

    std::error_code ec;
    if ( !fs::is_directory( base, ec ) )
        return {};

    static const char *entryNames[] = { "index.html", "index.htm" };
    std::vector<GameEntry> games;

    for ( auto &entry : fs::directory_iterator( base, ec ) ) {
        if ( ec )
            break;
        if ( !entry.is_directory( ec ) )
            continue;
        std::string dirName = entry.path().filename().string();
        if ( !dirName.empty() && dirName[0] == '.' )
            continue;

        std::string found;
        for ( auto &en : entryNames ) {
            if ( fs::is_regular_file( entry.path() / en, ec ) ) {
                found = en;
                break;
            }
        }
        if ( found.empty() )
            continue;

        GameEntry g;
        g.id = dirName;
        g.name = guessGameName( dirName );
        g.entryFile = found;
        games.push_back( std::move( g ) );
    }

    std::sort( games.begin(), games.end(),
               []( const GameEntry &a, const GameEntry &b ) { return a.id < b.id; } );
    return games;
}

// ===== 内部 HTTP 服务(提供游戏静态文件) =====
namespace {

struct GameServerState {
    std::mutex mtx;
    std::string gameDir;
    bool set = false;
};

GameServerState g_state;
std::unique_ptr<httplib::Server> g_server;
std::thread g_thread;
std::mutex g_lifecycleMtx;
std::atomic<int> g_port{ 0 };
std::atomic<bool> g_running{ false };
std::string g_baseUrl;

static fs::path resolveFile( const std::string &urlPath ) {
    std::string decoded = utils::urlDecode( urlPath );
    while ( !decoded.empty() && ( decoded.front() == '/' || decoded.front() == '\\' ) )
        decoded.erase( decoded.begin() );
    if ( decoded.empty() )
        return {};

    std::string rootStr;
    {
        std::lock_guard<std::mutex> lock( g_state.mtx );
        if ( !g_state.set || g_state.gameDir.empty() )
            return {};
        rootStr = g_state.gameDir;
    }

    std::error_code ec;
    fs::path rootCanonical = fs::weakly_canonical( fs::path( rootStr ), ec );
    if ( ec )
        return {};
    fs::path filePath = fs::weakly_canonical( rootCanonical / decoded, ec );
    if ( ec )
        return {};

    std::string absStr = filePath.string();
    std::string rootCanonStr = rootCanonical.string();
    if ( !utils::fs::isWithin( rootCanonStr, absStr ) )
        return {};

    return filePath;
}

static void setupGameServerRoutes( httplib::Server &svr ) {
    svr.Get( "/__health", []( const httplib::Request &, httplib::Response &res ) {
        res.set_content( "OK", "text/plain" );
    } );

    svr.Get( R"(.*)", []( const httplib::Request &req, httplib::Response &res ) {
        if ( req.path == "/__health" )
            return;

        fs::path filePath = resolveFile( req.path );
        if ( filePath.empty() ) {
            res.status = 404;
            res.set_content( "Not Found", "text/plain; charset=utf-8" );
            return;
        }

        std::error_code ec;
        if ( !fs::is_regular_file( filePath, ec ) ) {
            res.status = 404;
            res.set_content( "Not Found", "text/plain; charset=utf-8" );
            return;
        }

        std::string content;
        if ( !utils::fs::readFile( filePath.string(), content ) ) {
            res.status = 500;
            res.set_content( "Read Error", "text/plain; charset=utf-8" );
            return;
        }
        res.set_content( content, Server::contentType( filePath ) );
    } );
}

static void startGameHttpServerLocked() {
    if ( g_server )
        return;

    auto server = std::make_unique<httplib::Server>();
    setupGameServerRoutes( *server );

    int port = server->bind_to_any_port( "127.0.0.1" );
    if ( port <= 0 ) {
        throw std::runtime_error( "无法绑定游戏服务端口" );
    }

    g_port = port;
    g_baseUrl = "http://127.0.0.1:" + std::to_string( port ) + "/";
    g_server = std::move( server );
    g_running = true;

    g_thread = std::thread( []() {
        try {
            if ( g_server )
                g_server->listen_after_bind();
        } catch ( ... ) {
        }
        g_running = false;
    } );
}

static void stopGameHttpServerLocked() {
    if ( g_server ) {
        g_server->stop();
    }
    if ( g_thread.joinable() ) {
        g_thread.join();
    }
    g_server.reset();
    g_port = 0;
    g_baseUrl.clear();
    g_running = false;

    {
        std::lock_guard<std::mutex> lock( g_state.mtx );
        g_state.set = false;
        g_state.gameDir.clear();
    }
}

} // anonymous namespace

void shutdownGameHttpServer() {
    std::lock_guard<std::mutex> lock( g_lifecycleMtx );
    stopGameHttpServerLocked();
}

// ===== API 路由 =====

static void listGames( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;

    auto games = scanGames();
    Server::json arr = Server::json::array();
    for ( auto &g : games ) {
        Server::json j;
        j["id"] = g.id;
        j["name"] = g.name;
        j["entryFile"] = g.entryFile;
        arr.push_back( j );
    }
    Server::sendJson( res, { { "success", true }, { "games", arr } } );
}

static void startServer( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;

    std::string base = gameDataDir();
    if ( base.empty() )
        return Server::sendError( res, "游戏数据目录不可用", 500 );

    std::error_code ec;
    if ( !fs::is_directory( base, ec ) )
        return Server::sendError( res, "游戏数据目录不存在", 400 );

    {
        std::lock_guard<std::mutex> lock( g_lifecycleMtx );
        if ( !g_server ) {
            try {
                startGameHttpServerLocked();
            } catch ( const std::exception &e ) {
                return Server::sendError( res, std::string( "启动游戏服务失败: " ) + e.what(), 500 );
            }
        }
        {
            std::lock_guard<std::mutex> sl( g_state.mtx );
            g_state.gameDir = base;
            g_state.set = true;
        }
    }

    Server::json j;
    j["success"] = true;
    j["baseUrl"] = g_baseUrl;
    j["port"] = g_port.load();
    Server::sendJson( res, j );
}

static void getServerStatus( const httplib::Request &req, httplib::Response &res ) {
    if ( Server::guardLocalhost( req, res ) )
        return;

    Server::json j;
    j["success"] = true;
    j["running"] = g_running.load();
    j["port"] = g_port.load();
    j["baseUrl"] = g_baseUrl;
    Server::sendJson( res, j );
}

void registerGameRoutes( httplib::Server &svr ) {
    svr.Get( "/api/game/list", listGames );
    svr.Post( "/api/game/start", startServer );
    svr.Get( "/api/game/status", getServerStatus );

    LOG_DEBUG << "已注册游戏工具路由";
}

} // namespace routes::game
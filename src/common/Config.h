#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <set>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "CLI11.hpp"

class Config {
public:
    using json = nlohmann::json;

private:
    // 不可配置项
    static std::filesystem::path appPath;
    static std::string databasePath;
    static bool enableContextmenu;
    static bool enableAutoBoot;
    static std::pair<std::string, std::vector<std::string>> pathParameter;

    // 可配置项
    static std::string tempPath;
    static std::string uploadFilePath;
    static size_t maxUploadFileSize;
    static size_t maxPdfSize;
    static int inviteCodeDurationSEC;
    static int httpServerPort;
    static int httpsServerPort;
    static bool enableHttps;
    static std::string sslCertPath;
    static std::string sslKeyPath;
    static std::vector<std::string> bootParameter;
    static int logLevel;
    static int logFileMode; // 0=off, 1=single file, 2=multi file (by date)
    static std::string logFilePath;
    static std::string pdfToolPath;
    static std::string ffmpegPath;
    static std::string opensslPath;

    static int parseConfig( int argc, char *argv[] );

public:
    static void init( int argc, char *argv[] );

    // flag 0:配置转json 1:从json加载配置项
    static void configJson( json::array_t &config, char flag = 0 );

    static const std::string getAppPath();
    static const std::string getAppBaseName();
    static const std::string getAppFullPath();

    static const std::string &getDatabasePath();
    static const std::string &getTempPath();
    static const std::string &getUploadFilePath();

    static size_t getMaxUploadFileSize();

    static size_t getMaxPdfSize();

    static int getInviteCodeDurationSEC();

    static int getHttpServerPort();
    static int getHttpsServerPort();
    static bool getEnableHttps();
    static const std::string &getSslCertPath();
    static const std::string &getSslKeyPath();

    static const std::vector<std::string> &getBootParameter();

    // 路径参数(启动参数中提取的路径参数)
    static std::string setPathParameter( const std::vector<std::string> &args );
    static std::vector<std::string> getPathParameter( const std::string &id );

    static bool getEnableContextmenu( bool reread = false );
    static bool setEnableContextmenu( bool enable );

    static bool getEnableAutoBoot( bool reread = false );
    static bool setEnableAutoBoot( bool enable );

    static int getLogLevel();
    static int getLogFileMode();
    static const std::string &getLogFilePath();

    static const std::string &getPdfToolPath();
    static const std::string &getFfmpegPath();
    static const std::string &getOpensslPath();
};

#endif // CONFIG_H

#include "common/Logger.hpp"
#include "common/Config.h"
#include "common/App.h"
#include "AppInfo.h"

int main( int argc, char *argv[] ) {
    AppExtension::setOutputGBKCode( true );

    auto &logger = Logger::getInstance();
    logger.setOutputToConsole( true );
    logger.setOutputToFile( Logger::FileMode::FILE_OFF );
    logger.setLevel( Logger::Level::INFO );

    LOG_INFO << "========== 应用启动 ==========";
    LOG_INFO << "应用版本: " APP_VERSION;
    LOG_DEBUG << "命令行参数个数: " << argc;

    try {
        Config::init( argc, argv );
        LOG_INFO << "配置解析完成";
    } catch ( const std::exception &e ) {
        LOG_ERROR << "配置解析异常: " << e.what();
        return 1;
    }

    if ( !App::isBoot() ) {
        LOG_INFO << "检测到已有实例在运行，退出当前进程";
        return 0;
    }

    LOG_INFO << "开始初始化应用...";
    App app;
    if ( !app.init() ) {
        LOG_ERROR << "应用初始化失败";
        return 1;
    }
    LOG_INFO << "应用初始化成功";

    LOG_INFO << "进入主事件循环";
    int code = app.exec();
    LOG_INFO << "应用退出，返回码:" << code;
    LOG_INFO << "========== 应用结束 ==========";
    return code;
}

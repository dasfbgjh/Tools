#ifndef WEBVIEW_WRAPPER_H
#define WEBVIEW_WRAPPER_H

#include <thread>
#include <memory>
#include <list>
#include <mutex>
#include <functional>

#include <nlohmann/json.hpp>
#include "webview/webview.h"

class WebviewWrapper {
private:
    enum class ShowState {
        Show_Maximized,
        Show_Minimized,
        Show_Normal,
        Hide,
    };
    std::shared_ptr<std::thread> m_thread;

    std::list<std::function<void( webview::webview &view )>> m_eventHandlers;
    std::mutex m_eventMutex;

public:
    WebviewWrapper();
    ~WebviewWrapper();

    bool start();
    void stop();
    void navigate( const std::string &url );
    void setWindowRect( int x, int y, int w, int h );
    void setCenter();
    void showMaximized();
    void showMinimized();
    void showNormal();
    void hide();
    void runJavascript( const std::string &js );

private:
    static void initLog( webview::webview &view, std::string &init_js );
    static void initWindow( webview::webview &view );
    void initEventQueue( webview::webview &view, std::string &init_js );
    static void setWindowState( webview::webview &view, ShowState type );

    void postEvent( std::function<void( webview::webview &view )> );
};

#endif
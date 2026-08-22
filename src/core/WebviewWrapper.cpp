#include "WebviewWrapper.h"
#include "common/Logger.hpp"
#include "common/App.h"
#include "AppInfo.h"

WebviewWrapper::WebviewWrapper() : m_thread( nullptr ) {
}

WebviewWrapper::~WebviewWrapper() {
    stop();
}

bool WebviewWrapper::start() {
    if ( m_thread ) {
        showNormal();
        return false;
    }
    m_thread = std::make_shared<std::thread>( [this]() {
        webview::webview view( APP_BUILD_RELEASE == 0, nullptr );
        std::string init_js;
        initLog( view, init_js );
        initEventQueue( view, init_js );
        bindings( view );
        initWindow( view );
        view.init( init_js );

        view.set_html( Server::staticResource( "/webview/index.html", "" ) );
        view.set_size( 1200, 800, WEBVIEW_HINT_NONE );
        view.run();
    } );
    setCenter();
    return true;
}

void WebviewWrapper::stop() {
    if ( m_thread ) {
        postEvent( []( webview::webview &view ) {
            view.terminate();
        } );
        m_thread->join();
        m_thread = nullptr;
    }
}

void WebviewWrapper::navigate( const std::string &url ) {
    postEvent( [url]( webview::webview &view ) {
        LOG_DEBUG << "Navigate: " << url;
        view.navigate( url );
    } );
}

void WebviewWrapper::setWindowRect( int x, int y, int w, int h ) {
    postEvent( [x, y, w, h]( webview::webview &view ) {
#ifdef _WIN32
        HWND hwnd = (HWND)view.window().value();
        if ( hwnd ) {
            UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED;
            SetWindowPos( hwnd, NULL, x, y, w, h, flags );
        }
#endif
    } );
}

void WebviewWrapper::setCenter() {
#ifdef _WIN32
    RECT rcWorkArea;
    SystemParametersInfo( SPI_GETWORKAREA, 0, &rcWorkArea, 0 );
    int workAreaWidth = rcWorkArea.right - rcWorkArea.left;
    int workAreaHeight = rcWorkArea.bottom - rcWorkArea.top;

    int w = workAreaWidth * 0.8;
    int h = workAreaHeight * 0.9;
    int x = rcWorkArea.left + ( workAreaWidth - w ) * 0.5;
    int y = rcWorkArea.top + ( workAreaHeight - h ) * 0.5;

    setWindowRect( x, y, w, h );
#endif
}

void WebviewWrapper::showMaximized() {
    postEvent( []( webview::webview &view ) {
        setWindowState( view, ShowState::Show_Maximized );
    } );
}

void WebviewWrapper::showMinimized() {
    postEvent( []( webview::webview &view ) {
        setWindowState( view, ShowState::Show_Minimized );
    } );
}

void WebviewWrapper::showNormal() {
    postEvent( []( webview::webview &view ) {
        setWindowState( view, ShowState::Show_Normal );
    } );
}

void WebviewWrapper::hide() {
    postEvent( []( webview::webview &view ) {
        setWindowState( view, ShowState::Hide );
    } );
}

void WebviewWrapper::runJavascript( const std::string &js ) {
    postEvent( [js]( webview::webview &view ) {
        view.eval( js );
    } );
}

void WebviewWrapper::initLog( webview::webview &view, std::string &init_js ) {
    view.bind( "__log", []( std::string s ) -> std::string {
        auto json = nlohmann::json::parse( s );
        auto level = json[0].get<int>();
        auto msg = json[1].get<std::string>();
        switch ( static_cast<Logger::Level>( level ) ) {
        case Logger::Level::DEBUG:
            LOG_DEBUG << msg;
            break;
        case Logger::Level::ERR:
            LOG_ERROR << msg;
            break;
        case Logger::Level::WARN:
            LOG_WARN << msg;
            break;
        case Logger::Level::INFO:
            LOG_INFO << msg;
            break;
        default:
            LOG_INFO << msg;
            break;
        }
        return "{}";
    } );

    auto buildLog = []( int level, const std::string &name ) -> std::string {
        return "console." + name + " = function (...args) {\n__log(" +
               std::to_string( level ) + " , args.join(' '))\n}\n";
    };
    std::string js;
    js += buildLog( static_cast<int>( Logger::Level::DEBUG ), "log" );
    js += buildLog( static_cast<int>( Logger::Level::ERR ), "error" );
    js += buildLog( static_cast<int>( Logger::Level::WARN ), "warn" );
    js += buildLog( static_cast<int>( Logger::Level::INFO ), "info" );
    js += buildLog( static_cast<int>( Logger::Level::DEBUG ), "debug" );
    init_js.append( js );
}

void WebviewWrapper::initWindow( webview::webview &view ) {
#ifdef _WIN32
    static WNDPROC originalWndProc = nullptr;
    HWND hwnd = (HWND)view.window().value();
    if ( hwnd == nullptr )
        return;
    if ( !IsWindow( hwnd ) )
        return;

    typedef LRESULT CALLBACK ( *Proc )( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam );
    Proc proc = [] CALLBACK( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam ) -> LRESULT {
        if ( msg == WM_GETMINMAXINFO ) {
            RECT rcWorkArea;
            SystemParametersInfo( SPI_GETWORKAREA, 0, &rcWorkArea, 0 );
            MINMAXINFO *pMinMax = (MINMAXINFO *)lParam;
            pMinMax->ptMaxSize.x = rcWorkArea.right - rcWorkArea.left;
            pMinMax->ptMaxSize.y = rcWorkArea.bottom - rcWorkArea.top;
            pMinMax->ptMaxPosition.x = rcWorkArea.left;
            pMinMax->ptMaxPosition.y = rcWorkArea.top;
            return 0;
        }
        if ( msg == WM_NCCALCSIZE ) {
            if ( wParam ) {
                NCCALCSIZE_PARAMS *pParams = (NCCALCSIZE_PARAMS *)lParam;
                if ( IsZoomed( hwnd ) ) {
                    RECT rcWorkArea;
                    SystemParametersInfo( SPI_GETWORKAREA, 0, &rcWorkArea, 0 );
                    pParams->rgrc[0].left = rcWorkArea.left;
                    pParams->rgrc[0].top = rcWorkArea.top;
                    pParams->rgrc[0].right = rcWorkArea.right;
                    pParams->rgrc[0].bottom = rcWorkArea.bottom;
                    return 0;
                } else {
                    pParams->rgrc[0].top -= 10;
                }
            }
        }
        return CallWindowProc( originalWndProc, hwnd, msg, wParam, lParam );
    };

    originalWndProc = (WNDPROC)SetWindowLongPtr( hwnd, GWLP_WNDPROC, (LONG_PTR)proc );

    // 移除窗口标题栏
    LONG style = GetWindowLong( hwnd, GWL_STYLE );
    style &= ~WS_BORDER;
    SetWindowLong( hwnd, GWL_STYLE, style );

    // 应用新的窗口样式
    SetWindowPos( hwnd, NULL, 0, 0, 0, 0,
                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED );
#endif

    view.bind( "__exit", [&view]( std::string ) -> std::string {
        auto app = App::getInstance();
        if ( app )
            app->postEvent( []( App *app ) { app->exit(); } );
        return "{}";
    } );

    view.bind( "__close", [&view]( std::string ) -> std::string {
        setWindowState( view, ShowState::Hide );
        return "{}";
    } );

    view.bind( "__minimize", [&view]( std::string ) -> std::string {
        setWindowState( view, ShowState::Show_Minimized );
        return "{}";
    } );

    view.bind( "__maximize", [&view]( std::string ) -> std::string {
#ifdef _WIN32
        HWND hwnd = (HWND)view.window().value();
        if ( IsZoomed( hwnd ) ) {
            setWindowState( view, ShowState::Show_Normal );
            return "{\"maximized\" : false}";
        } else {
            setWindowState( view, ShowState::Show_Maximized );
            return "{\"maximized\" : true}";
        }
#endif
        return "{}";
    } );

    view.bind( "__updateWindowPos", [&view]( std::string msg ) -> std::string {
#ifdef _WIN32
        HWND hwnd = (HWND)view.window().value();
        if ( IsZoomed( hwnd ) )
            return "{}";
        auto json = nlohmann::json::parse( msg );
        auto obj = json[0].get<nlohmann::json::object_t>();
        int dx = obj["dx"];
        int dy = obj["dy"];
        dy = dy < 0 ? dy - 2 : dy;

        RECT rc;
        GetWindowRect( hwnd, &rc );
        SetWindowPos( hwnd, NULL, rc.left + dx, rc.top + dy,
                      rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE );
#endif
        return "{}";
    } );
}

void WebviewWrapper::bindings( webview::webview &view ) {
    view.bind( "__windowPage", []( std::string msg ) -> std::string {
        const unsigned char *data = nullptr;
        std::string content = Server::staticResource( "/webview/window.html", "" );
        nlohmann::json res( { { "content", content } } );
        return res.dump();
    } );
}

void WebviewWrapper::initEventQueue( webview::webview &view, std::string &init_js ) {
    view.bind( "__handleEvent", [&view, this]( std::string msg ) -> std::string {
        std::lock_guard<std::mutex> lock( m_eventMutex );
        auto startPts = std::clock();
        while ( !m_eventHandlers.empty() && ( std::clock() - startPts < 200 ) ) {
            auto handler = m_eventHandlers.front();
            m_eventHandlers.pop_front();
            handler( view );
        }
        return "{}";
    } );

    init_js.append( R"(
    async function handleEvent() {
        await window.__handleEvent();
        setTimeout(handleEvent, 60);
    }
    handleEvent();
)" );
}

void WebviewWrapper::setWindowState( webview::webview &view, ShowState type ) {
#ifdef _WIN32
    HWND hwnd = (HWND)view.window().value();
    if ( hwnd == NULL )
        return;
    if ( type == ShowState::Show_Normal ) {
        ShowWindow( hwnd, SW_RESTORE );
        SetForegroundWindow( hwnd );
    } else if ( type == ShowState::Show_Maximized ) {
        ShowWindow( hwnd, SW_MAXIMIZE );
        SetForegroundWindow( hwnd );
    } else if ( type == ShowState::Show_Minimized ) {
        ShowWindow( hwnd, SW_MINIMIZE );
    } else if ( type == ShowState::Hide ) {
        ShowWindow( hwnd, SW_HIDE );
    }
#endif
}

void WebviewWrapper::postEvent( std::function<void( webview::webview &view )> callback ) {
    std::lock_guard<std::mutex> lock( m_eventMutex );
    m_eventHandlers.push_back( callback );
}

#include "EventLoop.h"
#include "common/Logger.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

int EventLoop::run() {
    return m_context.run();
}

void EventLoop::stop() {
    if ( !m_context.stopped() )
        m_context.stop();
}

EventLoop &EventLoop::instance() {
    static EventLoop ins;
    return ins;
}

std::map<std::string, std::string> EventLoop::currentEnv() {
    std::map<std::string, std::string> env;
    auto envs = boost::process::environment::current();
    for ( auto it : envs ) {
        auto key = it.key();
        if ( key.size() < 1 )
            continue;
        auto value = it.value();
        env[it.key().string()] = value.size() > 0 ? value.string() : "";
    }
    return env;
}

void EventLoop::readPipe(
    const std::shared_ptr<EventLoop::pipe_read> &pipe,
    const buffer_ptr &buffer,
    const read_callback &callback ) {
    if ( buffer->size() < 1 )
        buffer->resize( 1024 * 1024 );
    pipe->async_read_some(
        boost::asio::buffer( *buffer ),
        [pipe, buffer, callback]( const error_code &ec, std::size_t size ) {
            if ( callback( ec, buffer, size ) )
                readPipe( pipe, buffer, callback );
        } );
}

std::shared_ptr<EventLoop::pipe_read> EventLoop::createPipeRead() {
    return std::make_shared<pipe_read>( m_context );
}

std::shared_ptr<EventLoop::process> EventLoop::runProcess(
    const std::vector<std::string> &cmd,
    const std::filesystem::path &workDir,
    const std::map<std::string, std::string> &env,
    const std::shared_ptr<EventLoop::pipe_read> &out,
    const std::shared_ptr<EventLoop::pipe_read> &err,
    std::string *errorMsg ) {
    if ( cmd.empty() ) {
        if ( errorMsg )
            *errorMsg = "命令为空";
        LOG_WARN << "命令为空";
        return nullptr;
    }

    std::string exePath = cmd.front();
    if ( !std::filesystem::exists( exePath ) ) {
        if ( errorMsg )
            *errorMsg = "命令" + exePath + "不存在";
        LOG_WARN << "命令" << exePath << "不存在";
        return nullptr;
    }

    std::vector<std::string> argv( cmd.begin() + 1, cmd.end() );
    process_startdir cwd( workDir.string() );

    std::vector<std::string> environment;
    for ( auto &e : env )
        environment.push_back( e.first + "=" + e.second );
    if ( environment.empty() )
        environment.push_back( "" );

    process_stdio io;
    io.in = nullptr;
    if ( out )
        io.out = *out;
    if ( err )
        io.err = *err;

    try {
        return std::make_shared<process>(
            m_context, exePath, argv, cwd,
            process_env( environment ), io );
    } catch ( const std::exception &e ) {
        if ( errorMsg )
            *errorMsg = e.what();
        LOG_WARN << "启动进程:" << e.what();
        return nullptr;
    }
}

EventLoop::ProcessResult EventLoop::runProcessSync(
    const std::vector<std::string> &cmd,
    const std::filesystem::path &workDir,
    const std::map<std::string, std::string> &env ) {

    ProcessResult result;

    auto outPipe = EventLoop::instance().createPipeRead();
    auto errPipe = EventLoop::instance().createPipeRead();

    std::string errorMsg;
    auto proc = EventLoop::instance().runProcess( cmd, workDir, env, outPipe, errPipe, &errorMsg );
    if ( proc == nullptr ) {
        result.error = errorMsg;
        return result;
    }
    result.started = true;

    auto outBuf = std::make_shared<std::vector<char>>( 4096 );
    auto errBuf = std::make_shared<std::vector<char>>( 4096 );

    // 累积输出的共享缓冲区
    auto outData = std::make_shared<std::string>();
    auto errData = std::make_shared<std::string>();

    readPipe( outPipe, outBuf, [outData]( error_code ec, buffer_ptr buf, std::size_t s ) -> bool {
        if ( s > 0 )
            outData->append( buf->data(), s );
        if ( ec )
            return false;
        return true;
    } );
    readPipe( errPipe, errBuf, [errData]( error_code ec, buffer_ptr buf, std::size_t s ) -> bool {
        if ( s > 0 )
            errData->append( buf->data(), s );
        if ( ec )
            return false;
        return true;
    } );

    error_code ec;
    proc->wait( ec );
    if ( ec )
        LOG_ERROR << "进程退出错误:" << ec.message();

    // 等待进程退出，确保所有输出都被读取
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    result.exitCode = proc->exit_code();
    result.output = std::move( *outData );
    result.error = std::move( *errData );

    return result;
}

std::shared_ptr<EventLoop::timer> EventLoop::runTimer(
    std::chrono::steady_clock::duration delay,
    timer_callback callback ) {
    auto t = std::make_shared<timer>( m_context, delay );
    t->async_wait( [this, delay, callback]( const error_code &ec ) {
        if ( callback( ec ) )
            runTimer( delay, callback );
    } );
    return t;
}

// ---- AsyncProcess 实现 ----

AsyncProcess::~AsyncProcess() {
}

bool AsyncProcess::start(
    const std::vector<std::string> &cmd,
    const std::filesystem::path &workDir,
    const std::map<std::string, std::string> &env,
    std::string *errorMsg ) {
    if ( cmd.empty() )
        return false;
    m_stdout = EventLoop::instance().createPipeRead();
    m_err = EventLoop::instance().createPipeRead();
    m_process = EventLoop::instance().runProcess( cmd, workDir, env, m_stdout, m_err, errorMsg );
    return m_process != nullptr;
}

int AsyncProcess::id() const {
    if ( !m_process )
        return 0;
    return static_cast<int>( m_process->id() );
}

bool AsyncProcess::isRunning() const {
    if ( !m_process )
        return false;
    boost::system::error_code ec;
    return m_process->running( ec );
}

int AsyncProcess::exitCode() const {
    if ( !m_process )
        return -1;
    return m_process->exit_code();
}

int AsyncProcess::wait() {
    if ( !m_process )
        return -1;
    return m_process->wait();
}

void AsyncProcess::asyncWait( std::function<void( int exitCode )> callback ) {
    if ( !m_process ) {
        if ( callback )
            callback( -1 );
        return;
    }
    m_process->async_wait(
        [callback = std::move( callback )]( const boost::system::error_code &ec, int exit_code ) {
            if ( !callback )
                return;
            if ( ec ) {
                callback( -1 );
            } else {
                callback( exit_code );
            }
        } );
}

#ifdef _WIN32
// 方案 1：纯 Win32 API，CreateToolhelp32Snapshot 递归枚举子进程 + TerminateProcess。
// 优点：不依赖外部程序、速度快、一次性把整棵进程树全部杀掉（解决 cmd /c ping 这类孙进程残留）。
// 缺点：属于强制结束（类似 taskkill /F），不提供"优雅"语义；存在极小幅 TOCTOU 竞态
//       （枚举完成到真正 kill 之间又 fork 出新进程的情况）——本工具场景可忽略。
static void terminateProcessTreeNative( DWORD pid ) {
    HANDLE hSnapshot = ::CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
    if ( hSnapshot == INVALID_HANDLE_VALUE )
        return;
    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof( pe32 );
    std::vector<DWORD> children;
    if ( ::Process32FirstW( hSnapshot, &pe32 ) ) {
        do {
            if ( pe32.th32ParentProcessID == pid )
                children.push_back( pe32.th32ProcessID );
        } while ( ::Process32NextW( hSnapshot, &pe32 ) );
    }
    ::CloseHandle( hSnapshot );
    // 先递归杀子进程，再杀父进程（避免父进程提前退出后子进程被收养到 PID 1，再也找不到父子关系）
    for ( DWORD child : children )
        terminateProcessTreeNative( child );
    HANDLE hProcess = ::OpenProcess( PROCESS_TERMINATE, FALSE, pid );
    if ( hProcess ) {
        ::TerminateProcess( hProcess, 1 );
        ::CloseHandle( hProcess );
    }
}

// 方案 2：用系统内置 taskkill.exe 终止整棵树（graceful：/T 不加 /F；force：再加 /F）。
// 优点：一行命令，Windows 官方工具，"优雅/强制"两种语义都原生支持。
// 缺点：需要创建外部进程，速度比纯 API 略慢；taskkill 执行期间需要等待。
static bool terminateProcessTreeViaTaskkill( DWORD pid, bool force ) {
    std::string cmd = "taskkill /PID " + std::to_string( pid ) + " /T";
    if ( force )
        cmd += " /F";
    std::vector<char> buf( cmd.begin(), cmd.end() );
    buf.push_back( '\0' );

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ::memset( &si, 0, sizeof( si ) );
    si.cb = sizeof( si );
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if ( !::CreateProcessA( NULL, buf.data(), NULL, NULL, FALSE,
                            CREATE_NO_WINDOW, NULL, NULL, &si, &pi ) )
        return false;
    ::WaitForSingleObject( pi.hProcess, 15000 );
    DWORD exitCode = STILL_ACTIVE;
    ::GetExitCodeProcess( pi.hProcess, &exitCode );
    ::CloseHandle( pi.hProcess );
    ::CloseHandle( pi.hThread );
    return exitCode == 0;
}
#endif // _WIN32

void AsyncProcess::terminate() {
    if ( !m_process )
        return;
#ifdef _WIN32
    // terminate 语义：优雅地让进程树退出。纯 Win32 没有简单的"对整个进程树优雅退出"
    // （GUI 进程要发 WM_CLOSE、控制台进程要发 CTRL_BREAK_EVENT，且都要递归处理子进程），
    // 这里采用 taskkill /T（不加 /F），它会按进程类型分别发送合适的关闭信号给整棵树。
    int pid = id();
    if ( pid > 0 ) {
        bool ok = terminateProcessTreeViaTaskkill( static_cast<DWORD>( pid ), false );
        if ( ok )
            return;
    }
#endif
    boost::system::error_code ec;
    m_process->terminate( ec );
}

void AsyncProcess::kill() {
    if ( !m_process )
        return;
#ifdef _WIN32
    // kill 语义：强制结束整棵进程树。这里走纯 Win32 版本（快照 + 递归 TerminateProcess，
    // 即 test.cpp 里 terminateProcessTree 的方案），不依赖外部 taskkill 进程，速度更快。
    int pid = id();
    if ( pid > 0 )
        terminateProcessTreeNative( static_cast<DWORD>( pid ) );
    // 兜底：防止快照竞态漏掉，再对主句柄本身调用一次 TerminateProcess
    auto handle = m_process->native_handle();
    if ( handle )
        ::TerminateProcess( handle, 1 );
#else
    boost::system::error_code ec;
    m_process->terminate( ec );
#endif
}

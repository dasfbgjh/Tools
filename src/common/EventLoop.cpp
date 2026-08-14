#include "EventLoop.h"
#include "common/Logger.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

#if defined( _WIN32 )
#include <windows.h>
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
    const std::shared_ptr<EventLoop::pipe_read> &err ) {
    if ( cmd.empty() )
        return nullptr;

    std::string exePath = cmd.front();
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

    auto proc = EventLoop::instance().runProcess( cmd, workDir, env, outPipe, errPipe );
    if ( proc == nullptr )
        return result;
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
    const std::map<std::string, std::string> &env ) {
    if ( cmd.empty() )
        return false;
    m_stdout = EventLoop::instance().createPipeRead();
    m_err = EventLoop::instance().createPipeRead();
    m_process = EventLoop::instance().runProcess( cmd, workDir, env, m_stdout, m_err );
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

void AsyncProcess::terminate() {
    if ( !m_process )
        return;
    boost::system::error_code ec;
    m_process->terminate( ec );
}

void AsyncProcess::kill() {
    if ( !m_process )
        return;
#if defined( _WIN32 )
    auto handle = m_process->native_handle();
    if ( handle )
        ::TerminateProcess( handle, 1 );
#else
    boost::system::error_code ec;
    m_process->terminate( ec );
#endif
}

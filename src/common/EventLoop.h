#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <map>
#include <filesystem>

#include <boost/asio.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>
#include <boost/process/v2/start_dir.hpp>
#include <boost/process/v2/environment.hpp>
#if defined( _WIN32 )
#include <boost/process/v2/windows/creation_flags.hpp>
#include <boost/process/v2/windows/show_window.hpp>
#endif

class EventLoop {
public:
    using context = boost::asio::io_context;
    using pipe_read = boost::asio::readable_pipe;
    using pipe_write = boost::asio::writable_pipe;
    using timer = boost::asio::steady_timer;
    using process = boost::process::v2::process;
    using process_env = boost::process::v2::process_environment;
    using process_stdio = boost::process::v2::process_stdio;
    using process_startdir = boost::process::v2::process_start_dir;
    using error_code = boost::system::error_code;

    using buffer_ptr = std::shared_ptr<std::vector<char>>;
    using read_callback = std::function<bool( error_code, buffer_ptr, std::size_t )>;
    using timer_callback = std::function<bool( error_code )>;

    // 同步执行进程的返回结果
    struct ProcessResult {
        bool started = false; // 进程是否成功启动
        int exitCode = -1;    // 退出码（仅在 started=true 时有意义）
        std::string output;   // stdout
        std::string error;    // stderr
    };

private:
    context m_context;

    EventLoop() = default;
    EventLoop( const EventLoop &other ) = delete;
    EventLoop( EventLoop &&other ) = delete;
    EventLoop &operator=( const EventLoop &other ) = delete;
    EventLoop &operator=( EventLoop &&other ) = delete;

public:
    // 运行事件循环，返回处理的事件数量
    int run();

    // 停止事件循环
    void stop();

    static EventLoop &instance();

    static void delayBoot( const std::string &cmd, int seconds );

    // 获取当前进程的环境变量集
    static std::map<std::string, std::string> currentEnv();

    // 读取管道数据，回调函数返回true时，继续读取，返回false时，停止读取
    static void readPipe( const std::shared_ptr<pipe_read> &pipe,
                          const buffer_ptr &buffer, const read_callback &callback );

    // 创建一个绑定到主事件循环 io_context 的读管道
    std::shared_ptr<pipe_read> createPipeRead();

    static std::string processPath( const std::string &exe );

    // 运行一个进程，返回进程对象
    std::shared_ptr<process> runProcess(
        const std::vector<std::string> &cmd,
        const std::filesystem::path &workDir = std::filesystem::current_path(),
        const std::map<std::string, std::string> &env = currentEnv(),
        const std::shared_ptr<pipe_read> &out = nullptr,
        const std::shared_ptr<pipe_read> &err = nullptr,
        std::string *errorMsg = nullptr );

    // 同步运行一个进程，阻塞等待结束并返回捕获的 stdout/stderr 与退出码
    static ProcessResult runProcessSync(
        const std::vector<std::string> &cmd,
        const std::filesystem::path &workDir = std::filesystem::current_path(),
        const std::map<std::string, std::string> &env = currentEnv() );

    // 运行一个定时器，回调函数返回true时，定时器继续运行，返回false时，定时器停止
    std::shared_ptr<timer> runTimer( std::chrono::steady_clock::duration delay, timer_callback callback );
};

// 异步进程句柄：封装一个长生命周期的子进程，
// 提供启动、终止、阻塞等待、状态查询及管道访问能力。
class AsyncProcess {
private:
    std::shared_ptr<EventLoop::process> m_process;
    std::shared_ptr<EventLoop::pipe_read> m_stdout;
    std::shared_ptr<EventLoop::pipe_read> m_err;

public:
    AsyncProcess() = default;
    ~AsyncProcess();

    AsyncProcess( const AsyncProcess & ) = delete;

    AsyncProcess &operator=( const AsyncProcess & ) = delete;

    AsyncProcess( AsyncProcess &&other ) noexcept = default;

    AsyncProcess &operator=( AsyncProcess &&other ) noexcept = default;

    // 启动进程。返回 true 表示启动成功。
    bool start(
        const std::vector<std::string> &cmd,
        const std::filesystem::path &workDir = std::filesystem::current_path(),
        const std::map<std::string, std::string> &env = EventLoop::currentEnv(),
        std::string *errorMsg = nullptr );

    // 是否成功启动
    bool started() const {
        return m_process != nullptr;
    }

    // 子进程 pid，未启动时返回 0
    int id() const;

    // 进程是否仍在运行
    bool isRunning() const;

    // 退出码（仅在进程退出后有效）
    int exitCode() const;

    // 阻塞等待进程结束，返回退出码
    int wait();

    // 进程未启动时，callback 立即以 -1 调用。
    void asyncWait( std::function<void( int exitCode )> callback );

    // 优雅终止（发送信号/事件）
    void terminate();

    // 强制结束（Windows: TerminateProcess，其他: terminate 信号）
    void kill();

    // stdout 读管道
    std::shared_ptr<EventLoop::pipe_read> outPipe() const {
        return m_stdout;
    }

    // stderr 读管道
    std::shared_ptr<EventLoop::pipe_read> errPipe() const {
        return m_err;
    }
};

#endif

#include "routes/Mcp.h"

#include <boost/filesystem.hpp>
#include <sstream>
#include <chrono>
#include <thread>
#include <cctype>
#include <iomanip>
#include <fstream>
#include <type_traits>
#include <cstdio>
#include <cstring>
#include <memory>
#include <regex>
#include <mutex>
#include <atomic>
#include <algorithm>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif
#include "core/Server.h"
#include "core/Utils.h"
#include "common/Config.h"
#include "common/Logger.hpp"

namespace fs = boost::filesystem;

namespace routes::mcpRoutes {

// ===========================================================================
// 全局 MCP Server 单例 + 默认工具/资源/提示
// ===========================================================================
static std::mutex g_mcpServerMutex;
static std::shared_ptr<::mcp::McpServer> g_mcpServer;
static std::atomic<bool> g_registered{ false };

::mcp::McpServer &getMcpServer() {
    std::lock_guard<std::mutex> lk( g_mcpServerMutex );
    if ( !g_mcpServer ) {
        g_mcpServer = std::make_shared<::mcp::McpServer>();
    }
    return *g_mcpServer;
}

// ---------------------------------------------------------------------------
// 辅助：构造 tool 调用结果（只有一条文本内容的快捷函数）
// ---------------------------------------------------------------------------
static mcp::core::ToolCallResult makeTextResult( std::string text, bool isError = false ) {
    mcp::core::ToolCallResult r;
    r.isError = isError;
    mcp::core::Content::Text tc;
    tc.text = std::move( text );
    r.content.emplace_back( std::move( tc ) );
    return r;
}

// ---------------------------------------------------------------------------
// 工具 1: echo - 烟雾测试
// ---------------------------------------------------------------------------
static void registerToolEcho( ::mcp::McpServer &srv ) {
    mcp::core::JsonSchemaProperty pmsg;
    pmsg.type = "string";
    pmsg.description = "要回显的内容";

    mcp::core::ToolInputSchema sch;
    sch.properties.emplace( "message", std::move( pmsg ) );
    sch.required = { "message" };

    mcp::core::Tool t;
    t.name = "echo";
    t.description = "回显输入的 message 参数，用于连通性验证";
    t.inputSchema = std::move( sch );
    t.destructive = false;
    t.idempotent = true;

    srv.registerTool( std::move( t ), []( const std::optional<mcp::core::json> &args ) {
        std::string msg;
        if ( args && args->contains( "message" ) ) {
            if ( ( *args )["message"].is_string() )
                msg = ( *args )["message"].get<std::string>();
            else if ( !( *args )["message"].is_null() )
                msg = ( *args )["message"].dump();
        }
        return makeTextResult( msg.empty() ? std::string{ "(empty)" } : msg );
    } );
}

// ---------------------------------------------------------------------------
// 工具 2: run_shell_command - 同步执行 shell 命令（注意风险，仅限 localhost）
// ---------------------------------------------------------------------------
static void registerToolRunCommand( ::mcp::McpServer &srv ) {
    using mcp::core::JsonSchemaProperty;
    using mcp::core::Tool;
    using mcp::core::ToolInputSchema;
    mcp::core::Tool t;
    t.name = "run_shell_command";
    t.description = "在本机同步执行 shell 命令并返回 stdout+stderr+exitCode。\n"
                    "Windows 下使用 cmd /c，类 Unix 使用 /bin/sh -c。"
                    "建议命令不要超过 30 秒，超时将被强制终止。";
    ToolInputSchema sch;
    JsonSchemaProperty pCmd;
    pCmd.type = "string";
    pCmd.description = "要执行的命令字符串（会被传给 shell），例如: dir /b";
    sch.properties.emplace( "command", std::move( pCmd ) );
    JsonSchemaProperty pTimeout;
    pTimeout.type = "integer";
    pTimeout.description = "超时时间（秒），默认 30 秒，最大 300 秒";
    sch.properties.emplace( "timeout_seconds", std::move( pTimeout ) );
    JsonSchemaProperty pShell;
    pShell.type = "string";
    pShell.description = "可选，自定义 shell 路径";
    sch.properties.emplace( "shell", std::move( pShell ) );
    sch.required = { "command" };
    t.inputSchema = std::move( sch );
    t.destructive = true; // shell 命令可能破坏系统
    t.idempotent = false;

    srv.registerTool( std::move( t ), []( const std::optional<mcp::core::json> &args ) {
        if ( !args || !args->contains( "command" ) || !( *args )["command"].is_string() ) {
            return makeTextResult( "缺少 command 参数", true );
        }
        const std::string command = ( *args )["command"].get<std::string>();
        int timeoutSec = 30;
        if ( args->contains( "timeout_seconds" ) && ( *args )["timeout_seconds"].is_number_integer() ) {
            timeoutSec = ( *args )["timeout_seconds"].get<int>();
            if ( timeoutSec < 1 )
                timeoutSec = 1;
            if ( timeoutSec > 300 )
                timeoutSec = 300;
        }
        std::string shell;
        if ( args->contains( "shell" ) && ( *args )["shell"].is_string() ) {
            shell = ( *args )["shell"].get<std::string>();
        }

        // 跨平台带超时的子进程执行（使用平台原生 API，避免 boost.process v1/v2 版本差异）
        struct ExecResult {
            int exitCode;
            std::string outStr;
            std::string errStr;
            bool timedOut;
        };
        auto execFn = [&]() -> ExecResult {
#if defined( _WIN32 )
            // ---- Windows: CreateProcess + 管道 + WaitForSingleObject ----
            SECURITY_ATTRIBUTES sa{};
            sa.nLength = sizeof( SECURITY_ATTRIBUTES );
            sa.bInheritHandle = TRUE;
            sa.lpSecurityDescriptor = nullptr;

            HANDLE hOutRead = nullptr, hOutWrite = nullptr;
            HANDLE hErrRead = nullptr, hErrWrite = nullptr;
            if ( !CreatePipe( &hOutRead, &hOutWrite, &sa, 0 ) )
                return { -1, "", "CreatePipe(stdout) 失败", false };
            if ( !CreatePipe( &hErrRead, &hErrWrite, &sa, 0 ) ) {
                CloseHandle( hOutRead );
                CloseHandle( hOutWrite );
                return { -1, "", "CreatePipe(stderr) 失败", false };
            }
            // 子进程只继承写端句柄
            SetHandleInformation( hOutRead, HANDLE_FLAG_INHERIT, 0 );
            SetHandleInformation( hErrRead, HANDLE_FLAG_INHERIT, 0 );

            // 构造 cmdline: <shell> /c <command>   (或 shell=空时直接使用 cmd /c)
            std::string sh = shell.empty() ? "cmd.exe" : shell;
            std::string shArgs;
            if ( !shell.empty() ) {
                // 自定义 shell: 尝试判断是 cmd 还是 bash-like
                bool isCmdLike = ( sh.find( "cmd" ) != std::string::npos );
                shArgs = sh + " " + ( isCmdLike ? "/c " : "-c " ) + "\"" + command + "\"";
            } else {
                shArgs = "cmd.exe /c " + command;
            }
            std::vector<char> cmdLine( shArgs.begin(), shArgs.end() );
            cmdLine.push_back( '\0' );

            STARTUPINFOA si{};
            si.cb = sizeof( STARTUPINFOA );
            si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            si.hStdInput = GetStdHandle( STD_INPUT_HANDLE );
            si.hStdOutput = hOutWrite;
            si.hStdError = hErrWrite;
            si.wShowWindow = SW_HIDE;

            PROCESS_INFORMATION pi{};
            BOOL ok = CreateProcessA( nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi );
            // 关闭父进程不需要的写端（否则读端永远不会得到 EOF）
            CloseHandle( hOutWrite );
            CloseHandle( hErrWrite );
            if ( !ok ) {
                DWORD err = GetLastError();
                CloseHandle( hOutRead );
                CloseHandle( hErrRead );
                return { -1, "", std::string( "CreateProcessA 失败: err=" ) + std::to_string( err ), false };
            }

            DWORD waitMs = static_cast<DWORD>( timeoutSec ) * 1000UL;
            DWORD wr = WaitForSingleObject( pi.hProcess, waitMs );

            auto readPipe = []( HANDLE h, std::string &out ) {
                char buf[4096];
                DWORD bytesRead = 0;
                for ( ;; ) {
                    DWORD avail = 0;
                    if ( !PeekNamedPipe( h, nullptr, 0, nullptr, &avail, nullptr ) )
                        break;
                    if ( avail == 0 )
                        break;
                    if ( !ReadFile( h, buf, sizeof( buf ), &bytesRead, nullptr ) || bytesRead == 0 )
                        break;
                    out.append( buf, bytesRead );
                }
            };

            // 超时：终止进程，然后读取剩余输出
            bool timedOut = ( wr == WAIT_TIMEOUT );
            if ( timedOut ) {
                TerminateProcess( pi.hProcess, (UINT)-999 );
                WaitForSingleObject( pi.hProcess, 2000 );
            }
            // 再等一小会，确保管道剩余数据可读
            Sleep( 20 );
            std::string outStr, errStr;
            readPipe( hOutRead, outStr );
            readPipe( hErrRead, errStr );

            DWORD exitCode = (DWORD)-999;
            GetExitCodeProcess( pi.hProcess, &exitCode );
            CloseHandle( pi.hProcess );
            CloseHandle( pi.hThread );
            CloseHandle( hOutRead );
            CloseHandle( hErrRead );
            return { (int)exitCode, outStr, errStr, timedOut };
#else
            // ---- POSIX: pipe + fork + exec + select/poll ----
            int outPipe[2], errPipe[2];
            if (pipe(outPipe) != 0) return { -1, "", "pipe(stdout) 失败", false };
            if (pipe(errPipe) != 0) { close(outPipe[0]); close(outPipe[1]); return { -1, "", "pipe(stderr) 失败", false }; }

            pid_t pid = fork();
            if (pid < 0) {
                close(outPipe[0]); close(outPipe[1]); close(errPipe[0]); close(errPipe[1]);
                return { -1, "", "fork 失败", false };
            }
            if (pid == 0) {
                // 子进程
                dup2(outPipe[1], STDOUT_FILENO);
                dup2(errPipe[1], STDERR_FILENO);
                close(outPipe[0]); close(outPipe[1]);
                close(errPipe[0]); close(errPipe[1]);
                const char *sh = shell.empty() ? "/bin/sh" : shell.c_str();
                ::execl(sh, sh, "-c", command.c_str(), nullptr);
                _exit(127);
            }
            // 父进程
            close(outPipe[1]); close(errPipe[1]);
            std::string outStr, errStr;
            auto readUntil = [](int fd, std::string &out) {
                char buf[4096];
                for (;;) {
                    ssize_t n = ::read(fd, buf, sizeof(buf));
                    if (n <= 0) break;
                    out.append(buf, (size_t)n);
                }
            };

            // 等待 timeout 或 子进程结束
            int wstatus = 0;
            bool timedOut = false;
            int waited = 0;
            const int stepMs = 100;
            for (;;) {
                int r = ::waitpid(pid, &wstatus, WNOHANG);
                if (r == pid) break;
                if (r < 0) { timedOut=false; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
                waited += stepMs;
                if (waited >= timeoutSec * 1000) {
                    timedOut = true;
                    ::kill(pid, SIGTERM);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    if (::waitpid(pid, &wstatus, WNOHANG) <= 0) {
                        ::kill(pid, SIGKILL);
                        ::waitpid(pid, &wstatus, 0);
                    }
                    break;
                }
            }
            readUntil(outPipe[0], outStr);
            readUntil(errPipe[0], errStr);
            close(outPipe[0]); close(errPipe[0]);
            int exitCode = -1;
            if (timedOut) exitCode = -999;
            else if (WIFEXITED(wstatus)) exitCode = WEXITSTATUS(wstatus);
            else if (WIFSIGNALED(wstatus)) exitCode = -WTERMSIG(wstatus);
            return { exitCode, outStr, errStr, timedOut };
#endif
        };

        try {
            ExecResult r = execFn();
            std::ostringstream os;
            os << "exit_code: " << r.exitCode << "\n";
            if ( r.timedOut ) {
                os << "*** MCP: 命令超时 (>" << timeoutSec << "s) 已被终止 ***\n";
            }
            auto append = [&]( const char *label, std::string &s ) {
                if ( s.empty() )
                    return;
                if ( s.size() > 64 * 1024 ) {
                    os << label << " (truncated from " << s.size() << " bytes) ---\n";
                    os << s.substr( 0, 64 * 1024 ) << "\n...(truncated)\n";
                } else {
                    os << label << " ---\n"
                       << s;
                    if ( !s.empty() && s.back() != '\n' )
                        os << "\n";
                }
            };
            append( "--- stdout", r.outStr );
            append( "--- stderr", r.errStr );
            return makeTextResult( os.str() );
        } catch ( const std::exception &e ) {
            return makeTextResult( std::string( "run_shell_command 异常: " ) + e.what(), true );
        }
    } );
}

// ---------------------------------------------------------------------------
// 辅助：将 fs::last_write_time 返回值转为 std::time_t
// 兼容 boost 版本差异：旧版返回 std::time_t / intmax_t; 新版为 chrono time_point
// ---------------------------------------------------------------------------
namespace {
template <typename T, typename std::enable_if<
                          std::is_arithmetic<typename std::remove_reference<T>::type>::value ||
                              std::is_same<typename std::remove_cv<typename std::remove_reference<T>::type>::type, std::time_t>::value,
                          int>::type = 0>
inline std::time_t ftime_to_time_t_impl( T &&v, int ) {
    return static_cast<std::time_t>( std::forward<T>( v ) );
}

template <typename T>
inline auto ftime_to_time_t_impl( T &&v, long ) -> decltype( std::remove_cv<typename std::remove_reference<T>::type>::type::clock::to_time_t( std::forward<T>( v ) ),
                                                             std::time_t{} ) {
    using TP = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
    return TP::clock::to_time_t( std::forward<T>( v ) );
}

template <typename T>
inline std::time_t ftime_to_time_t_impl( T &&, ... ) {
    return 0;
}

template <typename T>
inline std::time_t convertFtimeToTimeT( T &&v ) {
    return ftime_to_time_t_impl( std::forward<T>( v ), 0 );
}
} // namespace

// ---------------------------------------------------------------------------
// 工具 3: list_directory - 列出目录内容
// ---------------------------------------------------------------------------
static void registerToolListDir( mcp::McpServer &srv ) {
    using mcp::core::JsonSchemaProperty;
    using mcp::core::Tool;
    using mcp::core::ToolInputSchema;
    mcp::core::Tool t;
    t.name = "list_directory";
    t.description = "列出本机目录内容（仅展示路径、大小、类型、修改时间），不会递归。";
    ToolInputSchema sch;
    JsonSchemaProperty p;
    p.type = "string";
    p.description = "目录路径（支持绝对路径或相对当前工作目录的路径）";
    sch.properties.emplace( "path", std::move( p ) );
    JsonSchemaProperty pHidden;
    pHidden.type = "boolean";
    pHidden.description = "是否同时列出以点开头的隐藏条目，默认 false";
    sch.properties.emplace( "show_hidden", std::move( pHidden ) );
    sch.required = { "path" };
    t.inputSchema = std::move( sch );
    t.destructive = false;
    t.idempotent = true;

    srv.registerTool( std::move( t ), []( const std::optional<mcp::core::json> &args ) {
        if ( !args || !args->contains( "path" ) )
            return makeTextResult( "缺少 path 参数", true );
        const std::string pathStr = ( *args )["path"].get<std::string>();
        bool showHidden = false;
        if ( args->contains( "show_hidden" ) && ( *args )["show_hidden"].is_boolean() ) {
            showHidden = ( *args )["show_hidden"].get<bool>();
        }

        boost::system::error_code ec;
        fs::path p( pathStr );
        if ( !fs::exists( p, ec ) || !fs::is_directory( p, ec ) ) {
            return makeTextResult( "路径不存在或不是目录: " + pathStr +
                                       ( ec ? std::string( " (" ) + ec.message() + ")" : "" ),
                                   true );
        }

        std::ostringstream os;
        os << std::left << std::setw( 8 ) << "TYPE"
           << std::setw( 14 ) << "SIZE"
           << std::setw( 20 ) << "MTIME"
           << " NAME\n";
        os << std::string( 80, '-' ) << "\n";

        int count = 0;
        fs::directory_iterator it( p, ec ), end;
        for ( ; it != end && !ec; it.increment( ec ) ) {
            const auto &entry = *it;
            fs::path namePath = entry.path().filename();
            std::string name = namePath.string();
            if ( !showHidden && !name.empty() && name[0] == '.' )
                continue;

            std::string type;
            uintmax_t size = 0;
            std::string mtimeStr = "-";

            boost::system::error_code ec2;
            if ( fs::is_directory( entry.status( ec2 ) ) )
                type = "DIR";
            else if ( fs::is_regular_file( entry.status( ec2 ) ) ) {
                type = "FILE";
                boost::system::error_code ec3;
                size = fs::file_size( entry.path(), ec3 );
            } else if ( fs::is_symlink( entry.status( ec2 ) ) )
                type = "LINK";
            else
                type = "OTHER";

            try {
                auto ftime = fs::last_write_time( entry.path(), ec2 );
                if ( !ec2 ) {
                    std::time_t tt = convertFtimeToTimeT( std::move( ftime ) );
                    if ( tt != 0 ) {
                        std::tm tmv{};
#if defined( _WIN32 )
                        localtime_s( &tmv, &tt );
#else
                        localtime_r(&tt, &tmv);
#endif
                        char buf[32];
                        std::strftime( buf, sizeof( buf ), "%Y-%m-%d %H:%M:%S", &tmv );
                        mtimeStr = buf;
                    }
                }
            } catch ( ... ) {
            }

            os << std::left << std::setw( 8 ) << type
               << std::setw( 14 ) << ( type == "FILE" ? std::to_string( size ) : std::string( "-" ) )
               << std::setw( 20 ) << mtimeStr
               << " " << name << "\n";
            if ( ++count > 2000 ) {
                os << "... (仅展示前 2000 条)\n";
                break;
            }
        }
        if ( ec )
            os << "(警告: 枚举被中断: " << ec.message() << ")\n";
        os << "共 " << count << " 项";
        return makeTextResult( os.str() );
    } );
}

// ---------------------------------------------------------------------------
// 工具 4: read_text_file - 读取文本文件
// ---------------------------------------------------------------------------
static void registerToolReadFile( mcp::McpServer &srv ) {
    using mcp::core::JsonSchemaProperty;
    using mcp::core::Tool;
    using mcp::core::ToolInputSchema;
    mcp::core::Tool t;
    t.name = "read_text_file";
    t.description = "按文本方式读取本机文件（可选编码、范围）。二进制文件请谨慎使用，会截断。";
    ToolInputSchema sch;
    JsonSchemaProperty p;
    p.type = "string";
    p.description = "文件路径";
    sch.properties.emplace( "path", std::move( p ) );
    JsonSchemaProperty pMax;
    pMax.type = "integer";
    pMax.description = "最大读取字节数，默认 1MB (1048576)";
    sch.properties.emplace( "max_bytes", std::move( pMax ) );
    JsonSchemaProperty pOffset;
    pOffset.type = "integer";
    pOffset.description = "起始偏移字节，默认 0";
    sch.properties.emplace( "offset", std::move( pOffset ) );
    sch.required = { "path" };
    t.inputSchema = std::move( sch );
    t.destructive = false;
    t.idempotent = true;

    srv.registerTool( std::move( t ), []( const std::optional<mcp::core::json> &args ) {
        if ( !args || !args->contains( "path" ) )
            return makeTextResult( "缺少 path 参数", true );
        const std::string pathStr = ( *args )["path"].get<std::string>();
        int64_t maxBytes = 1024 * 1024;
        int64_t offset = 0;
        if ( args->contains( "max_bytes" ) )
            maxBytes = std::max<int64_t>( 1, std::min<int64_t>( ( *args )["max_bytes"].get<int64_t>(), 16 * 1024 * 1024 ) );
        if ( args->contains( "offset" ) )
            offset = std::max<int64_t>( 0, ( *args )["offset"].get<int64_t>() );

        boost::system::error_code ec;
        if ( !fs::exists( pathStr, ec ) || !fs::is_regular_file( pathStr, ec ) ) {
            return makeTextResult( "文件不存在或不是普通文件: " + pathStr, true );
        }
        std::ifstream ifs( pathStr, std::ios::binary );
        if ( !ifs.is_open() )
            return makeTextResult( "无法打开文件: " + pathStr, true );
        if ( offset )
            ifs.seekg( offset );
        std::vector<char> buf( static_cast<size_t>( maxBytes ) );
        ifs.read( buf.data(), static_cast<std::streamsize>( maxBytes ) );
        const std::streamsize gcount = ifs.gcount();
        std::string content( buf.data(), static_cast<size_t>( gcount ) );
        // 替换不可打印字符，防止 MCP 客户端解析问题
        for ( auto &ch : content ) {
            unsigned char uc = static_cast<unsigned char>( ch );
            if ( uc < 0x09 || ( uc > 0x0D && uc < 0x20 ) || uc == 0x7F )
                ch = '.';
        }
        std::ostringstream os;
        os << "文件: " << pathStr << " | 偏移: " << offset << " | 读取字节: " << gcount;
        if ( ifs.peek() != EOF )
            os << " (未到 EOF, 还有后续内容)";
        os << "\n"
           << content;
        return makeTextResult( os.str() );
    } );
}

// ---------------------------------------------------------------------------
// 工具 5: get_time - 获取当前时间
// ---------------------------------------------------------------------------
static void registerToolGetTime( mcp::McpServer &srv ) {
    using mcp::core::JsonSchemaProperty;
    using mcp::core::Tool;
    using mcp::core::ToolInputSchema;
    mcp::core::Tool t;
    t.name = "get_time";
    t.description = "获取本机当前时间（可指定 strftime 格式）。";
    ToolInputSchema sch;
    JsonSchemaProperty pf;
    pf.type = "string";
    pf.description = "strftime 格式串，默认 \"%Y-%m-%d %H:%M:%S %z\"";
    sch.properties.emplace( "format", std::move( pf ) );
    t.inputSchema = std::move( sch );
    t.destructive = false;
    t.idempotent = false;

    srv.registerTool( std::move( t ), []( const std::optional<mcp::core::json> &args ) {
        std::string fmt = "%Y-%m-%d %H:%M:%S %z";
        if ( args && args->contains( "format" ) && ( *args )["format"].is_string() )
            fmt = ( *args )["format"].get<std::string>();
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t( now );
        std::tm tmv{};
#if defined( _WIN32 )
        localtime_s( &tmv, &tt );
#else
        localtime_r(&tt, &tmv);
#endif
        char buf[256];
        size_t n = std::strftime( buf, sizeof( buf ), fmt.c_str(), &tmv );
        std::string result( buf, n );
        std::ostringstream os;
        os << "unix_seconds: " << tt << "\nlocal_time:   " << result;
        return makeTextResult( os.str() );
    } );
}

// ---------------------------------------------------------------------------
// 工具 6: get_server_info - 返回当前 Tools 应用的基础信息
// ---------------------------------------------------------------------------
static void registerToolServerInfo( mcp::McpServer &srv ) {
    mcp::core::Tool t;
    t.name = "get_server_info";
    t.description = "返回当前 Tools 应用的基础信息（应用名、版本、工作目录等）。";
    t.destructive = false;
    t.idempotent = true;

    srv.registerTool( std::move( t ), []( const std::optional<mcp::core::json> & ) {
        mcp::core::json j;
        j["application"] = "Tools";
#ifdef _WIN32
        j["platform"] = "Windows";
#else
        j["platform"] = "Unix";
#endif
        try {
            char cwdBuf[4096];
#ifdef _WIN32
            _getcwd( cwdBuf, sizeof( cwdBuf ) );
#else
            getcwd(cwdBuf, sizeof(cwdBuf));
#endif
            j["cwd"] = cwdBuf;
        } catch ( ... ) {
            j["cwd"] = nullptr;
        }
        j["config"] = mcp::core::json::object( { { "http_port", Config::getHttpServerPort() },
                                                 { "https_port", Config::getHttpsServerPort() },
                                                 { "https_enable", Config::getEnableHttps() },
                                                 { "temp_dir", Config::getTempPath() },
                                                 { "upload_dir", Config::getUploadFilePath() },
                                                 { "log_level", Config::getLogLevel() },
                                                 { "pdf_tool", Config::getPdfToolPath() },
                                                 { "ffmpeg_path", Config::getFfmpegPath() },
                                                 { "openssl_path", Config::getOpensslPath() } } );
        std::ostringstream os;
        os << std::setw( 2 ) << j;
        return makeTextResult( os.str() );
    } );
}

// ---------------------------------------------------------------------------
// 辅助：把 json 格式化为 TextContent 结果（带缩进）
// ---------------------------------------------------------------------------
static mcp::core::ToolCallResult makeJsonResult( const mcp::core::json &j, bool isError = false ) {
    std::ostringstream os;
    os << std::setw( 2 ) << j;
    return makeTextResult( os.str(), isError );
}

// ===========================================================================
// MCP 调试客户端辅助（基于 httplib 同步 HTTP 客户端）
// ===========================================================================
namespace {
struct ParsedUrl {
    std::string scheme = "http"; // 目前仅支持 http；https 会报提示
    std::string host = "127.0.0.1";
    int port = 80;
    std::string path = "/mcp"; // 含前导 '/'
    std::string error;         // 非空 = 解析失败
};

static ParsedUrl parseMcpEndpointUrl( const std::string &url ) {
    ParsedUrl r;
    if ( url.empty() ) {
        r.error = "endpoint 为空";
        return r;
    }
    std::string s = url;
    // 裁剪空白
    while ( !s.empty() && std::isspace( (unsigned char)s.front() ) )
        s.erase( s.begin() );
    while ( !s.empty() && std::isspace( (unsigned char)s.back() ) )
        s.pop_back();

    // 1) 抽取 scheme
    std::string rest = s;
    auto posScheme = s.find( "://" );
    if ( posScheme != std::string::npos ) {
        r.scheme = s.substr( 0, posScheme );
        rest = s.substr( posScheme + 3 );
        std::transform( r.scheme.begin(), r.scheme.end(), r.scheme.begin(),
                        []( unsigned char c ) { return std::tolower( c ); } );
    }
    // 2) 抽取 host[:port][/path]
    auto slash = rest.find( '/' );
    std::string auth = ( slash == std::string::npos ) ? rest : rest.substr( 0, slash );
    std::string p = ( slash == std::string::npos ) ? "" : rest.substr( slash );
    if ( p.empty() )
        p = "/mcp";
    r.path = p;

    if ( auth.empty() ) {
        r.error = "endpoint 缺少 host";
        return r;
    }
    auto colon = auth.find( ':' );
    if ( colon != std::string::npos ) {
        r.host = auth.substr( 0, colon );
        std::string portStr = auth.substr( colon + 1 );
        try {
            int pv = std::stoi( portStr );
            if ( pv <= 0 || pv > 65535 ) {
                r.error = "端口范围无效: " + portStr;
                return r;
            }
            r.port = pv;
        } catch ( ... ) {
            r.error = "端口解析失败: " + portStr;
            return r;
        }
    } else {
        r.host = auth;
        r.port = ( r.scheme == "https" ) ? 443 : 80;
    }
    if ( r.scheme != "http" && r.scheme != "https" ) {
        r.error = "目前仅支持 http(s) 协议；传入 scheme=" + r.scheme;
    }
    return r;
}

struct McpCallOutcome {
    bool ok = false;
    int httpStatus = 0;
    long long elapsedMs = 0;
    std::string errorMsg;
    mcp::core::json request;
    mcp::core::json response; // 完整 http body 解析结果
    bool sseResponse = false; // 响应是否为 SSE 格式
    std::string sseEndpoint;  // SSE 会话建立时返回的 endpoint URL
};

// ---- SSE 响应解析：从 text/event-stream body 中提取 JSON-RPC 数据 ----
static bool parseSseBody( const std::string &body, mcp::core::json &outJson, std::string &outEndpoint ) {
    // SSE 格式: event: xxx\ndata: yyy\n\n
    // 我们关注 event: message 中的 data (JSON-RPC 响应)
    // 以及 event: endpoint 中的 data (POST URL)
    std::istringstream stream( body );
    std::string line;
    std::string currentEvent;
    std::string currentData;
    bool foundMessage = false;
    bool foundEndpoint = false;

    while ( std::getline( stream, line ) ) {
        // 移除行尾 \r
        if ( !line.empty() && line.back() == '\r' )
            line.pop_back();

        if ( line.empty() ) {
            // 空行 = 一个事件结束
            if ( currentEvent == "message" && !currentData.empty() ) {
                try {
                    outJson = mcp::core::json::parse( currentData );
                    foundMessage = true;
                } catch ( ... ) {
                    // 忽略解析失败
                }
            } else if ( currentEvent == "endpoint" && !currentData.empty() ) {
                outEndpoint = currentData;
                foundEndpoint = true;
            }
            currentEvent.clear();
            currentData.clear();
        } else if ( line.substr( 0, 6 ) == "event:" ) {
            currentEvent = line.substr( 6 );
            // trim
            while ( !currentEvent.empty() && currentEvent.front() == ' ' )
                currentEvent.erase( currentEvent.begin() );
            while ( !currentEvent.empty() && currentEvent.back() == ' ' )
                currentEvent.pop_back();
        } else if ( line.substr( 0, 6 ) == "data:" ) {
            std::string d = line.substr( 6 );
            while ( !d.empty() && d.front() == ' ' )
                d.erase( d.begin() );
            if ( !currentData.empty() )
                currentData += "\n";
            currentData += d;
        }
        // 忽略其他 SSE 字段 (id, retry, 注释等)
    }

    // 处理最后一个事件 (可能不以空行结尾)
    if ( !line.empty() ) {
        if ( currentEvent == "message" && !currentData.empty() ) {
            try {
                outJson = mcp::core::json::parse( currentData );
                foundMessage = true;
            } catch ( ... ) {
            }
        } else if ( currentEvent == "endpoint" && !currentData.empty() ) {
            outEndpoint = currentData;
            foundEndpoint = true;
        }
    }

    return foundMessage || foundEndpoint;
}

// ---- 判断 HTTP 响应是否为 SSE ----
static bool isSseContent( const httplib::Response &res ) {
    auto it = res.headers.find( "Content-Type" );
    if ( it == res.headers.end() )
        return false;
    const std::string &ct = it->second;
    return ct.find( "text/event-stream" ) != std::string::npos;
}

static McpCallOutcome postJsonRpc( const std::string &endpoint,
                                   const mcp::core::json &rpcBody,
                                   int timeoutSec ) {
    using clock_t = std::chrono::high_resolution_clock;
    McpCallOutcome out;
    out.request = rpcBody;
    if ( timeoutSec < 1 )
        timeoutSec = 1;
    if ( timeoutSec > 600 )
        timeoutSec = 600;

    auto t0 = clock_t::now();
    try {
        ParsedUrl u = parseMcpEndpointUrl( endpoint );
        if ( !u.error.empty() ) {
            out.errorMsg = u.error;
            return out;
        }
        if ( u.scheme == "https" ) {
            out.errorMsg = "调试工具暂不支持 HTTPS（需要项目编译并启用 httplib OpenSSL）；"
                           "请改用 http:// 或在本机部署反向代理。";
            return out;
        }
        httplib::Client cli( u.host, u.port );
        cli.set_read_timeout( timeoutSec, 0 );
        cli.set_write_timeout( timeoutSec, 0 );
        cli.set_connection_timeout( timeoutSec, 0 );

        const std::string bodyText = rpcBody.dump();

        // 添加 Accept 头，声明支持 application/json 和 text/event-stream
        httplib::Headers headers;
        headers.emplace( "Accept", "application/json, text/event-stream" );
        headers.emplace( "Content-Type", "application/json" );

        auto res = cli.Post( u.path.c_str(), headers, bodyText, "application/json" );
        auto t1 = clock_t::now();
        out.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();

        if ( !res ) {
            out.errorMsg = "httplib::Client 连接失败（无响应）；可能目标主机/端口未开放或超时 (" + std::to_string( timeoutSec ) + "s)";
            return out;
        }
        out.httpStatus = res->status;

        if ( res->body.empty() ) {
            out.response = nullptr;
            out.ok = ( out.httpStatus >= 200 && out.httpStatus < 300 );
            out.errorMsg = out.ok ? "空响应 body" : "HTTP " + std::to_string( out.httpStatus ) + " 且 body 为空";
            return out;
        }

        // 检查是否为 SSE 响应
        if ( isSseContent( *res ) ) {
            out.sseResponse = true;
            mcp::core::json sseJson;
            std::string sseEp;
            if ( parseSseBody( res->body, sseJson, sseEp ) ) {
                out.response = sseJson;
                if ( !sseEp.empty() )
                    out.sseEndpoint = sseEp;
            } else {
                out.errorMsg = "SSE 响应解析失败: " + res->body.substr( 0, 200 );
                out.response = res->body;
                return out;
            }
        } else {
            try {
                out.response = mcp::core::json::parse( res->body );
            } catch ( const std::exception &pe ) {
                out.response = res->body; // 存原样字符串
                out.errorMsg = std::string( "响应 body JSON 解析失败: " ) + pe.what();
                return out;
            }
        }

        // MCP JSON-RPC 成功判据：HTTP 2xx 且响应中不含顶层 "error" 键（或含 "result"）
        out.ok = ( out.httpStatus >= 200 && out.httpStatus < 300 );
        if ( out.ok && out.response.is_object() ) {
            if ( out.response.contains( "error" ) ) {
                out.ok = false;
                try {
                    out.errorMsg = "RPC Error " + out.response.at( "error" ).dump( 2 );
                } catch ( ... ) {
                    out.errorMsg = "RPC Error (详见 response.error)";
                }
            }
        } else if ( !out.ok ) {
            out.errorMsg = "HTTP " + std::to_string( out.httpStatus );
        }
    } catch ( const std::exception &e ) {
        out.errorMsg = std::string( "postJsonRpc 异常: " ) + e.what();
    }
    return out;
}

// ---- SSE 传输模式：先 GET 建立 SSE 会话，获取 endpoint，再 POST ----
// 返回的 outcome 中 sseEndpoint 会被设置，供后续请求复用
static McpCallOutcome postJsonRpcSse( const std::string &endpoint,
                                      const mcp::core::json &rpcBody,
                                      int timeoutSec,
                                      std::string &sseSessionEndpoint ) {
    using clock_t = std::chrono::high_resolution_clock;
    McpCallOutcome out;
    out.request = rpcBody;
    if ( timeoutSec < 1 )
        timeoutSec = 1;
    if ( timeoutSec > 600 )
        timeoutSec = 600;

    auto t0 = clock_t::now();
    try {
        ParsedUrl u = parseMcpEndpointUrl( endpoint );
        if ( !u.error.empty() ) {
            out.errorMsg = u.error;
            return out;
        }
        if ( u.scheme == "https" ) {
            out.errorMsg = "SSE 模式暂不支持 HTTPS";
            return out;
        }
        httplib::Client cli( u.host, u.port );
        cli.set_read_timeout( timeoutSec, 0 );
        cli.set_write_timeout( timeoutSec, 0 );
        cli.set_connection_timeout( timeoutSec, 0 );

        // 如果已有 SSE endpoint，直接 POST 到该 endpoint
        if ( !sseSessionEndpoint.empty() ) {
            const std::string bodyText = rpcBody.dump();
            httplib::Headers headers;
            headers.emplace( "Accept", "application/json, text/event-stream" );
            headers.emplace( "Content-Type", "application/json" );

            auto res = cli.Post( sseSessionEndpoint.c_str(), headers, bodyText, "application/json" );
            auto t1 = clock_t::now();
            out.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();

            if ( !res ) {
                out.errorMsg = "SSE POST 失败: 无响应";
                return out;
            }
            out.httpStatus = res->status;

            if ( res->body.empty() ) {
                out.ok = ( out.httpStatus >= 200 && out.httpStatus < 300 );
                out.errorMsg = out.ok ? "SSE 空响应" : "HTTP " + std::to_string( out.httpStatus );
                return out;
            }

            // 处理 SSE 响应
            if ( isSseContent( *res ) ) {
                out.sseResponse = true;
                mcp::core::json sseJson;
                std::string newEp;
                if ( parseSseBody( res->body, sseJson, newEp ) ) {
                    out.response = sseJson;
                    if ( !newEp.empty() )
                        sseSessionEndpoint = newEp;
                } else {
                    out.errorMsg = "SSE 响应解析失败";
                    return out;
                }
            } else {
                try {
                    out.response = mcp::core::json::parse( res->body );
                } catch ( const std::exception &pe ) {
                    out.errorMsg = std::string( "SSE 响应 JSON 解析失败: " ) + pe.what();
                    return out;
                }
            }

            out.ok = ( out.httpStatus >= 200 && out.httpStatus < 300 );
            if ( out.ok && out.response.is_object() && out.response.contains( "error" ) ) {
                out.ok = false;
                try {
                    out.errorMsg = "RPC Error " + out.response.at( "error" ).dump( 2 );
                } catch ( ... ) {
                    out.errorMsg = "RPC Error";
                }
            }
            return out;
        }

        // 第一步：GET 建立 SSE 会话
        httplib::Headers getHeaders;
        getHeaders.emplace( "Accept", "text/event-stream" );
        auto getRes = cli.Get( u.path.c_str(), getHeaders );

        if ( !getRes ) {
            out.errorMsg = "SSE 会话建立失败: GET 无响应";
            return out;
        }

        if ( !isSseContent( *getRes ) ) {
            auto ctIt = getRes->headers.find( "Content-Type" );
            out.errorMsg = "SSE 会话建立失败: 服务端未返回 text/event-stream (实际: " + ( ctIt != getRes->headers.end() ? ctIt->second : "unknown" ) + ")";
            return out;
        }

        // 从 SSE 响应中提取 endpoint
        mcp::core::json dummyJson;
        std::string ep;
        parseSseBody( getRes->body, dummyJson, ep );
        if ( ep.empty() ) {
            out.errorMsg = "SSE 会话建立失败: 未收到 endpoint 事件";
            return out;
        }

        // 将 endpoint 转为绝对路径
        std::string basePath = u.path;
        // 确保 basePath 不以 / 结尾
        while ( !basePath.empty() && basePath.back() == '/' )
            basePath.pop_back();

        // ep 可能是绝对路径或以 / 开头的相对路径
        if ( !ep.empty() && ep[0] != '/' )
            ep = "/" + ep;
        sseSessionEndpoint = ep;

        // 第二步：POST 到 SSE endpoint
        const std::string bodyText = rpcBody.dump();
        httplib::Headers postHeaders;
        postHeaders.emplace( "Accept", "application/json, text/event-stream" );
        postHeaders.emplace( "Content-Type", "application/json" );

        auto postRes = cli.Post( sseSessionEndpoint.c_str(), postHeaders, bodyText, "application/json" );
        auto t1 = clock_t::now();
        out.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();

        if ( !postRes ) {
            out.errorMsg = "SSE POST 失败: 无响应";
            return out;
        }
        out.httpStatus = postRes->status;

        if ( postRes->body.empty() ) {
            out.ok = ( out.httpStatus >= 200 && out.httpStatus < 300 );
            out.errorMsg = out.ok ? "SSE 空响应" : "HTTP " + std::to_string( out.httpStatus );
            return out;
        }

        if ( isSseContent( *postRes ) ) {
            out.sseResponse = true;
            mcp::core::json sseJson;
            std::string newEp;
            if ( parseSseBody( postRes->body, sseJson, newEp ) ) {
                out.response = sseJson;
                if ( !newEp.empty() )
                    sseSessionEndpoint = newEp;
            } else {
                out.errorMsg = "SSE 响应解析失败";
                return out;
            }
        } else {
            try {
                out.response = mcp::core::json::parse( postRes->body );
            } catch ( const std::exception &pe ) {
                out.errorMsg = std::string( "SSE 响应 JSON 解析失败: " ) + pe.what();
                return out;
            }
        }

        out.ok = ( out.httpStatus >= 200 && out.httpStatus < 300 );
        if ( out.ok && out.response.is_object() && out.response.contains( "error" ) ) {
            out.ok = false;
            try {
                out.errorMsg = "RPC Error " + out.response.at( "error" ).dump( 2 );
            } catch ( ... ) {
                out.errorMsg = "RPC Error";
            }
        }
    } catch ( const std::exception &e ) {
        out.errorMsg = std::string( "postJsonRpcSse 异常: " ) + e.what();
    }
    return out;
}

static mcp::core::json outcomeSummary( const McpCallOutcome &o, const char *stepLabel ) {
    mcp::core::json j = mcp::core::json::object();
    j["step"] = stepLabel ? stepLabel : "request";
    j["ok"] = o.ok;
    j["http_status"] = o.httpStatus;
    j["elapsed_ms"] = o.elapsedMs;
    if ( !o.errorMsg.empty() )
        j["error"] = o.errorMsg;
    if ( !o.request.is_null() )
        j["request"] = o.request;
    if ( !o.response.is_null() )
        j["response"] = o.response;
    return j;
}

// 构造一个带 id 的 JSON-RPC request 对象
static mcp::core::json makeRpcRequest( int id, const std::string &method,
                                       const mcp::core::json &params = mcp::core::json() ) {
    mcp::core::json j = {
        { "jsonrpc", "2.0" },
        { "id", id },
        { "method", method } };
    if ( !params.is_null() )
        j["params"] = params;
    return j;
}
static mcp::core::json makeRpcNotification( const std::string &method,
                                            const mcp::core::json &params = mcp::core::json() ) {
    mcp::core::json j = {
        { "jsonrpc", "2.0" },
        { "method", method } };
    if ( !params.is_null() )
        j["params"] = params;
    return j;
}
} // namespace

// ---------------------------------------------------------------------------
// MCP 调试核心：根据 args 生成调试报告 JSON。
//   - 输入校验失败：返回 {"ok": false, "error": "...", "steps": []}
//   - 正常执行：返回完整 report（含 steps / tool_names / ok 等字段）
//   被 registerToolMcpDebug（MCP 工具）与 POST /api/local/mcp_debug HTTP 代理路由共用。
// ---------------------------------------------------------------------------
static mcp::core::json runMcpDebugReport( const mcp::core::json &args ) {
    mcp::core::json report = mcp::core::json::object();
    report["steps"] = mcp::core::json::array();

    auto inputError = [&]( const std::string &msg ) -> mcp::core::json {
        report["ok"] = false;
        report["error"] = msg;
        return report;
    };

    if ( !args.is_object() || !args.contains( "endpoint" ) || !args["endpoint"].is_string() )
        return inputError( "缺少必需参数: endpoint (string)" );
    if ( !args.contains( "action" ) || !args["action"].is_string() )
        return inputError( "缺少必需参数: action (string)" );

    const std::string endpoint = args["endpoint"].get<std::string>();
    std::string action = args["action"].get<std::string>();
    std::transform( action.begin(), action.end(), action.begin(),
                    []( unsigned char c ) { return std::tolower( c ); } );
    int timeoutSec = 30;
    if ( args.contains( "timeout_seconds" ) && args["timeout_seconds"].is_number_integer() ) {
        timeoutSec = args["timeout_seconds"].get<int>();
        if ( timeoutSec < 1 )
            timeoutSec = 1;
        if ( timeoutSec > 600 )
            timeoutSec = 600;
    }
    bool skipHandshake = false;
    if ( args.contains( "skip_handshake" ) && args["skip_handshake"].is_boolean() )
        skipHandshake = args["skip_handshake"].get<bool>();
    std::string protoVer = "2025-03-26";
    if ( args.contains( "protocol_version" ) && args["protocol_version"].is_string() )
        protoVer = args["protocol_version"].get<std::string>();
    std::string clientName = "MCP-Debug-Tool";
    if ( args.contains( "client_name" ) && args["client_name"].is_string() )
        clientName = args["client_name"].get<std::string>();
    std::string clientVersion = "1.0";
    if ( args.contains( "client_version" ) && args["client_version"].is_string() )
        clientVersion = args["client_version"].get<std::string>();

    // 传输模式: auto / streamable_http / sse
    std::string transport = "auto";
    if ( args.contains( "transport" ) && args["transport"].is_string() ) {
        transport = args["transport"].get<std::string>();
        std::transform( transport.begin(), transport.end(), transport.begin(),
                        []( unsigned char c ) { return std::tolower( c ); } );
    }

    report["target_endpoint"] = endpoint;
    report["action"] = action;
    report["timeout_seconds"] = timeoutSec;
    report["transport_mode"] = transport;
    bool anyError = false;

    // ---- 传输层封装：支持 auto / streamable_http / sse ----
    std::string sseSessionEp; // SSE 会话 endpoint (在多次调用间复用)
    bool sseDetected = false; // auto 模式下是否检测到 SSE 服务端
    int rpcIdCounter = 0;

    auto doPost = [&]( const mcp::core::json &rpcBody ) -> McpCallOutcome {
        McpCallOutcome out;
        if ( transport == "sse" || ( transport == "auto" && sseDetected ) ) {
            out = postJsonRpcSse( endpoint, rpcBody, timeoutSec, sseSessionEp );
        } else {
            out = postJsonRpc( endpoint, rpcBody, timeoutSec );
            // auto 模式: 如果返回 SSE 相关错误，切换到 SSE 模式重试
            if ( transport == "auto" && !out.ok ) {
                bool needsSse = false;
                // 检查 errorMsg 中是否包含 SSE 相关关键词
                if ( !out.errorMsg.empty() ) {
                    needsSse = ( out.errorMsg.find( "text/event-stream" ) != std::string::npos || out.errorMsg.find( "SSE" ) != std::string::npos );
                }
                // 检查响应体中是否包含 SSE 相关信息 (JSON-RPC error.message)
                if ( !needsSse && out.response.is_object() && out.response.contains( "error" ) ) {
                    const auto &err = out.response["error"];
                    if ( err.is_object() && err.contains( "message" ) ) {
                        const auto &msg = err["message"];
                        if ( msg.is_string() ) {
                            const std::string msgStr = msg.get<std::string>();
                            needsSse = ( msgStr.find( "text/event-stream" ) != std::string::npos || msgStr.find( "SSE" ) != std::string::npos );
                        }
                    }
                }
                if ( needsSse ) {
                    // 切换到 SSE 模式重试
                    auto sseOut = postJsonRpcSse( endpoint, rpcBody, timeoutSec, sseSessionEp );
                    sseDetected = true;
                    report["transport_detected"] = "sse";
                    return sseOut;
                }
            }
        }
        return out;
    };

    auto pushStep = [&]( const McpCallOutcome &o, const char *label ) {
        report["steps"].push_back( outcomeSummary( o, label ) );
        if ( !o.ok )
            anyError = true;
    };

    // ---- 辅助：构造 initialize / initialized ----
    auto buildInitializeParams = [&]() {
        // MCP 规范 ClientCapabilities: roots / sampling / experimental
        // 注意：tools / resources / prompts 是服务端能力，客户端不应在 capabilities 中声明
        mcp::core::json p = {
            { "protocolVersion", protoVer },
            { "capabilities", { { "roots", { { "listChanged", true } } }, { "sampling", { { "queueSize", 1 } } } } },
            { "clientInfo", { { "name", clientName }, { "version", clientVersion } } } };
        return p;
    };

    // ---- 对需要握手的动作，先执行 initialize + initialized ----
    bool needHandshake = ( action != "initialize" && action != "raw" && !skipHandshake );
    if ( needHandshake ) {
        auto lastInitialize = doPost( makeRpcRequest( 1, "initialize", buildInitializeParams() ) );
        pushStep( lastInitialize, "initialize" );
        if ( lastInitialize.ok ) {
            // 读取服务端返回的 protocolVersion，供后续步骤参考
            if ( lastInitialize.response.is_object() && lastInitialize.response.contains( "result" ) && lastInitialize.response["result"].is_object() && lastInitialize.response["result"].contains( "protocolVersion" ) ) {
                const auto &svrVer = lastInitialize.response["result"]["protocolVersion"];
                if ( svrVer.is_string() ) {
                    std::string svrVerStr = svrVer.get<std::string>();
                    if ( svrVerStr != protoVer ) {
                        report["server_protocol_version"] = svrVerStr;
                        report["protocol_warning"] = "客户端请求版本 " + protoVer + " 与服务端版本 " + svrVerStr + " 不一致，后续调用可能受影响";
                    }
                }
            }
            auto n = doPost( makeRpcNotification( "notifications/initialized" ) );
            pushStep( n, "notifications/initialized" );
        }
    }

    // ---- 动作分发 ----
    try {
        if ( action == "initialize" ) {
            auto r = doPost( makeRpcRequest( 1, "initialize", buildInitializeParams() ) );
            pushStep( r, "initialize" );
            if ( r.ok ) {
                auto n = doPost( makeRpcNotification( "notifications/initialized" ) );
                pushStep( n, "notifications/initialized" );
            }
        } else if ( action == "probe" ) {
            // probe 已经在 needHandshake 里跑过 initialize + notifications/initialized；
            // 这里再追加 tools_list / resources_list / prompts_list / serverinfo 推断
            mcp::core::json tlParams = mcp::core::json::object();
            if ( args.contains( "limit" ) && args["limit"].is_number_integer() )
                tlParams["limit"] = args["limit"];
            auto t = doPost( makeRpcRequest( 2, "tools/list", tlParams ) );
            pushStep( t, "tools_list" );
            if ( t.ok && t.response.is_object() && t.response.contains( "result" ) ) {
                auto &res = t.response["result"];
                if ( res.contains( "tools" ) && res["tools"].is_array() ) {
                    report["tool_names"] = mcp::core::json::array();
                    for ( auto &tm : res["tools"] ) {
                        if ( tm.is_object() && tm.contains( "name" ) )
                            report["tool_names"].push_back( tm["name"] );
                    }
                }
            }
            auto r2 = doPost( makeRpcRequest( 3, "resources/list", mcp::core::json() ) );
            pushStep( r2, "resources_list" );
            auto r3 = doPost( makeRpcRequest( 4, "prompts/list", mcp::core::json() ) );
            pushStep( r3, "prompts_list" );
        } else if ( action == "tools_list" || action == "resources_list" || action == "prompts_list" ) {
            std::string method = ( action == "tools_list" )       ? "tools/list"
                                 : ( action == "resources_list" ) ? "resources/list"
                                                                  : "prompts/list";
            mcp::core::json params = mcp::core::json::object();
            if ( args.contains( "cursor" ) && args["cursor"].is_string() )
                params["cursor"] = args["cursor"].get<std::string>();
            if ( args.contains( "limit" ) && args["limit"].is_number_integer() )
                params["limit"] = args["limit"].get<int>();
            auto r = doPost( makeRpcRequest( 10, method, params ) );
            pushStep( r, action.c_str() );
            if ( r.ok && action == "tools_list" && r.response.is_object() && r.response.contains( "result" ) ) {
                auto &res = r.response["result"];
                report["tool_names"] = mcp::core::json::array();
                if ( res.contains( "tools" ) && res["tools"].is_array() )
                    for ( auto &tm : res["tools"] )
                        if ( tm.is_object() && tm.contains( "name" ) )
                            report["tool_names"].push_back( tm["name"] );
            }
        } else if ( action == "call_tool" ) {
            if ( !args.contains( "tool_name" ) || !args["tool_name"].is_string() )
                return inputError( "call_tool 需要参数: tool_name" );
            mcp::core::json params = {
                { "name", args["tool_name"].get<std::string>() } };
            if ( args.contains( "tool_args" ) ) {
                if ( args["tool_args"].is_string() ) {
                    // 允许用户传入 JSON 字符串
                    const std::string s = args["tool_args"].get<std::string>();
                    try {
                        params["arguments"] = mcp::core::json::parse( s );
                    } catch ( std::exception &e ) {
                        // 解析失败 → 当作纯字符串对象 {_raw: s} 传入
                        params["arguments"] = { { "_raw", s } };
                        report["warn"] = std::string( "tool_args 字符串解析失败，当作 _raw 原值传入: " ) + e.what();
                    }
                } else {
                    params["arguments"] = args["tool_args"];
                }
            }
            auto r = doPost( makeRpcRequest( 20, "tools/call", params ) );
            pushStep( r, "tools/call" );
        } else if ( action == "read_resource" ) {
            if ( !args.contains( "resource_uri" ) || !args["resource_uri"].is_string() )
                return inputError( "read_resource 需要参数: resource_uri" );
            mcp::core::json params = { { "uri", args["resource_uri"].get<std::string>() } };
            auto r = doPost( makeRpcRequest( 30, "resources/read", params ) );
            pushStep( r, "resources/read" );
        } else if ( action == "get_prompt" ) {
            if ( !args.contains( "prompt_name" ) || !args["prompt_name"].is_string() )
                return inputError( "get_prompt 需要参数: prompt_name" );
            mcp::core::json params = { { "name", args["prompt_name"].get<std::string>() } };
            if ( args.contains( "prompt_args" ) && args["prompt_args"].is_object() )
                params["arguments"] = args["prompt_args"];
            auto r = doPost( makeRpcRequest( 40, "prompts/get", params ) );
            pushStep( r, "prompts/get" );
        } else if ( action == "raw" ) {
            if ( !args.contains( "raw_method" ) || !args["raw_method"].is_string() )
                return inputError( "raw 需要参数: raw_method" );
            const std::string method = args["raw_method"].get<std::string>();
            mcp::core::json params = mcp::core::json();
            if ( args.contains( "raw_params" ) )
                params = args["raw_params"];
            // 若 method 以 "notifications/" 开头或以 "notify/" 前缀声明 → 发 notification（无 id）
            bool isNotify = ( args.contains( "notification" ) && args["notification"].is_boolean()
                                  ? args["notification"].get<bool>()
                                  : ( method.rfind( "notifications/", 0 ) == 0 ) );
            mcp::core::json body = isNotify
                                       ? makeRpcNotification( method, params )
                                       : makeRpcRequest( 99, method, params );
            auto r = doPost( body );
            pushStep( r, isNotify ? ( "raw-notify:" + method ).c_str()
                                  : ( "raw-request:" + method ).c_str() );
        } else {
            return inputError( std::string( "未知 action: " ) + args["action"].dump() + "；允许值: probe / initialize / tools_list / call_tool / resources_list / "
                                                                                        "read_resource / prompts_list / get_prompt / raw" );
        }
    } catch ( const std::exception &e ) {
        anyError = true;
        report["fatal_exception"] = std::string( e.what() );
    }

    report["ok"] = !anyError;
    return report;
}

// ---------------------------------------------------------------------------
// 工具 7: mcp_debug — 对任意 MCP 服务执行握手/工具枚举/调用/资源/提示/原始请求调试
//   注：实际逻辑由 runMcpDebugReport 实现，与 POST /api/local/mcp_debug HTTP 代理路由共用。
// ---------------------------------------------------------------------------
static void registerToolMcpDebug( mcp::McpServer &srv ) {
    using mcp::core::JsonSchemaProperty;
    using mcp::core::Tool;
    using mcp::core::ToolInputSchema;
    mcp::core::Tool t;
    t.name = "mcp_debug";
    t.description =
        "调试目标 MCP 服务（HTTP/JSON-RPC）。支持的 action：\n"
        "  - probe         : 完整诊断 (initialize → notifications/initialized → tools_list)\n"
        "  - initialize    : 仅发送 initialize 握手\n"
        "  - tools_list    : 请求 tools/list，可带 cursor/limit 分页\n"
        "  - call_tool     : 调用指定工具（参数 tool_name, tool_args(object)）\n"
        "  - resources_list: 请求 resources/list，可带 cursor/limit\n"
        "  - read_resource : 读取指定资源 URI（resource_uri）\n"
        "  - prompts_list  : 请求 prompts/list，可带 cursor/limit\n"
        "  - get_prompt    : 获取指定 prompt（prompt_name, prompt_args(object)）\n"
        "  - raw           : 发送自定义 method/params（raw_method, raw_params）\n"
        "注意：除非 action=initialize/raw/设置了 skip_handshake=true，否则在执行目标动作前会先发送 initialize。"
        "\n\n典型调用：\n"
        "  action=probe        endpoint=http://127.0.0.1:8080/mcp\n"
        "  action=call_tool    endpoint=http://host:port/mcp  tool_name=run_shell_command  tool_args={\"command\":\"echo hi\"}\n";
    t.destructive = true; // call_tool 可能触发目标端破坏性操作
    t.idempotent = false;

    ToolInputSchema sch;
    auto addProp = [&]( const std::string &name, mcp::core::JsonSchemaProperty p, bool required = false ) {
        sch.properties.emplace( name, std::move( p ) );
        if ( required )
            sch.required.push_back( name );
    };
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "string";
        p.description = "目标 MCP 服务 HTTP 端点 URL，例如 http://127.0.0.1:8080/mcp";
        addProp( "endpoint", std::move( p ), true );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "string";
        p.description = "动作：probe / initialize / tools_list / call_tool / resources_list / "
                        "read_resource / prompts_list / get_prompt / raw";
        p.enumValues = std::vector<std::string>{ "probe", "initialize", "tools_list", "call_tool", "resources_list",
                                                 "read_resource", "prompts_list", "get_prompt", "raw" };
        addProp( "action", std::move( p ), true );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "integer";
        p.description = "超时（秒），默认 30，范围 1~600";
        addProp( "timeout_seconds", std::move( p ) );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "boolean";
        p.description = "是否跳过 initialize 握手（默认 false）。对 initialize/raw 之外的动作有效。";
        addProp( "skip_handshake", std::move( p ) );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "string";
        p.description = "initialize 用：协议版本，默认 2025-03-26";
        addProp( "protocol_version", std::move( p ) );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "string";
        p.description = "initialize 用：clientInfo.name，默认 MCP-Debug-Tool";
        addProp( "client_name", std::move( p ) );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "string";
        p.description = "initialize 用：clientInfo.version，默认 1.0";
        addProp( "client_version", std::move( p ) );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "string";
        p.description = "call_tool 必需：工具名";
        addProp( "tool_name", std::move( p ) );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "object";
        p.description = "call_tool 可选：工具参数 object（或 JSON 字符串）";
        addProp( "tool_args", std::move( p ) );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "string";
        p.description = "read_resource 必需：资源 URI（resource://... 或 https://...）";
        addProp( "resource_uri", std::move( p ) );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "string";
        p.description = "get_prompt 必需：prompt 名";
        addProp( "prompt_name", std::move( p ) );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "object";
        p.description = "get_prompt 可选：prompt 参数";
        addProp( "prompt_args", std::move( p ) );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "string";
        p.description = "raw 必需：自定义 JSON-RPC method";
        addProp( "raw_method", std::move( p ) );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.description = "raw 可选：自定义 JSON-RPC params（任意 json 值，object/array/...）";
        addProp( "raw_params", std::move( p ) );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "string";
        p.description = "分页 cursor：tools_list / resources_list / prompts_list 可选";
        addProp( "cursor", std::move( p ) );
    }
    {
        mcp::core::JsonSchemaProperty p;
        p.type = "integer";
        p.description = "分页 limit：tools_list / resources_list / prompts_list 可选";
        addProp( "limit", std::move( p ) );
    }

    t.inputSchema = std::move( sch );

    // 实际逻辑委托给 runMcpDebugReport（与 POST /api/local/mcp_debug HTTP 代理路由共用同一实现）
    srv.registerTool( std::move( t ), []( const std::optional<mcp::core::json> &args ) -> mcp::core::ToolCallResult {
        mcp::core::json a = args.value_or( mcp::core::json::object() );
        auto report = runMcpDebugReport( a );
        // 输入校验失败：保留原有"文本错误"返回行为（便于 AI 客户端直观看到错误原因）
        if ( !report.value( "ok", false ) && report.contains( "error" ) && report["error"].is_string() )
            return makeTextResult( report["error"].get<std::string>(), true );
        return makeJsonResult( report, !report.value( "ok", false ) );
    } );
}

// ===========================================================================
// Resource 示例: resource://tools/readme
// ===========================================================================
static void registerResources( mcp::McpServer &srv ) {
    mcp::core::Resource readme;
    readme.uri = "resource://tools/readme";
    readme.name = "Tools MCP README";
    readme.description = "本 MCP 服务器随附的说明文档，解释已注册工具与安全策略。";
    readme.mimeType = "text/markdown";
    srv.registerResource( std::move( readme ), []( const std::string &uri ) {
        (void)uri;
        mcp::core::ResourceContent c;
        c.uri = "resource://tools/readme";
        c.mimeType = "text/markdown";
        static const char *kText = R"MCPDOC(
# Tools MCP Server 说明

本服务通过 `POST /mcp` 暴露 MCP (Model Context Protocol) JSON-RPC 接口。
**安全策略**：默认仅允许本机 (localhost) 访问，外部请求将被拒绝。

## 已注册工具

| 工具名              | 描述                                                     |
|---------------------|----------------------------------------------------------|
| echo                | 烟雾测试，回显 message                                    |
| run_shell_command   | 执行 shell 命令（cmd/sh，默认 30s 超时）                 |
| list_directory      | 列出指定目录内容（类型/大小/修改时间）                    |
| read_text_file      | 读取文本文件（最大 1MB，可设 offset/max_bytes）           |
| get_time            | 获取本机当前时间戳和格式化字符串                          |
| get_server_info     | 获取 Tools 应用基础信息及当前配置                        |

## 典型交互流程

1. 调用 `initialize`：客户端提交 protocolVersion、capabilities、clientInfo
2. 收到 Response 后发送 `notifications/initialized`
3. `tools/list` / `resources/list` / `prompts/list` 发现能力
4. 使用 `tools/call` 执行工具

## 开发备注

- 路由文件: `src/routes/Mcp.cpp`
- 协议封装: `src/Mcp.h` / `src/Mcp.cpp`
- 服务内核: `src/core/McpServer.h` / `src/core/McpServer.cpp`
)MCPDOC";
        c.text = std::string( kText );
        mcp::core::ReadResourceResult r;
        r.contents.push_back( std::move( c ) );
        return r;
    } );
}

// ===========================================================================
// Prompt 示例: system-reviewer
// ===========================================================================
static void registerPrompts( mcp::McpServer &srv ) {
    mcp::core::Prompt p;
    p.name = "system-reviewer";
    p.description = "生成针对指定路径代码/目录的审查 Prompt 模板，给 AI 助手一套检查点。";
    mcp::core::PromptArgument arg;
    arg.name = "path";
    arg.description = "目标目录或文件路径";
    arg.required = true;
    p.arguments.push_back( std::move( arg ) );

    srv.registerPrompt( std::move( p ),
                        []( const std::optional<mcp::core::json> &args ) -> mcp::core::GetPromptResult {
                            std::string target = ".";
                            if ( args && args->contains( "path" ) )
                                target = ( *args )["path"].get<std::string>();
                            mcp::core::GetPromptResult result;
                            result.description = "代码/目录审查任务模板";

                            mcp::core::Content::Text sys;
                            sys.text = "你是一名资深代码审查者。请针对用户提供的路径下的代码进行审查，"
                                       "关注：1) 编译与类型安全；2) 资源释放与生命周期；3) 并发安全；"
                                       "4) 输入校验与边界；5) 可维护性与命名。针对每条问题给出代码位置+建议。";
                            mcp::core::PromptMessage m1{ mcp::core::Role::User, std::move( sys ) };
                            result.messages.push_back( std::move( m1 ) );

                            mcp::core::Content::Text user;
                            user.text = "请对路径 `" + target + "` 执行审查。"
                                                                " 先列出目录结构，再按关注点逐项总结，最后给出 Top 10 问题。";
                            mcp::core::PromptMessage m2{ mcp::core::Role::User, std::move( user ) };
                            result.messages.push_back( std::move( m2 ) );
                            return result;
                        } );
}

// ===========================================================================
// Roots Provider 示例: 暴露当前工作目录和配置目录
// ===========================================================================
static void registerRootsProvider( mcp::McpServer &srv ) {
    srv.setRootsProvider( []() -> std::vector<mcp::core::Root> {
        std::vector<mcp::core::Root> roots;
        try {
            char cwdBuf[4096];
#ifdef _WIN32
            _getcwd( cwdBuf, sizeof( cwdBuf ) );
#else
            getcwd( cwdBuf, sizeof( cwdBuf ) );
#endif
            mcp::core::Root r1;
            r1.uri = std::string( "file://" ) + cwdBuf;
            r1.name = "Working directory";
            roots.push_back( std::move( r1 ) );
        } catch ( ... ) {
        }
        try {
            mcp::core::Root r2;
            r2.uri = std::string( "file://" ) + Config::getTempPath();
            r2.name = "Tools temp directory";
            roots.push_back( std::move( r2 ) );
        } catch ( ... ) {
        }
        return roots;
    } );
}

// ===========================================================================
// 注册所有默认工具/资源/提示，只跑一次
// ===========================================================================
static void ensureDefaultRegistration( ::mcp::McpServer &srv ) {
    if ( g_registered.load( std::memory_order_acquire ) )
        return;
    std::lock_guard<std::mutex> lk( g_mcpServerMutex );
    if ( g_registered.load( std::memory_order_relaxed ) )
        return;

    registerToolEcho( srv );
    registerToolRunCommand( srv );
    registerToolListDir( srv );
    registerToolReadFile( srv );
    registerToolGetTime( srv );
    registerToolServerInfo( srv );
    registerToolMcpDebug( srv );

    registerResources( srv );
    registerPrompts( srv );
    registerRootsProvider( srv );

    g_registered.store( true, std::memory_order_release );
    LOG_INFO << "MCP ensureDefaultRegistration: done";
}

// ===========================================================================
// 路由注册
// ===========================================================================
void registerMcpRoutes( httplib::Server &svr ) {
    auto &srv = getMcpServer();
    ensureDefaultRegistration( srv );

    // --- POST /mcp: Streamable HTTP JSON-RPC 入口 ---
    svr.Post( "/mcp", [&srv]( const httplib::Request &req, httplib::Response &res ) {
        // 安全守卫：仅本机可调用 (可执行 shell、读文件等)
        if ( Server::guardLocalhost( req, res ) )
            return;

        const std::string &body = req.body;
        LOG_DEBUG << "MCP POST (来自:" << req.remote_addr << ") body前200: "
                  << body.substr( 0, std::min<size_t>( 200, body.size() ) );

        mcp::core::json output;
        try {
            if ( body.empty() ) {
                auto r = mcp::core::makeErrorResponse( mcp::core::RequestId{}, mcp::core::ErrorCode::InvalidRequest, "请求体为空" );
                output = mcp::core::json( r );
            } else {
                mcp::core::json j = mcp::core::json::parse( body );
                output = srv.handleBatchOrSingle( j );
            }
            // 纯通知（batch 全是 notification 时 handleBatchOrSingle 返回空数组）
            Server::sendJson( res, output, 200 );
        } catch ( const mcp::core::json::parse_error &e ) {
            auto r = mcp::core::makeErrorResponse( mcp::core::RequestId{}, mcp::core::ErrorCode::ParseError,
                                                   std::string( "Parse error: " ) + e.what() );

            Server::sendJson( res, mcp::core::json( r ), 200 );
        } catch ( const std::invalid_argument &e ) {
            auto r = mcp::core::makeErrorResponse( mcp::core::RequestId{}, mcp::core::ErrorCode::InvalidRequest, e.what() );
            Server::sendJson( res, mcp::core::json( r ), 200 );
        } catch ( const std::exception &e ) {
            auto r = mcp::core::makeErrorResponse( mcp::core::RequestId{}, mcp::core::ErrorCode::InternalError, e.what() );
            Server::sendJson( res, mcp::core::json( r ), 500 );
        }
    } );

    svr.Get( "/mcp", []( const httplib::Request &req, httplib::Response &res ) {
        if ( Server::guardLocalhost( req, res ) )
            return;
        res.status = 500;
        res.set_content( "MCP 服务不支持 SSE 模式，请使用 Streamable HTTP 模式", "text/plain" );
    } );
}

} // namespace routes::mcpRoutes

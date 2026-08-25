#include <iostream>
#include <map>
#include <list>
#include <stack>
#include <queue>
#include <deque>
#include <string>
#include <cstring>
#include "common/Logger.hpp"
#include <nlohmann/json.hpp>

#include <boost/asio.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>
#include <boost/process/v2/start_dir.hpp>
#include <boost/process/v2/environment.hpp>
#if defined( _WIN32 )
#include <boost/process/v2/windows/creation_flags.hpp>
#include <boost/process/v2/windows/show_window.hpp>
#endif

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

using json = nlohmann::json;

namespace bpw = boost::process::v2::windows;

int main( int argc, char *argv[] ) {

    return 0;
}

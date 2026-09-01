#ifndef ROUTES_MCP_H
#define ROUTES_MCP_H

#include <httplib.h>
#include "core/McpServer.h"

namespace routes::mcpRoutes {

void registerMcpRoutes( httplib::Server &svr );

mcp::McpServer &getMcpServer();

} // namespace routes::mcpRoutes

#endif // ROUTES_MCP_H

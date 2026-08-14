#ifndef ADMIN_H
#define ADMIN_H

#include <httplib.h>
#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include "core/Auth.hpp"
#include "core/Utils.h"
#include "core/Server.h"
#include "common/App.h"
#include "common/Config.h"

namespace routes::admin {

namespace fs = std::filesystem;

void registerAdminRoutes( httplib::Server &svr );

// Serialize share with all permissions (admin view)
Server::json shareToJson( const Database::Row &r );

void adminUsers( const httplib::Request &req, httplib::Response &res );

void adminFsBrowse( const httplib::Request &req, httplib::Response &res );

void adminSharesList( const httplib::Request &req, httplib::Response &res );

void adminSharesCreate( const httplib::Request &req, httplib::Response &res );

void adminSharesUpdate( const httplib::Request &req, httplib::Response &res );

void adminSharesDelete( const httplib::Request &req, httplib::Response &res );

// 根据 id 查询启动时传入的路径列表（本机访问）
void adminParameterPaths( const httplib::Request &req, httplib::Response &res );

// 读取应用配置 app_config（本机访问，返回带类型的 JSON）
void adminConfigGet( const httplib::Request &req, httplib::Response &res );

// 更新应用配置 app_config（本机访问，部分更新，重启后生效）
void adminConfigPut( const httplib::Request &req, httplib::Response &res );

// 创建用户（本机访问）
void adminUserCreate( const httplib::Request &req, httplib::Response &res );

// 更新用户（本机访问，修改昵称/可选重置密码）
void adminUserUpdate( const httplib::Request &req, httplib::Response &res );

// 删除用户（本机访问，同时清理其会话）
void adminUserDelete( const httplib::Request &req, httplib::Response &res );

// 列出当前活跃的文件传输任务 + 速度历史（本机访问）
void adminTransfers( const httplib::Request &req, httplib::Response &res );
} // namespace routes::admin
#endif // ADMIN_H

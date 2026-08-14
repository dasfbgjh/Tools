# Toolbox - 开发者工具箱

[![C++](https://img.shields.io/badge/C++-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![CMake](https://img.shields.io/badge/CMake-3.10+-green.svg)](https://cmake.org/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Version](https://img.shields.io/badge/Version-1.0.0-orange.svg)](#)

一款面向开发者的综合性工具箱应用，集成了上百种实用工具，采用 C++ 后端 + Web 前端架构，通过 WebView 提供原生桌面体验。

## ✨ 功能特性

### 🔐 账户与权限系统
- 用户注册 / 登录 / 会话管理
- 基于 Token 的身份认证
- 用户昵称与偏好设置
- 管理员后台（用户管理、分享管理、传输监控）

### 📋 团队剪贴板
- 创建/加入团队，支持多人协作
- 剪贴板历史记录（文本、HTML、图片、文件）
- 团队邀请码机制
- 文件下载令牌管理

### 📁 文件分享服务
- 文件/目录分享
- 细粒度权限控制（访问/下载/上传/删除/重命名）
- 用户级与全局权限配置

### 🛠 在线小工具（100+）
#### 文本处理
- 文本大小写转换、文本统计、文本对比（Diff）
- 文本去空格、NATO 字母表转换、文本混淆器
- Lorem Ipsum 生成器、数字缩写生成器、字符串 Slugify
- 罗马数字转换、占位符 SVG 生成器

#### 编码转换
- Base64 图片互转、URL 编码、Unicode 转换
- HTML 实体转换、HTML ↔ Markdown 互转
- JSON/YAML/TOML/Properties 格式互转
- 字符编码转换、二维码生成、WiFi 二维码

#### 加密与安全
- 密码生成器、密码强度分析
- JWT 解码器、BIP39 助记词
- OTP 一次性密码、UUID 生成器、Token 生成器
- 基础认证生成器、加密工具箱

#### 网络工具
- IPv4/IPv6 地址转换、子网计算器、IP 范围扩展
- IPv6 ULA 生成器、随机端口生成器
- MAC 地址生成/查询、HTTP 状态码速查
- Cron 表达式生成器、User-Agent 解析器
- URL 解析器、Safelink 解码器、IBAN 验证
- 手机号解析、邮箱规范化

#### 开发辅助
- JSON 编辑器/格式化/对比、JSON 转换器
- YAML 查看器、TOML 转换器
- 代码格式化、HTTP 请求测试器
- HTML 所见即所得编辑器、CSS 渐变生成器
- 图标设计器、Emoji 选择器
- Git 备忘录、正则表达式测试/备忘录
- 键码信息查询、ASCII 艺术字生成器

#### 计算工具
- 数学表达式计算器、百分比计算器
- ETA 预估时间计算器、日期计算器
- 时间戳转换器、时区转换器
- 温度转换器、数字进制转换
- CHMOD 权限计算器、基准测试构建器

#### 图片处理
- 图片格式转换、图片压缩、图片水印
- OCR 文字识别（基于 RapidOCR，支持中英文）
- 图标设计器、图片标注工具

#### PDF 工具
- PDF 压缩、PDF 格式转换、PDF 管理
- PDF 水印、PDF 签名验证

### 💻 本地系统工具
- **HTTP 服务器管理** - 多实例 HTTP 文件服务器，支持目录挂载与代理
- **进程管理器** - 自定义进程配置，环境变量管理，自动启动，实时状态监控
- **系统监视器** - CPU/内存/磁盘/网络/GPU/电池实时监控（基于 hwinfo）
- **批处理重命名** - 文件批量重命名工具
- **FFmpeg 工具** - 音视频转码、下载（集成 yt-dlp）
- **证书工具** - SSL/TLS 证书生成与管理
- **文档阅读器** - 多源文档聚合浏览
- **摄像头录制** - 本地摄像头视频录制

## 🏗 项目架构

```
Toolbox/
├── src/                          # C++ 后端源码
│   ├── main.cpp                  # 程序入口
│   ├── common/                   # 通用模块
│   │   ├── App.cpp/h             # 应用核心类
│   │   ├── Config.cpp/h          # 配置解析（CLI11）
│   │   ├── SystemTray.cpp/h      # 系统托盘
│   │   ├── EventLoop.cpp/h       # 事件循环
│   │   └── Logger.hpp            # 日志系统
│   ├── core/                     # 核心功能
│   │   ├── Server.cpp/h          # HTTP 服务器（httplib）
│   │   ├── Database.cpp/h        # SQLite 数据库封装
│   │   ├── WebviewWrapper.cpp/h  # WebView 封装
│   │   ├── HttpServerManager.cpp/h  # 本地HTTP服务器管理
│   │   ├── ProcManager.cpp/h     # 进程管理
│   │   ├── TransferTracker.cpp/h # 传输追踪
│   │   ├── Sha256.cpp/h          # SHA256 哈希
│   │   └── Utils.cpp/h           # 工具函数
│   └── routes/                   # API 路由
│       ├── Auth.cpp/h            # 认证接口
│       ├── Admin.cpp/h           # 管理员接口
│       ├── Clipboard.cpp         # 剪贴板接口
│       ├── Teams.cpp             # 团队接口
│       ├── FileService.cpp       # 文件服务接口
│       ├── Tools.cpp             # 在线工具接口
│       ├── PdfTools.cpp          # PDF 工具接口
│       ├── LocalTools.cpp        # 本地工具接口
│       ├── FfmpegTool.cpp        # FFmpeg 接口
│       ├── CertTool.cpp          # 证书工具接口
│       ├── DocTool.cpp           # 文档工具接口
│       ├── SysMonitor.cpp        # 系统监控接口
│       └── OcrTools.cpp          # OCR 工具接口
├── resource/                     # 前端资源
│   ├── data/
│   │   ├── html/                 # 前端页面
│   │   │   ├── admin/            # 管理后台
│   │   │   ├── auth/             # 登录注册页
│   │   │   ├── clipboard/        # 剪贴板页面
│   │   │   ├── fileservice/      # 文件分享页面
│   │   │   ├── local-tools/      # 本地系统工具页面
│   │   │   ├── tools/            # 100+ 在线小工具页面
│   │   │   ├── app.js            # 主应用脚本
│   │   │   ├── api.js            # API 封装
│   │   │   └── theme.js          # 主题管理
│   │   └── sql/
│   │       └── schema.sql        # 数据库表结构
│   └── src/                      # 资源编译
│       ├── generate.cpp          # 资源打包工具
│       └── resource.cpp          # 资源嵌入
├── 3rdparty/                     # 第三方库
│   ├── RapidOcr/                 # OCR 引擎
│   ├── httplib/                  # C++ HTTP 库
│   ├── hwinfo/                   # 硬件信息库
│   ├── webview/                  # WebView 跨平台库
│   ├── webview2/                 # Windows WebView2 SDK
│   ├── qrcodegen/                # 二维码生成
│   ├── stb/                      # STB 图像库
│   ├── tray/                     # 系统托盘库
│   └── cli11/                    # 命令行解析库
├── appInfo/                      # 应用元信息
│   ├── AppInfo.cmake             # 版本信息配置
│   └── config/                   # Windows 资源（图标/Manifest）
├── bin/                          # 编译输出目录
│   ├── database.db               # SQLite 数据库文件
│   └── RapidOCR/                 # OCR 模型文件
└── CMakeLists.txt                # 顶层构建脚本
```

## 🚀 快速开始

### 环境要求

| 依赖 | 版本要求 |
|------|----------|
| CMake | ≥ 3.10 |
| C++ 编译器 | 支持 C++17 标准 |
| Windows SDK | ≥ 10.0（Windows 平台） |
| WebView2 Runtime | 已预装（Windows 10+） |

### 构建步骤

#### 使用 MSYS2 MinGW64（推荐）

```bash
# 1. 克隆项目
git clone <repository-url>
cd Toolbox

# 2. 创建构建目录
mkdir build && cd build

# 3. 配置 CMake
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..

# 4. 编译
cmake --build .

# 5. 运行（输出在 bin/ 目录）
../bin/Tools.exe
```

### 依赖库说明

项目依赖以下静态库，请确保已正确安装或放置在 MinGW 库目录中：

- `libsqlite3.a` - SQLite3 数据库
- `libboost_process-mt.a` - Boost.Process 进程管理
- `libboost_filesystem-mt.a` - Boost.Filesystem 文件系统
- `libssl.a` / `libcrypto.a` - OpenSSL（测试用）

## 📦 目录结构说明

### 数据库

应用首次启动时会自动创建 `bin/database.db`（SQLite），包含以下核心数据表：

| 表名 | 说明 |
|------|------|
| `users` | 用户账户信息 |
| `sessions` | 登录会话 Token |
| `clipboard_teams` | 剪贴板团队 |
| `clipboard_team_members` | 团队成员关系 |
| `clipboard_items` | 剪贴板历史记录 |
| `file_shares` | 文件分享条目 |
| `file_share_permissions` | 分享权限 |
| `http_servers` | 本地 HTTP 服务器配置 |
| `proc_configs` | 进程管理器配置 |
| `memos` | 备忘录数据 |
| `doc_sources` | 文档阅读源配置 |
| `app_config` | 全局应用配置 |
| `user_settings` | 用户个性化设置 |

### OCR 模型

`bin/RapidOCR/` 目录下包含 OCR 模型文件：
- `ch-mobile-v4/` - 移动端模型（轻量快速）
- `ch-server-v4/` - 服务端模型（高精度）
- `RapidOcrOnnx.dll` - OCR 引擎运行时

每个模型目录包含：
- `det.onnx` - 文本检测模型
- `cls.onnx` - 方向分类模型
- `rec.onnx` - 文本识别模型
- `keys.txt` - 字符字典

## 🖱 使用方式

1. **启动应用**：双击 `Tools.exe` 或命令行运行
2. **系统托盘**：应用启动后最小化至托盘，右键托盘图标可：
   - 打开主界面
   - 打开设置
   - 启用/禁用开机自启
   - 退出应用
3. **Web 访问**：也可直接通过浏览器访问 `http://localhost:<端口>` 使用

### 命令行参数

```
Tools.exe [OPTIONS]

Options:
  -h,--help                   打印帮助信息
  -p,--port INT               HTTP 服务器端口（默认：自动分配）
  --no-gui                    不启动 WebView 窗口，仅运行后台服务
  --debug                     启用调试日志
```

## 🔧 开发调试

### Debug 模式

Debug 构建会直接从磁盘读取前端资源文件，无需重新编译资源：

```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
```

修改 `resource/data/html/` 下的文件后，刷新页面即可看到变更。

### 资源打包

Release 模式下，所有前端资源会被编译为二进制嵌入可执行文件中。修改前端后需重新编译：

```bash
cmake --build . --target tools_resource
cmake --build .
```

## 🙏 第三方库致谢

本项目的成长离不开众多优秀开源项目的支持，在此向所有贡献者致以最诚挚的感谢！

### C++ 后端库

| 库名称 | 作用 |
|--------|--------|
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | 提供 HTTP 服务器 |
| [hwinfo](https://github.com/lfreist/hwinfo) | 获取硬件信息 |
| [tray](https://github.com/zserge/tray) | 系统托盘 |
| [stb](https://github.com/nothings/stb) | 图像处理 |
| [qrcodegen](https://github.com/nayuki/qr-code-generator-library) | 二维码生成 |
| [CLI11](https://github.com/CLIUtils/CLI11) | 命令行解析库 |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON 处理 |
| [SQLite3](https://www.sqlite.org/) | 数据库存储 |
| [Boost](https://www.boost.org/) | 进程管理 |
| [OpenSSL](https://www.openssl.org/) | https 服务依赖 |
| [RapidOCR](https://github.com/RapidAI/RapidOCR) | 图片 OCR 功能 |
| [ONNX Runtime](https://onnxruntime.ai/) | RapidOCR 依赖 |
| [webview](https://github.com/webview/webview) | 提供 WebView 功能 |

### 前端库

| 库名称 | 作用 |
|--------|--------|
| [JiSuXiang](https://github.com/star7th/jisuxiang) | web 工具参照 JiSuXiang 原生实现 |
| [it-tools](https://github.com/CorentinTh/it-tools) | web 工具参照 it-tools 原生实现 |
| [Font Awesome](https://fontawesome.com/) | 图标 |
| [marked.js](https://github.com/markedjs/marked) | Markdown 解析 |

---

## 📄 开源许可证声明

**本项目完全开源。** 本项目自身代码在默认情况下遵循各第三方库许可证的约束；如无特别约束，可按 MIT 许可证使用。

---

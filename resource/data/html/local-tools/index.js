(function () {
    'use strict';
    var App = window.App;
    var grid = document.getElementById('tool-grid');

    // localhost 检查
    var host = window.location.hostname;
    if (host !== '127.0.0.1' && host !== 'localhost' && host !== '::1') {
        App.showBanner ? null : null;
        grid.innerHTML = '<div class="admin-empty"><div class="icon">🚫</div><h3>本页面仅限本机访问</h3><p>请在 127.0.0.1 / localhost 打开</p></div>';
        return;
    }

    var tools = [
        { code: 'batch-rename', name: '批量重命名', desc: '按规则批量重命名本机文件或文件夹，支持编号、替换、插入、转换大小写', icon: 'Aa', url: './batch_rename.html' },
        { code: 'http-servers', name: 'HTTP 路径挂载 / 代理', desc: '添加端口并配置不同路径挂载本地目录或代理请求，支持启动/停止监听', icon: '⇄', url: './http_servers.html' },
        { code: 'process-mgr', name: '服务进程管理', desc: '配置并启动/停止本机进程，实时查看输出，可配置命令、工作目录、环境变量等', icon: '▶', url: './processes.html' },
        { code: 'ffmpeg', name: 'FFmpeg 视频处理', desc: '调用本地 FFmpeg 进行截取、格式转换、压缩，支持 GPU 编码与并行任务', icon: '🎬', url: './ffmpeg.html' },
        { code: 'ffmpeg-download', name: 'FFmpeg 视频下载 / 录制', desc: '从 m3u8/HTTP/HTTPS 拉取视频，或录制 RTMP/RTSP 直播流到本地', icon: '⬇', url: './ffmpeg_download.html' },
        { code: 'cert', name: '自签名证书生成', desc: '基于本地 OpenSSL 生成自签名 X.509 证书，支持 IP/DNS SAN 与自定义主体信息', icon: '🔒', url: './cert_tool.html' },
        { code: 'docs', name: '文档阅读', desc: '指定本机目录，递归浏览 Markdown / HTML 文档，左侧目录树 + 右侧内容', icon: '📚', url: './docs.html' },
        { code: 'sys-monitor', name: '系统监测', desc: '查看本机硬件信息（CPU/内存/磁盘/网络）与实时占用率，基于 hwinfo 库', icon: '📊', url: './sys_monitor.html' },
        { code: 'image-annotator', name: '图片标注工具', desc: '截屏图片标注工具，支持遮罩选区、绘制图形、添加文字、模糊、撤销恢复和保存导出', icon: '✏️', url: './image_annotator.html' }
    ];

    tools.forEach(function (t) {
        var card = document.createElement('a');
        card.className = 'admin-card';
        card.href = t.url;
        card.innerHTML =
            '<div class="admin-card-icon">' + t.icon + '</div>' +
            '<div class="admin-card-body">' +
            '<h3>' + Api.escapeHtml(t.name) + '</h3>' +
            '<p>' + Api.escapeHtml(t.desc) + '</p>' +
            '</div>';
        grid.appendChild(card);
    });
})();
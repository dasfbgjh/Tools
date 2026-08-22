(function (window) {
    'use strict';
    const ICON_SUN = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="5"/><line x1="12" y1="1" x2="12" y2="3"/><line x1="12" y1="21" x2="12" y2="23"/><line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/><line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/><line x1="1" y1="12" x2="3" y2="12"/><line x1="21" y1="12" x2="23" y2="12"/><line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/><line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/></svg>';
    const ICON_MOON = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/></svg>';
    const ICON_MEMO = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 4h12a2 2 0 0 1 2 2v14l-4-3-4 3-4-3-4 3V6a2 2 0 0 1 2-2z"/><line x1="8" y1="9" x2="16" y2="9"/><line x1="8" y1="13" x2="13" y2="13"/></svg>';
    const ICON_LOGOUT = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" y1="12" x2="9" y2="12"/></svg>';

    const App = window.App;

    const navItems = [
        { id: 'tools', name: '工具箱', path: './tools/' },
        { id: 'clipboard', name: '剪切板', path: './clipboard/', isBrand: true },
        { id: 'fileservice', name: '文件服务器', path: './fileservice/' },
        { id: 'localtools', name: '本机工具', path: './local-tools/', localhostOnly: true },
        { id: 'admin', name: '管理员', path: './admin/', localhostOnly: true }
    ];
    const defaultPage = navItems[0];
    let currentPageId = defaultPage.id;
    const headerEl = document.getElementById('app-header');
    const iframe = document.getElementById('content-frame');

    function toggleTheme() {
        var current = document.documentElement.getAttribute('data-theme') || 'dark';
        var next = current === 'dark' ? 'light' : 'dark';
        window.Theme.update(next);

        // 无论是否登录，都调用接口保存到数据库
        Api.settings.update({ theme: next }).catch(function () { });
        var btn = document.getElementById('theme-btn');
        if (btn) {
            btn.innerHTML = next === 'dark' ? ICON_SUN : ICON_MOON;
            btn.title = next === 'dark' ? '切换浅色主题' : '切换深色主题';
        } else {
            renderShellHeader();
        }
    }

    // ===== QR Code modal =====
    function openQrModal() {
        var overlay = document.createElement('div');
        overlay.className = 'modal-overlay';
        overlay.id = '__qr_modal';
        overlay.innerHTML =
            '<div class="modal" style="width: 400px; height: auto;">' +
            '<div class="modal-header" style="padding: 12px 14px;">' +
            '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="margin-right: 8px;"><path d="M3 3h7v7H3V3zm2 2v3h3V5H5zm9-2h7v7h-7V3zm2 2v3h3V5h-3zM3 14h7v7H3v-7zm2 2v3h3v-3H5zm9-2h2v2h-2v-2zm4 0h3v3h-2v-1h-1v-2zm-4 4h2v3h-2v-3zm4 1h1v-1h2v3h-3v-2zm0-2h1v1h-1v-1z"/></svg>' +
            '<span class="modal-title" style="font-size: 14px;">扫描二维码访问</span>' +
            '<button class="btn btn-ghost btn-sm" data-modal-close>×</button>' +
            '</div>' +
            '<div class="modal-body" id="qr-body" style="padding: 14px;">获取网络地址中...</div>' +
            '</div>';
        document.body.appendChild(overlay);

        overlay.addEventListener('click', function (e) {
            if (e.target === overlay || e.target.hasAttribute('data-modal-close')) {
                var modal = document.getElementById('__qr_modal');
                if (modal) modal.remove();
            }
        });

        var body = document.getElementById('qr-body');
        if (!body) return;
        Api.localTools.localIp().then(function (data) {
            if (!data.success || !data.ips || !data.ips.length) {
                body.innerHTML = '<p style="text-align: center; color: #94a3b8;">无可用网络地址</p>';
                return;
            }
            var port = window.location.port ? ':' + window.location.port : '';
            var html = '<div style="display: flex; flex-direction: column; gap: 14px;">' +
                '<div>' +
                '<label style="font-size: 13px; margin-bottom: 6px; display: block;">选择网络地址</label>' +
                '<select id="qr-ip" class="select" style="width: 100%; font-size: 13px;">';
            data.ips.forEach(function (ip) { html += '<option value="' + escapeHtml(ip) + '">' + escapeHtml(ip) + '</option>'; });
            html += '</select></div>' +
                '<div style="text-align: center;">' +
                '<img id="qr-img" src="" alt="QR" style="width: 200px; height: 200px; border-radius: 8px; background: #fff;"/>' +
                '</div>' +
                '<div style="display: flex; gap: 8px;">' +
                '<input id="qr-url" class="input" readonly style="flex: 1; font-size: 13px; padding: 8px;" />' +
                '<button class="btn btn-outline btn-sm" id="qr-copy" style="padding: 8px;" title="复制链接">' +
                '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>' +
                '</button>' +
                '</div>' +
                '<p style="font-size: 12px; color: #94a3b8; text-align: center; margin: 0;">扫描二维码或复制上方链接到浏览器打开</p>' +
                '</div>';
            body.innerHTML = html;
            var sel = document.getElementById('qr-ip');
            var img = document.getElementById('qr-img');
            var urlInput = document.getElementById('qr-url');
            function render(ip) {
                var url = 'http://' + ip + port;
                urlInput.value = url;
                img.src = Api.tools.qrcode(url);
            }
            sel.addEventListener('change', function () { render(sel.value); });
            render(data.ips[0]);
            document.getElementById('qr-copy').addEventListener('click', function () {
                urlInput.select();
                try {
                    document.execCommand('copy');
                    var btn = this;
                    btn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#22c55e" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"/></svg>';
                    setTimeout(function () {
                        btn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>';
                    }, 1500);
                } catch (e) { }
            });
        }).catch(function () {
            body.innerHTML = '<p class="text-danger text-center">获取网络地址失败</p>';
        });
    }

    function escapeHtml(s) {
        return window.App.escapeHtml(s);
    }

    function savePage(page) {
        try {
            localStorage.setItem('tools-current-page', page);
        } catch (e) {

        }
        Api.settings.update({ currentPageId: page }).catch(function () { });
    }

    function getSavedPage() {
        try {
            return localStorage.getItem('tools-current-page');
        } catch (e) {
            return null;
        }
    }

    function getNavUrl(pageId) {
        var item = navItems.find(function (n) { return n.id === pageId; });
        var base = item ? item.path : defaultPage.path;
        // 将当前URL的所有查询参数（排除page）传递给iframe
        var qp = new URLSearchParams(window.location.search);
        qp.delete('page');
        var qs = qp.toString();
        if (qs) {
            var sep = base.indexOf('?') === -1 ? '?' : '&';
            return base + sep + qs;
        }
        return base;
    }

    function toAbsolutePath(relativePath) {
        if (!relativePath) return relativePath;

        if (relativePath.startsWith('http://') || relativePath.startsWith('https://')) {
            var url = new URL(relativePath);
            return url.pathname;
        }

        var currentPath = window.location.pathname;
        var basePath = currentPath.substring(0, currentPath.lastIndexOf('/') + 1);

        if (relativePath.startsWith('/')) {
            return relativePath;
        }

        if (relativePath.startsWith('./')) {
            relativePath = relativePath.substring(2);
        }

        while (relativePath.startsWith('../')) {
            relativePath = relativePath.substring(3);
            basePath = basePath.substring(0, basePath.substring(0, basePath.length - 1).lastIndexOf('/') + 1);
        }

        return basePath + relativePath;
    }

    function appendQueryParams(url) {
        // 将当前URL的所有查询参数（排除page）附加到目标URL
        var qp = new URLSearchParams(window.location.search);
        qp.delete('page');
        var qs = qp.toString();
        if (qs) {
            var sep = url.indexOf('?') === -1 ? '?' : '&';
            return url + sep + qs;
        }
        return url;
    }

    function isLocalhost() {
        var host = window.location.hostname;
        return host === '127.0.0.1' || host === 'localhost' || host === '::1';
    }

    function getLeftNavItems() {
        return navItems.filter(function (n) {
            return !n.showInRightNav && (!n.localhostOnly || isLocalhost());
        });
    }

    function navigateTo(page) {
        currentPageId = page;
        savePage(page);
        iframe.src = getNavUrl(page);
        renderShellHeader();
    }

    // 动态加载 marked.js（memo 依赖）
    var _markedLoading = false;
    function loadMarked(cb) {
        if (window.marked) { cb && cb(); return; }
        if (_markedLoading) {
            var t = setInterval(function () {
                if (window.marked) { clearInterval(t); cb && cb(); }
            }, 30);
            return;
        }
        _markedLoading = true;
        var s = document.createElement('script');
        s.src = './lib/marked.js';
        s.onload = function () { _markedLoading = false; cb && cb(); };
        s.onerror = function () { _markedLoading = false; alert('加载 marked.js 失败'); };
        document.head.appendChild(s);
    }

    // 动态加载 memo.js（仅在用户首次点击备忘录时加载，节省带宽）
    var _memoLoading = false;
    function loadMemoModule(cb) {
        if (window.MemoModule) { cb && cb(); return; }
        if (_memoLoading) {
            var t = setInterval(function () {
                if (window.MemoModule) { clearInterval(t); cb && cb(); }
            }, 30);
            return;
        }
        _memoLoading = true;
        if (!document.getElementById('memo-css')) {
            var link = document.createElement('link');
            link.rel = 'stylesheet';
            link.id = 'memo-css';
            link.href = './memo.css';
            document.head.appendChild(link);
        }
        loadMarked(function () {
            var s = document.createElement('script');
            s.src = './memo.js';
            s.onload = function () { _memoLoading = false; cb && cb(); };
            s.onerror = function () { _memoLoading = false; alert('加载备忘录模块失败'); };
            document.head.appendChild(s);
        });
    }

    function renderShellHeader() {
        var user = App.state.user;
        var authed = !!user;

        var leftNav = '';
        getLeftNavItems().forEach(function (item) {
            var isActive = currentPageId === item.id;
            var isBrandClass = item.isBrand ? ' nav-brand' : '';
            leftNav += '<button class="btn btn-ghost btn-sm' + isBrandClass + (isActive ? ' active' : '') + '" data-nav="' + item.id + '">' + item.name + '</button>';
        });

        // Right nav
        var rightNav = '';
        if (authed) {
            // User info
            rightNav += '<div class="header-info" id="user-info" title="点击修改昵称">' +
                '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="margin-right: 6px;"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/></svg>' +
                '<span>' + escapeHtml(user.nickname || user.email) + '</span>' +
                '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="margin-left: 4px; opacity: 0.6;"><path d="M17 3a2.828 2.828 0 1 1 4 4L7.5 20.5 2 22l1.5-5.5L17 3z"/></svg>' +
                '</div>';
            // Logout
            rightNav += '<button class="btn btn-ghost btn-sm btn-icon" id="logout-btn" title="退出登录">' + ICON_LOGOUT + '</button>';
        } else {
            rightNav += '<a href="/auth/login.html"><button class="btn btn-ghost btn-sm">登录</button></a>' +
                '<a href="/auth/register.html"><button class="btn btn-ghost btn-sm">注册</button></a>';
        }
        // Theme toggle
        var isDark = document.documentElement.getAttribute('data-theme') === 'dark';
        // Memo button
        rightNav += '<button class="btn btn-ghost btn-sm btn-icon" id="memo-btn" title="备忘录">' + ICON_MEMO + '</button>';
        rightNav += '<button class="btn btn-ghost btn-sm btn-icon theme-toggle" id="theme-btn" title="' + (isDark ? '切换浅色主题' : '切换深色主题') + '">' + (isDark ? ICON_SUN : ICON_MOON) + '</button>';
        // QR code
        rightNav += '<button class="btn btn-ghost btn-sm btn-icon" id="qr-btn" title="显示二维码"><svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor"><path d="M3 3h7v7H3V3zm2 2v3h3V5H5zm9-2h7v7h-7V3zm2 2v3h3V5h-3zM3 14h7v7H3v-7zm2 2v3h3v-3H5zm9-2h2v2h-2v-2zm4 0h3v3h-2v-1h-1v-2zm-4 4h2v3h-2v-3zm4 1h1v-1h2v3h-3v-2zm0-2h1v1h-1v-1z"/></svg></button>';
        rightNav += '<button class="btn btn-ghost btn-sm btn-icon" id="refresh-btn" title="刷新页面"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="23 4 23 10 17 10"/><polyline points="1 20 1 14 7 14"/><path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"/></svg></button>';

        headerEl.innerHTML =
            '<header class="app-header"><div class="container flex items-center justify-between">' +
            '<div class="flex items-center space-x-2">' +
            '<a href="javascript:void(0)" class="brand-logo" data-nav="tools" title="工具箱首页">' +
            '<img src="./favicon.ico" alt="工具箱" /></a>' +
            leftNav + '</div>' +
            '<nav class="flex items-center space-x-2">' + rightNav + '</nav>' +
            '</div></header>';

        // Bind nav buttons
        headerEl.querySelectorAll('[data-nav]').forEach(function (btn) {
            btn.addEventListener('click', function () {
                var nav = btn.getAttribute('data-nav');
                navigateTo(nav);
            });
        });

        // Logout
        var logoutBtn = document.getElementById('logout-btn');
        if (logoutBtn) {
            logoutBtn.addEventListener('click', function () {
                Api.auth.logout().then(function () {
                    App.logout();
                    window.location.href = '/auth/login.html';
                });
            });
        }

        // User info (change nickname)
        var userInfo = document.getElementById('user-info');
        if (userInfo) {
            userInfo.addEventListener('click', function () {
                var overlay = document.createElement('div');
                overlay.className = 'modal-overlay';
                overlay.id = '__nick_modal';
                overlay.innerHTML =
                    '<div class="modal" style="width: 320px; height: auto;">' +
                    '<div class="modal-header" style="padding: 10px 14px;">' +
                    '<span class="modal-title" style="font-size: 14px;">修改昵称</span>' +
                    '<button class="btn btn-ghost btn-sm" data-modal-close>×</button>' +
                    '</div>' +
                    '<div class="modal-body" style="padding: 14px;">' +
                    '<div style="margin-bottom: 12px;">' +
                    '<label style="font-size: 13px; margin-bottom: 4px; display: block;">昵称</label>' +
                    '<input id="nick-input" class="input" style="font-size: 13px; padding: 8px;" />' +
                    '</div>' +
                    '</div>' +
                    '<div class="modal-footer" style="padding: 10px 14px; justify-content: flex-end; gap: 8px;">' +
                    '<button class="btn btn-outline btn-sm" data-modal-close style="padding: 6px 14px;">取消</button>' +
                    '<button class="btn btn-sm" id="nick-save" style="padding: 6px 14px;">保存</button>' +
                    '</div>' +
                    '</div>';
                document.body.appendChild(overlay);

                overlay.addEventListener('click', function (e) {
                    if (e.target === overlay || e.target.hasAttribute('data-modal-close')) {
                        var modal = document.getElementById('__nick_modal');
                        if (modal) modal.remove();
                    }
                });

                var input = document.getElementById('nick-input');
                input.value = App.state.user.nickname || '';
                input.focus();
                document.getElementById('nick-save').addEventListener('click', function () {
                    Api.auth.updateMe({ nickname: input.value.trim() }).then(function (data) {
                        if (data.success) {
                            App.setUser(data.user);
                            var modal = document.getElementById('__nick_modal');
                            if (modal) modal.remove();
                            renderShellHeader();
                        } else {
                            alert(data.error || '更新失败');
                        }
                    });
                });
            });
        }

        // QR button
        var qrBtn = document.getElementById('qr-btn');
        if (qrBtn) qrBtn.addEventListener('click', openQrModal);

        // Refresh button
        var refreshBtn = document.getElementById('refresh-btn');
        if (refreshBtn) refreshBtn.addEventListener('click', function () {
            try { iframe.contentWindow.location.reload(); } catch (ex) { iframe.src = iframe.src; }
        });

        // Theme toggle button
        var themeBtn = document.getElementById('theme-btn');
        if (themeBtn) themeBtn.addEventListener('click', toggleTheme);

        // Memo button (顶栏备忘录入口)
        var memoBtn = document.getElementById('memo-btn');
        if (memoBtn) {
            memoBtn.addEventListener('click', function () {
                if (window.MemoModule && window.MemoModule.open) {
                    window.MemoModule.open();
                } else {
                    loadMemoModule(function () { window.MemoModule && window.MemoModule.open(); });
                }
            });
        }
    }

    // ===== Shell initialization (main page with navigation + iframe) =====
    function initShell() {
        currentPageId = getSavedPage() || defaultPage.id;
        var initialIframeUrl = null; // 若通过URL查询参数指定了页面，保存初始iframe地址

        // Check for initial page parameter (e.g. ?page=/admin/&shareId=xxx)
        var params = new URLSearchParams(window.location.search);
        var initialPage = params.get('page');
        var themeQp = params.get('theme');
        var initialPageSpecified = !!initialPage; // URL查询参数是否指定了页面

        iframe.addEventListener('load', function () {
            try {
                var p = iframe.contentWindow.location.pathname;
                var matched = navItems.find(function (n) {
                    var normalized = toAbsolutePath(n.path);
                    return p.indexOf(normalized) !== -1;
                });
                // 只有匹配到已知导航项时才同步 currentPageId 并重渲染；未匹配时保持点击时的状态
                if (matched) {
                    currentPageId = matched.id;
                    renderShellHeader();
                }
            } catch (e) { /* cross-origin */ }
        });

        if (initialPage) {
            var matched = navItems.find(function (n) { return n.path === initialPage; });
            if (matched) {
                currentPageId = matched.id;
                initialIframeUrl = appendQueryParams(matched.path);
                iframe.src = initialIframeUrl;
            } else {
                initialIframeUrl = appendQueryParams(initialPage);
                iframe.src = initialIframeUrl;
            }
        }

        // 若URL查询参数已指定初始页面（上面已设置iframe.src），此处不再覆盖
        if (!initialIframeUrl) {
            iframe.src = getNavUrl(currentPageId);
        }

        // Listen for storage events (from iframes changing auth/theme state)
        window.addEventListener('storage', function (e) {
            if (e.key === 'auth-storage') {
                App.loadState();
                renderShellHeader();
            } else if (e.key === window.Theme.themeKey) {
                renderShellHeader();
            }
        });

        // Initial render
        renderShellHeader();

        // Initialize: check auth and render
        Api.auth.me().then(function (me) {
            if (me.success) {
                App.setUser(me.user);
            }
        }).then(function () {
            // 无论是否登录，从数据库加载共享设置（主题、当前页面）
            return Api.settings.list().then(function (sd) {
                if (sd.success && sd.settings) {
                    var s = sd.settings;
                    if (s.theme)
                        window.Theme.update(s.theme);

                    // URL查询参数指定了初始页面时，不从数据库配置覆盖currentPageId
                    if (s.currentPageId && !initialPageSpecified) {
                        currentPageId = s.currentPageId;
                        try { localStorage.setItem('tools-current-page', s.currentPageId); } catch (e) { }
                    }
                }
            }).catch(function () { });
        }).then(function () {
            renderShellHeader();
            // 若URL查询参数指定了页面（如带share参数进入管理员页面），保持原URL不覆盖
            if (initialIframeUrl) return;
            iframe.src = getNavUrl(currentPageId);
        });
    }

    initShell();

})(window);

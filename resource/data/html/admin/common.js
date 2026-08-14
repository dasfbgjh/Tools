(function () {
    'use strict';
    var App = window.App;

    function isLocalhost() {
        var h = window.location.hostname;
        return h === '127.0.0.1' || h === 'localhost' || h === '::1';
    }

    // ===== Tab switching =====
    function activateTab(tab, skipSave) {
        var btns = document.querySelectorAll('.tab-btn');
        btns.forEach(function (b) {
            b.classList.toggle('active', b.getAttribute('data-tab') === tab);
        });
        document.querySelectorAll('.tab-pane').forEach(function (p) { p.classList.remove('active'); });
        var pane = document.getElementById('tab-' + tab);
        if (pane) pane.classList.add('active');
        // Lazy-load data on first switch
        if (tab === 'shares' && !document.getElementById('shares-container').dataset.loaded) {
            if (typeof window.AdminShares !== 'undefined') window.AdminShares.loadShares();
        } else if (tab === 'settings' && !document.getElementById('config-form').dataset.loaded) {
            if (typeof window.AdminSettings !== 'undefined') window.AdminSettings.loadConfig();
        } else if (tab === 'transfers') {
            if (typeof window.AdminTransfers !== 'undefined') {
                window.AdminTransfers.onActivate();
            }
        }
        // 通知其它 tab 取消激活（暂停轮询等）
        ['users', 'shares', 'settings', 'transfers'].forEach(function (other) {
            if (other === tab) return;
            var mod = window[ 'Admin' + other.charAt(0).toUpperCase() + other.slice(1) ];
            if (mod && typeof mod.onDeactivate === 'function') mod.onDeactivate();
        });
        if (!skipSave) {
            try { localStorage.setItem('admin-active-tab', tab); } catch (e) { }
            Api.settings.update({ adminActiveTab: tab }).catch(function () { });
        }
    }

    function initTabs() {
        document.querySelectorAll('.tab-btn').forEach(function (btn) {
            btn.addEventListener('click', function () {
                activateTab(btn.getAttribute('data-tab'));
            });
        });
    }

    // ===== Filesystem browser modal =====
    function toApiPath(nativePath) {
        return nativePath.replace(/\\/g, '/');
    }

    function toNativePath(apiPath) {
        return apiPath.replace(/\//g, '\\');
    }

    function buildBreadcrumb(fullPath) {
        if (!fullPath) {
            return '<span>根目录</span>';
        }
        var native = fullPath;
        if (native.length > 3 && native.charAt(native.length - 1) === '\\') {
            native = native.substring(0, native.length - 1);
        }
        var parts = native.split('\\');
        var html = '<a data-path="">根</a>';
        var accum = '';
        parts.forEach(function (p) {
            if (!p) return;
            if (accum) {
                accum += '\\' + p;
            } else {
                accum = p + '\\';
            }
            var display = p;
            if (display.charAt(display.length - 1) === '\\') {
                display = display.substring(0, display.length - 1);
            }
            html += '<span class="sep">/</span><a data-path="' + App.escapeHtml(accum) + '">' + App.escapeHtml(display) + '</a>';
        });
        return html;
    }

    var fsCurrentPath = '';
    var fsSelectedPath = '';
    var fsMode = 'dir';
    var fsCallback = null;

    function showFsBrowser(callback, mode) {
        fsMode = mode || 'dir';
        fsCallback = callback;
        var body =
            '<div class="fs-path-input">' +
            '<input type="text" class="input" id="fs-path-field" placeholder="输入路径直接跳转，如 C:\\Users 或 D:\\project" />' +
            '<button class="btn btn-sm" id="fs-go">跳转</button>' +
            '</div>' +
            '<div class="fs-breadcrumb" id="fs-bc"></div>' +
            '<div class="fs-browser" id="fs-list">加载中...</div>';
        var footer = '<button class="btn btn-outline" data-modal-close>取消</button>' +
            (fsMode === 'file' ? '' : '<button class="btn" id="fs-select">选择此目录</button>');
        App.openModal(fsMode === 'file' ? '选择文件' : '浏览文件系统', body, footer, 'modal-lg');
        fsCurrentPath = '';
        fsSelectedPath = '';
        loadFsDir('');
        var pathField = document.getElementById('fs-path-field');
        var goBtn = document.getElementById('fs-go');
        var goHandler = function () {
            var v = pathField.value.trim();
            if (!v) return;
            var native = v.replace(/\//g, '\\');
            if (/^[A-Za-z]:$/.test(native)) native += '\\';
            loadFsDir(native);
        };
        goBtn.addEventListener('click', goHandler);
        pathField.addEventListener('keydown', function (e) {
            if (e.key === 'Enter') { e.preventDefault(); goHandler(); }
        });
        var selectBtn = document.getElementById('fs-select');
        if (selectBtn) {
            selectBtn.addEventListener('click', function () {
                if (typeof fsCallback === 'function') fsCallback(fsCurrentPath);
                App.closeModal();
            });
        }
    }

    function loadFsDir(fullPath) {
        fsCurrentPath = fullPath;
        var pathField = document.getElementById('fs-path-field');
        if (pathField) pathField.value = fullPath || '';
        var apiPath = toApiPath(fullPath);
        Api.admin.browseFs(apiPath).then(function (data) {
            if (!data.success) { document.getElementById('fs-list').innerHTML = '<p class="text-danger">' + (data.error || '加载失败') + '</p>'; return; }
            var bc = document.getElementById('fs-bc');
            bc.innerHTML = buildBreadcrumb(fullPath);
            bc.querySelectorAll('a').forEach(function (a) {
                a.addEventListener('click', function () { loadFsDir(this.getAttribute('data-path')); });
            });
            var list = document.getElementById('fs-list');
            if (!data.entries || data.entries.length === 0) {
                list.innerHTML = '<p class="text-muted text-center" style="padding:20px;">空目录</p>';
                return;
            }
            var fileMode = fsMode === 'file';
            var html = '';
            data.entries.forEach(function (e) {
                if (!e.isDir) return;
                var fp = e.fullPath || '';
                var icon = window.FileIcons ? window.FileIcons.getIcon(e.name, true) : '📁';
                html += '<div class="fs-entry" data-name="' + App.escapeHtml(e.name) + '" data-fullpath="' + App.escapeHtml(fp) + '">' +
                    '<span class="icon">' + icon + '</span><span>' + App.escapeHtml(e.name) + '</span>' +
                    '</div>';
            });
            data.entries.forEach(function (e) {
                if (e.isDir) return;
                var fp = e.fullPath || '';
                var ficon = window.FileIcons ? window.FileIcons.getIcon(e.name, false) : '📄';
                if (fileMode) {
                    html += '<div class="fs-entry fs-entry-file" data-name="' + App.escapeHtml(e.name) + '" data-fullpath="' + App.escapeHtml(fp) + '">' +
                        '<span class="icon">' + ficon + '</span><span>' + App.escapeHtml(e.name) + '</span>' +
                        '</div>';
                } else {
                    html += '<div class="fs-entry" style="cursor:default;opacity:.6;">' +
                        '<span class="icon">' + ficon + '</span><span>' + App.escapeHtml(e.name) + '</span>' +
                        '</div>';
                }
            });
            list.innerHTML = html;
            list.querySelectorAll('.fs-entry[data-name]').forEach(function (el) {
                el.addEventListener('click', function () {
                    var fp = this.getAttribute('data-fullpath');
                    if (this.classList.contains('fs-entry-file')) {
                        fsSelectedPath = fp;
                        if (typeof fsCallback === 'function') {
                            fsCallback(fp);
                            App.closeModal();
                        }
                    } else {
                        loadFsDir(fp);
                    }
                });
            });
        });
    }

    // Export to global
    window.AdminCommon = {
        isLocalhost: isLocalhost,
        activateTab: activateTab,
        initTabs: initTabs,
        showFsBrowser: showFsBrowser
    };
})();
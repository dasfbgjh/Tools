(function () {
    'use strict';
    var App = window.App;
    var banner = document.getElementById('banner');

    function showBanner(type, msg) {
        var html = '';
        var cls = 'info';
        if (type === 'error') cls = 'err';
        else if (type === 'warn') cls = 'warn';
        else if (type === 'success') cls = 'ok';
        html = '<div class="banner banner-' + cls + '">' + App.escapeHtml(msg) + '</div>';
        document.getElementById('banner').innerHTML = html;
    }
    function clearBanner() {
        document.getElementById('banner').innerHTML = '';
    }

    // ===== Tab 切换 =====
    document.querySelectorAll('.br-tab').forEach(function (t) {
        t.addEventListener('click', function () {
            document.querySelectorAll('.br-tab').forEach(function (x) { x.classList.remove('active'); });
            document.querySelectorAll('.br-pane').forEach(function (x) { x.classList.remove('active'); });
            t.classList.add('active');
            var pane = document.querySelector('.br-pane[data-tab="' + t.getAttribute('data-tab') + '"]');
            if (pane) pane.classList.add('active');
            // 切 tab 时按新 tab 的规则重算预览
            previewAll();
        });
    });

    // ===== 文件列表 =====
    var items = []; // [{oldName, isDir, oldPath, newName, status, err, modified, size}]
    var sortState = { key: null, dir: null }; // {key:'name'|'date'|null, dir:'asc'|'desc'|null}

    function addItem(p) {
        // 支持传路径字符串 或 完整 entry 对象
        var entry = (typeof p === 'object' && p !== null) ? p : null;
        var fullPath = entry ? entry.fullPath : p;
        // 去重
        for (var i = 0; i < items.length; i++) {
            if (items[i].oldPath === fullPath) return false;
        }
        var sep = fullPath.indexOf('\\') >= 0 ? '\\' : '/';
        var idx = fullPath.lastIndexOf(sep);
        var name = idx >= 0 ? fullPath.substring(idx + 1) : fullPath;
        // 隐藏文件判断
        var isHidden = name.charAt(0) === '.';
        items.push({
            oldName: name,
            oldPath: fullPath,
            isDir: !!(entry && entry.isDir),
            isHidden: isHidden,
            modified: entry ? (entry.modified || 0) : 0,
            size: entry ? (entry.size || 0) : 0,
            newName: name,
            status: 'pending', // pending | success | error
            err: ''
        });
        renderTable();
        previewAll();
        return true;
    }

    function removeItem(idx) {
        items.splice(idx, 1);
        renderTable();
        previewAll();
    }

    function moveItem(idx, delta) {
        var j = idx + delta;
        if (j < 0 || j >= items.length) return;
        var tmp = items[idx];
        items[idx] = items[j];
        items[j] = tmp;
        renderTable();
        previewAll();
    }

    function clearItems() {
        if (items.length === 0 && history.length === 0) return;
        var msg = items.length > 0
            ? '确认清空全部 ' + items.length + ' 项？'
            : '确认清空 ' + history.length + ' 步撤销历史？';
        if (!confirm(msg)) return;
        items = [];
        history = [];
        updateUndoBtn();
        renderTable();
        previewAll();
    }

    function applySort(key, dir) {
        if (!key || !dir) {
            sortState.key = null;
            sortState.dir = null;
        } else {
            sortState.key = key;
            sortState.dir = dir;
            items.sort(function (a, b) {
                var av, bv;
                if (key === 'name') {
                    av = (a.oldName || '').toLowerCase();
                    bv = (b.oldName || '').toLowerCase();
                    if (av < bv) return dir === 'asc' ? -1 : 1;
                    if (av > bv) return dir === 'asc' ? 1 : -1;
                    return 0;
                }
                if (key === 'date') {
                    av = a.modified || 0;
                    bv = b.modified || 0;
                    return dir === 'asc' ? av - bv : bv - av;
                }
                return 0;
            });
        }
        // 更新排序按钮视觉
        document.querySelectorAll('.br-sort-btn').forEach(function (b) {
            var bKey = b.getAttribute('data-sort');
            var arrow = b.querySelector('.br-sort-arrow');
            if (bKey === key && dir) {
                b.classList.add('active');
                if (arrow) arrow.textContent = dir === 'asc' ? '▲' : '▼';
            } else {
                b.classList.remove('active');
                if (arrow) arrow.textContent = '↕';
            }
        });
        renderTable();
    }

    function renderTable() {
        var tbody = document.getElementById('br-tbody');
        if (items.length === 0) {
            tbody.innerHTML = '<tr class="br-empty-row"><td colspan="6"><div class="br-empty">尚未添加文件 · 点击"添加文件/文件夹"开始</div></td></tr>';
            document.getElementById('br-stats').textContent = '0 个文件';
            return;
        }
        var html = '';
        items.forEach(function (it, i) {
            var statusHtml = '';
            if (it.status === 'success') statusHtml = '<span class="br-status ok">完成</span>';
            else if (it.status === 'error') statusHtml = '<span class="br-status err" title="' + App.escapeHtml(it.err || '') + '">失败</span>';
            else statusHtml = '<span class="br-status pending">待处理</span>';
            var newCls = it.newName !== it.oldName ? 'br-new changed' : 'br-new';
            var upDis = i === 0 ? ' disabled' : '';
            var dnDis = i === items.length - 1 ? ' disabled' : '';
            html += '<tr' + (it.status === 'error' ? ' class="br-row-err"' : '') + '>' +
                '<td>' +
                '<span class="br-handle">' +
                '<button data-act="up" data-i="' + i + '" title="上移"' + upDis + '>↑</button>' +
                '<button data-act="down" data-i="' + i + '" title="下移"' + dnDis + '>↓</button>' +
                '</span>' +
                '</td>' +
                '<td class="br-old">' + App.escapeHtml(it.oldName) + '</td>' +
                '<td class="' + newCls + '">' + App.escapeHtml(it.newName || '') + '</td>' +
                '<td class="br-path" title="' + App.escapeHtml(it.oldPath) + '">' + App.escapeHtml(it.oldPath) + '</td>' +
                '<td>' + statusHtml + '</td>' +
                '<td class="br-actions"><button class="btn btn-sm btn-ghost" data-act="remove" data-i="' + i + '">移除</button></td>' +
                '</tr>';
        });
        tbody.innerHTML = html;
        tbody.querySelectorAll('button[data-act]').forEach(function (btn) {
            var act = btn.getAttribute('data-act');
            var i = parseInt(btn.getAttribute('data-i'), 10);
            btn.addEventListener('click', function () {
                if (act === 'remove') removeItem(i);
                else if (act === 'up') moveItem(i, -1);
                else if (act === 'down') moveItem(i, 1);
            });
        });
        document.getElementById('br-stats').textContent = items.length + ' 个文件';
    }

    // ===== 规则计算 =====
    // 把 name 拆成 base + ext（最后一个 .）
    function splitName(name) {
        if (!name) return { base: '', ext: '' };
        var dot = name.lastIndexOf('.');
        if (dot <= 0) return { base: name, ext: '' };
        return { base: name.substring(0, dot), ext: name.substring(dot + 1) };
    }

    function pad(num, digits) {
        var s = String(num);
        while (s.length < digits) s = '0' + s;
        return s;
    }

    // 应用单条规则到一条 item
    function applyRule(item, idx, rule) {
        var target = rule.target || 'filename';
        var parts = splitName(item.oldName);
        var base = parts.base, ext = parts.ext;
        var nb = base, ne = ext;
        switch (rule.type) {
            case 'regular': {
                var num = rule.start + idx * rule.step;
                var composed = rule.prefix + (rule.main || pad(num, rule.digits)) + rule.suffix;
                if (target === 'filename') {
                    nb = composed;
                } else if (target === 'ext') {
                    ne = composed;
                } else {
                    // 全部：保留原文件名和扩展名的拼接方式
                    // filename 部分用组成值，ext 部分也用组成值
                    // 当原文件没有扩展名时(ne='')，ne 仍写空（按 splitName 决定）
                    // 目标文件名 = base + (ext ? '.' + ext : '')
                    nb = composed;
                    // "全部"时扩展名也用同一组成值；若原文件无扩展名则保持无
                    if (ext) ne = composed;
                }
                break;
            }
            case 'replace': {
                var replacedBase = base;
                var replacedExt = ext;
                if (rule.find) {
                    var safeFind = rule.find.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
                    var re = new RegExp(safeFind, 'g');
                    replacedBase = base.replace(re, rule.replace || '');
                    replacedExt = ext.replace(re, rule.replace || '');
                }
                if (target === 'filename') nb = replacedBase;
                else if (target === 'ext') ne = replacedExt;
                else { nb = replacedBase; ne = replacedExt; }
                break;
            }
            case 'delete': {
                var n = rule.del || 0;
                var applyDel = function (s) {
                    if (!s || n === 0) return s;
                    if (n > 0) {
                        // 正数：删除前 N 个字符
                        return s.length > n ? s.substring(n) : '';
                    } else {
                        // 负数：删除后 N 个字符
                        var k = -n;
                        return s.length > k ? s.substring(0, s.length - k) : '';
                    }
                };
                var dBase = applyDel(base);
                var dExt = applyDel(ext);
                if (target === 'filename') nb = dBase;
                else if (target === 'ext') ne = dExt;
                else { nb = dBase; ne = dExt; }
                break;
            }
            case 'insert': {
                var pos = rule.pos || 0;
                var insText = rule.text || '';
                var applyInsert = function (s) {
                    if (s.length === 0) return insText;
                    var idx;
                    if (pos < 0) {
                        // 负数从后往前：-1 = 末尾，-2 = 最后一个字符前，-N = 倒数第 N 位置
                        var fromEnd = -pos;
                        idx = s.length - fromEnd + 1;
                    } else {
                        idx = Math.min(pos, s.length);
                    }
                    idx = Math.max(0, Math.min(idx, s.length));
                    return s.substring(0, idx) + insText + s.substring(idx);
                };
                var insertedBase = applyInsert(base);
                var insertedExt = applyInsert(ext);
                if (target === 'filename') nb = insertedBase;
                else if (target === 'ext') ne = insertedExt;
                else { nb = insertedBase; ne = insertedExt; }
                break;
            }
            case 'case': {
                var mode = rule.mode;
                function applyCase(s) {
                    if (!s) return s;
                    if (mode === 'upper') return s.toUpperCase();
                    if (mode === 'lower') return s.toLowerCase();
                    if (mode === 'cap') {
                        return s.charAt(0).toUpperCase() + s.substring(1);
                    }
                    if (mode === 'uncap') {
                        return s.charAt(0).toLowerCase() + s.substring(1);
                    }
                    return s;
                }
                var casedBase = applyCase(base);
                var casedExt = applyCase(ext);
                if (target === 'filename') nb = casedBase;
                else if (target === 'ext') ne = casedExt;
                else { nb = casedBase; ne = casedExt; }
                break;
            }
        }
        // 拼接（无扩展名则不加点）
        var out = nb;
        if (ne) out += '.' + ne;
        return out;
    }

    function currentRule() {
        var tab = document.querySelector('.br-tab.active').getAttribute('data-tab');
        if (tab === 'regular') {
            return {
                type: 'regular',
                target: getRadio('rg-target'),
                start: parseInt(document.getElementById('rg-start').value, 10) || 0,
                step: parseInt(document.getElementById('rg-step').value, 10) || 1,
                prefix: document.getElementById('rg-prefix').value,
                main: document.getElementById('rg-main').value,
                digits: Math.max(1, parseInt(document.getElementById('rg-digits').value, 10) || 1),
                suffix: document.getElementById('rg-suffix').value
            };
        }
        if (tab === 'replace') {
            return {
                type: 'replace',
                target: getRadio('rp-target'),
                find: document.getElementById('rp-find').value,
                replace: document.getElementById('rp-replace').value
            };
        }
        if (tab === 'delete') {
            return {
                type: 'delete',
                target: getRadio('dl-target'),
                del: parseInt(document.getElementById('dl-del').value, 10) || 0
            };
        }
        if (tab === 'insert') {
            return {
                type: 'insert',
                target: getRadio('in-target'),
                text: document.getElementById('in-text').value,
                pos: parseInt(document.getElementById('in-pos').value, 10) || 0
            };
        }
        if (tab === 'case') {
            return {
                type: 'case',
                target: getRadio('cs-target'),
                mode: getRadio('cs-mode')
            };
        }
        return null;
    }

    function getRadio(name) {
        var el = document.querySelector('input[name="' + name + '"]:checked');
        return el ? el.value : '';
    }

    function previewAll() {
        if (!document.getElementById('chk-autopreview').checked) return;
        var rule = currentRule();
        if (!rule) return;
        items.forEach(function (it, i) {
            // 已成功/失败的不重新计算
            if (it.status === 'success' || it.status === 'error') return;
            try {
                it.newName = applyRule(it, i, rule);
            } catch (e) {
                it.newName = it.oldName;
            }
        });
        renderTable();
    }

    // ===== 自动预览开关 + 输入变化 =====
    document.getElementById('chk-autopreview').addEventListener('change', function () {
        if (this.checked) previewAll();
    });
    document.querySelectorAll('.br-pane input, .br-pane select').forEach(function (el) {
        el.addEventListener('input', previewAll);
        el.addEventListener('change', previewAll);
    });

    // ===== 工具栏 =====
    document.getElementById('btn-add').addEventListener('click', openFsBrowser);
    document.getElementById('btn-clear').addEventListener('click', clearItems);
    document.getElementById('btn-apply').addEventListener('click', applyRename);
    document.getElementById('btn-undo').addEventListener('click', undoLast);

    // ===== 排序按钮 (二态切换) =====
    document.querySelectorAll('.br-sort-btn').forEach(function (b) {
        b.addEventListener('click', function () {
            var k = b.getAttribute('data-sort');
            // 点击同一按钮: 升 → 降 → 取消
            // 点击不同按钮: 切换到该键，默认升序
            if (sortState.key === k) {
                if (sortState.dir === 'asc') {
                    applySort(k, 'desc');
                } else if (sortState.dir === 'desc') {
                    applySort(null, null);
                } else {
                    applySort(k, 'asc');
                }
            } else {
                applySort(k, 'asc');
            }
        });
    });

    // ===== 文件浏览器 =====
    var fsPanel = document.getElementById('br-fs');
    var fsCurrent = '';
    var fsSelected = {}; // path -> true
    var fsAllowed = '';  // '' = all; 'file' = only files; 'dir' = only dirs

    function openFsBrowser() {
        fsSelected = {};
        fsPanel.hidden = false;
        document.getElementById('br-fs-path').value = '';
        document.getElementById('br-fs-count').textContent = '已选 0 项';
        loadFsDir('');
    }
    function closeFsBrowser() { fsPanel.hidden = true; }
    document.getElementById('br-fs-cancel').addEventListener('click', closeFsBrowser);
    document.getElementById('br-fs-go').addEventListener('click', function () {
        var v = document.getElementById('br-fs-path').value.trim();
        if (!v) return;
        loadFsDir(v);
    });
    document.getElementById('br-fs-path').addEventListener('keydown', function (e) {
        if (e.key === 'Enter') { e.preventDefault(); document.getElementById('br-fs-go').click(); }
    });
    document.getElementById('br-fs-done').addEventListener('click', function () {
        var entries = Object.values(fsSelected);
        entries.forEach(function (e) { addItem(e); });
        closeFsBrowser();
    });

    function toApiPath(p) { return p.replace(/\\/g, '/'); }
    function toNativePath(p) { return p.replace(/\//g, '\\'); }
    function dirOf(p) {
        if (!p) return '';
        var n = p;
        if (n.length > 3 && n.charAt(n.length - 1) === '\\') n = n.substring(0, n.length - 1);
        var idx = Math.max(n.lastIndexOf('\\'), n.lastIndexOf('/'));
        if (idx < 0) return '';
        if (idx === 2 && n.charAt(1) === ':') return n.substring(0, 3); // C:\
        return n.substring(0, idx);
    }

    function loadFsDir(path) {
        fsCurrent = path;
        document.getElementById('br-fs-path').value = path;
        var list = document.getElementById('br-fs-list');
        list.innerHTML = '<div class="text-muted text-center" style="padding:20px;">加载中...</div>';
        Api.localTools.browse(toApiPath(path)).then(function (data) {
            if (!data.success) { list.innerHTML = '<div class="text-danger text-center" style="padding:20px;">' + App.escapeHtml(data.error || '加载失败') + '</div>'; return; }
            renderFsBreadcrumb(data.path || path);
            var entries = data.entries || [];
            if (entries.length === 0) {
                list.innerHTML = '<div class="text-muted text-center" style="padding:20px;">空目录</div>';
                updateSelectAll();
                return;
            }
            // 排序：文件夹在前
            entries.sort(function (a, b) {
                if (a.isDir !== b.isDir) return a.isDir ? -1 : 1;
                return a.name.localeCompare(b.name, 'zh-CN');
            });
            var html = '';
            // 返回上级
            if (data.path) {
                html += '<div class="br-fs-entry" data-act="up"><span class="icon">↩</span><span>..</span></div>';
            }
            entries.forEach(function (e) {
                var checked = fsSelected[e.fullPath] ? ' checked' : '';
                html += '<div class="br-fs-entry" data-path="' + App.escapeHtml(e.fullPath) + '" data-isdir="' + (e.isDir ? 1 : 0) + '" data-mod="' + (e.modified || 0) + '" data-size="' + (e.size || 0) + '">' +
                    '<label class="br-fs-check" title="选中"><input type="checkbox" data-sel' + checked + '></label>' +
                    '<span class="icon">' + FileIcons.getIcon(e.name, e.isDir) + '</span>' +
                    '<span class="br-fs-name">' + App.escapeHtml(e.name) + '</span>' +
                    '</div>';
            });
            list.innerHTML = html;
            list.querySelectorAll('.br-fs-entry').forEach(function (el) {
                var act = el.getAttribute('data-act');
                if (act === 'up') {
                    el.addEventListener('click', function () { loadFsDir(dirOf(fsCurrent)); });
                    return;
                }
                var p = el.getAttribute('data-path');
                var isDir = el.getAttribute('data-isdir') === '1';
                var mod = parseInt(el.getAttribute('data-mod') || '0', 10);
                var sz = parseInt(el.getAttribute('data-size') || '0', 10);
                var name = el.querySelector('.br-fs-name');
                name.style.cursor = isDir ? 'pointer' : 'default';
                name.addEventListener('click', function (e) {
                    e.stopPropagation();
                    if (isDir) loadFsDir(p);
                });
                var cb = el.querySelector('input[data-sel]');
                cb.addEventListener('change', function (e) {
                    e.stopPropagation();
                    if (cb.checked) {
                        fsSelected[p] = {
                            fullPath: p,
                            name: p.replace(/^.*[\\\/]/, ''),
                            isDir: isDir,
                            modified: mod,
                            size: sz
                        };
                    } else {
                        delete fsSelected[p];
                    }
                    updateFsCount();
                    updateSelectAll();
                });
            });
            updateSelectAll();
        });
    }

    function updateSelectAll() {
        var sa = document.getElementById('br-fs-sel-all');
        if (!sa) return;
        var cbs = document.querySelectorAll('#br-fs-list input[data-sel]');
        if (cbs.length === 0) { sa.checked = false; sa.indeterminate = false; return; }
        var checked = 0;
        cbs.forEach(function (c) { if (c.checked) checked++; });
        sa.checked = checked === cbs.length;
        sa.indeterminate = checked > 0 && checked < cbs.length;
    }

    // 全选复选框：严格二态切换（全选 ↔ 全不选）
    var fsSelAll = document.getElementById('br-fs-sel-all');
    if (fsSelAll) {
        // 阻止原生 toggle：自己控制目标状态
        fsSelAll.addEventListener('click', function (e) {
            e.preventDefault();
            var cbs = document.querySelectorAll('#br-fs-list input[data-sel]');
            if (cbs.length === 0) return;
            // 计算当前已选数量 → 决定目标
            var checkedCount = 0;
            cbs.forEach(function (c) { if (c.checked) checkedCount++; });
            // 全部已选 → 切换为全不选；否则（含 indeterminate）→ 切换为全选
            var target = (checkedCount === cbs.length) ? false : true;
            // 直接更新所有行
            cbs.forEach(function (c) {
                if (c.checked !== target) {
                    c.checked = target;
                    var p = c.closest('.br-fs-entry').getAttribute('data-path');
                    var isDir = c.closest('.br-fs-entry').getAttribute('data-isdir') === '1';
                    var mod = parseInt(c.closest('.br-fs-entry').getAttribute('data-mod') || '0', 10);
                    var sz = parseInt(c.closest('.br-fs-entry').getAttribute('data-size') || '0', 10);
                    if (target) {
                        fsSelected[p] = { fullPath: p, name: p.replace(/^.*[\\\/]/, ''), isDir: isDir, modified: mod, size: sz };
                    } else {
                        delete fsSelected[p];
                    }
                }
            });
            updateFsCount();
            updateSelectAll();
        });
    }

    function renderFsBreadcrumb(p) {
        var bc = document.getElementById('br-fs-bc');
        if (!p) { bc.innerHTML = '<span class="br-fs-bc-root">本机</span>'; return; }
        var n = p;
        if (n.length > 3 && n.charAt(n.length - 1) === '\\') n = n.substring(0, n.length - 1);
        var parts = n.split(/[\\\/]/).filter(Boolean);
        var html = '<a data-path="">本机</a>';
        var accum = '';
        parts.forEach(function (s) {
            if (accum) accum += '\\';
            else accum = s + '\\';
            html += '<span class="sep">/</span><a data-path="' + App.escapeHtml(accum) + '">' + App.escapeHtml(s) + '</a>';
        });
        bc.innerHTML = html;
        bc.querySelectorAll('a').forEach(function (a) {
            a.addEventListener('click', function () { loadFsDir(this.getAttribute('data-path')); });
        });
    }

    function updateFsCount() {
        var n = Object.keys(fsSelected).length;
        document.getElementById('br-fs-count').textContent = '已选 ' + n + ' 项';
    }

    // ===== 撤销历史 =====
    var history = []; // [{ ts, ops: [{oldPath, newName}] }]
    function updateUndoBtn() {
        var btn = document.getElementById('btn-undo');
        if (!btn) return;
        var n = history.length;
        btn.disabled = n === 0;
        var c = document.getElementById('undo-count');
        if (c) c.textContent = '(' + n + ')';
        btn.title = n === 0 ? '暂无可撤销的操作' : '撤销最近一次批量重命名（剩 ' + n + ' 步）';
    }

    function undoLast() {
        if (history.length === 0) { showBanner('warn', '暂无可撤销的操作'); return; }
        var entry = history.pop();
        if (!entry.ops || entry.ops.length === 0) { updateUndoBtn(); return; }
        var ops = entry.ops.slice();
        var btn = document.getElementById('btn-undo');
        btn.disabled = true;
        showBanner('info', '正在撤销 ' + ops.length + ' 项...');
        Api.localTools.rename(ops, true).then(function (data) {
            if (!data.success) {
                // 失败时把这一项塞回栈顶
                history.push(entry);
                showBanner('error', '撤销失败: ' + (data.error || '未知错误'));
                updateUndoBtn();
                return;
            }
            // 同步更新 items: 用返回的 newPath 找到对应行，还原 oldName/oldPath，状态重置为 pending
            ops.forEach(function (op) {
                // 找当前 items 中 oldPath 等于 op.oldPath 的行（即刚刚重命名后的路径）
                for (var k = 0; k < items.length; k++) {
                    if (items[k].oldPath === op.oldPath) {
                        items[k].oldName = op.newName;
                        var parent = dirOf(items[k].oldPath);
                        items[k].oldPath = (parent ? parent + '\\' : '') + op.newName;
                        items[k].status = 'pending';
                        items[k].err = '';
                        break;
                    }
                }
            });
            renderTable();
            previewAll();
            var failed = data.failCount || 0;
            if (failed > 0) {
                // 部分失败时，把失败的项作为新栈顶留着，告知用户
                var remaining = data.results
                    .map(function (r, i) { return r && r.ok ? null : ops[i]; })
                    .filter(Boolean);
                if (remaining.length > 0) {
                    entry.ops = remaining;
                    history.push(entry);
                    showBanner('warn', '已撤销 ' + (ops.length - remaining.length) + ' 项，' + remaining.length + ' 项失败（保留以便重试）');
                } else {
                    showBanner('success', '已撤销 ' + ops.length + ' 项');
                }
            } else {
                showBanner('success', '已撤销 ' + ops.length + ' 项');
            }
            updateUndoBtn();
        }).catch(function () {
            history.push(entry);
            showBanner('error', '撤销请求失败');
            updateUndoBtn();
        });
    }

    // ===== 应用重命名 =====
    function applyRename() {
        clearBanner();
        if (items.length === 0) { showBanner('warn', '请先添加文件'); return; }
        var unchanged = items.filter(function (it) { return it.newName === it.oldName; });
        if (unchanged.length === items.length) { showBanner('warn', '没有可应用的变更'); return; }
        // 检查空名
        for (var i = 0; i < items.length; i++) {
            if (!items[i].newName || /[\\\/:*?"<>|]/.test(items[i].newName)) {
                showBanner('error', '第 ' + (i + 1) + ' 项新名称不合法: ' + items[i].newName);
                return;
            }
        }
        var allowOverwrite = document.getElementById('chk-overwrite').checked;
        // 记录原始 (oldName, oldPath)，用于后续撤销
        var origSnapshot = {};
        items.forEach(function (it) {
            if (it.newName !== it.oldName) {
                origSnapshot[it.oldPath] = { name: it.oldName, path: it.oldPath };
            }
        });
        var payload = items
            .filter(function (it) { return it.newName !== it.oldName; })
            .map(function (it) { return { oldPath: it.oldPath, newName: it.newName }; });
        if (!confirm('将重命名 ' + payload.length + ' 个项目，是否继续？')) return;
        document.getElementById('btn-apply').disabled = true;
        showBanner('info', '正在重命名...');
        Api.localTools.rename(payload, allowOverwrite).then(function (data) {
            document.getElementById('btn-apply').disabled = false;
            if (!data.success) { showBanner('error', data.error || '重命名失败'); return; }
            // 收集成功项 → 入历史栈
            var ops = [];
            var i = 0;
            items.forEach(function (it) {
                if (it.newName === it.oldName) return;
                var r = data.results[i++];
                if (r && r.ok) {
                    var origKey = it.oldPath; // 调用前原路径
                    var orig = origSnapshot[origKey];
                    ops.push({ oldPath: r.newPath || it.oldPath, newName: orig ? orig.name : it.oldName });
                    it.status = 'success';
                    it.oldName = it.newName;
                    it.oldPath = r.newPath || it.oldPath;
                    it.err = '';
                } else {
                    it.status = 'error';
                    it.err = (r && r.error) || '未知错误';
                }
            });
            if (ops.length > 0) {
                history.push({ ts: Date.now(), ops: ops });
                updateUndoBtn();
            }
            renderTable();
            var failCount = data.failCount || 0;
            if (failCount === 0) showBanner('success', '全部 ' + data.okCount + ' 项已成功重命名');
            else showBanner('warn', '成功 ' + data.okCount + ' 项，失败 ' + failCount + ' 项');
        }).catch(function () {
            document.getElementById('btn-apply').disabled = false;
            showBanner('error', '请求失败');
        });
    }

    // 初始
    renderTable();
    updateUndoBtn();
})();

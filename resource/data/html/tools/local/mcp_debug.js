(function () {
    'use strict';

    var App = window.App || {};
    App.escapeHtml = App.escapeHtml || function (s) {
        return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) {
            return ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c];
        });
    };

    // ===== 动作说明（帮助文本） =====
    var ACTION_HELP = {
        probe:            '发送 initialize + notifications/initialized，再连续发起 tools_list / resources_list / prompts_list，汇总出对方能力、工具名列表与步骤耗时。用于快速判断：服务可用？握手版本对齐？能列工具？',
        initialize:       '仅执行一次握手：发送 initialize（附带客户端参数）后发送 notifications/initialized 通知。用于确认协议版本、能力协商、服务端信息返回。',
        tools_list:       '请求 tools/list，若返回大量工具可设置 limit / cursor 分页。',
        call_tool:        '请求 tools/call 调用对方工具。tool_args 可输入 JSON 对象或 JSON 字符串；若字符串解析失败会被包装为 {_raw: "..."} 传入。',
        resources_list:   '请求 resources/list，返回资源 URI 列表与描述。',
        read_resource:    '请求 resources/read，按 URI 读取资源（resource://... 或 HTTP(S) URL）。',
        prompts_list:     '请求 prompts/list，返回提示词目录。',
        get_prompt:       '请求 prompts/get，按 name 获取提示词，可附带 prompt_args 变量。',
        raw:              '发送任意自定义 JSON-RPC 消息；默认 method 以 notifications/ 开头会发通知（无 id），可勾选 notification 强制无 id。params 可留空（空对象）或填写任何合法 JSON 文本/对象。'
    };

    // ===== UI 引用 =====
    var $ = function (id) { return document.getElementById(id); };
    var f = {
        endpoint:   $('f-endpoint'),
        action:     $('f-action'),
        timeout:    $('f-timeout'),
        skip:       $('f-skip-handshake'),
        proto:      $('f-proto'),
        clientName: $('f-client-name'),
        clientVer:  $('f-client-version'),
        transport:  $('f-transport'),
        dynBody:    $('dyn-body'),
        dynTitle:   $('dyn-title')
    };
    var el = {
        help:       $('action-help'),
        btnRun:     $('btn-run'),
        btnClear:   $('btn-clear'),
        btnExamples:$('btn-examples'),
        notif:      $('notifications'),
        empty:      $('empty-state'),
        steps:      $('steps'),
        overview:   $('overview'),
        ovAction:   $('ov-action'),
        ovEndpoint: $('ov-endpoint'),
        ovSteps:    $('ov-steps'),
        ovTools:    $('ov-tools'),
        summary:    $('summary-head'),
        sumOk:      $('sum-ok'),
        sumElapsed: $('sum-elapsed')
    };

    // ===== 工具函数 =====
    function showBanner(type, msg) {
        if (!el.notif) return;
        var cls = 'info';
        if (type === 'error') cls = 'error';
        else if (type === 'warn') cls = 'warn';
        else if (type === 'success') cls = 'success';
        el.notif.innerHTML = '<div class="tool-banner ' + cls + '">' + App.escapeHtml(msg) + '</div>';
        el.notif.hidden = false;
        if (type === 'success') setTimeout(clearBanner, 3500);
    }
    function clearBanner() {
        if (el.notif) {
            el.notif.innerHTML = '';
            el.notif.hidden = true;
        }
    }

    function isLocalhost() {
        var h = location.hostname;
        return h === '127.0.0.1' || h === 'localhost' || h === '::1';
    }

    function prettyJson(v) {
        try {
            if (typeof v === 'string') {
                try { return JSON.stringify(JSON.parse(v), null, 2); }
                catch (e) { return v; }
            }
            return JSON.stringify(v, null, 2);
        } catch (e) { return String(v); }
    }
    function compactJson(v) {
        try {
            if (typeof v === 'string') {
                try { return JSON.stringify(JSON.parse(v)); }
                catch (e) { return v; }
            }
            return JSON.stringify(v);
        } catch (e) { return String(v); }
    }
    function copyText(text, onDone) {
        if (!text && text !== '') return;
        try {
            if (navigator.clipboard && navigator.clipboard.writeText) {
                navigator.clipboard.writeText(text).then(function () { if (onDone) onDone(); })
                    .catch(function () { fallback(text); });
            } else { fallback(text); }
        } catch (e) { fallback(text); }
        function fallback(t) {
            try {
                var ta = document.createElement('textarea');
                ta.value = t; ta.style.position = 'fixed';
                ta.style.left = '-9999px'; document.body.appendChild(ta);
                ta.select(); document.execCommand('copy');
                document.body.removeChild(ta);
                if (onDone) onDone();
            } catch (e2) { /* ignore */ }
        }
    }

    // ===== 动态参数：按 action 渲染表单 =====
    function clearDyn() { f.dynBody.innerHTML = ''; }
    function addRow(labelEl, inputEl, hint) {
        var row = document.createElement('div');
        row.className = 'ui-form-row';
        row.appendChild(labelEl);
        row.appendChild(inputEl);
        if (hint) {
            var h = document.createElement('div');
            h.className = 'ui-hint';
            h.style.width = '100%';
            h.style.paddingLeft = '8px';
            h.textContent = hint;
            row.appendChild(h);
        }
        f.dynBody.appendChild(row);
    }
    function mkLabel(text, widthPx) {
        var l = document.createElement('label');
        l.className = 'ui-form-label';
        if (widthPx) { l.style.width = widthPx + 'px'; l.style.flex = '0 0 ' + widthPx + 'px'; }
        l.textContent = text;
        return l;
    }
    function mkInput(type, id, value) {
        var i = document.createElement(type === 'textarea' ? 'textarea' : 'input');
        if (type !== 'textarea') {
            i.type = type;
            if (value != null) i.value = value;
        } else if (value != null) {
            i.value = value;
        }
        i.className = 'ui-form-input' + (type === 'textarea' ? ' md-jsonarea' : ' md-input-full');
        if (id) i.id = id;
        if (type !== 'textarea' && type !== 'checkbox' && type !== 'select') {
            i.style.flex = '1';
        }
        return i;
    }

    function renderDynamicParams(action) {
        clearDyn();

        if (action === 'tools_list' || action === 'resources_list' || action === 'prompts_list') {
            f.dynTitle.textContent = '分页参数（可选）';
            var cursor = mkInput('text', 'p-cursor', '');
            cursor.placeholder = 'cursor (可选)';
            addRow(mkLabel('cursor', 88), cursor);
            var limit = mkInput('number', 'p-limit', '');
            limit.min = 1; limit.placeholder = 'limit (可选，正整数)';
            addRow(mkLabel('limit', 88), limit);
            return;
        }
        if (action === 'call_tool') {
            f.dynTitle.textContent = '工具调用参数';
            var tn = mkInput('text', 'p-toolname', '');
            tn.placeholder = '例如: echo / run_shell_command / list_directory';
            addRow(mkLabel('工具名 *', 88), tn, '必需。填写目标 MCP 服务中实际存在的工具名，可用 probe 先查列表。');
            var targs = mkInput('textarea', 'p-toolarags', '{\n  \n}');
            targs.placeholder = 'object 或 JSON 字符串；留空会不传 arguments';
            var wrap = document.createElement('div');
            wrap.style.width = '100%';
            wrap.appendChild(targs);
            var lbl = mkLabel('tool_args', 88);
            lbl.style.alignSelf = 'flex-start';
            lbl.style.paddingTop = '0.5rem';
            addRow(lbl, wrap, '填写对象 {}；若写成字符串，后端会尝试 JSON.parse，解析失败会用 {_raw: "..."} 兜底并给出 warn。');
            return;
        }
        if (action === 'read_resource') {
            f.dynTitle.textContent = '资源参数';
            var uri = mkInput('text', 'p-uri', '');
            uri.placeholder = 'resource://tools/readme  或  https://... 或 file://...';
            addRow(mkLabel('resource_uri *', 104), uri, '必需。资源 URI，须与目标 MCP 服务已注册的资源一致。');
            return;
        }
        if (action === 'get_prompt') {
            f.dynTitle.textContent = '提示词参数';
            var pn = mkInput('text', 'p-promptname', '');
            pn.placeholder = '提示词名，可用 prompts_list 先查';
            addRow(mkLabel('prompt_name *', 112), pn, '必需。');
            var pa = mkInput('textarea', 'p-promptargs', '{}');
            pa.placeholder = 'object 或留空';
            var w = document.createElement('div');
            w.style.width = '100%';
            w.appendChild(pa);
            var lbl = mkLabel('prompt_args', 112);
            lbl.style.alignSelf = 'flex-start';
            lbl.style.paddingTop = '0.5rem';
            addRow(lbl, w, '可选。提示词变量，object：{"lang":"zh-CN","tone":"formal"}');
            return;
        }
        if (action === 'raw') {
            f.dynTitle.textContent = '自定义 JSON-RPC 参数';
            var m = mkInput('text', 'p-rawmethod', '');
            m.placeholder = '例如: initialize / tools/list / resources/read / my.custom.method';
            addRow(mkLabel('raw_method *', 112), m, '必需。任意 method 字符串。');

            var p = mkInput('textarea', 'p-rawparams', '{}');
            p.placeholder = '任意 JSON 值；留空或填 {} 不传 params';
            var w2 = document.createElement('div');
            w2.style.width = '100%';
            w2.appendChild(p);
            var lbl2 = mkLabel('raw_params', 112);
            lbl2.style.alignSelf = 'flex-start';
            lbl2.style.paddingTop = '0.5rem';
            addRow(lbl2, w2, '可填写 object / array / string / 数字；若填文本后端会作为原始 JSON 解析；解析失败按字符串传入。');

            var notif = document.createElement('label');
            notif.className = 'ui-toggle';
            notif.style.marginLeft = 'auto';
            notif.innerHTML = '<input type="checkbox" id="p-notification"> <span>作为通知发送（无 JSON-RPC id）。不填但 method 以 notifications/ 开头时自动勾选。</span>';
            var rowNotif = document.createElement('div');
            rowNotif.className = 'ui-form-row';
            rowNotif.appendChild(notif);
            f.dynBody.appendChild(rowNotif);
            return;
        }

        f.dynTitle.textContent = '动态参数';
        var none = document.createElement('div');
        none.className = 'ui-hint';
        none.textContent = '当前动作无需额外参数。';
        f.dynBody.appendChild(none);
    }

    function readDynamicParams(action) {
        var p = {};
        function v(id) {
            var e = document.getElementById(id);
            if (!e) return undefined;
            if (e.type === 'checkbox') return e.checked;
            if (e.type === 'number') {
                if (e.value === '' || e.value == null) return undefined;
                return parseInt(e.value, 10);
            }
            if (e.tagName === 'TEXTAREA') {
                return (e.value == null || e.value === '') ? undefined : e.value;
            }
            return (e.value == null || e.value === '') ? undefined : e.value;
        }
        function parseJsonString(s) {
            if (s == null || s === '') return undefined;
            s = s.trim();
            if (!s) return undefined;
            try { return JSON.parse(s); }
            catch (e) { return s; /* 字符串原文；后端会二次判断 */ }
        }

        if (action === 'tools_list' || action === 'resources_list' || action === 'prompts_list') {
            if (typeof v('p-cursor') === 'string' && v('p-cursor').length) p.cursor = v('p-cursor');
            if (typeof v('p-limit') === 'number' && !isNaN(v('p-limit'))) p.limit = v('p-limit');
        } else if (action === 'call_tool') {
            p.tool_name = v('p-toolname') || '';
            var t = v('p-toolarags');
            if (t != null && t !== '') p.tool_args = parseJsonString(t);
        } else if (action === 'read_resource') {
            p.resource_uri = v('p-uri') || '';
        } else if (action === 'get_prompt') {
            p.prompt_name = v('p-promptname') || '';
            var pa = v('p-promptargs');
            if (pa != null && pa !== '') {
                var parsed = parseJsonString(pa);
                if (parsed && typeof parsed === 'object') p.prompt_args = parsed;
            }
        } else if (action === 'raw') {
            p.raw_method = v('p-rawmethod') || '';
            var rp = v('p-rawparams');
            if (rp != null && rp !== '') p.raw_params = parseJsonString(rp);
            p.notification = !!v('p-notification');
        }
        return p;
    }

    // ===== 采集最终请求参数 =====
    function collectArgs() {
        var endpoint = (f.endpoint.value || '').trim();
        var action   = f.action.value;
        var timeout  = parseInt(f.timeout.value || '30', 10);
        if (isNaN(timeout) || timeout < 1) timeout = 30;
        if (timeout > 600) timeout = 600;

        var args = {
            endpoint: endpoint,
            action: action,
            timeout_seconds: timeout,
            skip_handshake: !!f.skip.checked,
            protocol_version: (f.proto.value || '').trim() || '2025-03-26',
            client_name: (f.clientName.value || '').trim() || 'MCP-Debug-Tool-WebUI',
            client_version: (f.clientVer.value || '').trim() || '1.0',
            transport: (f.transport && f.transport.value) || 'auto'
        };
        var dyn = readDynamicParams(action);
        Object.keys(dyn).forEach(function (k) { args[k] = dyn[k]; });
        return args;
    }

    // ===== 调用后端 MCP 调试代理（POST /api/local/mcp_debug）=====
    // 后端代理直接返回 runMcpDebugReport 生成的 report JSON，无需走 /mcp + tools/call 的
    // JSON-RPC 包装。report 字段：{ ok, action, target_endpoint, steps, tool_names?, warn?, fatal_exception?, error? }
    function callMcpDebugTool(args, onProgress) {
        var t0 = Date.now();
        onProgress && onProgress({ phase: 'requesting', t0: t0 });

        return fetch('/api/local/mcp_debug', {
            method: 'POST',
            credentials: 'include',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(args)
        }).then(function (res) {
            return res.text().then(function (t) {
                var report;
                try { report = t ? JSON.parse(t) : null; }
                catch (e) {
                    throw new Error('响应 JSON 解析失败: ' + e.message + '，原始：' + t.slice(0, 200));
                }
                if (!report) throw new Error('空响应');
                // 后端始终返回 200 + report JSON：
                //   - 输入校验失败：{ ok:false, error:"...", steps:[] }
                //   - 正常执行：{ ok:bool, steps:[...], ... }
                if (!res.ok) {
                    var errMsg = (report && report.error) ? report.error
                              : (res.statusText ? res.statusText : ('HTTP ' + res.status));
                    var err = new Error(errMsg);
                    err.report = report;
                    throw err;
                }
                report.__elapsedMs = Date.now() - t0;
                return report;
            });
        });
    }

    // ===== 结果渲染 =====
    function renderResult(report) {
        el.empty.hidden = true;
        el.summary.hidden = false;
        el.overview.hidden = false;
        el.steps.style.display = 'block';  // 确保步骤列表可见（覆盖 :empty 规则）
        clearBanner();  // 清除旧通知

        var ok = report.ok === true;
        el.sumOk.className = 'ui-badge ' + (ok ? 'ok' : 'err');
        el.sumOk.textContent = ok ? '全部成功' : '存在失败';
        var transportInfo = '';
        if (report.transport_detected === 'sse') {
            transportInfo = ' · SSE 传输';
        }
        el.sumElapsed.textContent = '总计 ' + (report.__elapsedMs ? report.__elapsedMs : '?') + ' ms' + transportInfo;

        el.ovAction.textContent   = String(report.action || '–');
        el.ovEndpoint.textContent = String(report.target_endpoint || '–');
        el.ovEndpoint.title       = el.ovEndpoint.textContent;
        var steps = Array.isArray(report.steps) ? report.steps : [];
        el.ovSteps.textContent    = steps.length;

        var toolNames = Array.isArray(report.tool_names) ? report.tool_names : null;
        el.ovTools.textContent = toolNames ? toolNames.length : '–';

        el.steps.innerHTML = '';

        // 工具名展示 banner（若有）
        if (toolNames && toolNames.length) {
            var list = document.createElement('ul');
            list.className = 'md-tool-list';
            toolNames.forEach(function (n) {
                var li = document.createElement('li');
                li.textContent = String(n);
                li.title = '点击复制';
                li.style.cursor = 'copy';
                li.addEventListener('click', function () {
                    copyText(String(n), function () {
                        showBanner('success', '已复制工具名: ' + n);
                    });
                });
                list.appendChild(li);
            });
            var wrap = document.createElement('div');
            wrap.style.padding = '0.25rem 0.75rem 0';
            wrap.appendChild(list);
            el.steps.appendChild(wrap);
        }

        // 若有 error / warn / fatal_exception
        if (report.error || report.warn || report.fatal_exception) {
            var banner = document.createElement('div');
            var isFatal  = !!report.fatal_exception;
            var isError  = !!report.error;
            banner.className = 'tool-banner ' + ((isFatal || isError) ? 'error' : 'warn');
            banner.style.margin = '0.5rem 0.75rem 0';
            if (isFatal) {
                banner.innerHTML = '<strong>致命异常：</strong>' + App.escapeHtml(report.fatal_exception);
            } else if (isError) {
                banner.innerHTML = '<strong>参数错误：</strong>' + App.escapeHtml(report.error);
            } else {
                banner.innerHTML = '<strong>Warn：</strong>' + App.escapeHtml(report.warn);
            }
            el.steps.appendChild(banner);
        }

        // 协议版本不一致警告
        if (report.protocol_warning) {
            var pv = document.createElement('div');
            pv.className = 'tool-banner warn';
            pv.style.margin = '0.5rem 0.75rem 0';
            pv.innerHTML = '<strong>协议版本提示：</strong>' + App.escapeHtml(report.protocol_warning);
            el.steps.appendChild(pv);
        }

        // Steps
        if (!steps.length) {
            var emptyStep = document.createElement('div');
            emptyStep.className = 'admin-empty';
            emptyStep.style.padding = '1.5rem';
            emptyStep.innerHTML = '<div class="icon">—</div><p>本次调用未产生步骤。</p>';
            el.steps.appendChild(emptyStep);
            return;
        }

        steps.forEach(function (step, idx) {
            el.steps.appendChild(renderStepCard(step, idx));
        });
    }

    function renderStepCard(step, idx) {
        var ok = step.ok === true;
        var card = document.createElement('div');
        card.className = 'md-step';
        card.dataset.ok = ok ? 'true' : 'false';
        card.dataset.open = 'true';

        var head = document.createElement('div');
        head.className = 'md-step-head';

        var arrow = document.createElement('span');
        arrow.className = 'md-step-arrow';
        arrow.textContent = '▸';
        head.appendChild(arrow);

        var name = document.createElement('span');
        name.className = 'md-step-name';
        name.textContent = (idx + 1) + '. ' + (step.step || 'request');
        head.appendChild(name);

        var chips = document.createElement('span');
        chips.className = 'md-step-chips';

        var stBadge = document.createElement('span');
        stBadge.className = 'ui-badge ' + (ok ? 'ok' : 'err');
        stBadge.textContent = ok ? 'OK' : 'FAIL';
        chips.appendChild(stBadge);

        if (typeof step.http_status === 'number') {
            var c1 = document.createElement('span');
            var httpOk = step.http_status >= 200 && step.http_status < 300;
            c1.className = 'md-step-chip ' + (httpOk ? 'http-ok' : 'http-err');
            c1.textContent = 'HTTP ' + step.http_status;
            chips.appendChild(c1);
        }
        if (typeof step.elapsed_ms === 'number') {
            var c2 = document.createElement('span');
            c2.className = 'md-step-chip';
            c2.textContent = step.elapsed_ms + ' ms';
            chips.appendChild(c2);
        }
        head.appendChild(chips);

        head.addEventListener('click', function () {
            var cur = card.dataset.open === 'true';
            card.dataset.open = cur ? 'false' : 'true';
        });

        var body = document.createElement('div');
        body.className = 'md-step-body';

        // error (顶层)
        if (step.error) {
            var errSec = mkSection('错误信息');
            var errPre = document.createElement('pre');
            errPre.className = 'md-pre err';
            errPre.textContent = String(step.error);
            errSec.secBody.appendChild(errPre);
            body.appendChild(errSec.root);
        }

        // request
        if (step.request != null && typeof step.request !== 'undefined') {
            var reqSec = mkSection('Request', step.request);
            body.appendChild(reqSec.root);
        }
        // response
        if (step.response != null && typeof step.response !== 'undefined') {
            var resSec = mkSection('Response', step.response);
            body.appendChild(resSec.root);
        }

        card.appendChild(head);
        card.appendChild(body);
        return card;
    }

    function mkSection(title, jsonValue) {
        var root = document.createElement('div');
        root.className = 'md-step-sec';

        var head = document.createElement('div');
        head.className = 'md-step-sec-title';
        var label = document.createElement('span');
        label.textContent = title;
        head.appendChild(label);

        var actions = document.createElement('div');
        actions.className = 'md-step-sec-actions';
        head.appendChild(actions);

        var secBody = document.createElement('div');
        root.appendChild(head);
        root.appendChild(secBody);

        var pre = document.createElement('pre');
        pre.className = 'md-pre';
        var curView = 'pretty';
        function apply() {
            pre.textContent = curView === 'pretty' ? prettyJson(jsonValue) : compactJson(jsonValue);
        }
        apply();
        secBody.appendChild(pre);

        var btnToggle = document.createElement('button');
        btnToggle.className = 'md-mini-btn';
        btnToggle.textContent = '压缩';
        btnToggle.title = '在格式化 / 压缩 JSON 视图之间切换';
        btnToggle.addEventListener('click', function () {
            curView = (curView === 'pretty') ? 'compact' : 'pretty';
            btnToggle.textContent = (curView === 'pretty') ? '压缩' : '格式化';
            apply();
        });
        actions.appendChild(btnToggle);

        var btnCopy = document.createElement('button');
        btnCopy.className = 'md-mini-btn';
        btnCopy.textContent = '复制';
        btnCopy.title = '复制该 JSON 到剪贴板';
        btnCopy.addEventListener('click', function () {
            var text = (curView === 'pretty') ? prettyJson(jsonValue) : compactJson(jsonValue);
            copyText(text, function () {
                btnCopy.textContent = '✓ 已复制';
                setTimeout(function () { btnCopy.textContent = '复制'; }, 1200);
            });
        });
        actions.appendChild(btnCopy);

        return { root: root, secBody: secBody, pre: pre };
    }

    // ===== 动作帮助文本 =====
    function refreshActionHelp() {
        var a = f.action.value;
        var t = ACTION_HELP[a] || '';
        el.help.textContent = '当前动作 [' + a + ']：' + t;
        renderDynamicParams(a);
    }

    // ===== 使用示例（快速填充表单） =====
    function showExamples() {
        // 简易“示例”选择（1~6），不走模态框，直接 banner 提示
        var html = '';
        var examples = [
            { name: '① 本机服务自检', endpoint: 'http://127.0.0.1:' + location.port + '/mcp', action: 'probe' },
            { name: '② 握手', endpoint: 'http://127.0.0.1:8080/mcp', action: 'initialize' },
            { name: '③ 列工具', endpoint: 'http://127.0.0.1:8080/mcp', action: 'tools_list' },
            { name: '④ 调用 echo', endpoint: 'http://127.0.0.1:8080/mcp', action: 'call_tool',
              extra: function () {
                  document.getElementById('p-toolname').value = 'echo';
                  document.getElementById('p-toolarags').value = '{\n  "message": "Hi from MCP Debug UI!"\n}';
              }},
            { name: '⑤ 读 README 资源', endpoint: 'http://127.0.0.1:' + location.port + '/mcp', action: 'read_resource',
              extra: function () {
                  document.getElementById('p-uri').value = 'resource://tools/readme';
              }},
            { name: '⑥ 自定义: server-info 风格 raw', endpoint: 'http://127.0.0.1:' + location.port + '/mcp', action: 'raw',
              extra: function () {
                  document.getElementById('p-rawmethod').value = 'server-info';
                  document.getElementById('p-rawparams').value = '{}';
                  document.getElementById('p-notification').checked = false;
              }}
        ];
        var container = document.createElement('div');
        container.style.display = 'flex';
        container.style.flexWrap = 'wrap';
        container.style.gap = '0.375rem';
        examples.forEach(function (ex, i) {
            var btn = document.createElement('button');
            btn.className = 'btn btn-sm btn-outline';
            btn.textContent = ex.name;
            btn.addEventListener('click', function () {
                f.endpoint.value = ex.endpoint;
                f.action.value = ex.action;
                refreshActionHelp();
                if (typeof ex.extra === 'function') ex.extra();
                showBanner('success', '已填充示例 [' + ex.name + ']，请点击"执行调试"。');
            });
            container.appendChild(btn);
        });
        var close = document.createElement('button');
        close.className = 'btn btn-sm';
        close.textContent = '关闭';
        close.style.marginLeft = 'auto';
        container.appendChild(close);
        close.addEventListener('click', clearBanner);

        el.notif.innerHTML = '';
        el.notif.hidden = false;
        var box = document.createElement('div');
        box.className = 'tool-banner info';
        box.style.display = 'flex';
        box.style.alignItems = 'center';
        box.style.flexWrap = 'wrap';
        box.style.gap = '0.5rem';
        var t = document.createElement('strong');
        t.style.marginRight = '0.25rem';
        t.textContent = '快速示例：';
        box.appendChild(t);
        box.appendChild(container);
        el.notif.appendChild(box);
    }

    // ===== 按钮绑定 =====
    f.action.addEventListener('change', refreshActionHelp);

    el.btnClear.addEventListener('click', function () {
        el.empty.hidden = false;
        el.summary.hidden = true;
        el.overview.hidden = true;
        el.steps.innerHTML = '';
        el.steps.style.display = '';  // 恢复默认，让 :empty 规则生效
        clearBanner();
        showBanner('info', '结果面板已清空');
    });

    el.btnExamples.addEventListener('click', showExamples);

    el.btnRun.addEventListener('click', function () {
        var args = collectArgs();
        if (!args.endpoint) { showBanner('error', '请填写目标端点 endpoint（例如 http://127.0.0.1:8080/mcp）'); return; }
        if (!args.action)  { showBanner('error', '请选择调试动作 action'); return; }
        if (!isLocalhost()) { showBanner('error', '本工具仅限本机浏览器访问（127.0.0.1 / localhost）'); return; }

        // call_tool / read_resource / get_prompt / raw：校验额外必填
        if (args.action === 'call_tool' && !args.tool_name) {
            showBanner('error', 'call_tool 需要填写 tool_name'); return;
        }
        if (args.action === 'read_resource' && !args.resource_uri) {
            showBanner('error', 'read_resource 需要填写 resource_uri'); return;
        }
        if (args.action === 'get_prompt' && !args.prompt_name) {
            showBanner('error', 'get_prompt 需要填写 prompt_name'); return;
        }
        if (args.action === 'raw' && !args.raw_method) {
            showBanner('error', 'raw 需要填写 raw_method'); return;
        }

        var oldLabel = el.btnRun.textContent;
        el.btnRun.textContent = '请求中…';
        el.btnRun.disabled = true;
        clearBanner();

        callMcpDebugTool(args).then(function (report) {
            renderResult(report);
            if (report.error) {
                showBanner('error', '参数错误：' + report.error);
            } else if (report.ok) {
                showBanner('success',
                    '调试完成，共 ' + (report.steps ? report.steps.length : 0) + ' 步，总耗时 ' + report.__elapsedMs + ' ms');
            } else {
                showBanner('warn', '调试部分步骤失败，请查看失败步骤的 error / response 字段。');
            }
        }).catch(function (err) {
            // 后端 4xx 输入校验错误：err.report 已含 error/steps，渲染后展示
            if (err && err.report) {
                renderResult(err.report);
                showBanner('error', '参数错误：' + (err.message || '未知错误'));
            } else {
                showBanner('error', '请求失败：' + (err && err.message ? err.message : err));
            }
        }).finally(function () {
            el.btnRun.textContent = oldLabel;
            el.btnRun.disabled = false;
        });
    });

    // ===== 初始化 =====
    (function init() {
        // 主机名非本机：隐藏按钮并展示告警
        if (!isLocalhost()) {
            var panel = document.querySelector('.md-layout');
            if (panel) panel.innerHTML =
                '<div class="admin-empty" style="grid-column:1/-1;"><div class="icon">🚫</div>' +
                '<h3>本页面仅限本机访问</h3><p>请在 127.0.0.1 / localhost 打开</p></div>';
            el.btnRun.disabled = true;
            return;
        }
        // 默认端口填当前页面的实际端口（方便调试自身服务）
        if (!f.endpoint.value && location.port) {
            f.endpoint.value = 'http://127.0.0.1:' + location.port + '/mcp';
        }
        refreshActionHelp();
    })();
})();
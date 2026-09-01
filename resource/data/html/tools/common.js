/* ===== Tools Module - Shared JavaScript =====
 * Provides:
 *   - Tool header rendering (renderToolHeader)
 *   - Clipboard copy with feedback (copyText)
 *   - Shorthand helpers ($, $$, el)
 *   - Banner show/hide (showBanner)
 *   - JSON syntax highlighting (highlightJson)
 *   - Back-to-top button
 */
(function (window) {
    'use strict';

    var Tools = {};

    // ===== Shorthand DOM helpers =====
    Tools.$ = function (id) { return document.getElementById(id); };
    Tools.$$ = function (sel, root) { return (root || document).querySelectorAll(sel); };
    Tools.el = function (tag, attrs, children) {
        var node = document.createElement(tag);
        if (attrs) {
            for (var k in attrs) {
                if (!Object.prototype.hasOwnProperty.call(attrs, k)) continue;
                if (k === 'class') node.className = attrs[k];
                else if (k === 'text') node.textContent = attrs[k];
                else if (k === 'html') node.innerHTML = attrs[k];
                else if (k.indexOf('on') === 0 && typeof attrs[k] === 'function') {
                    node.addEventListener(k.slice(2).toLowerCase(), attrs[k]);
                } else if (k === 'style' && typeof attrs[k] === 'object') {
                    for (var s in attrs[k]) node.style[s] = attrs[k][s];
                } else {
                    node.setAttribute(k, attrs[k]);
                }
            }
        }
        if (children) {
            if (!Array.isArray(children)) children = [children];
            children.forEach(function (c) {
                if (c == null) return;
                if (typeof c === 'string' || typeof c === 'number') {
                    node.appendChild(document.createTextNode(String(c)));
                } else {
                    node.appendChild(c);
                }
            });
        }
        return node;
    };

    // ===== Compute relative backHref based on current URL depth =====
    // /tools/{code}.html -> ../tools.html
    // /tools/local/{code}.html -> ../../tools.html
    Tools.getBackHref = function () {
        var path = window.location.pathname;
        // Count segments between /tools/ and the filename
        if (path.indexOf('/tools/local/') !== -1) return '../../tools.html';
        if (path.indexOf('/tools/') !== -1) return '../tools.html';
        return '../tools.html';
    };

    // ===== Tool header =====
    // Renders the standard tool header into #tool-header-container.
    // opts: { title, description, icon (emoji or char), backText, backHref }
    Tools.renderToolHeader = function (opts) {
        var container = Tools.$('tool-header-container');
        if (!container) return;
        opts = opts || {};
        var backHref = opts.backHref || Tools.getBackHref();
        var backText = opts.backText || '返回';
        var icon = opts.icon || '🔧';
        var title = opts.title || '';
        var desc = opts.description || '';

        container.innerHTML = '';
        container.appendChild(Tools.el('header', { class: 'tool-header' }, [
            Tools.el('a', { class: 'btn btn-outline btn-sm btn-back', href: backHref }, [backText]),
            Tools.el('div', { class: 'tool-icon' }, [icon]),
            Tools.el('div', {}, [
                Tools.el('h1', { text: title }),
                desc ? Tools.el('p', { class: 'tool-desc', text: desc }) : null
            ])
        ]));
    };

    // ===== Catalog cache for tool metadata =====
    var _catalogPromise = null;
    var _catalogByFile = null; // { fileCode: {title, icon, desc} }
    var _catalogByCode = null; // { toolCode: {title, icon, desc} }

    Tools.getCatalog = function () {
        if (_catalogPromise) return _catalogPromise;
        if (!window.Api || !window.Api.tools || !window.Api.tools.catalog) {
            _catalogPromise = Promise.resolve({ tools: [] });
            return _catalogPromise;
        }
        _catalogPromise = window.Api.tools.catalog().then(function (resp) {
            var arr = (resp && resp.tools) || [];
            _catalogByFile = {};
            _catalogByCode = {};
            for (var i = 0; i < arr.length; i++) {
                var t = arr[i];
                if (t.code) _catalogByCode[t.code] = t;
                var u = t.url || '';
                var um = u.match(/\/([^/]+)\.html?$/);
                if (um) _catalogByFile[um[1]] = t;
            }
            return resp;
        }).catch(function () {
            _catalogByFile = {};
            _catalogByCode = {};
            return { tools: [] };
        });
        return _catalogPromise;
    };

    Tools.lookupToolByFile = function (fileCode) {
        if (!_catalogByFile) return null;
        return _catalogByFile[fileCode] || null;
    };

    // Auto-inject tool header on detail pages (URL: /tools/{code}.html or /tools/local/{code}.html)
    Tools.autoInjectHeader = function () {
        // Match both /tools/xxx.html and /tools/local/xxx.html
        var m = window.location.pathname.match(/\/tools\/(?:local\/)?([^/]+)\.html?$/);
        if (!m) return;
        var fileCode = m[1];
        // Skip if already index/tools page
        if (fileCode === 'index') return;
        var toolPage = document.querySelector('.tool-page');
        if (!toolPage) return;
        // Skip if opted-out via data attribute (e.g. <body data-no-header> or .tool-page[data-no-header])
        var noHeaderHost = document.querySelector('[data-no-header]');
        if (toolPage.hasAttribute('data-no-header')) return;
        if (noHeaderHost) return;
        // Skip if a header already exists
        if (toolPage.querySelector('.tool-header')) return;

        var backHref = Tools.getBackHref();
        var title = document.title || '工具';
        var icon = '🔧';
        var desc = '';

        var inject = function (meta) {
            if (meta) {
                title = meta.title || title;
                icon = meta.icon || icon;
                desc = meta.desc || desc;
            }
            var header = Tools.el('header', { class: 'tool-header' }, [
                Tools.el('a', { class: 'btn btn-outline btn-sm btn-back', href: backHref }, ['返回']),
                Tools.el('div', { class: 'tool-icon' }, [icon]),
                Tools.el('div', {}, [
                    Tools.el('h1', { text: title }),
                    desc ? Tools.el('p', { class: 'tool-desc', text: desc }) : null
                ])
            ]);
            toolPage.insertBefore(header, toolPage.firstChild);
        };

        // Try cached first, then fetch
        var cached = Tools.lookupToolByFile(fileCode);
        if (cached) {
            inject(cached);
        } else {
            Tools.getCatalog().then(function () {
                var found = Tools.lookupToolByFile(fileCode);
                inject(found);
            });
        }
    };

    // ===== Clipboard =====
    Tools.copyText = function (text, btnEl, feedbackText) {
        if (!text) return Promise.resolve(false);
        feedbackText = feedbackText || '已复制';
        function fallback() {
            try {
                var ta = document.createElement('textarea');
                ta.value = text;
                ta.style.position = 'fixed';
                ta.style.opacity = '0';
                document.body.appendChild(ta);
                ta.select();
                var ok = document.execCommand('copy');
                document.body.removeChild(ta);
                return ok;
            } catch (e) { return false; }
        }
        var p;
        if (navigator.clipboard && navigator.clipboard.writeText) {
            p = navigator.clipboard.writeText(text).then(function () { return true; }).catch(function () { return fallback(); });
        } else {
            p = Promise.resolve(fallback());
        }
        return p.then(function (ok) {
            if (ok && btnEl) {
                var orig = btnEl.innerHTML;
                btnEl.innerHTML = '✓ ' + feedbackText;
                btnEl.disabled = true;
                setTimeout(function () {
                    btnEl.innerHTML = orig;
                    btnEl.disabled = false;
                }, 1500);
            }
            return ok;
        });
    };

    // ===== Banners =====
    Tools.showBanner = function (containerId, type, message, autoCloseMs) {
        var c = Tools.$(containerId);
        if (!c) return;
        c.innerHTML = '';
        var banner = Tools.el('div', { class: 'tool-banner ' + type });
        banner.style.display = 'flex';
        banner.style.alignItems = 'center';
        banner.style.gap = '0.5rem';
        banner.innerHTML = '<span>' + message + '</span>' +
            '<span style="cursor:pointer;margin-left:auto;opacity:0.6;font-size:1rem;" title="关闭">✕</span>';
        c.appendChild(banner);
        var closeBtn = banner.querySelectorAll('span')[1];
        var close = function () {
            if (banner.parentNode) banner.parentNode.removeChild(banner);
        };
        closeBtn.addEventListener('click', close);
        var ms = autoCloseMs || 3000;
        if (ms > 0) setTimeout(close, ms);
    };
    Tools.clearBanner = function (containerId) {
        var c = Tools.$(containerId);
        if (c) c.innerHTML = '';
    };

    // ===== JSON syntax highlighting =====
    Tools.highlightJson = function (jsonStr) {
        if (!jsonStr) return '';
        var escaped = jsonStr
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;');
        return escaped.replace(
            /("(\\u[a-zA-Z0-9]{4}|\\[^u]|[^\\"])*"(\s*:)?|\b(true|false|null)\b|-?\d+(?:\.\d*)?(?:[eE][+\-]?\d+)?)/g,
            function (match) {
                var cls = 'json-number';
                if (/^"/.test(match)) {
                    if (/:$/.test(match)) cls = 'json-key';
                    else cls = 'json-string';
                } else if (/true|false/.test(match)) cls = 'json-bool';
                else if (/null/.test(match)) cls = 'json-null';
                return '<span class="' + cls + '">' + match + '</span>';
            }
        );
    };

    // ===== Back to top button =====
    Tools.initBackToTop = function () {
        var btn = Tools.el('button', {
            class: 'tool-back-top',
            html: '<svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="18 15 12 9 6 15"></polyline></svg>',
            title: '回到顶部',
            onclick: function () { window.scrollTo({ top: 0, behavior: 'smooth' }); }
        });
        document.body.appendChild(btn);
        window.addEventListener('scroll', function () {
            if (window.scrollY > 300) btn.classList.add('visible');
            else btn.classList.remove('visible');
        });
    };

    // ===== Utility: escape HTML =====
    Tools.escapeHtml = function (s) {
        if (s == null) return '';
        return String(s)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#39;');
    };

    // ===== Utility: download text/blob =====
    Tools.download = function (filename, content, mime) {
        var blob;
        if (content instanceof Blob) blob = content;
        else blob = new Blob([content], { type: mime || 'text/plain;charset=utf-8' });
        var url = URL.createObjectURL(blob);
        var a = Tools.el('a', { href: url, download: filename });
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
    };

    // ===== Utility: read file as text/data-url/array-buffer =====
    Tools.readFile = function (file, mode) {
        return new Promise(function (resolve, reject) {
            var reader = new FileReader();
            reader.onload = function () { resolve(reader.result); };
            reader.onerror = function () { reject(reader.error); };
            if (mode === 'dataUrl') reader.readAsDataURL(file);
            else if (mode === 'arrayBuffer') reader.readAsArrayBuffer(file);
            else reader.readAsText(file);
        });
    };

    // ===== API helpers (delegated to Api module) =====
    Tools.apiGet = function (url) { return window.Api.get(url); };
    Tools.apiPost = function (url, body, isForm) { return window.Api.post(url, body); };

    // Auto-init back to top on DOMContentLoaded
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', function () {
            Tools.autoInjectHeader();
            Tools.initBackToTop();
        });
    } else {
        Tools.autoInjectHeader();
        Tools.initBackToTop();
    }

    window.Tools = Tools;
})(window);
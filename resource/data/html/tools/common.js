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

    // ===== Tool header =====
    // Renders the standard tool header into #tool-header-container.
    // opts: { title, description, icon (emoji or char), backText, backHref }
    Tools.renderToolHeader = function (opts) {
        var container = Tools.$('tool-header-container');
        if (!container) return;
        opts = opts || {};
        var backHref = opts.backHref || '/tools/';
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
            html: '↑',
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
        return window.Api.escapeHtml(s);
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

    // ===== Tool metadata map (for auto-injecting header) =====
    var TOOL_META = {
        json_formatter: { icon: '{}', title: 'JSON格式化', desc: 'JSON美化、压缩、校验与路径查询' },
        http_tester: { icon: '🌐', title: 'HTTP接口测试', desc: '发送GET/POST等请求,测试API接口' },
        timestamp_converter: { icon: '⏰', title: '时间戳转换', desc: 'Unix时间戳与日期时间互转' },
        encoding_converter: { icon: '🔄', title: '编码转换', desc: 'Base64/URL/Unicode/Hex多编码互转' },
        ip_lookup: { icon: '📡', title: 'IP地址查询', desc: '查询IP归属地、运营商信息' },
        image_compressor: { icon: '🖼️', title: '图片压缩', desc: '在线压缩JPEG/PNG图片' },
        qrcode_generator: { icon: '📱', title: '二维码生成', desc: '生成文本/URL二维码' },
        icon_designer: { icon: '🎨', title: '图标设计', desc: '在线设计App图标和favicon' },
        json_editor: { icon: '✏️', title: 'JSON编辑器', desc: '可视化编辑JSON数据' },
        json_converter: { icon: '🔄', title: 'JSON转换', desc: 'JSON与XML/CSV/YAML互转' },
        regex_tester: { icon: '🔑', title: '正则表达式测试', desc: '在线测试正则表达式匹配' },
        crypto_tools: { icon: '🔒', title: '加密解密工具', desc: 'MD5/SHA/AES/DES哈希与加密' },
        url_encoder: { icon: '🔗', title: 'URL编码解码', desc: 'URL编码与解码工具' },
        unicode_converter: { icon: '🔤', title: 'Unicode转换', desc: '中文与Unicode编码互转' },
        jwt_decoder: { icon: '🎟️', title: 'JWT解析', desc: '解析JWT令牌的Header与Payload' },
        number_base_converter: { icon: '🔢', title: '进制转换', desc: '二/八/十/十六进制互转' },
        base64_to_image: { icon: '🖼️', title: 'Base64图片互转', desc: 'Base64与图片互转' },
        date_calculator: { icon: '📅', title: '日期计算器', desc: '计算两个日期差值与加减' },
        timezone_converter: { icon: '🌍', title: '时区转换', desc: '世界各时区时间转换' },
        cron_generator: { icon: '⏱️', title: 'Cron表达式生成', desc: '生成与解析Cron定时表达式' },
        text_counter: { icon: '📏', title: '字数统计', desc: '统计字符、单词、行数等' },
        text_space_stripper: { icon: '🧹', title: '去空格工具', desc: '去除文本中的空格与换行' },
        html_markdown_converter: { icon: '📝', title: 'HTML与Markdown互转', desc: 'HTML与Markdown格式互转' },
        code_formatter: { icon: '📋', title: '代码格式化', desc: 'HTML/CSS/JS/SQL代码美化' },
        yml_properties_converter: { icon: '📄', title: 'YML与Properties互转', desc: 'YAML与Properties配置互转' },
        password_generator: { icon: '🔑', title: '密码生成器', desc: '生成随机强密码' },
        color_tools: { icon: '🎭', title: '颜色工具', desc: 'RGB/HEX/HSL颜色转换与调色' },
        css_gradient_generator: { icon: '🌈', title: 'CSS渐变生成器', desc: '生成线性/径向CSS渐变' },
        image_watermark: { icon: '💧', title: '图片水印', desc: '为图片添加文字水印' },
        image_ocr: { icon: '🔍', title: '图片OCR识别', desc: '基于RapidOcr识别图片中的文字' },
        pdf_manager: { icon: '📚', title: 'PDF合并分割', desc: '合并、分割、提取、旋转PDF文件' },
        pdf_compressor: { icon: '🗜️', title: 'PDF压缩', desc: '压缩PDF文件大小' },
        pdf_watermark: { icon: '💧', title: 'PDF水印', desc: '为PDF添加文字水印' },
        pdf_converter: { icon: '📑', title: 'PDF转换', desc: 'PDF与其他格式互转' },

        // ===== New tools (ported from it-tools) =====
        uuid_generator: { icon: '🆔', title: 'UUID生成器', desc: '生成UUID v1/v4/v7与ULID' },
        text_to_binary: { icon: '💾', title: '文本与二进制互转', desc: '文本与二进制字符串互转' },
        basic_auth_generator: { icon: '🔐', title: 'Basic Auth生成器', desc: '生成HTTP Basic Auth认证头' },
        html_entities: { icon: '🔡', title: 'HTML实体编解码', desc: 'HTML实体编码与解码' },
        token_generator: { icon: '🔑', title: 'Token生成器', desc: '生成随机Token、UUID、JWT格式等' },
        lorem_ipsum_generator: { icon: '📜', title: 'Lorem Ipsum生成器', desc: '生成占位用拉丁文假文本' },
        case_converter: { icon: '🔠', title: '大小写转换', desc: '多种命名风格互转' },
        text_to_nato_alphabet: { icon: '📻', title: '北约音标字母', desc: '字母转北约音标与摩斯电码' },
        slugify_string: { icon: '🏷️', title: 'URL Slug生成器', desc: '将文本转为URL友好的slug' },
        list_converter: { icon: '📃', title: '列表转换器', desc: '列表去重、排序、添加前后缀等' },
        numeronym_generator: { icon: '#️⃣', title: '数字缩写生成器', desc: '生成i18n、k8s等数字缩写' },
        text_diff: { icon: '⚖️', title: '文本对比', desc: '逐行对比两段文本的差异' },
        roman_numeral_converter: { icon: '🏛️', title: '罗马数字转换', desc: '阿拉伯数字与罗马数字互转' },
        math_evaluator: { icon: '🧮', title: '数学表达式计算', desc: '安全计算数学表达式' },
        percentage_calculator: { icon: '📈', title: '百分比计算器', desc: '多种百分比计算模式' },
        temperature_converter: { icon: '🌡️', title: '温度转换', desc: '摄氏/华氏/开尔文/兰氏互转' },
        chmod_calculator: { icon: '🐧', title: 'Chmod计算器', desc: 'Linux文件权限计算器' },
        mime_types: { icon: '📎', title: 'MIME类型查询', desc: '文件扩展名与MIME类型对照表' },
        http_status_codes: { icon: '📶', title: 'HTTP状态码查询', desc: 'HTTP状态码含义速查表' },
        mac_address_generator: { icon: '💻', title: 'MAC地址生成器', desc: '生成随机MAC地址' },
        random_port_generator: { icon: '🎰', title: '随机端口生成器', desc: '生成随机网络端口号' },
        ipv4_subnet_calculator: { icon: '🖥️', title: 'IPv4子网计算器', desc: 'CIDR子网掩码与主机范围计算' },
        url_parser: { icon: '🔗', title: 'URL解析器', desc: '解析URL各组件和查询参数' },
        device_information: { icon: '📱', title: '设备信息', desc: '查看浏览器和设备详细信息' },
        meta_tag_generator: { icon: '🏷️', title: '元标签生成器', desc: '生成HTML元标签和OG标签' },
        bip39_mnemonic: { icon: '🔐', title: 'BIP39助记词', desc: '生成和验证BIP39加密货币助记词' },
        json_diff: { icon: '🔍', title: 'JSON对比', desc: '深度对比两个JSON对象的差异' },

        // ===== Newly implemented tools =====
        password_strength_analyser: { icon: '🛡️', title: '密码强度分析器', desc: '分析密码强度、熵值与破解时间' },
        pdf_signature_checker: { icon: '✍️', title: 'PDF签名检查器', desc: '检测PDF数字签名与证书信息' },
        toml_converter: { icon: '📐', title: 'TOML转换器', desc: 'TOML与JSON/YAML互转' },
        otp_generator: { icon: '🔑', title: 'OTP生成器', desc: '生成和验证TOTP/HOTP一次性密码' },
        keycode_info: { icon: '⌨️', title: '键码信息', desc: '查看键盘按键的keyCode等信息' },
        user_agent_parser: { icon: '🔍', title: '用户代理解析器', desc: '解析User-Agent字符串' },
        html_wysiwyg_editor: { icon: '📝', title: 'HTML所见即所得编辑器', desc: '在线富文本HTML编辑器' },
        safelink_decoder: { icon: '🔓', title: '安全链接解码器', desc: '解码Google/Outlook等安全链接' },
        wifi_qr_code_generator: { icon: '📶', title: 'WiFi二维码生成器', desc: '生成WiFi连接二维码' },
        svg_placeholder_generator: { icon: '🖼️', title: 'SVG占位符生成器', desc: '生成SVG格式占位图片' },
        camera_recorder: { icon: '📹', title: '摄像头录制器', desc: '录制摄像头视频并下载' },
        git_memo: { icon: '📋', title: 'Git备忘', desc: 'Git命令速查表' },
        docker_run_to_compose: { icon: '🐳', title: 'Docker Run转Compose', desc: '将docker run命令转为docker-compose.yml' },
        yaml_viewer: { icon: '📄', title: 'YAML查看器', desc: 'YAML格式化、校验与树形查看' },
        email_normalizer: { icon: '📧', title: '邮箱标准化', desc: '邮箱地址标准化与批量处理' },
        regex_memo: { icon: '📒', title: '正则备忘', desc: '正则表达式语法速查表' },
        ipv4_address_converter: { icon: '🔢', title: 'IPv4地址转换器', desc: 'IPv4地址与十进制/十六进制互转' },
        ipv4_range_expander: { icon: '📊', title: 'IPv4范围扩展器', desc: '计算包含IP范围的最小CIDR' },
        mac_address_lookup: { icon: '💻', title: 'MAC地址查询', desc: '通过OUI查询MAC地址厂商' },
        ipv6_ula_generator: { icon: '🌐', title: 'IPv6 ULA生成器', desc: '生成RFC 4193 IPv6唯一本地地址' },
        eta_calculator: { icon: '⏳', title: 'ETA计算器', desc: '计算预计完成时间和剩余时间' },
        chronometer: { icon: '⏱️', title: '计时器', desc: '秒表计时与记圈' },
        benchmark_builder: { icon: '⚡', title: '基准测试构建器', desc: '对比JavaScript代码执行性能' },
        string_obfuscator: { icon: '🔒', title: '字符串混淆器', desc: '将字符串混淆为多种编码形式' },
        ascii_text_drawer: { icon: '🔤', title: 'ASCII文本绘图', desc: '将文本转为ASCII艺术字' },
        emoji_picker: { icon: '😀', title: 'Emoji选择器', desc: '浏览和复制Emoji表情' },
        phone_parser: { icon: '📞', title: '电话号码解析器', desc: '解析和格式化国际电话号码' },
        iban_validator: { icon: '🏦', title: 'IBAN验证器', desc: '验证和解析国际银行账号' }
    };

    // Auto-inject tool header on detail pages (URL: /tools/{code}.html, not index.html)
    Tools.autoInjectHeader = function () {
        var m = window.location.pathname.match(/^\/tools\/([^/]+)\.html?$/);
        if (!m || m[1] === 'index') return;
        var code = m[1];
        var meta = TOOL_META[code] || {};
        var toolPage = document.querySelector('.tool-page');
        if (!toolPage) return;

        // Skip if a header already exists
        if (toolPage.querySelector('.tool-header')) return;

        var title = meta.title || document.title || '工具';
        var icon = meta.icon || '🔧';
        var desc = meta.desc || '';

        var header = Tools.el('header', { class: 'tool-header' }, [
            Tools.el('a', { class: 'btn btn-outline btn-sm btn-back', href: '/tools/' }, ['返回']),
            Tools.el('div', { class: 'tool-icon' }, [icon]),
            Tools.el('div', {}, [
                Tools.el('h1', { text: title }),
                desc ? Tools.el('p', { class: 'tool-desc', text: desc }) : null
            ])
        ]);
        toolPage.insertBefore(header, toolPage.firstChild);
    };

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

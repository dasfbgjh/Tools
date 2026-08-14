(function () {
    'use strict';

    // ===== Tool catalog =====
    // Each tool: {code, title, desc, icon, cats:[], keywords:[] }
    var TOOLS = [
        // common
        { code: 'json_formatter', icon: '{ }', cats: ['common', 'json'], title: 'JSON格式化', desc: 'JSON美化、压缩、校验与路径查询', keywords: ['json', '格式化', '美化', '压缩', '校验', 'formatter'] },
        { code: 'http_tester', icon: '🌐', cats: ['common', 'network'], title: 'HTTP接口测试', desc: '发送GET/POST等请求,测试API接口', keywords: ['http', 'api', '请求', 'postman', '接口测试'] },
        { code: 'timestamp_converter', icon: '⏰', cats: ['common', 'datetime'], title: '时间戳转换', desc: 'Unix时间戳与日期时间互转', keywords: ['时间戳', 'timestamp', 'unix', '时间', '日期'] },
        { code: 'encoding_converter', icon: '🔄', cats: ['common', 'encoding'], title: '编码转换', desc: 'Base64/URL/Unicode/Hex多编码互转', keywords: ['编码', 'base64', 'url', 'unicode', 'hex', '解码'] },
        { code: 'ip_lookup', icon: '📡', cats: ['common', 'network'], title: 'IP地址查询', desc: '查询IP归属地、运营商信息', keywords: ['ip', '地址', '归属地', '运营商', '查询'] },
        { code: 'image_compressor', icon: '🖼️', cats: ['common', 'image'], title: '图片压缩', desc: '在线压缩JPEG/PNG图片', keywords: ['图片', '压缩', 'image', 'compress'] },
        { code: 'qrcode_generator', icon: '📱', cats: ['common', 'image'], title: '二维码生成', desc: '生成文本/URL二维码', keywords: ['二维码', 'qrcode', '扫码'] },
        { code: 'icon_designer', icon: '🎨', cats: ['common', 'image'], title: '图标设计', desc: '在线设计App图标和favicon', keywords: ['图标', 'icon', 'logo', 'favicon'] },

        // json
        { code: 'json_editor', icon: '✏️', cats: ['common', 'json'], title: 'JSON编辑器', desc: '可视化编辑JSON数据', keywords: ['json', '编辑器', 'editor'] },
        { code: 'json_converter', icon: '🔄', cats: ['json'], title: 'JSON转换', desc: 'JSON与XML/CSV/YAML互转', keywords: ['json', 'xml', 'csv', 'yaml', '转换'] },

        // encoding
        { code: 'regex_tester', icon: '🔑', cats: ['text'], title: '正则表达式测试', desc: '在线测试正则表达式匹配', keywords: ['正则', 'regex', '表达式'] },
        { code: 'crypto_tools', icon: '🔒', cats: ['encoding'], title: '加密解密工具', desc: 'MD5/SHA/AES/DES哈希与加密', keywords: ['加密', '解密', 'md5', 'sha', 'aes', 'hash'] },
        { code: 'url_encoder', icon: '🔗', cats: ['encoding'], title: 'URL编码解码', desc: 'URL编码与解码工具', keywords: ['url', '编码', '解码', 'urlencode'] },
        { code: 'unicode_converter', icon: '🔤', cats: ['encoding'], title: 'Unicode转换', desc: '中文与Unicode编码互转', keywords: ['unicode', '中文', '编码'] },
        { code: 'jwt_decoder', icon: '🎟️', cats: ['encoding'], title: 'JWT解析', desc: '解析JWT令牌的Header与Payload', keywords: ['jwt', 'token', '令牌', '解析'] },
        { code: 'number_base_converter', icon: '🔢', cats: ['encoding'], title: '进制转换', desc: '二/八/十/十六进制互转', keywords: ['进制', '二进制', '十六进制', 'binary', 'hex'] },
        { code: 'base64_to_image', icon: '🖼️', cats: ['image', 'encoding'], title: 'Base64图片互转', desc: 'Base64与图片互转', keywords: ['base64', '图片', 'image'] },

        // datetime
        { code: 'date_calculator', icon: '📅', cats: ['datetime'], title: '日期计算器', desc: '计算两个日期差值与加减', keywords: ['日期', '计算', '差值', '天数'] },
        { code: 'timezone_converter', icon: '🌍', cats: ['datetime'], title: '时区转换', desc: '世界各时区时间转换', keywords: ['时区', 'timezone', 'UTC', 'GMT'] },
        { code: 'cron_generator', icon: '⏱️', cats: ['datetime'], title: 'Cron表达式生成', desc: '生成与解析Cron定时表达式', keywords: ['cron', 'crontab', '定时', '调度'] },

        // text
        { code: 'text_counter', icon: '📏', cats: ['text'], title: '字数统计', desc: '统计字符、单词、行数等', keywords: ['字数', '统计', '字符', '行数'] },
        { code: 'text_space_stripper', icon: '🧹', cats: ['text'], title: '去空格工具', desc: '去除文本中的空格与换行', keywords: ['空格', '换行', 'trim', 'strip'] },
        { code: 'html_markdown_converter', icon: '📝', cats: ['code', 'text'], title: 'HTML与Markdown互转', desc: 'HTML与Markdown格式互转', keywords: ['html', 'markdown', 'md', '转换'] },

        // code
        { code: 'code_formatter', icon: '📋', cats: ['code'], title: '代码格式化', desc: 'HTML/CSS/JS/SQL代码美化', keywords: ['代码', '格式化', '美化', 'formatter'] },
        { code: 'yml_properties_converter', icon: '📄', cats: ['code'], title: 'YML与Properties互转', desc: 'YAML与Properties配置互转', keywords: ['yml', 'yaml', 'properties', '配置'] },
        { code: 'password_generator', icon: '🔑', cats: ['code'], title: '密码生成器', desc: '生成随机强密码', keywords: ['密码', 'password', '随机', '生成'] },

        // frontend
        { code: 'color_tools', icon: '🎭', cats: ['frontend'], title: '颜色工具', desc: 'RGB/HEX/HSL颜色转换与调色', keywords: ['颜色', 'color', 'rgb', 'hex', 'hsl'] },
        { code: 'css_gradient_generator', icon: '🌈', cats: ['frontend'], title: 'CSS渐变生成器', desc: '生成线性/径向CSS渐变', keywords: ['css', '渐变', 'gradient', '背景'] },

        // image
        { code: 'image_converter', icon: '🔄', cats: ['image'], title: '图片格式转换', desc: '使用stb_image库转换图片格式', keywords: ['图片', '转换', '格式', 'png', 'jpg', 'bmp', 'image', 'converter'] },
        { code: 'image_ocr', icon: '🔍', cats: ['image', 'common'], title: '图片OCR识别', desc: '基于RapidOcr识别图片中的文字', keywords: ['图片', 'ocr', '文字识别', '光标识别', 'rapidocr', '文字提取'] },
        { code: 'image_watermark', icon: '💧', cats: ['image'], title: '图片水印', desc: '为图片添加文字水印', keywords: ['图片', '水印', 'watermark'] },

        // pdf
        { code: 'pdf_manager', icon: '📚', cats: ['pdf', 'common'], title: 'PDF合并分割', desc: '合并、分割、提取、旋转PDF文件', keywords: ['pdf', '合并', '分割', '提取', '旋转', 'merger'] },
        { code: 'pdf_compressor', icon: '🗜️', cats: ['pdf', 'common'], title: 'PDF压缩', desc: '压缩PDF文件大小', keywords: ['pdf', '压缩', 'compress'] },
        { code: 'pdf_watermark', icon: '💧', cats: ['pdf'], title: 'PDF水印', desc: '为PDF添加文字水印', keywords: ['pdf', '水印', 'watermark'] },

        // ===== New tools (ported from it-tools) =====
        // encoding
        { code: 'uuid_generator', icon: '🆔', cats: ['encoding', 'common'], title: 'UUID生成器', desc: '生成UUID v1/v4/v7与ULID', keywords: ['uuid', 'ulid', 'guid', '唯一id', 'random'] },
        { code: 'text_to_binary', icon: '💾', cats: ['encoding'], title: '文本与二进制互转', desc: '文本与二进制字符串互转', keywords: ['二进制', 'binary', '编码', '0和1'] },
        { code: 'basic_auth_generator', icon: '🔐', cats: ['encoding', 'network'], title: 'Basic Auth生成器', desc: '生成HTTP Basic Auth认证头', keywords: ['basic', 'auth', '认证', 'base64', 'header'] },
        { code: 'html_entities', icon: '🔡', cats: ['encoding', 'frontend'], title: 'HTML实体编解码', desc: 'HTML实体编码与解码', keywords: ['html', '实体', 'entities', '编码', '解码'] },
        { code: 'token_generator', icon: '🔑', cats: ['encoding', 'crypto'], title: 'Token生成器', desc: '生成随机Token、UUID、JWT格式等', keywords: ['token', '随机', 'uuid', 'jwt', 'api key'] },

        // text
        { code: 'lorem_ipsum_generator', icon: '📜', cats: ['text'], title: 'Lorem Ipsum生成器', desc: '生成占位用拉丁文假文本', keywords: ['lorem', 'ipsum', '占位', '假文', 'dummy'] },
        { code: 'case_converter', icon: '🔠', cats: ['text'], title: '大小写转换', desc: '多种命名风格互转(camel/snake/kebab等)', keywords: ['大小写', 'case', 'camel', 'snake', 'kebab', 'pascal'] },
        { code: 'text_to_nato_alphabet', icon: '📻', cats: ['text'], title: '北约音标字母', desc: '字母转北约音标与摩斯电码', keywords: ['nato', '音标', '摩斯', 'morse', '字母'] },
        { code: 'slugify_string', icon: '🏷️', cats: ['text'], title: 'URL Slug生成器', desc: '将文本转为URL友好的slug', keywords: ['slug', 'url', 'seo', '拼音', 'permalink'] },
        { code: 'list_converter', icon: '📃', cats: ['text'], title: '列表转换器', desc: '列表去重、排序、添加前后缀等', keywords: ['列表', 'list', '去重', '排序', '转换'] },
        { code: 'numeronym_generator', icon: '#️⃣', cats: ['text'], title: '数字缩写生成器', desc: '生成i18n、k8s等数字缩写', keywords: ['缩写', 'numeronym', 'i18n', 'k8s'] },
        { code: 'text_diff', icon: '⚖️', cats: ['text'], title: '文本对比', desc: '逐行对比两段文本的差异', keywords: ['diff', '对比', '差异', 'compare'] },
        { code: 'roman_numeral_converter', icon: '🏛️', cats: ['text', 'math'], title: '罗马数字转换', desc: '阿拉伯数字与罗马数字互转', keywords: ['罗马', 'roman', '数字', '转换'] },

        // math
        { code: 'math_evaluator', icon: '🧮', cats: ['math', 'code'], title: '数学表达式计算', desc: '安全计算数学表达式', keywords: ['数学', '计算', '表达式', 'math', 'evaluate', '计算器'] },
        { code: 'percentage_calculator', icon: '📈', cats: ['math'], title: '百分比计算器', desc: '多种百分比计算模式', keywords: ['百分比', 'percent', '计算', '增减'] },
        { code: 'temperature_converter', icon: '🌡️', cats: ['math', 'datetime'], title: '温度转换', desc: '摄氏/华氏/开尔文/兰氏互转', keywords: ['温度', 'temperature', '摄氏', '华氏', '开尔文'] },

        // code
        { code: 'chmod_calculator', icon: '🐧', cats: ['code'], title: 'Chmod计算器', desc: 'Linux文件权限计算器', keywords: ['chmod', '权限', 'linux', 'rwx', 'octal'] },
        { code: 'mime_types', icon: '📎', cats: ['code'], title: 'MIME类型查询', desc: '文件扩展名与MIME类型对照表', keywords: ['mime', '类型', '扩展名', 'content-type', '文件'] },

        // network
        { code: 'http_status_codes', icon: '📶', cats: ['network'], title: 'HTTP状态码查询', desc: 'HTTP状态码含义速查表', keywords: ['http', '状态码', 'status', 'code', '响应'] },
        { code: 'mac_address_generator', icon: '💻', cats: ['network'], title: 'MAC地址生成器', desc: '生成随机MAC地址', keywords: ['mac', '地址', '随机', '网卡', 'oui'] },
        { code: 'random_port_generator', icon: '🎰', cats: ['network'], title: '随机端口生成器', desc: '生成随机网络端口号', keywords: ['端口', 'port', '随机', 'random'] },
        { code: 'ipv4_subnet_calculator', icon: '🖥️', cats: ['network'], title: 'IPv4子网计算器', desc: 'CIDR子网掩码与主机范围计算', keywords: ['ipv4', '子网', 'subnet', 'cidr', '掩码', 'mask'] },
        { code: 'url_parser', icon: '🔗', cats: ['network', 'frontend'], title: 'URL解析器', desc: '解析URL各组件和查询参数', keywords: ['url', '解析', 'query', 'params', 'components'] },
        { code: 'device_information', icon: '📱', cats: ['network', 'common'], title: '设备信息', desc: '查看浏览器和设备详细信息', keywords: ['设备', '浏览器', 'user agent', '屏幕', '系统'] },
        { code: 'meta_tag_generator', icon: '🏷️', cats: ['frontend'], title: '元标签生成器', desc: '生成HTML元标签和OG标签', keywords: ['meta', 'og', '标签', 'seo', 'social'] },

        // json
        { code: 'json_diff', icon: '🔍', cats: ['json'], title: 'JSON对比', desc: '深度对比两个JSON对象的差异', keywords: ['json', 'diff', '对比', '差异', 'compare'] },

        // crypto
        { code: 'bip39_mnemonic', icon: '🔐', cats: ['crypto', 'common'], title: 'BIP39助记词', desc: '生成和验证BIP39加密货币助记词', keywords: ['bip39', '助记词', 'mnemonic', '加密货币', '钱包'] },
        { code: 'password_strength_analyser', icon: '🛡️', cats: ['crypto'], title: '密码强度分析器', desc: '分析密码强度、熵值与破解时间', keywords: ['密码', '强度', '熵', '安全', 'password'] },
        { code: 'pdf_signature_checker', icon: '✍️', cats: ['crypto', 'pdf'], title: 'PDF签名检查器', desc: '检测PDF数字签名与证书信息', keywords: ['pdf', '签名', '数字签名', '证书', 'signature'] },

        // converter - TOML
        { code: 'toml_converter', icon: '📐', cats: ['code'], title: 'TOML转换器', desc: 'TOML与JSON/YAML互转', keywords: ['toml', 'json', 'yaml', '转换', '配置'] },

        // web
        { code: 'otp_generator', icon: '🔑', cats: ['crypto', 'network'], title: 'OTP生成器', desc: '生成和验证TOTP/HOTP一次性密码', keywords: ['otp', 'totp', 'hotp', '双因素', '2fa'] },
        { code: 'keycode_info', icon: '⌨️', cats: ['frontend', 'code'], title: '键码信息', desc: '查看键盘按键的keyCode等信息', keywords: ['keycode', '键盘', '按键', 'key', 'code'] },
        { code: 'user_agent_parser', icon: '🔍', cats: ['network'], title: '用户代理解析器', desc: '解析User-Agent字符串', keywords: ['user-agent', 'ua', '浏览器', '解析'] },
        { code: 'html_wysiwyg_editor', icon: '📝', cats: ['frontend'], title: 'HTML所见即所得编辑器', desc: '在线富文本HTML编辑器', keywords: ['html', '编辑器', 'wysiwyg', '富文本'] },
        { code: 'safelink_decoder', icon: '🔓', cats: ['network'], title: '安全链接解码器', desc: '解码Google/Outlook等安全链接', keywords: ['safelink', '安全链接', '解码', 'url', 'google'] },

        // images and videos
        { code: 'wifi_qr_code_generator', icon: '📶', cats: ['image'], title: 'WiFi二维码生成器', desc: '生成WiFi连接二维码', keywords: ['wifi', '二维码', 'qrcode', '无线网络'] },
        { code: 'svg_placeholder_generator', icon: '🖼️', cats: ['image', 'frontend'], title: 'SVG占位符生成器', desc: '生成SVG格式占位图片', keywords: ['svg', '占位符', 'placeholder', '图片'] },
        { code: 'camera_recorder', icon: '📹', cats: ['image'], title: '摄像头录制器', desc: '录制摄像头视频并下载', keywords: ['摄像头', '录制', 'camera', '视频', 'webcam'] },

        // development
        { code: 'git_memo', icon: '📋', cats: ['code'], title: 'Git备忘', desc: 'Git命令速查表', keywords: ['git', '命令', '备忘', '速查'] },
        { code: 'docker_run_to_compose', icon: '🐳', cats: ['code'], title: 'Docker Run转Compose', desc: '将docker run命令转为docker-compose.yml', keywords: ['docker', 'compose', 'yaml', '转换'] },
        { code: 'yaml_viewer', icon: '📄', cats: ['code', 'json'], title: 'YAML查看器', desc: 'YAML格式化、校验与树形查看', keywords: ['yaml', '格式化', '查看器', '树形'] },
        { code: 'email_normalizer', icon: '📧', cats: ['text'], title: '邮箱标准化', desc: '邮箱地址标准化与批量处理', keywords: ['邮箱', 'email', '标准化', 'normalize'] },
        { code: 'regex_memo', icon: '📒', cats: ['text', 'code'], title: '正则备忘', desc: '正则表达式语法速查表', keywords: ['正则', 'regex', '备忘', '速查', '语法'] },

        // network
        { code: 'ipv4_address_converter', icon: '🔢', cats: ['network'], title: 'IPv4地址转换器', desc: 'IPv4地址与十进制/十六进制互转', keywords: ['ipv4', '地址', '转换', '十进制', '十六进制'] },
        { code: 'ipv4_range_expander', icon: '📊', cats: ['network'], title: 'IPv4范围扩展器', desc: '计算包含IP范围的最小CIDR', keywords: ['ipv4', '范围', 'cidr', '子网', '扩展'] },
        { code: 'mac_address_lookup', icon: '💻', cats: ['network'], title: 'MAC地址查询', desc: '通过OUI查询MAC地址厂商', keywords: ['mac', '地址', 'oui', '厂商', '查询'] },
        { code: 'ipv6_ula_generator', icon: '🌐', cats: ['network'], title: 'IPv6 ULA生成器', desc: '生成RFC 4193 IPv6唯一本地地址', keywords: ['ipv6', 'ula', '唯一本地地址', 'rfc4193'] },

        // math
        { code: 'eta_calculator', icon: '⏳', cats: ['math'], title: 'ETA计算器', desc: '计算预计完成时间和剩余时间', keywords: ['eta', '预计', '完成', '时间', '进度'] },

        // measurement
        { code: 'chronometer', icon: '⏱️', cats: ['datetime'], title: '计时器', desc: '秒表计时与记圈', keywords: ['计时器', '秒表', 'chronometer', 'lap', '圈'] },
        { code: 'benchmark_builder', icon: '⚡', cats: ['code', 'math'], title: '基准测试构建器', desc: '对比JavaScript代码执行性能', keywords: ['基准', 'benchmark', '性能', '测试', '对比'] },

        // text
        { code: 'string_obfuscator', icon: '🔒', cats: ['text', 'crypto'], title: '字符串混淆器', desc: '将字符串混淆为多种编码形式', keywords: ['混淆', 'obfuscator', '编码', 'unicode', 'hex'] },
        { code: 'ascii_text_drawer', icon: '🔤', cats: ['text'], title: 'ASCII文本绘图', desc: '将文本转为ASCII艺术字', keywords: ['ascii', '艺术', '绘图', '字符画', 'banner'] },
        { code: 'emoji_picker', icon: '😀', cats: ['text'], title: 'Emoji选择器', desc: '浏览和复制Emoji表情', keywords: ['emoji', '表情', '选择器', 'picker'] },

        // data
        { code: 'phone_parser', icon: '📞', cats: ['network'], title: '电话号码解析器', desc: '解析和格式化国际电话号码', keywords: ['电话', 'phone', '解析', '格式化', '国际'] },
        { code: 'iban_validator', icon: '🏦', cats: ['network'], title: 'IBAN验证器', desc: '验证和解析国际银行账号', keywords: ['iban', '银行', '账号', '验证', '解析'] }
    ];

    // ===== Categories =====
    var CATEGORIES = [
        { code: 'all', name: '全部工具' },
        { code: 'common', name: '常用工具' },
        { code: 'json', name: 'JSON工具' },
        { code: 'encoding', name: '编码加密' },
        { code: 'network', name: '网络工具' },
        { code: 'datetime', name: '时间日期' },
        { code: 'code', name: '代码工具' },
        { code: 'text', name: '文本处理' },
        { code: 'image', name: '图像工具' },
        { code: 'frontend', name: '前端开发' },
        { code: 'pdf', name: 'PDF工具' },
        { code: 'math', name: '数学计算' },
        { code: 'crypto', name: '加密工具' }
    ];

    var SEARCH_TERM_KEY = 'tool-search-term';
    var state = {
        activeCat: localStorage.getItem('tool-active-cat') || 'all',
        searchTerm: localStorage.getItem(SEARCH_TERM_KEY) || '',
        toolLastUsed: {} // {code: timestampMs} 从用户设置同步加载
    };

    // 无论是否登录，都从数据库加载共享设置
    Api.settings.list().then(function (sd) {
        if (sd.success && sd.settings) {
            if (sd.settings.toolActiveCat) {
                state.activeCat = sd.settings.toolActiveCat;
                try { localStorage.setItem('tool-active-cat', state.activeCat); } catch (e) { }
                renderCategories();
            }
            if (sd.settings.toolLastUsed) {
                try {
                    var parsed = JSON.parse(sd.settings.toolLastUsed);
                    if (parsed && typeof parsed === 'object') {
                        state.toolLastUsed = parsed;
                    }
                } catch (e) { }
            }
            renderTools();
        }
    }).catch(function () { });

    function renderCategories() {
        var box = document.getElementById('categories');
        box.innerHTML = '';
        CATEGORIES.forEach(function (cat) {
            var btn = document.createElement('button');
            btn.className = 'tool-cat-btn' + (state.activeCat === cat.code ? ' active' : '');
            btn.textContent = cat.name;
            btn.onclick = function () {
                state.activeCat = cat.code;
                try { localStorage.setItem('tool-active-cat', cat.code); } catch (e) { }
                // 无论是否登录，都调用接口保存
                Api.settings.update({ toolActiveCat: cat.code }).catch(function () { });
                renderCategories();
                renderTools();
            };
            box.appendChild(btn);
        });
    }

    function matchTool(tool) {
        // category filter
        if (state.activeCat !== 'all' && tool.cats.indexOf(state.activeCat) === -1) return false;
        // search filter
        if (!state.searchTerm) return true;
        var term = state.searchTerm.toLowerCase();
        if (tool.title.toLowerCase().indexOf(term) !== -1) return true;
        if (tool.desc.toLowerCase().indexOf(term) !== -1) return true;
        for (var i = 0; i < tool.keywords.length; i++) {
            if (tool.keywords[i].toLowerCase().indexOf(term) !== -1) return true;
        }
        return false;
    }

    function sortTools(list) {
        if (state.searchTerm) {
            // rank by title match first
            var term = state.searchTerm.toLowerCase();
            list.sort(function (a, b) {
                var at = a.title.toLowerCase().indexOf(term);
                var bt = b.title.toLowerCase().indexOf(term);
                if (at === -1 && bt === -1) return 0;
                if (at === -1) return 1;
                if (bt === -1) return -1;
                return at - bt;
            });
        } else {
            // 按最近使用时间降序；未使用的工具排在后部，组内 common 优先 + title 字典序
            list.sort(function (a, b) {
                var at = state.toolLastUsed[a.code] || 0;
                var bt = state.toolLastUsed[b.code] || 0;
                if (at !== bt) return bt - at; // 时间越近越靠前
                var ac = a.cats.indexOf('common') !== -1 ? 0 : 1;
                var bc = b.cats.indexOf('common') !== -1 ? 0 : 1;
                if (ac !== bc) return ac - bc;
                return a.title.localeCompare(b.title, 'zh');
            });
        }
        return list;
    }

    function renderTools() {
        var grid = document.getElementById('tool-grid');
        var empty = document.getElementById('empty-state');
        var filtered = sortTools(TOOLS.filter(matchTool));
        grid.innerHTML = '';

        if (filtered.length === 0) {
            empty.classList.remove('hidden');
            return;
        }
        empty.classList.add('hidden');

        filtered.forEach(function (tool) {
            var card = document.createElement('a');
            card.className = 'tool-card-item';
            card.href = '/tools/' + tool.code + '.html';
            card.addEventListener('click', function () {
                state.toolLastUsed[tool.code] = Date.now();
                try {
                    Api.settings.update({ toolLastUsed: JSON.stringify(state.toolLastUsed) }).catch(function () { });
                } catch (e) { }
            });

            var iconBox = document.createElement('div');
            iconBox.className = 'icon-box';
            iconBox.textContent = tool.icon;

            var contentWrapper = document.createElement('div');
            contentWrapper.className = 'content-wrapper';

            var title = document.createElement('h3');
            title.textContent = tool.title;
            contentWrapper.appendChild(title);

            var desc = document.createElement('p');
            desc.textContent = tool.desc;
            contentWrapper.appendChild(desc);

            var topRow = document.createElement('div');
            topRow.className = 'top-row';
            topRow.appendChild(iconBox);
            topRow.appendChild(contentWrapper);
            card.appendChild(topRow);

            var tags = document.createElement('div');
            tags.className = 'tool-tags';
            tool.cats.forEach(function (catCode) {
                var cat = CATEGORIES.find(function (c) { return c.code === catCode; });
                if (cat && cat.code !== 'all') {
                    var tag = document.createElement('span');
                    tag.className = 'tool-tag';
                    tag.textContent = cat.name;
                    tags.appendChild(tag);
                }
            });
            card.appendChild(tags);

            grid.appendChild(card);
        });
    }

    var searchInput = document.getElementById('tool-search');
    if (searchInput) {
        // 恢复搜索词到输入框
        if (state.searchTerm) searchInput.value = state.searchTerm;
        searchInput.addEventListener('input', function (e) {
            state.searchTerm = e.target.value.trim();
            try {
                if (state.searchTerm) localStorage.setItem(SEARCH_TERM_KEY, state.searchTerm);
                else localStorage.removeItem(SEARCH_TERM_KEY);
            } catch (ex) { }
            renderTools();
        });
    }

    renderCategories();
    renderTools();
})();
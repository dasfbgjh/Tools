// ======================================================
// 共享文件图标库：基于扩展名返回 SVG 图标
// 可被文件服务器、批量重命名文件浏览器等使用
// 用法：
//   <script src="../fileicons.js"></script>
//   FileIcons.getIcon(name, isDir)    // 返回 SVG 字符串
//   FileIcons.iconClass(name, isDir)  // 返回 CSS class 名（fi-folder / fi-image / ...）
// ======================================================
(function () {
    'use strict';
    if (window.FileIcons) return;

    var FI = {
        folder: '<svg class="fi fi-folder" viewBox="0 0 24 24" width="18" height="18" fill="currentColor" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round"><path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/></svg>',
        image: '<svg class="fi fi-image" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/></svg>',
        video: '<svg class="fi fi-video" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="2" width="20" height="20" rx="2.18" ry="2.18"/><line x1="7" y1="2" x2="7" y2="22"/><line x1="17" y1="2" x2="17" y2="22"/><line x1="2" y1="12" x2="22" y2="12"/><line x1="2" y1="7" x2="7" y2="7"/><line x1="2" y1="17" x2="7" y2="17"/><line x1="17" y1="17" x2="22" y2="17"/><line x1="17" y1="7" x2="22" y2="7"/></svg>',
        audio: '<svg class="fi fi-audio" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>',
        pdf: '<svg class="fi fi-pdf" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="9" y1="13" x2="15" y2="13"/><line x1="9" y1="17" x2="15" y2="17"/></svg>',
        doc: '<svg class="fi fi-doc" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="8" y1="13" x2="16" y2="13"/><line x1="8" y1="17" x2="13" y2="17"/></svg>',
        sheet: '<svg class="fi fi-sheet" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><path d="M8 13h8M8 17h8M12 13v4"/></svg>',
        slide: '<svg class="fi fi-slide" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M2 3h20"/><path d="M21 3v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V3"/><line x1="12" y1="16" x2="12" y2="21"/><line x1="8" y1="21" x2="16" y2="21"/></svg>',
        archive: '<svg class="fi fi-archive" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="21 8 21 21 3 21 3 8"/><rect x="1" y="3" width="22" height="5"/><line x1="10" y1="12" x2="14" y2="12"/></svg>',
        code: '<svg class="fi fi-code" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/></svg>',
        exec: '<svg class="fi fi-exec" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="4" width="16" height="16" rx="2" ry="2"/><rect x="9" y="9" width="6" height="6"/><line x1="9" y1="2" x2="9" y2="4"/><line x1="15" y1="2" x2="15" y2="4"/><line x1="9" y1="20" x2="9" y2="22"/><line x1="15" y1="20" x2="15" y2="22"/><line x1="20" y1="9" x2="22" y2="9"/><line x1="20" y1="14" x2="22" y2="14"/><line x1="2" y1="9" x2="4" y2="9"/><line x1="2" y1="14" x2="4" y2="14"/></svg>',
        text: '<svg class="fi fi-text" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="9" y1="13" x2="15" y2="13"/><line x1="11" y1="17" x2="13" y2="17"/></svg>',
        font: '<svg class="fi fi-font" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="4 7 4 4 20 4 20 7"/><line x1="9" y1="20" x2="15" y2="20"/><line x1="12" y1="4" x2="12" y2="20"/></svg>',
        cert: '<svg class="fi fi-cert" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/><polyline points="9 12 11 14 15 10"/></svg>',
        book: '<svg class="fi fi-book" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M2 3h6a4 4 0 0 1 4 4v14a3 3 0 0 0-3-3H2z"/><path d="M22 3h-6a4 4 0 0 0-4 4v14a3 3 0 0 1 3-3h7z"/></svg>',
        file: '<svg class="fi fi-file" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M13 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z"/><polyline points="13 2 13 9 20 9"/></svg>'
    };

    var RULES = [
        { type: 'image', test: /^(png|jpe?g|gif|svg|webp|bmp|ico|tiff?|avif|heic)$/ },
        { type: 'video', test: /^(mp4|mov|avi|mkv|flv|webm|wmv|m4v|3gp|ts|mpg|mpeg|rmvb|vob)$/ },
        { type: 'audio', test: /^(mp3|wav|flac|aac|ogg|m4a|wma|ape|opus|aiff|mid|midi)$/ },
        { type: 'pdf', test: /^pdf$/ },
        { type: 'doc', test: /^(doc|docx|rtf|odt|wps)$/ },
        { type: 'sheet', test: /^(xls|xlsx|csv|ods|numbers|et)$/ },
        { type: 'slide', test: /^(ppt|pptx|pps|ppsx|odp|key|dps)$/ },
        { type: 'archive', test: /^(zip|rar|7z|tar|gz|bz2|xz|tgz|z|iso|cab|jar|war|ear)$/ },
        { type: 'code', test: /^(html?|css|scss|less|jsx?|tsx?|vue|svelte|py|pyw|java|class|c|h|cc|cpp|cxx|hpp|cs|go|rs|rb|php|swift|kt|kts|scala|sh|bash|zsh|bat|cmd|ps1|json|ya?ml|xml|toml|ini|conf|sql|db|lua|pl|r|dart|ex|exs|erl|hs|ml|clj|tsv)$/ },
        { type: 'exec', test: /^(exe|msi|app|dmg|apk|xapk|ipa|pkg|deb|rpm)$/ },
        { type: 'text', test: /^(txt|log|md|markdown|nfo|readme|cue|srt|ass|sub|lrc)$/ },
        { type: 'font', test: /^(ttf|otf|woff2?|eot)$/ },
        { type: 'cert', test: /^(crt|pem|cer|key|p12|pfx|der)$/ },
        { type: 'book', test: /^(epub|mobi|azw3|fb2|djvu)$/ }
    ];

    function getExt(name) {
        if (!name) return '';
        var idx = name.lastIndexOf('.');
        if (idx < 0 || idx >= name.length - 1) return '';
        return name.substring(idx + 1).toLowerCase();
    }

    // 返回图标类型 key：folder | image | video | ... | file
    function classify(name, isDir) {
        if (isDir) return 'folder';
        var ext = getExt(name);
        if (!ext) return 'file';
        for (var i = 0; i < RULES.length; i++) {
            if (RULES[i].test.test(ext)) return RULES[i].type;
        }
        return 'file';
    }

    // 返回 SVG 字符串
    function getIcon(name, isDir) {
        return FI[classify(name, isDir)] || FI.file;
    }

    // 返回 CSS class 后缀（不含 "fi-" 前缀），用于自定义样式覆盖
    function iconClass(name, isDir) {
        return 'fi-' + classify(name, isDir);
    }

    window.FileIcons = {
        classify: classify,
        getIcon: getIcon,
        iconClass: iconClass,
        getExt: getExt,
        _svg: FI
    };
})();

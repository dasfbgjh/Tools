(function () {
    'use strict';

    var Api = window.Api;

    var PHONE_W_TO_H = 9 / 16;
    var BORDER = 3;
    var FRAME_PAD = 12;

    var state = {
        games: [],
        baseUrl: '',
        activeGame: null,
        mode: localStorage.getItem('game-mode') || 'pc',
        zoom: parseInt(localStorage.getItem('game-zoom'), 10) || 100,
        sidebarCollapsed: localStorage.getItem('game-sidebar-collapsed') === '1',
        searchQuery: ''
    };

    function $(sel) { return document.querySelector(sel); }

    function renderSidebar() {
        var list = $('#game-list');
        if (!list) return;
        list.innerHTML = '';
        var q = state.searchQuery.toLowerCase();
        var idx = 0;
        state.games.forEach(function (g) {
            if (q && g.name.toLowerCase().indexOf(q) === -1 && g.id.toLowerCase().indexOf(q) === -1) return;
            idx++;
            var li = document.createElement('li');
            li.className = 'game-list-item' + (state.activeGame && state.activeGame.id === g.id ? ' active' : '');
            li.innerHTML = '<span class="game-list-idx">' + idx + '.' + '</span><span class="game-list-name">' + g.name + '</span>';
            li.addEventListener('click', function () { selectGame(g); });
            list.appendChild(li);
        });
    }

    function selectGame(game) {
        state.activeGame = game;
        renderSidebar();
        renderGameArea();
    }

    function renderModeButtons() {
        var pcBtn = $('#mode-pc');
        var mobileBtn = $('#mode-mobile');
        if (pcBtn) pcBtn.classList.toggle('active', state.mode === 'pc');
        if (mobileBtn) mobileBtn.classList.toggle('active', state.mode === 'mobile');
        renderZoomLabel();
    }

    function renderZoomLabel() {
        var label = $('#game-zoom-label');
        if (label) label.textContent = state.zoom + '%';
        var slider = $('#game-zoom-slider');
        if (slider) slider.value = state.zoom;
    }

    function setZoom(val) {
        state.zoom = val;
        try { localStorage.setItem('game-zoom', String(val)); } catch (e) { }
        renderZoomLabel();
        renderGameArea();
    }

    function buildGameUrl(game) {
        if (!state.baseUrl) return '';
        return state.baseUrl + game.id + '/' + game.entryFile;
    }

    function renderGameArea() {
        var area = $('#game-area');
        if (!area) return;

        if (!state.activeGame) {
            area.innerHTML = '<div class="game-empty"><div class="game-empty-icon">🎮</div><div class="game-empty-text">从左侧列表选择一个游戏开始</div></div>';
            area.className = 'game-area pc-mode';
            return;
        }

        var url = buildGameUrl(state.activeGame);
        if (!url) {
            area.innerHTML = '<div class="game-empty"><div class="game-empty-icon">⚠️</div><div class="game-empty-text">游戏服务未启动</div></div>';
            area.className = 'game-area pc-mode';
            return;
        }

        area.className = 'game-area ' + state.mode + '-mode';

        var scale = state.zoom / 100;

        if (state.mode === 'pc') {
            area.innerHTML = '<div class="game-iframe-wrap" style="transform:scale(' + scale + ')"><iframe src="' + url + '" allowfullscreen allow="autoplay; fullscreen"></iframe></div>';
            return;
        }

        var availW = area.clientWidth - FRAME_PAD * 2;
        var availH = area.clientHeight - FRAME_PAD * 2;

        var frameHFromW = availW / PHONE_W_TO_H + BORDER * 2;
        var frameWFromH = (availH - BORDER * 2) * PHONE_W_TO_H + BORDER * 2;

        var frameW, frameH;
        if (frameHFromW <= availH) {
            frameW = availW + BORDER * 2;
            frameH = frameHFromW;
        } else {
            frameH = availH + BORDER * 2;
            frameW = frameWFromH;
        }
        frameW = Math.floor(frameW);
        frameH = Math.floor(frameH);

        var screenW = frameW - BORDER * 2;
        var screenH = frameH - BORDER * 2;

        area.innerHTML =
            '<div class="phone-frame" style="width:' + frameW + 'px;height:' + frameH + 'px;transform:scale(' + scale + ')">' +
            '<iframe src="' + url + '" style="width:' + screenW + 'px;height:' + screenH + 'px" allowfullscreen allow="autoplay; fullscreen"></iframe>' +
            '</div>';
    }

    function setMode(mode) {
        state.mode = mode;
        try { localStorage.setItem('game-mode', mode); } catch (e) { }
        renderModeButtons();
        renderGameArea();
    }

    function toggleSidebar() {
        state.sidebarCollapsed = !state.sidebarCollapsed;
        try { localStorage.setItem('game-sidebar-collapsed', state.sidebarCollapsed ? '1' : '0'); } catch (e) { }
        var sidebar = $('#game-sidebar');
        if (sidebar) sidebar.classList.toggle('collapsed', state.sidebarCollapsed);
    }

    function init() {
        var sidebar = $('#game-sidebar');
        if (sidebar && state.sidebarCollapsed) sidebar.classList.add('collapsed');

        var collapseBtn = $('#sidebar-collapse');
        if (collapseBtn) collapseBtn.addEventListener('click', toggleSidebar);

        var expandBtn = $('#sidebar-expand');
        if (expandBtn) expandBtn.addEventListener('click', toggleSidebar);

        var pcBtn = $('#mode-pc');
        var mobileBtn = $('#mode-mobile');
        if (pcBtn) pcBtn.addEventListener('click', function () { setMode('pc'); });
        if (mobileBtn) mobileBtn.addEventListener('click', function () { setMode('mobile'); });

        var searchInput = $('#game-search-input');
        if (searchInput) {
            searchInput.addEventListener('input', function () {
                state.searchQuery = this.value.trim();
                renderSidebar();
            });
        }

        var zoomSlider = $('#game-zoom-slider');
        if (zoomSlider) {
            zoomSlider.addEventListener('input', function () {
                setZoom(parseInt(this.value, 10) || 100);
            });
        }

        renderModeButtons();

        Api.localTools.game.start().then(function (r) {
            if (r && r.success) {
                state.baseUrl = r.baseUrl || '';
            }
            return Api.localTools.game.list();
        }).then(function (r) {
            if (r && r.success && r.games) {
                state.games = r.games;
            }
            renderSidebar();
            renderGameArea();
        }).catch(function () {
            renderSidebar();
            renderGameArea();
        });

        var resizeTimer;
        window.addEventListener('resize', function () {
            clearTimeout(resizeTimer);
            resizeTimer = setTimeout(function () {
                if (state.activeGame && state.mode === 'mobile') renderGameArea();
            }, 200);
        });
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
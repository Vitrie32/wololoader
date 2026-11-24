/**
 * Scriptorium: the mod editor's navigation and sections.
 *
 * The editor shows one mod. Its left nav lists every kind of content the mod can hold, with a
 * count, and the main column shows the section you pick:
 *   Civilizations  new civs and changes to the game's civs (basic.js draws them)
 *   Scripts        scripts/main.lua (runs at start), modules scripts/**.lua (require), and visual
 *                  scripts (scripts/graphs, graph.js, compiled into scripts/visual.lua)
 *   Sprites        resources/_common/drs/graphics|interface/*.slp (slpviewer.js)
 *   Taunts         taunts/NN name.mp3|wav (chat numbers; NN >= 43 are new taunts)
 *   Files          every file of the mod folder: download, replace, delete, add
 *
 * The file list comes from GET /api/mod-tree (one call per mod load and per change). Files are read
 * and written through /api/file. Unsaved code edits are registered with the save hooks of app.js
 * (Scriptorium.onSave), so Save and Ctrl+S write them with the visual scripts and mod.json.
 * Vanilla ES2020, no dependencies.
 */
(function () {
  'use strict';

  const S = window.Scriptorium;
  if (!S) return;

  // ==========================================================================
  // State and helpers
  // ==========================================================================
  const st = {
    section: 'civs',
    tree: null,          // [{ path, size }] of the open mod, null until loaded
    treeFolder: null,
    code: null,          // path of the open code editor, or null
    codes: new Map(),    // path -> { path, text, saved, dirty }: Lua buffers (kept while unsaved)
    urls: new Map(),     // path -> object URL of a sound (for playing)
    nowPlaying: null,    // { path, url } of the last sound played
    filter: '',          // Files section filter
    mainText: ''         // text of scripts/main.lua as last read (see requiredByMain)
  };

  const $ = (id) => document.getElementById(id);
  const root = () => $('basic-root');
  const CHEV_SVG = '<svg class="sc-chev" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 3 11 8 6 13"/></svg>';
  const wireDisclosures = (el) => { if (S.wireDisclosures) S.wireDisclosures(el); };

  function esc(str) {
    if (str === null || str === undefined) return '';
    return String(str).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;').replace(/'/g, '&#039;');
  }

  function folder() { return S.state.currentModData ? S.state.currentModData.folder : ''; }
  function civsOf() { return (S.state.currentModData && S.state.currentModData.civs) || {}; }

  function fileUrl(path) {
    return `/api/file?folder=${encodeURIComponent(folder())}&path=${encodeURIComponent(path)}`;
  }
  async function readText(path) { return (await S.api(fileUrl(path))).text(); }
  async function readBlob(path) { return (await S.api(fileUrl(path))).blob(); }
  async function writeFile(path, body) { await S.api(fileUrl(path), { method: 'PUT', body }); }
  async function removeFile(path) { await S.api(fileUrl(path), { method: 'DELETE' }); }

  function basename(path) { return path.slice(path.lastIndexOf('/') + 1); }
  function dirname(path) { const i = path.lastIndexOf('/'); return i < 0 ? '' : path.slice(0, i); }

  function formatSize(n) {
    if (n === null || n === undefined) return '';
    if (n < 1024) return n + ' B';
    if (n < 1024 * 1024) return (n / 1024).toFixed(n < 10240 ? 1 : 0) + ' KB';
    return (n / 1048576).toFixed(1) + ' MB';
  }

  // Ids of the game's units (for the game icons in the nav and on cards)
  function unitIcon(id, size) {
    const units = (S.state.catalog && S.state.catalog.units) || [];
    const unit = units.find(u => u.id === id);
    return window.GameIcons ? GameIcons.unitMarkup(unit, size) : '';
  }

  // Tree queries. Paths use "/" and are relative to the mod folder.
  const treeList = () => st.tree || [];
  const luaFiles = () => treeList().filter(f => /^scripts\/.+\.lua$/i.test(f.path));
  const graphFiles = () => treeList().filter(f => /^scripts\/graphs\/[^/]+\.json$/i.test(f.path));
  const spriteFiles = () => treeList().filter(f => /^resources\/_common\/drs\/(graphics|interface)\/[^/]+\.slp$/i.test(f.path));
  const tauntFiles = () => treeList().filter(f => /^taunts\/[^/]+\.(mp3|wav)$/i.test(f.path));

  // A taunt is "NN name.ext": the number is what the player types in chat.
  function parseTaunt(path, size) {
    const base = basename(path);
    const m = /^(\d+) (.+)\.(mp3|wav)$/i.exec(base);
    return {
      path, size,
      num: m ? Number(m[1]) : null,
      name: m ? m[2] : base.replace(/\.[^.]+$/, ''),
      ext: base.slice(base.lastIndexOf('.') + 1).toLowerCase()
    };
  }
  const TAUNT_FIRST_NEW = 43;     // taunts 1..42 are the game's; 43 and up are new
  const TAUNT_LAST = 256;         // the chat accepts numbers up to 256
  const NAME_RULE = /^[A-Za-z0-9_\-][A-Za-z0-9_\- ]{0,58}$/;   // ASCII: the file name goes through the ANSI API

  function slugTitle(slug) {
    const t = slug.replace(/[-_]+/g, ' ').trim();
    return t ? t.charAt(0).toUpperCase() + t.slice(1) : slug;
  }

  // Visual scripts: the saved ones of the mod plus the ones open or unsaved in this session.
  function visualList() {
    const map = new Map();
    for (const f of graphFiles()) {
      const slug = basename(f.path).replace(/\.json$/i, '');
      map.set(slug, { slug, file: basename(f.path), path: f.path, name: slugTitle(slug), dirty: false, isNew: false });
    }
    const bw = window.BasicWorkspace;
    if (bw) {
      for (const g of bw.visualScripts()) {
        const cur = map.get(g.slug) || { slug: g.slug, file: g.file, path: 'scripts/graphs/' + g.file, name: slugTitle(g.slug), dirty: false, isNew: true };
        if (g.name) cur.name = g.name;
        cur.dirty = !!g.dirty;
        map.set(g.slug, cur);
      }
    }
    return [...map.values()].sort((a, b) => a.name.localeCompare(b.name));
  }

  // ==========================================================================
  // Loading the mod folder
  // ==========================================================================
  async function loadTree() {
    const data = S.state.currentModData;
    if (!data) { st.tree = null; return; }
    const want = data.folder;
    try {
      const list = await S.api(`/api/mod-tree?folder=${encodeURIComponent(want)}`);
      if (S.state.currentModData && S.state.currentModData.folder === want) {
        st.tree = Array.isArray(list) ? list : [];
        st.treeFolder = want;
        st.mainText = '';
        if (st.tree.some(f => f.path === MAIN_LUA)) {
          try { st.mainText = await readText(MAIN_LUA); } catch (e) { st.mainText = ''; }
        }
      }
    } catch (e) {
      st.tree = st.tree || [];
      S.showToast('Could not read the mod folder: ' + e.message, 'error');
    }
  }

  function countOf(id) {
    const sec = SECTIONS.find(s => s.id === id);
    return sec ? sec.count() : 0;
  }

  function defaultSection() {
    for (const id of ['civs', 'scripts', 'sprites', 'taunts']) if (countOf(id) > 0) return id;
    return 'civs';
  }

  // ==========================================================================
  // Navigation
  // ==========================================================================
  const NAV_ICONS = {
    civs: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M3 9l9-7 9 7v11a2 2 0 01-2 2H5a2 2 0 01-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>',
    scripts: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><polyline points="8 7 3 12 8 17"/><polyline points="16 7 21 12 16 17"/><line x1="14" y1="4" x2="10" y2="20"/></svg>',
    sprites: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="18" height="18" rx="3"/><circle cx="9" cy="9" r="2"/><path d="M21 15l-5-5-9 9"/></svg>',
    taunts: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M11 5L6 9H3v6h3l5 4z"/><path d="M15.5 8.5a5 5 0 010 7"/><path d="M18.5 5.5a9 9 0 010 13"/></svg>',
    files: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M3 7a2 2 0 012-2h4l2 2h8a2 2 0 012 2v8a2 2 0 01-2 2H5a2 2 0 01-2-2z"/></svg>'
  };

  // Civilizations uses the Monk's game icon (real game art) when the catalog has the unit.
  function navIcon(id) {
    if (id === 'civs') {
      const units = (S.state.catalog && S.state.catalog.units) || [];
      if (units.some(u => u.id === 125 && u.icon >= 0)) return unitIcon(125, 26);
      return NAV_ICONS.civs;
    }
    return NAV_ICONS[id] || '';
  }

  function renderNav() {
    const nav = $('mod-nav');
    if (!nav) return;
    if (!S.state.currentModData) { nav.innerHTML = ''; return; }
    const filesCount = treeList().length;
    nav.innerHTML = `
      <div class="nav-title">Contents</div>
      ${SECTIONS.map(sec => {
        const n = sec.count();
        return `<button class="nav-item${sec.id === st.section ? ' active' : ''}" data-section="${sec.id}" title="${esc(sec.label)}">
          <span class="nav-icon">${navIcon(sec.id)}</span>
          <span class="nav-label">${esc(sec.label)}</span>
          <span class="nav-count${n ? '' : ' zero'}">${n}</span>
        </button>`;
      }).join('')}
      <div class="nav-foot">
        <span class="nav-foot-text">${filesCount} file${filesCount === 1 ? '' : 's'} in the mod folder</span>
      </div>`;
    nav.querySelectorAll('[data-section]').forEach(btn => {
      btn.addEventListener('click', () => show(btn.dataset.section));
    });
    // the nav sits in the workspace: its game icons are filled in by basic.js's observer
  }

  // ==========================================================================
  // Page frame: header with the section's title and actions, then the body
  // ==========================================================================
  function showLoading() {
    const r = root();
    if (r) r.innerHTML = '<div class="basic-editor-loading">Loading...</div>';
  }

  function renderPage() {
    const r = root();
    if (!r || !S.state.currentModData) return;
    if (st.code || (window.BasicWorkspace && BasicWorkspace.editorOpen())) return;
    const sec = SECTIONS.find(s => s.id === st.section) || SECTIONS[0];
    if (sec.id === 'sprites' && spriteFiles().length > 0) {
      // the sprite viewer has its own panes and lists; the nav stays
      if (S.state.pane !== 'sprites') S.showPane('sprites');
      return;
    }
    S.showPane('main');
    r.innerHTML = `
      <div class="sec-page">
        <header class="sec-head">
          <div class="sec-head-text">
            <h2 class="sec-title">${esc(sec.label)}</h2>
            <p class="sec-sub">${esc(sec.sub)}</p>
          </div>
          <div class="sec-actions">${sec.actions()}</div>
        </header>
        <div class="sec-body" data-role="body"></div>
      </div>`;
    const body = r.querySelector('[data-role="body"]');
    r.onclick = onAction;
    sec.render(body);
    wireDisclosures(body);
  }

  // One click handler for the whole page: any element with data-act runs its action.
  function onAction(e) {
    const el = e.target.closest('[data-act]');
    if (!el || !root().contains(el)) return;
    const fn = ACTIONS[el.dataset.act];
    if (!fn) return;
    e.preventDefault();
    e.stopPropagation();
    fn(el);
  }

  // Redraws the nav and, unless an editor is open, the section page.
  function refresh() {
    if (!S.state.currentModData) return;
    renderNav();
    if (!st.code && !(window.BasicWorkspace && BasicWorkspace.editorOpen())) renderPage();
  }

  async function show(id) {
    if (!S.state.currentModData) return;
    if (window.BasicWorkspace) BasicWorkspace.leaveEditor();
    st.section = id;
    st.code = null;
    renderNav();
    renderPage();
  }

  // ==========================================================================
  // Dialogs (one modal at a time, in #modal-container)
  // ==========================================================================
  // buttons: [{ label, primary, onClick(dialog, close) }]; onClick returns false to keep the dialog open.
  function openDialog(opts) {
    const modal = $('modal-container');
    if (!modal) return () => {};
    modal.innerHTML = `
      <div class="modal-dialog dialog-${opts.width || 'md'}" role="dialog" aria-modal="true" aria-label="${esc(opts.title)}">
        <div class="modal-header">
          <h3 class="modal-title">${esc(opts.title)}</h3>
          <button class="btn-modal-close" aria-label="Close">&times;</button>
        </div>
        <div class="modal-body">${opts.body}</div>
        <div class="dialog-foot">
          ${(opts.buttons || []).map((b, i) => `<button class="btn btn-sm ${b.primary ? 'btn-save' : 'btn-wood'}" data-i="${i}">${esc(b.label)}</button>`).join('')}
        </div>
      </div>`;
    modal.style.display = 'flex';
    const dialog = modal.querySelector('.modal-dialog');
    const close = () => { modal.style.display = 'none'; modal.innerHTML = ''; };
    dialog.querySelector('.btn-modal-close').onclick = close;
    dialog.querySelectorAll('[data-i]').forEach(btn => {
      btn.onclick = async () => {
        const b = opts.buttons[Number(btn.dataset.i)];
        const r = b.onClick ? await b.onClick(dialog, close) : undefined;
        if (r !== false) close();
      };
    });
    if (opts.onOpen) opts.onOpen(dialog, close);
    return close;
  }

  function dialogError(dialog, msg) {
    const el = dialog.querySelector('.form-error');
    if (el) el.textContent = msg || '';
  }

  // ==========================================================================
  // Section: Scripts
  // ==========================================================================
  const MAIN_LUA = 'scripts/main.lua';
  const VISUAL_LUA = 'scripts/visual.lua';     // generated from the visual scripts (basic.js rebuildMainLua)
  // A module name is what require() takes (the DLL checks the same rule): ai.economy = scripts/ai/economy.lua
  const MODULE_NAME_RULE = /^[A-Za-z0-9_]+(\.[A-Za-z0-9_]+)*$/;

  function moduleName(path) { return path.replace(/^scripts\//i, '').replace(/\.lua$/i, '').replace(/\//g, '.'); }
  function modulePath(name) { return 'scripts/' + name.split('.').join('/') + '.lua'; }
  function scriptKind(path) { return path === MAIN_LUA ? 'main' : path === VISUAL_LUA ? 'visual' : 'module'; }

  // '' when the name is usable, else the reason (same rules as require in the game)
  function checkModuleName(name) {
    if (!name) return 'Type a name.';
    if (name.length > 64) return 'The name is longer than 64 characters.';
    if (!MODULE_NAME_RULE.test(name)) return 'Use letters, digits and "_", with a single "." between parts (no spaces, "/", "\\" or "..").';
    if (modulePath(name) === MAIN_LUA) return 'main is main.lua itself: use Create main.lua.';
    if (modulePath(name) === VISUAL_LUA) return 'visual is the file that Scriptorium writes from the visual scripts.';
    return '';
  }

  // main.lua as the editor sees it: the open buffer when there is one, else the file as last read.
  function mainSource() {
    const buf = st.codes.get(MAIN_LUA);
    return buf ? buf.text : st.mainText;
  }
  function requiredByMain(name) {
    return !!(window.ScGraph && ScGraph.requiresModule(mainSource(), name));
  }

  function moduleTemplate(name) {
    return `-- ${modulePath(name)}: a module of the mod. main.lua loads it with require("${name}").
-- It runs the first time it is required; the value it returns is kept and shared.
local M = {}

function M.hello()
    UI.Message("Hello from ${name}")
end

return M
`;
  }
  const MAIN_TEMPLATE = `-- scripts/main.lua: runs once when the game starts (MODDING.md, section 14).
-- Game.OnStart(fn) runs fn at the start of every game. More events: Game.OnSecond, Game.OnUnitCreated, ...

Game.OnStart(function()
    UI.Message("Hello from my mod!")
end)
`;

  function renderScripts(body) {
    const lua = luaFiles();
    const hasMain = lua.some(f => f.path === MAIN_LUA);
    const visual = visualList();

    const luaRows = lua.map(f => {
      const buf = st.codes.get(f.path);
      const kind = scriptKind(f.path);
      const dirty = buf && buf.dirty;
      let tags;
      if (kind === 'main') tags = '<span class="pill ok">Runs at start</span>';
      else if (kind === 'visual') tags = '<span class="pill muted" title="Written by Scriptorium from the visual scripts">Generated</span>';
      else {
        const used = requiredByMain(moduleName(f.path));
        tags = '<span class="pill muted" title="Loaded by main.lua with require">Module (load with require)</span>'
          + (used ? '<span class="pill ok">main.lua requires it</span>' : '<span class="pill warn" title="main.lua has no require of this module">Not required</span>');
      }
      const sub = kind === 'module' ? esc(moduleName(f.path)) + ' &middot; ' + esc(f.path) : esc(f.path);
      return `<button class="list-row" data-act="open-code" data-path="${esc(f.path)}">
        <span class="list-glyph lua">lua</span>
        <span class="list-main">
          <span class="list-title">${esc(basename(f.path))}</span>
          <span class="list-sub mono">${sub} &middot; ${esc(formatSize(f.size))}</span>
        </span>
        <span class="list-tags">
          ${tags}
          ${dirty ? '<span class="pill accent">Unsaved</span>' : ''}
        </span>
        <span class="list-chev" aria-hidden="true">&rsaquo;</span>
      </button>`;
    }).join('');

    const visualRows = visual.map(v => `
      <button class="list-row" data-act="open-script" data-path="${esc(v.file)}">
        <span class="list-glyph graph">node</span>
        <span class="list-main">
          <span class="list-title">${esc(v.name)}</span>
          <span class="list-sub mono">scripts/graphs/${esc(v.file)}</span>
        </span>
        <span class="list-tags">
          ${v.dirty ? '<span class="pill accent">Unsaved</span>' : ''}
          ${v.isNew ? '<span class="pill muted">New</span>' : ''}
        </span>
        <span class="list-chev" aria-hidden="true">&rsaquo;</span>
      </button>`).join('');

    body.innerHTML = `
      <details class="sc-section" data-pref-key="sec-visual-scripts" open>
        <summary class="sc-section-summary">
          ${CHEV_SVG}
          <span class="sc-section-title">Visual scripts</span>
          <span class="sc-section-meta">${visual.length} script${visual.length === 1 ? '' : 's'}</span>
        </summary>
        <div class="sc-section-body">
          <p class="basic-form-hint">No-code event graphs compiled into <span class="mono">scripts/visual.lua</span> on save.</p>
          ${visual.length ? `<div class="list">${visualRows}</div>` : `
            <div class="empty-card compact">
              <h3 class="empty-card-title">No visual scripts yet</h3>
              <p class="empty-card-text">Build game events with nodes — for example, give gold when a unit is created.</p>
              <div class="empty-card-actions"><button class="btn btn-save btn-sm" data-act="visual-new">New visual script</button></div>
            </div>`}
        </div>
      </details>

      <details class="sc-section" data-pref-key="sec-lua-scripts">
        <summary class="sc-section-summary">
          ${CHEV_SVG}
          <span class="sc-section-title">Lua scripts</span>
          <span class="sc-section-meta">${lua.length} file${lua.length === 1 ? '' : 's'}</span>
        </summary>
        <div class="sc-section-body">
          <p class="basic-form-hint"><span class="mono">scripts/main.lua</span> runs at game start and loads modules via <span class="mono">require("name")</span>.</p>
          ${lua.length ? `<div class="list">${luaRows}</div>` : ''}
          ${hasMain ? '' : `
            <div class="inline-empty">
              <div>
                <strong>No main.lua yet</strong>
                <span>Create it to write custom Lua events.</span>
              </div>
              <button class="btn btn-wood btn-sm" data-act="lua-create-main">Create main.lua</button>
            </div>`}
        </div>
      </details>`;
  }

  // ==========================================================================
  // Code editor (Lua): textarea over a highlighted copy, line numbers, Tab, Enter keeps indent
  // ==========================================================================
  const LUA_KEYWORDS = new Set(['and', 'break', 'do', 'else', 'elseif', 'end', 'false', 'for', 'function', 'goto',
    'if', 'in', 'local', 'nil', 'not', 'or', 'repeat', 'return', 'then', 'true', 'until', 'while']);
  const LUA_API = new Set(['Game', 'UI', 'Time', 'Sound', 'wl', 'MOD', 'print', 'pairs', 'ipairs', 'math', 'string',
    'table', 'tostring', 'tonumber', 'type', 'select', 'error', 'pcall', 'next', 'setmetatable', 'getmetatable', 'assert']);
  const LUA_TOKEN = /(--\[(=*)\[[\s\S]*?\]\2\])|(--[^\n]*)|(\[(=*)\[[\s\S]*?\]\5\])|("(?:\\[\s\S]|[^"\\\n])*"?)|('(?:\\[\s\S]|[^'\\\n])*'?)|(\b0[xX][0-9a-fA-F]+\b|\b\d+(?:\.\d*)?(?:[eE][+-]?\d+)?|\.\d+)|([A-Za-z_]\w*)/g;

  // HTML of the source with spans for comments, strings, numbers, keywords and the game's API.
  function highlightLua(src) {
    let out = '';
    let last = 0;
    LUA_TOKEN.lastIndex = 0;
    let m;
    while ((m = LUA_TOKEN.exec(src))) {
      out += esc(src.slice(last, m.index));
      const t = m[0];
      let cls = null;
      if (m[1] || m[3]) cls = 'tk-c';
      else if (m[4] || m[6] || m[7]) cls = 'tk-s';
      else if (m[8]) cls = 'tk-n';
      else if (m[9]) cls = LUA_KEYWORDS.has(t) ? 'tk-k' : (LUA_API.has(t) ? 'tk-b' : null);
      out += cls ? `<span class="${cls}">${esc(t)}</span>` : esc(t);
      last = LUA_TOKEN.lastIndex;
    }
    out += esc(src.slice(last));
    return out + '\n';   // a trailing line keeps the highlighted copy as tall as the textarea
  }

  function openCode(path) {
    if (!S.state.currentModData) return;
    if (window.BasicWorkspace) BasicWorkspace.leaveEditor();
    if (/^scripts\//i.test(path)) st.section = 'scripts';
    const r = root();
    r.innerHTML = '<div class="basic-editor-loading">Opening ' + esc(basename(path)) + '...</div>';
    (async () => {
      let buf = st.codes.get(path);
      if (!buf || !buf.dirty) {
        try {
          const text = await readText(path);
          buf = { path, text, saved: text, dirty: false };
          st.codes.set(path, buf);
        } catch (e) {
          S.showToast('Could not open ' + path + ': ' + e.message, 'error');
          show(st.section);
          return;
        }
      }
      st.code = path;
      renderNav();
      renderCodeEditor(buf);
    })();
  }

  function renderCodeEditor(buf) {
    const r = root();
    const kind = scriptKind(buf.path);
    const isMain = kind === 'main';
    const name = kind === 'module' ? moduleName(buf.path) : '';
    const subText = kind === 'main' ? ' &middot; runs once when the game starts'
      : kind === 'visual' ? ' &middot; generated by Scriptorium: do not edit'
      : ` &middot; module: main.lua loads it with require("${esc(name)}")`;
    const moduleAction = kind !== 'module' ? '' : requiredByMain(name)
      ? '<span class="pill ok">main.lua requires it</span>'
      : `<button class="btn btn-wood btn-sm" data-act="lua-add-require" data-name="${esc(name)}">Add require to main.lua</button>`;
    r.innerHTML = `
      <div class="code-page">
        <header class="sec-head code-head">
          <button class="btn btn-wood btn-sm" data-act="code-back">&lsaquo; Scripts</button>
          <div class="sec-head-text">
            <h2 class="sec-title code-title">${esc(basename(buf.path))}</h2>
            <p class="sec-sub mono">${esc(buf.path)}${subText}</p>
          </div>
          <div class="sec-actions">
            ${moduleAction}
            <span class="code-state" data-role="state"></span>
            <button class="btn btn-save btn-sm" data-act="code-save">Save <kbd class="kbd-hint">Ctrl+S</kbd></button>
          </div>
        </header>
        <div class="code-area">
          <div class="code-editor">
            <div class="code-gutter" data-role="gutter" aria-hidden="true"></div>
            <div class="code-surface">
              <pre class="code-hl" aria-hidden="true"><code data-role="hl"></code></pre>
              <textarea class="code-input" data-role="input" spellcheck="false" autocapitalize="off"
                autocomplete="off" autocorrect="off" wrap="off" aria-label="Lua source of ${esc(basename(buf.path))}"></textarea>
            </div>
          </div>
        </div>
        <footer class="code-foot">
          <span data-role="pos">Ln 1, Col 1</span>
          <span>${isMain ? 'Loads the visual scripts with require("visual"). ' : kind === 'visual' ? 'Overwritten on every save of the visual scripts. ' : ''}Tab indents 4 spaces &middot; Ctrl+S saves</span>
        </footer>
      </div>`;

    const input = r.querySelector('[data-role="input"]');
    const hl = r.querySelector('[data-role="hl"]');
    const gutter = r.querySelector('[data-role="gutter"]');
    const pos = r.querySelector('[data-role="pos"]');
    const state = r.querySelector('[data-role="state"]');
    input.value = buf.text;

    const paint = () => {
      const text = input.value;
      hl.innerHTML = highlightLua(text);
      const lines = text.split('\n').length;
      let nums = '';
      for (let i = 1; i <= lines; i++) nums += i + '\n';
      gutter.textContent = nums;
      gutter.style.width = (String(lines).length * 8 + 26) + 'px';
    };
    const place = () => {
      const before = input.value.slice(0, input.selectionStart);
      const line = before.split('\n').length;
      const col = before.length - before.lastIndexOf('\n');
      pos.textContent = `Ln ${line}, Col ${col}`;
    };
    const dirtyUi = () => {
      state.textContent = buf.dirty ? 'Unsaved' : 'Saved';
      state.className = 'code-state' + (buf.dirty ? ' dirty' : '');
    };
    const pre = r.querySelector('.code-hl');
    const sync = () => {
      pre.scrollTop = input.scrollTop;
      pre.scrollLeft = input.scrollLeft;
      gutter.scrollTop = input.scrollTop;
    };

    input.addEventListener('input', () => {
      buf.text = input.value;
      buf.dirty = buf.text !== buf.saved;
      S.setExtraDirty('code:' + buf.path, buf.dirty);
      paint(); dirtyUi(); place();
    });
    input.addEventListener('scroll', sync);
    input.addEventListener('keyup', place);
    input.addEventListener('click', place);
    input.addEventListener('select', place);
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Tab' && !e.ctrlKey && !e.metaKey && !e.altKey && e.shiftKey) {
        // outdent: up to four spaces before the caret's line start
        e.preventDefault();
        const before = input.value.slice(0, input.selectionStart);
        const lineStart = before.lastIndexOf('\n') + 1;
        const spaces = (/^ {1,4}/.exec(input.value.slice(lineStart)) || [''])[0];
        if (spaces) {
          input.setSelectionRange(lineStart, lineStart + spaces.length);
          insertText(input, '');
          input.dispatchEvent(new Event('input'));
        }
      } else if (e.key === 'Tab' && !e.ctrlKey && !e.metaKey && !e.altKey) {
        e.preventDefault();
        insertText(input, '    ');
        input.dispatchEvent(new Event('input'));
      } else if (e.key === 'Enter' && !e.ctrlKey && !e.metaKey) {
        // keep the indent of the line above
        const before = input.value.slice(0, input.selectionStart);
        const lineStart = before.lastIndexOf('\n') + 1;
        const indent = (/^[ \t]*/.exec(before.slice(lineStart)) || [''])[0];
        e.preventDefault();
        insertText(input, '\n' + indent);
        input.dispatchEvent(new Event('input'));
      }
    });

    input.setSelectionRange(0, 0);
    input.scrollTop = 0;
    input.scrollLeft = 0;
    paint(); dirtyUi(); place(); sync();
    input.focus({ preventScroll: true });
  }

  function insertText(el, text) {
    const start = el.selectionStart, end = el.selectionEnd;
    el.value = el.value.slice(0, start) + text + el.value.slice(end);
    el.selectionStart = el.selectionEnd = start + text.length;
  }

  // Dialog: a new module (scripts/<name>.lua) with a template that returns a table.
  function newScriptDialog() {
    openDialog({
      title: 'New script',
      width: 'md',
      body: `
        <div class="form-field">
          <label for="lua-new-name">Module name</label>
          <input id="lua-new-name" type="text" class="form-input mono" placeholder="economy or ai.economy" autocomplete="off" spellcheck="false">
        </div>
        <p class="form-hint" data-role="preview"></p>
        <p class="form-hint">A module is loaded by main.lua with <span class="mono">require("name")</span>. Dots make folders:
          <span class="mono">ai.economy</span> is <span class="mono">scripts/ai/economy.lua</span>.</p>
        <p class="form-error" role="alert"></p>`,
      buttons: [
        { label: 'Cancel' },
        {
          label: 'Create script', primary: true,
          onClick: async (dialog) => {
            const name = dialog.querySelector('#lua-new-name').value.trim();
            const why = checkModuleName(name);
            if (why) return dialogError(dialog, why), false;
            const path = modulePath(name);
            if (treeList().some(f => f.path === path)) return dialogError(dialog, path + ' already exists.'), false;
            try {
              await writeFile(path, moduleTemplate(name));
            } catch (e) {
              return dialogError(dialog, e.message), false;
            }
            S.showToast(`Created ${path}`, 'success');
            await loadTree();
            st.section = 'scripts';
            openCode(path);
            return true;
          }
        }
      ],
      onOpen: (dialog) => {
        const input = dialog.querySelector('#lua-new-name');
        const preview = () => {
          const name = input.value.trim();
          const why = name ? checkModuleName(name) : '';
          dialog.querySelector('[data-role="preview"]').textContent = why || (name ? 'Saved as ' + modulePath(name) : '');
        };
        input.addEventListener('input', preview);
        input.focus();
      }
    });
  }

  // Adds require("name") to main.lua. An open main.lua with unsaved edits gets the line in its
  // buffer only (Save writes it), so unsaved work is never written by accident.
  async function addRequireToMain(name) {
    if (!luaFiles().some(f => f.path === MAIN_LUA)) {
      S.showToast('There is no main.lua yet: create it first.', 'error');
      return;
    }
    const buf = st.codes.get(MAIN_LUA);
    const current = buf ? buf.text : await readText(MAIN_LUA);
    const next = window.ScGraph.addRequire(current, name);
    if (next === current) {
      S.showToast(`main.lua already requires "${name}".`, 'success');
      return;
    }
    if (buf && buf.dirty) {
      buf.text = next;
      buf.dirty = next !== buf.saved;
      S.setExtraDirty('code:' + MAIN_LUA, buf.dirty);
      S.showToast(`Added require("${name}") to main.lua: Save to write it`, 'success');
    } else {
      await writeFile(MAIN_LUA, next);
      if (buf) { buf.text = buf.saved = next; buf.dirty = false; S.setExtraDirty('code:' + MAIN_LUA, false); }
      S.showToast(`Added require("${name}") to main.lua`, 'success');
    }
    await loadTree();
    if (st.code) {
      const open = st.codes.get(st.code);
      if (open) renderCodeEditor(open);
    } else {
      renderPage();
    }
    renderNav();
  }

  // Saves one buffer. Used by the Save button and by the save hook (saveAll).
  async function writeBuffer(buf) {
    const text = buf.text;
    await writeFile(buf.path, text);
    buf.saved = text;
    buf.dirty = buf.text !== buf.saved;
    S.setExtraDirty('code:' + buf.path, buf.dirty);
    if (st.code === buf.path) {
      const state = root().querySelector('[data-role="state"]');
      if (state) {
        state.textContent = buf.dirty ? 'Unsaved' : 'Saved';
        state.className = 'code-state' + (buf.dirty ? ' dirty' : '');
      }
    }
  }

  // ==========================================================================
  // Section: Sprites (the viewer does the work; an empty section offers to make one)
  // ==========================================================================
  function renderSprites(body) {
    body.innerHTML = `
      <div class="empty-card">
        <div class="empty-card-icon">${NAV_ICONS.sprites}</div>
        <h3 class="empty-card-title">No sprites yet</h3>
        <p class="empty-card-text">Sprites are the pictures of units, buildings and effects (SLP files). Make one from a PNG or JPG
          with the SLP creator: the mod gets its own copy, and the game's files stay untouched.</p>
        <div class="empty-card-actions">
          <button class="btn btn-save btn-sm" data-act="sprite-new">New sprite from PNG</button>
        </div>
      </div>`;
  }

  // ==========================================================================
  // Section: Taunts
  // ==========================================================================
  function playerHtml() {
    const np = st.nowPlaying;
    return `<div class="player-bar"${np ? '' : ' hidden'}>
      <span class="player-label">${np ? 'Now playing <strong>' + esc(basename(np.path)) + '</strong>' : ''}</span>
      <audio class="player" controls preload="auto" ${np ? `src="${esc(np.url)}"` : ''}></audio>
    </div>`;
  }

  function renderTaunts(body) {
    const taunts = tauntFiles().map(f => parseTaunt(f.path, f.size)).sort((a, b) => (a.num ?? 9999) - (b.num ?? 9999) || a.name.localeCompare(b.name));
    if (taunts.length === 0) {
      body.innerHTML = `
        <div class="empty-card">
          <div class="empty-card-icon">${NAV_ICONS.taunts}</div>
          <h3 class="empty-card-title">No taunts yet</h3>
          <p class="empty-card-text">Upload an MP3 or WAV to play when a player types its number (43+) in chat.</p>
          <div class="empty-card-actions"><button class="btn btn-save btn-sm" data-act="taunt-add">Add taunt</button></div>
        </div>`;
      return;
    }
    const nextFree = nextFreeTaunt(taunts);
    body.innerHTML = `
      ${playerHtml()}
      <div class="list-head">
        <span>${taunts.length} taunt${taunts.length === 1 ? '' : 's'} &middot; next free number <strong>${nextFree}</strong></span>
      </div>
      <div class="list">
        ${taunts.map(t => `
          <div class="taunt-row" data-path="${esc(t.path)}">
            <div class="taunt-num${t.num === null ? ' bad' : ''}${t.num !== null && t.num < TAUNT_FIRST_NEW ? ' replaces' : ''}">${t.num === null ? '?' : t.num}</div>
            <div class="list-main">
              <div class="list-title">${esc(t.name)}</div>
              <div class="list-sub mono">${esc(t.path)} &middot; ${esc(formatSize(t.size))}${t.num === null
                ? ' &middot; <span class="warn">no number: rename it to NN name.' + esc(t.ext) + '</span>'
                : (t.num < TAUNT_FIRST_NEW ? ' &middot; <span class="warn">replaces the game taunt ' + t.num + '</span>' : '')}</div>
            </div>
            <div class="row-actions">
              <button class="btn btn-wood btn-sm" data-act="taunt-play" data-path="${esc(t.path)}" title="Play this sound">&#9654; Play</button>
              <div class="sc-more-wrap">
                <button type="button" class="btn btn-ghost btn-sm" data-more-toggle aria-haspopup="true" aria-expanded="false" title="More actions">&#8943;</button>
                <div class="sc-more-menu" hidden>
                  <button type="button" class="sc-more-item" data-act="taunt-rename" data-path="${esc(t.path)}">Rename...</button>
                  <button type="button" class="sc-more-item" data-act="taunt-replace" data-path="${esc(t.path)}">Replace sound...</button>
                  <div class="sc-more-sep"></div>
                  <button type="button" class="sc-more-item danger" data-act="taunt-delete" data-path="${esc(t.path)}">Delete</button>
                </div>
              </div>
            </div>
          </div>`).join('')}
      </div>`;
  }

  function nextFreeTaunt(taunts) {
    const used = new Set(taunts.map(t => t.num));
    let n = TAUNT_FIRST_NEW;
    while (used.has(n) && n < TAUNT_LAST) n++;
    return n;
  }

  // Plays a sound of the mod: the file is read once per session and kept as an object URL.
  async function playSound(path) {
    try {
      let url = st.urls.get(path);
      if (!url) {
        url = URL.createObjectURL(await readBlob(path));
        st.urls.set(path, url);
      }
      st.nowPlaying = { path, url };
      const player = root().querySelector('audio.player');
      if (player) {
        player.src = url;
        player.play().catch(() => {});
      }
      const label = root().querySelector('.player-label');
      if (label) label.innerHTML = 'Now playing <strong>' + esc(basename(path)) + '</strong>';
      const bar = root().querySelector('.player-bar');
      if (bar) bar.hidden = false;
    } catch (e) {
      S.showToast('Could not play ' + basename(path) + ': ' + e.message, 'error');
    }
  }

  function forgetUrl(path) {
    const url = st.urls.get(path);
    if (url) URL.revokeObjectURL(url);
    st.urls.delete(path);
    if (st.nowPlaying && st.nowPlaying.path === path) st.nowPlaying = null;
  }

  // Dialog of a taunt: number, name and (for add and replace) a sound file.
  function tauntDialog(opts) {
    const taken = new Set(tauntFiles().map(f => parseTaunt(f.path).num).filter(n => n !== null));
    if (opts.current) taken.delete(opts.current.num);
    const suggest = opts.current ? opts.current.num : nextFreeTaunt(tauntFiles().map(f => parseTaunt(f.path)));
    const name = opts.current ? opts.current.name : '';
    const needFile = !opts.current || opts.replaceFile;
    openDialog({
      title: opts.title,
      width: 'md',
      body: `
        ${needFile ? `<div class="form-field">
          <label>${opts.current ? 'New sound file' : 'Sound file'} <span class="muted">MP3 or WAV</span></label>
          <input type="file" class="form-file" accept=".mp3,.wav,audio/mpeg,audio/wav">
        </div>` : ''}
        <div class="form-row">
          <div class="form-field narrow">
            <label for="taunt-num">Number</label>
            <input id="taunt-num" type="number" min="${TAUNT_FIRST_NEW}" max="${TAUNT_LAST}" step="1" value="${suggest}" class="form-input">
          </div>
          <div class="form-field grow">
            <label for="taunt-name">Name</label>
            <input id="taunt-name" type="text" maxlength="59" placeholder="wololo again" value="${esc(name)}" class="form-input" autocomplete="off">
          </div>
        </div>
        <p class="form-hint">The file is saved as <code class="mono" data-role="preview"></code>. Type the number in chat to play it.
          Numbers ${TAUNT_FIRST_NEW} to ${TAUNT_LAST} are free; ${TAUNT_FIRST_NEW - 1} and below replace the game's taunts.</p>
        <p class="form-error" role="alert"></p>`,
      buttons: [
        { label: 'Cancel' },
        {
          label: opts.saveLabel, primary: true,
          onClick: async (dialog, close) => {
            const num = Number(dialog.querySelector('#taunt-num').value);
            const nm = dialog.querySelector('#taunt-name').value.trim();
            const fileInput = dialog.querySelector('.form-file');
            const picked = fileInput && fileInput.files && fileInput.files[0];
            if (!Number.isInteger(num) || num < 1 || num > TAUNT_LAST) return dialogError(dialog, `The number must be from 1 to ${TAUNT_LAST}.`), false;
            if (num < TAUNT_FIRST_NEW && !opts.allowGameNumber) return dialogError(dialog, `Numbers below ${TAUNT_FIRST_NEW} replace the game's own taunts. Use ${TAUNT_FIRST_NEW} or more.`), false;
            if (taken.has(num)) return dialogError(dialog, `Number ${num} is already used by another taunt of this mod.`), false;
            if (!NAME_RULE.test(nm)) return dialogError(dialog, 'The name uses letters, digits, spaces, "-" and "_" only (ASCII, 1 to 59 characters).'), false;
            if (needFile && !picked) return dialogError(dialog, 'Pick a sound file.'), false;
            const ext = picked ? picked.name.slice(picked.name.lastIndexOf('.') + 1).toLowerCase() : (opts.current ? opts.current.ext : '');
            if (!/^(mp3|wav)$/.test(ext)) return dialogError(dialog, 'Only MP3 and WAV files can be taunts.'), false;
            if (picked && picked.size > 20 * 1024 * 1024) return dialogError(dialog, 'The sound file is larger than 20 MB.'), false;
            const newPath = `taunts/${num} ${nm}.${ext}`;
            try {
              await opts.apply({ newPath, picked, close });
            } catch (e) {
              return dialogError(dialog, e.message), false;
            }
            return true;
          }
        }
      ],
      onOpen: (dialog) => {
        const update = () => {
          const num = dialog.querySelector('#taunt-num').value;
          const nm = dialog.querySelector('#taunt-name').value.trim() || 'name';
          const fileInput = dialog.querySelector('.form-file');
          const picked = fileInput && fileInput.files && fileInput.files[0];
          const ext = picked ? picked.name.slice(picked.name.lastIndexOf('.') + 1).toLowerCase() : (opts.current ? opts.current.ext : 'mp3');
          dialog.querySelector('[data-role="preview"]').textContent = `taunts/${num} ${nm}.${ext}`;
        };
        dialog.querySelectorAll('input').forEach(i => i.addEventListener('input', update));
        dialog.querySelectorAll('.form-file').forEach(i => i.addEventListener('change', () => {
          const f = i.files[0];
          if (f && !dialog.querySelector('#taunt-name').value.trim()) {
            dialog.querySelector('#taunt-name').value = f.name.replace(/\.[^.]+$/, '').replace(/^\d+\s+/, '').slice(0, 59);
          }
          update();
        }));
        update();
        setTimeout(() => dialog.querySelector('#taunt-name').focus(), 30);
      }
    });
  }

  // ==========================================================================
  // Section: Files (the whole mod folder)
  // ==========================================================================
  function kindOf(path) {
    const low = path.toLowerCase();
    if (/^taunts\//.test(low) && /\.(mp3|wav)$/.test(low)) return 'Taunt';
    if (/^scripts\/graphs\//.test(low)) return 'Visual script';
    if (/^scripts\//.test(low) && low.endsWith('.lua')) return 'Lua script';
    if (low.endsWith('.slp')) return 'Sprite';
    if (/\.(mp3|wav|ogg)$/.test(low)) return 'Sound';
    if (/\.(png|jpg|jpeg|bmp|gif)$/.test(low)) return 'Image';
    if (low.endsWith('.rms')) return 'Random map';
    if (low.endsWith('.ai') || low.endsWith('.per')) return 'AI script';
    if (low.endsWith('.json')) return 'Data';
    return 'File';
  }

  const AUDIO_RE = /\.(mp3|wav|ogg)$/i;

  function renderFiles(body) {
    const all = treeList();
    if (all.length === 0) {
      body.innerHTML = `
        <div class="empty-card">
          <div class="empty-card-icon">${NAV_ICONS.files}</div>
          <h3 class="empty-card-title">No files yet</h3>
          <p class="empty-card-text">Sounds, pictures, sprites and scripts of the mod are listed here, so nothing is hidden.
            The civilization files (civs/) and mod.json are edited in Civilizations.</p>
          <div class="empty-card-actions"><button class="btn btn-save btn-sm" data-act="file-add">Add file</button></div>
        </div>`;
      return;
    }
    body.innerHTML = `
      ${playerHtml()}
      <div class="list-toolbar">
        <input type="search" class="form-input file-filter" placeholder="Filter by name or folder" value="${esc(st.filter)}" aria-label="Filter files">
        <span class="list-head-count" data-role="count"></span>
      </div>
      <div class="list file-list" data-role="list"></div>
      <p class="list-note">All files in the mod folder (except civs/ and mod.json, which are edited in Civilizations).</p>
      <input type="file" class="file-replace-input" hidden>`;
    const listEl = body.querySelector('[data-role="list"]');
    const countEl = body.querySelector('[data-role="count"]');
    const filter = body.querySelector('.file-filter');
    const paint = () => {
      const q = st.filter.trim().toLowerCase();
      const shown = all.filter(f => !q || f.path.toLowerCase().includes(q)).sort((a, b) => a.path.localeCompare(b.path));
      countEl.textContent = shown.length + (shown.length === all.length ? ' files' : ` of ${all.length} files`);
      listEl.innerHTML = shown.length ? shown.map(f => fileRowHtml(f)).join('')
        : '<div class="inline-empty"><div><strong>No file matches</strong><span>Try another name or folder.</span></div></div>';
    };
    filter.addEventListener('input', () => { st.filter = filter.value; paint(); });
    paint();
    body.querySelector('.file-replace-input').addEventListener('change', (e) => replaceFileWith(e.target));
  }

  function fileRowHtml(f) {
    const kind = kindOf(f.path);
    const dir = dirname(f.path);
    const audio = AUDIO_RE.test(f.path);
    const lua = /^scripts\/[^/]+\.lua$/i.test(f.path);
    const graph = /^scripts\/graphs\/[^/]+\.json$/i.test(f.path);
    const primary = lua ? `<button class="btn btn-wood btn-sm" data-act="open-code" data-path="${esc(f.path)}">Open</button>`
      : graph ? `<button class="btn btn-wood btn-sm" data-act="open-script" data-path="${esc(basename(f.path))}">Open</button>`
      : audio ? `<button class="btn btn-wood btn-sm" data-act="file-play" data-path="${esc(f.path)}">&#9654; Play</button>` : '';
    return `<div class="file-row" data-path="${esc(f.path)}">
      <span class="kind-tag kind-${kind.toLowerCase().replace(/\s+/g, '-')}">${esc(kind)}</span>
      <span class="list-main">
        <span class="list-title mono">${dir ? `<span class="dim">${esc(dir)}/</span>` : ''}${esc(basename(f.path))}</span>
      </span>
      <span class="file-size">${esc(formatSize(f.size))}</span>
      <span class="row-actions">
        ${primary}
        <div class="sc-more-wrap">
          <button type="button" class="btn btn-ghost btn-sm" data-more-toggle aria-haspopup="true" aria-expanded="false" title="More actions">&#8943;</button>
          <div class="sc-more-menu" hidden>
            <button type="button" class="sc-more-item" data-act="file-download" data-path="${esc(f.path)}">Download</button>
            <button type="button" class="sc-more-item" data-act="file-replace" data-path="${esc(f.path)}">Replace...</button>
            <div class="sc-more-sep"></div>
            <button type="button" class="sc-more-item danger" data-act="file-delete" data-path="${esc(f.path)}">Delete</button>
          </div>
        </div>
      </span>
    </div>`;
  }

  async function downloadFile(path) {
    try {
      const blob = await readBlob(path);
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = basename(path);
      document.body.appendChild(a);
      a.click();
      a.remove();
      setTimeout(() => URL.revokeObjectURL(url), 4000);
    } catch (e) {
      S.showToast('Download failed: ' + e.message, 'error');
    }
  }

  // Replace keeps the path of the file (a sound taunt keeps its number and name).
  let replacePath = null;
  function replaceFileWith(input) {
    const file = input.files && input.files[0];
    const path = replacePath;
    input.value = '';
    replacePath = null;
    if (!file || !path) return;
    if (file.size > 20 * 1024 * 1024) { S.showToast('The file is larger than 20 MB', 'error'); return; }
    writeFile(path, file).then(async () => {
      S.showToast(`Replaced ${basename(path)}`, 'success');
      await afterFileChange(path);
    }).catch(e => S.showToast('Replace failed: ' + e.message, 'error'));
  }

  async function deleteFile(path) {
    S.showConfirm('Delete file', `Delete ${path}? The file is removed from the mod folder and cannot be restored by Scriptorium.`, 'Delete', async () => {
      try {
        await removeFile(path);
        S.showToast(`Deleted ${basename(path)}`, 'success');
        await afterFileChange(path);
      } catch (e) {
        S.showToast('Delete failed: ' + e.message, 'error');
      }
    });
  }

  // Dialog: a new file in the mod folder (folder may be empty: the mod's root)
  function addFileDialog() {
    openDialog({
      title: 'Add file to the mod',
      width: 'md',
      body: `
        <div class="form-field">
          <label>File</label>
          <input type="file" class="form-file">
        </div>
        <div class="form-row">
          <div class="form-field grow">
            <label for="add-folder">Folder in the mod</label>
            <input id="add-folder" type="text" class="form-input mono" placeholder="empty = the mod's root" autocomplete="off">
          </div>
          <div class="form-field grow">
            <label for="add-name">File name</label>
            <input id="add-name" type="text" class="form-input mono" autocomplete="off">
          </div>
        </div>
        <p class="form-hint" data-role="preview"></p>
        <p class="form-error" role="alert"></p>`,
      buttons: [
        { label: 'Cancel' },
        {
          label: 'Add file', primary: true,
          onClick: async (dialog) => {
            const picked = dialog.querySelector('.form-file').files[0];
            const dir = dialog.querySelector('#add-folder').value.trim().replace(/^\/+|\/+$/g, '');
            const name = dialog.querySelector('#add-name').value.trim();
            if (!picked) return dialogError(dialog, 'Pick a file.'), false;
            if (!/^[A-Za-z0-9_.\- ]+$/.test(name) || name === '.' || name === '..') return dialogError(dialog, 'The file name uses letters, digits, spaces, ".", "-" and "_" only.'), false;
            if (dir && !/^([A-Za-z0-9_\- ]+\/?)+$/.test(dir)) return dialogError(dialog, 'The folder uses letters, digits, spaces, "-" and "_", with "/" between folders.'), false;
            const path = dir ? `${dir}/${name}` : name;
            if (path === 'mod.json' || /^civs(\/|$)/.test(path)) return dialogError(dialog, 'civs/ and mod.json are edited in Civilizations.'), false;
            if (picked.size > 20 * 1024 * 1024) return dialogError(dialog, 'The file is larger than 20 MB.'), false;
            try {
              await writeFile(path, picked);
            } catch (e) {
              return dialogError(dialog, e.message), false;
            }
            S.showToast(`Added ${path}`, 'success');
            await afterFileChange(path);
            return true;
          }
        }
      ],
      onOpen: (dialog) => {
        const fileInput = dialog.querySelector('.form-file');
        const folderInput = dialog.querySelector('#add-folder');
        const nameInput = dialog.querySelector('#add-name');
        const preview = () => {
          const dir = folderInput.value.trim().replace(/^\/+|\/+$/g, '');
          const name = nameInput.value.trim() || '...';
          const path = dir ? `${dir}/${name}` : name;
          const existing = treeList().some(f => f.path === path);
          dialog.querySelector('[data-role="preview"]').textContent = 'Saved as ' + path + (existing ? ' (replaces the file that is there)' : '');
        };
        fileInput.addEventListener('change', () => {
          const f = fileInput.files[0];
          if (f && !nameInput.value.trim()) nameInput.value = f.name;
          preview();
        });
        folderInput.addEventListener('input', preview);
        nameInput.addEventListener('input', preview);
        preview();
      }
    });
  }

  // After a file changed on disk (replaced, deleted, added): an unsaved Lua buffer of that file is
  // dropped, the tree is read again, and the open code editor shows the file as it is now (or
  // leaves when the file is gone).
  async function afterFileChange(path) {
    const wasOpen = !!path && st.code === path;
    if (path) {
      forgetUrl(path);
      st.codes.delete(path);
      S.setExtraDirty('code:' + path, false);
    }
    await loadTree();
    if (wasOpen) {
      if (treeList().some(f => f.path === path)) openCode(path);
      else show('scripts');
      return;
    }
    refresh();
  }

  // ==========================================================================
  // Section: Civilizations (basic.js owns the content and the element editors)
  // ==========================================================================
  function renderCivs(body) {
    if (window.BasicWorkspace) BasicWorkspace.renderCivCards(body);
  }

  const SECTIONS = [
    {
      id: 'civs', label: 'Civilizations',
      sub: 'New civilizations and changes to the civilizations of the game: units, buildings, bonuses.',
      count: () => Object.keys(civsOf()).length,
      actions: () => '<button class="btn btn-save btn-sm" data-act="new-element">New element</button>',
      render: renderCivs
    },
    {
      id: 'scripts', label: 'Scripts',
      sub: 'Visual event graphs and Lua scripts for your mod.',
      count: () => luaFiles().length + visualList().length,
      actions: () => {
        const hasMain = luaFiles().some(f => f.path === MAIN_LUA);
        return '<button class="btn btn-save btn-sm" data-act="visual-new">New visual script</button>'
          + '<div class="sc-more-wrap">'
          + '<button type="button" class="btn btn-wood btn-sm" data-more-toggle aria-haspopup="true" aria-expanded="false">More &#9662;</button>'
          + '<div class="sc-more-menu" hidden>'
          + '<button type="button" class="sc-more-item" data-act="lua-new">New Lua script...</button>'
          + (hasMain ? '' : '<button type="button" class="sc-more-item" data-act="lua-create-main">Create main.lua</button>')
          + '</div></div>';
      },
      render: renderScripts
    },
    {
      id: 'sprites', label: 'Sprites',
      sub: 'Pictures of units and buildings (SLP files) that the mod adds or replaces.',
      count: () => spriteFiles().length,
      actions: () => '<button class="btn btn-save btn-sm" data-act="sprite-new">New sprite from PNG</button>',
      render: renderSprites
    },
    {
      id: 'taunts', label: 'Taunts',
      sub: 'Sounds that play when a player types their number in chat.',
      count: () => tauntFiles().length,
      actions: () => '<button class="btn btn-save btn-sm" data-act="taunt-add">Add taunt</button>',
      render: renderTaunts
    },
    {
      id: 'files', label: 'Files',
      sub: 'Every file of the mod folder: sounds, pictures, scripts and anything else.',
      count: () => treeList().length,
      actions: () => '<button class="btn btn-save btn-sm" data-act="file-add">Add file</button>',
      render: renderFiles
    }
  ];

  // ==========================================================================
  // Actions (data-act on the page)
  // ==========================================================================
  function taskFailed(e) { S.showToast(e && e.message ? e.message : 'Something went wrong', 'error'); }

  const ACTIONS = {
    'new-element': () => window.BasicWorkspace && BasicWorkspace.newElement(),
    'visual-new': () => window.BasicWorkspace && BasicWorkspace.createNewScript(),
    'open-script': (el) => {
      if (window.BasicWorkspace) BasicWorkspace.openScript(el.dataset.path);
    },
    'open-code': (el) => openCode(el.dataset.path),
    'lua-new': () => newScriptDialog(),
    'lua-add-require': (el) => addRequireToMain(el.dataset.name).catch(taskFailed),
    'lua-create-main': async () => {
      try {
        await writeFile(MAIN_LUA, MAIN_TEMPLATE);
        S.showToast('Created scripts/main.lua', 'success');
        await loadTree();
        openCode(MAIN_LUA);
      } catch (e) { taskFailed(e); }
    },
    'code-back': () => show('scripts'),
    'code-save': async () => {
      const buf = st.codes.get(st.code);
      if (!buf) return;
      try {
        await writeBuffer(buf);
        S.showToast(`Saved ${buf.path}`, 'success');
        await loadTree();
        renderNav();
      } catch (e) { taskFailed(e); }
    },
    'sprite-new': () => {
      if (!window.SlpViewer) return;
      SlpViewer.open();
      setTimeout(() => {
        const btn = document.querySelector('#sprites-view .slpv-new');
        if (btn) btn.click();
      }, 60);
    },
    'taunt-add': () => {
      tauntDialog({
        title: 'Add a taunt',
        saveLabel: 'Add taunt',
        apply: async ({ newPath, picked, close }) => {
          await writeFile(newPath, picked);
          S.showToast(`Added ${newPath}`, 'success');
          close();
          await afterFileChange(newPath);
        }
      });
    },
    'taunt-rename': (el) => {
      const path = el.dataset.path;
      const cur = parseTaunt(path);
      tauntDialog({
        title: 'Rename taunt',
        saveLabel: 'Rename',
        current: cur,
        apply: async ({ newPath, close }) => {
          if (newPath !== path) {
            const blob = await readBlob(path);
            await writeFile(newPath, blob);
            await removeFile(path);
          }
          close();
          S.showToast(`Renamed to ${basename(newPath)}`, 'success');
          await afterFileChange(path);
        }
      });
    },
    'taunt-replace': (el) => {
      const path = el.dataset.path;
      const cur = parseTaunt(path);
      tauntDialog({
        title: 'Replace the sound',
        saveLabel: 'Replace',
        current: cur,
        replaceFile: true,
        apply: async ({ newPath, picked, close }) => {
          await writeFile(newPath, picked);
          if (newPath !== path) await removeFile(path);
          close();
          S.showToast(`Replaced ${basename(newPath)}`, 'success');
          await afterFileChange(newPath);
        }
      });
    },
    'taunt-delete': (el) => deleteFile(el.dataset.path),
    'taunt-play': (el) => playSound(el.dataset.path),
    'file-play': (el) => playSound(el.dataset.path),
    'file-add': () => addFileDialog(),
    'file-download': (el) => downloadFile(el.dataset.path),
    'file-replace': (el) => {
      replacePath = el.dataset.path;
      const input = root().querySelector('.file-replace-input');
      if (input) input.click();
    },
    'file-delete': (el) => deleteFile(el.dataset.path),
    'open-folder': () => S.api('/api/open-folder', { method: 'POST', body: { folder: folder() } }).catch(taskFailed)
  };

  // ==========================================================================
  // Save and revert hooks (app.js saveAll / revertMod)
  // ==========================================================================
  // 10: Lua buffers that are unsaved are written first, so the graph block (30) is built on them.
  S.onSave(async () => {
    for (const buf of st.codes.values()) {
      if (buf.dirty) await writeBuffer(buf);
    }
  }, 10);

  // 30: after the visual scripts were compiled into main.lua, the Lua buffers that are not
  // unsaved show the file as it is now; the tree and the counts are read again.
  S.onSave(async () => {
    for (const buf of [...st.codes.values()]) {
      if (buf.dirty) continue;
      try {
        const text = await readText(buf.path);
        if (text !== buf.saved) {
          buf.text = buf.saved = text;
          if (st.code === buf.path) {
            const r = root();
            const input = r && r.querySelector('[data-role="input"]');
            if (input) input.value = text;
          }
        }
      } catch (e) {
        st.codes.delete(buf.path);
      }
    }
    await loadTree();
    if (!st.code && !(window.BasicWorkspace && BasicWorkspace.editorOpen())) renderPage();
    renderNav();
  }, 30);

  S.onRevert(() => {
    for (const buf of [...st.codes.values()]) {
      if (!buf.dirty) continue;
      st.codes.delete(buf.path);
      S.setExtraDirty('code:' + buf.path, false);
    }
    if (st.code && !st.codes.has(st.code)) show(st.section);
  });

  // ==========================================================================
  // The mod was opened, closed or changed
  // ==========================================================================
  S.onModLoaded(async () => {
    for (const url of st.urls.values()) URL.revokeObjectURL(url);
    st.urls.clear();
    st.nowPlaying = null;
    st.codes.clear();
    st.code = null;
    st.tree = null;
    st.filter = '';
    if (!S.state.currentModData) {
      const nav = $('mod-nav');
      if (nav) nav.innerHTML = '';
      return;
    }
    showLoading();
    const folderAtStart = S.state.currentModData.folder;
    await loadTree();
    if (!S.state.currentModData || S.state.currentModData.folder !== folderAtStart) return;
    st.section = defaultSection();
    renderNav();
    renderPage();
  });

  S.onRefresh(() => {
    if (window.BasicWorkspace) BasicWorkspace.leaveEditor();
    st.code = null;
    refresh();
  });

  window.ModSections = {
    refresh,
    show,
    current: () => st.section,
    reload: async () => { await loadTree(); refresh(); },
    openCode
  };
})();

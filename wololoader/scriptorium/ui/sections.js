/**
 * Scriptorium: the mod editor's navigation and sections.
 *
 * The editor shows one mod. Its left nav lists every kind of content the mod can hold, with a
 * count, and the main column shows the section you pick:
 *   Civilizations  new civs and changes to the game's civs (basic.js draws them)
 *   Game modes     mod.json "game_modes" (custom lobby game modes)
 *   Resources      mod.json "resources" (custom resources for HUD, costs, effects, Lua)
 *   Settings       mod.json "settings" (player-configurable options read via MOD.settings)
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
    imgUrls: new Map(),  // path -> Promise<blob URL | null> for PNG previews (resources)
    hadKey: {            // whether mod.json originally had each optional array key on load
      game_modes: false,
      resources: false,
      settings: false
    },
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
  function modOf() { return (S.state.currentModData && S.state.currentModData.mod) || null; }
  function modList(key) {
    const m = modOf();
    return (m && Array.isArray(m[key])) ? m[key] : [];
  }

  function afterModJsonChange() {
    S.checkDirty();
    renderNav();
    renderPage();
  }

  function slugId(text, lowerOnly) {
    let s = String(text || '').trim().replace(/[\s\-]+/g, '_');
    s = lowerOnly ? s.toLowerCase().replace(/[^a-z0-9_]+/g, '') : s.replace(/[^A-Za-z0-9_]+/g, '');
    return s.replace(/_+/g, '_').replace(/^_+|_+$/g, '');
  }

  function checkRelPath(path, label) {
    if (path.length > 256) return `${label} is longer than 256 characters.`;
    if (/[\t\r\n]/.test(path)) return `${label} cannot contain tabs or line breaks.`;
    if (path.includes('..') || path.startsWith('/') || path.startsWith('\\') || /^[A-Za-z]:/.test(path)) {
      return `${label} must be a relative path inside the mod folder (no "..", leading slash, or drive letter).`;
    }
    return '';
  }

  function loadImgUrl(path) {
    if (!path) return Promise.resolve(null);
    if (!st.imgUrls.has(path)) {
      const p = readBlob(path)
        .then(blob => URL.createObjectURL(blob))
        .catch(() => null);
      st.imgUrls.set(path, p);
    }
    return st.imgUrls.get(path);
  }

  function forgetImgUrl(path) {
    const p = st.imgUrls.get(path);
    st.imgUrls.delete(path);
    if (p) p.then(url => { if (url) URL.revokeObjectURL(url); });
  }

  function clearImgUrls() {
    for (const p of st.imgUrls.values()) {
      p.then(url => { if (url) URL.revokeObjectURL(url); });
    }
    st.imgUrls.clear();
  }

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
  const rmsFiles = () => treeList()
    .filter(f => /^resources\/_common\/random-map-scripts\/.+\.rms$/i.test(f.path))
    .sort((a, b) => a.path.localeCompare(b.path));
  const pngFiles = () => treeList()
    .filter(f => /\.png$/i.test(f.path))
    .sort((a, b) => {
      const ai = /^icons\//i.test(a.path) ? 0 : 1;
      const bi = /^icons\//i.test(b.path) ? 0 : 1;
      return ai - bi || a.path.localeCompare(b.path);
    });

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
    for (const id of ['civs', 'scripts', 'sprites', 'taunts', 'game_modes', 'resources', 'settings']) {
      if (countOf(id) > 0) return id;
    }
    return 'civs';
  }

  // ==========================================================================
  // Navigation
  // ==========================================================================
  const NAV_ICONS = {
    civs: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M3 9l9-7 9 7v11a2 2 0 01-2 2H5a2 2 0 01-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>',
    game_modes: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M4 21V4"/><path d="M4 4h14l-2.5 5L18 14H4"/></svg>',
    resources: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><polygon points="12 2 20 9 12 22 4 9 12 2"/><polyline points="4 9 20 9"/><polyline points="12 2 9 9 12 22 15 9 12 2"/></svg>',
    settings: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><line x1="4" y1="6" x2="8" y2="6"/><line x1="13" y1="6" x2="20" y2="6"/><circle cx="10.5" cy="6" r="2.5"/><line x1="4" y1="12" x2="14" y2="12"/><line x1="19" y1="12" x2="20" y2="12"/><circle cx="16.5" cy="12" r="2.5"/><line x1="4" y1="18" x2="6" y2="18"/><line x1="11" y1="18" x2="20" y2="18"/><circle cx="8.5" cy="18" r="2.5"/></svg>',
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
      <div class="sec-page" data-sec="${esc(sec.id)}">
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
  const IMG_RE = /\.(png|jpe?g|gif|webp|bmp|svg)$/i;
  const TEXT_RE = /\.(txt|md|rms|lua|json|per|ai|cfg|ini|csv|xml|html|css|js|log)$/i;
  const SLP_RE = /\.slp$/i;

  const thumbCache = new Map();
  function fileView() {
    try { const v = localStorage.getItem('sc-file-view'); if (v === 'icons' || v === 'gallery') return v; } catch (e) { /* ignore */ }
    return 'list';
  }
  // <img src> cannot send the API token header, so pictures are fetched as blobs.
  async function blobUrlFor(path) {
    if (thumbCache.has(path)) return thumbCache.get(path);
    const p = readBlob(path).then(b => URL.createObjectURL(b));
    thumbCache.set(path, p);
    p.catch(() => thumbCache.delete(path));
    return p;
  }
  function hydrateThumbs(root) {
    const load = (img) => {
      blobUrlFor(img.dataset.thumb).then(u => { img.src = u; img.classList.add('loaded'); })
        .catch(() => { img.classList.add('broken'); });
    };
    const imgs = root.querySelectorAll('img[data-thumb]');
    if (!('IntersectionObserver' in window)) { imgs.forEach(load); return; }
    const io = new IntersectionObserver((entries) => {
      entries.forEach(en => { if (en.isIntersecting) { io.unobserve(en.target); load(en.target); } });
    }, { rootMargin: '200px' });
    imgs.forEach(i => io.observe(i));
  }
  function fileTileHtml(f, view) {
    const kind = kindOf(f.path);
    const img = IMG_RE.test(f.path);
    const act = (AUDIO_RE.test(f.path)) ? 'file-play' : 'file-preview';
    const visual = img
      ? `<img class="tile-img" data-thumb="${esc(f.path)}" alt="">`
      : `<span class="tile-kind kind-tag kind-${kind.toLowerCase().replace(/\s+/g, '-')}">${esc(kind)}</span>`;
    return `<button type="button" class="file-tile tile-${view}" data-act="${act}" data-path="${esc(f.path)}" title="${esc(f.path)}">
      <span class="tile-visual">${visual}</span>
      <span class="tile-name mono">${esc(basename(f.path))}</span>
      <span class="tile-size">${esc(formatSize(f.size))}</span>
    </button>`;
  }

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
        <div class="view-switch" role="group" aria-label="View">
          <button type="button" class="view-btn" data-view="list" title="List">&#9776; List</button>
          <button type="button" class="view-btn" data-view="icons" title="Large icons">&#9638; Icons</button>
          <button type="button" class="view-btn" data-view="gallery" title="Gallery (images only)">&#9635; Gallery</button>
        </div>
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
      const view = fileView();
      listEl.className = 'file-list view-' + view;
      body.querySelectorAll('.view-btn').forEach(b => b.classList.toggle('active', b.dataset.view === view));
      let items = shown;
      if (view === 'gallery') items = shown.filter(f => IMG_RE.test(f.path));
      listEl.innerHTML = items.length ? items.map(f => view === 'list' ? fileRowHtml(f) : fileTileHtml(f, view)).join('')
        : '<div class="inline-empty"><div><strong>No file matches</strong><span>' + (view === 'gallery' ? 'No pictures in this mod.' : 'Try another name or folder.') + '</span></div></div>';
      hydrateThumbs(listEl);
    };
    body.querySelectorAll('.view-btn').forEach(b => b.addEventListener('click', () => {
      try { localStorage.setItem('sc-file-view', b.dataset.view); } catch (e) { /* ignore */ }
      paint();
    }));
    filter.addEventListener('input', () => { st.filter = filter.value; paint(); });
    paint();
    body.querySelector('.file-replace-input').addEventListener('change', (e) => replaceFileWith(e.target));
  }

  function fileRowHtml(f) {
    const kind = kindOf(f.path);
    const dir = dirname(f.path);
    const audio = AUDIO_RE.test(f.path);
    const img = IMG_RE.test(f.path);
    const txt = TEXT_RE.test(f.path);
    const slp = SLP_RE.test(f.path);
    const lua = /^scripts\/[^/]+\.lua$/i.test(f.path);
    const graph = /^scripts\/graphs\/[^/]+\.json$/i.test(f.path);
    const primary = lua ? `<button class="btn btn-wood btn-sm" data-act="open-code" data-path="${esc(f.path)}">Open</button>`
      : graph ? `<button class="btn btn-wood btn-sm" data-act="open-script" data-path="${esc(basename(f.path))}">Open</button>`
      : audio ? `<button class="btn btn-wood btn-sm" data-act="file-play" data-path="${esc(f.path)}">&#9654; Play</button>`
      : (img || txt || slp) ? `<button class="btn btn-wood btn-sm" data-act="file-preview" data-path="${esc(f.path)}">Preview</button>` : '';
    const thumb = img
      ? `<img class="file-thumb" data-thumb="${esc(f.path)}" alt="">`
      : `<span class="kind-tag kind-${kind.toLowerCase().replace(/\s+/g, '-')}">${esc(kind)}</span>`;
    return `<div class="file-row" data-path="${esc(f.path)}">
      ${thumb}
      <span class="list-main">
        <span class="list-title mono">${dir ? `<span class="dim">${esc(dir)}/</span>` : ''}${esc(basename(f.path))}</span>
      </span>
      <span class="file-size">${esc(formatSize(f.size))}</span>
      <span class="row-actions">
        ${primary}
        <div class="sc-more-wrap">
          <button type="button" class="btn btn-ghost btn-sm" data-more-toggle aria-haspopup="true" aria-expanded="false" title="More actions">&#8943;</button>
          <div class="sc-more-menu" hidden>
            ${(lua || graph) ? `<button type="button" class="sc-more-item" data-act="file-preview" data-path="${esc(f.path)}">Preview</button>` : ''}
            <button type="button" class="sc-more-item" data-act="file-download" data-path="${esc(f.path)}">Download</button>
            <button type="button" class="sc-more-item" data-act="file-replace" data-path="${esc(f.path)}">Replace...</button>
            <div class="sc-more-sep"></div>
            <button type="button" class="sc-more-item danger" data-act="file-delete" data-path="${esc(f.path)}">Delete</button>
          </div>
        </div>
      </span>
    </div>`;
  }

  async function previewFileDialog(path) {
    if (!path) return;
    if (SLP_RE.test(path)) {
      if (window.SlpViewer) {
        const navBtn = document.querySelector('#mod-nav [data-sec="sprites"]');
        if (navBtn) navBtn.click();
        else SlpViewer.open();
      } else {
        S.showToast('Open Sprites from the left sidebar to inspect .slp files.', 'info');
      }
      return;
    }
    const entry = treeList().find(f => f.path === path);
    const sizeText = entry ? formatSize(entry.size) : '';
    if (IMG_RE.test(path)) {
      const url = '';
      openDialog({
        title: basename(path),
        width: 'lg',
        body: `
          <div class="file-preview-meta">
            <code class="rms-path mono">${esc(path)}</code>
            <span class="muted" data-role="img-dims">${esc(sizeText)}</span>
            <label class="gm-inline-toggle" style="margin-left:auto">
              <input type="checkbox" id="fp-pixel" checked>
              <span>Crisp pixels</span>
            </label>
          </div>
          <div class="file-preview-stage">
            <img class="file-preview-img pixelated" src="${esc(url)}" alt="${esc(basename(path))}">
          </div>`,
        buttons: [
          { label: 'Download', onClick: () => { downloadFile(path); return false; } },
          { label: 'Close', primary: true, onClick: () => true }
        ],
        onOpen: (dialog) => {
          const imgEl = dialog.querySelector('.file-preview-img');
          blobUrlFor(path).then(u => { imgEl.src = u; }).catch(() => { imgEl.alt = 'Could not load picture'; });
          const dimsEl = dialog.querySelector('[data-role="img-dims"]');
          const pixChk = dialog.querySelector('#fp-pixel');
          imgEl.addEventListener('load', () => {
            if (imgEl.naturalWidth && imgEl.naturalHeight) {
              dimsEl.textContent = `${imgEl.naturalWidth} × ${imgEl.naturalHeight} px` + (sizeText ? ` · ${sizeText}` : '');
            }
          });
          pixChk.addEventListener('change', () => {
            imgEl.classList.toggle('pixelated', pixChk.checked);
          });
        }
      });
      return;
    }
    if (TEXT_RE.test(path)) {
      const raw = await readText(path);
      const limit = 120000;
      const truncated = raw.length > limit;
      const shown = truncated ? raw.slice(0, limit) : raw;
      const lineCount = shown.split('\n').length;
      openDialog({
        title: basename(path),
        width: 'lg',
        body: `
          <div class="file-preview-meta">
            <code class="rms-path mono">${esc(path)}</code>
            <span class="muted">${lineCount} lines${sizeText ? ' · ' + esc(sizeText) : ''}${truncated ? ' · truncated at 120 KB' : ''}</span>
          </div>
          <pre class="file-preview-code mono">${esc(shown)}</pre>`,
        buttons: [
          { label: 'Copy', onClick: () => { copyToClipboard(raw, `Copied ${basename(path)}`); return false; } },
          { label: 'Download', onClick: () => { downloadFile(path); return false; } },
          { label: 'Close', primary: true, onClick: () => true }
        ]
      });
    }
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
      forgetImgUrl(path);
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
  // Section: Game modes (mod.json "game_modes")
  // ==========================================================================
  const GAME_MODE_ID_RULE = /^[a-z0-9_]+$/;
  const MAP_BASES = [
    { id: 'arabia', label: 'Arabia' },
    { id: 'fortress', label: 'Fortress' },
    { id: 'arena', label: 'Arena' },
    { id: 'nomad', label: 'Nomad' }
  ];
  const MAP_SIZES = [
    { id: '', label: 'Any (no minimum)' },
    { id: 'tiny', label: 'Tiny (2 players)' },
    { id: 'small', label: 'Small (3 players)' },
    { id: 'medium', label: 'Medium (4 players)' },
    { id: 'normal', label: 'Normal (6 players)' },
    { id: 'large', label: 'Large (8 players)' },
    { id: 'giant', label: 'Giant' },
    { id: 'ludikris', label: 'Ludikris' }
  ];

  const COPY_SVG = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="5.5" y="5.5" width="8" height="8" rx="1.5"/><path d="M3 10.5V3.5A1.5 1.5 0 0 1 4.5 2h7"/></svg>';

  const GAME_MODE_PRESETS = [
    {
      key: 'coop_survival',
      label: 'Co-op Survival',
      summary: 'All one team · Enemy AI · Arena (min: medium)',
      defaults: {
        id: 'coop_survival',
        name: 'Co-op Survival',
        description: 'All players share one locked team and defend together against an extra enemy AI player.',
        script_event: true,
        teams: 'all',
        enemy_player: true,
        map: {
          base: 'arena',
          min_size: 'medium',
          lock: true,
          script: 'resources/_common/random-map-scripts/CoopSurvival.rms'
        }
      }
    },
    {
      key: 'clash_1v1',
      label: '1v1 Clash Attack',
      summary: '2 players · Enemy AI · Full map visible · No auto-defeat · Arena',
      defaults: {
        id: 'clash_attack',
        name: '1v1 Clash Attack',
        description: '2-player attacker vs defender mode with the whole map revealed and scripted victory rules.',
        script_event: true,
        player_count: 2,
        enemy_player: true,
        reveal_map: true,
        no_auto_defeat: true,
        map: {
          base: 'arena',
          lock: true,
          script: 'resources/_common/random-map-scripts/ClashAttack.rms'
        }
      }
    },
    {
      key: 'custom_rules',
      label: 'Custom Rules',
      summary: 'Standard lobby · Script-driven · Fog of war on',
      defaults: {
        id: 'custom_rules',
        name: 'Custom Rules',
        description: 'Standard lobby setup with custom Lua match rules running on game start.',
        script_event: true,
        reveal_map: false
      }
    }
  ];

  async function copyToClipboard(text, toastMsg) {
    try {
      if (navigator.clipboard && navigator.clipboard.writeText) {
        await navigator.clipboard.writeText(text);
      } else {
        throw new Error('Clipboard API unavailable');
      }
      S.showToast(toastMsg || 'Copied to clipboard', 'success');
    } catch (e) {
      try {
        const ta = document.createElement('textarea');
        ta.value = text;
        ta.setAttribute('readonly', '');
        ta.style.position = 'fixed';
        ta.style.left = '-9999px';
        document.body.appendChild(ta);
        ta.select();
        const ok = document.execCommand('copy');
        ta.remove();
        if (ok) S.showToast(toastMsg || 'Copied to clipboard', 'success');
        else S.showToast('Could not copy to clipboard', 'error');
      } catch (e2) {
        S.showToast('Could not copy to clipboard', 'error');
      }
    }
  }

  function luaStringLit(str) {
    return String(str || '')
      .replace(/\\/g, '\\\\')
      .replace(/"/g, '\\"')
      .replace(/\r/g, '\\r')
      .replace(/\n/g, '\\n')
      .replace(/\t/g, '\\t');
  }

  function modeResourcesFor(modeId) {
    if (!modeId) return [];
    return modList('resources').filter(r => r && typeof r === 'object' && r.game_mode === modeId);
  }

  function uniqueModeId(baseId, list, suffixWord) {
    const clean = slugId(baseId, true) || 'mode';
    const used = new Set(list.map(m => (m && typeof m.id === 'string') ? m.id : '').filter(Boolean));
    const sep = suffixWord ? '_' + suffixWord : '';
    const first = clean.slice(0, 63 - sep.length) + sep;
    if (first && !used.has(first)) return first;
    for (let n = 2; n < 1000; n++) {
      const tail = sep + '_' + n;
      const candidate = clean.slice(0, 63 - tail.length) + tail;
      if (!used.has(candidate)) return candidate;
    }
    return clean.slice(0, 55) + '_' + Date.now().toString(36).slice(-6);
  }

  function uniqueModeName(baseName, list, isCopy) {
    const clean = String(baseName || '').trim() || 'Mode';
    const used = new Set(list.map(m => (m && typeof m.name === 'string') ? m.name : '').filter(Boolean));
    if (isCopy) {
      const first = `${clean} (Copy)`;
      if (!used.has(first)) return first;
      for (let n = 2; n < 1000; n++) {
        const candidate = `${clean} (Copy ${n})`;
        if (!used.has(candidate)) return candidate;
      }
      return `${clean} (Copy)`;
    }
    if (!used.has(clean)) return clean;
    for (let n = 2; n < 1000; n++) {
      const candidate = `${clean} ${n}`;
      if (!used.has(candidate)) return candidate;
    }
    return clean;
  }

  function buildModeLuaBoilerplate(item) {
    const id = (item && typeof item.id === 'string' && item.id) ? item.id : 'my_mode';
    const title = (item && typeof item.name === 'string' && item.name) ? item.name : id;
    const oneTeam = !!(item && typeof item.teams === 'string' && item.teams.toLowerCase() === 'all');
    const enemy = !!(item && item.enemy_player === true);
    const revealMap = !!(item && item.reveal_map === true);
    const noAutoDefeat = !!(item && item.no_auto_defeat === true);
    const pcRaw = (item && typeof item.player_count === 'number' && Number.isFinite(item.player_count))
      ? Math.trunc(item.player_count) : 0;
    const playerCount = (pcRaw >= 2 && pcRaw <= 8) ? pcRaw : 0;
    const mp = (item && item.map && typeof item.map === 'object' && !Array.isArray(item.map)) ? item.map : null;
    const resList = modeResourcesFor(id);
    const visRes = resList.filter(r => r.hidden !== true);

    const lines = [
      `-- Game mode: ${title} (id: "${id}")`,
      'local active = false',
      ''
    ];
    if (enemy) {
      lines.push('local enemy = nil');
      lines.push('');
    }
    lines.push(
      'Game.OnStart(function()',
      `    active = (Game.Mode() == "${luaStringLit(id)}")`,
      '    if not active then return end',
      '',
      '    local me = Game.LocalPlayer'
    );
    if (revealMap) {
      lines.push(
        '    local mapSize = Game.MapSize() or 120',
        '    local cx, cy = mapSize / 2, mapSize / 2'
      );
    }
    if (enemy) {
      lines.push(
        '    enemy = Game.ModeEnemy()',
        '    if enemy then',
        '        -- Extra AI opponent for this mode (call Game.SetModeEnemy(enemy.Id) when resuming saves)',
        '    end'
      );
    }
    if (oneTeam) {
      lines.push('    -- teams: "all" — all players share one locked team; iterate Game.Players() for allies');
    }
    if (playerCount) {
      lines.push(`    -- player_count: ${playerCount} — lobby locks to ${playerCount} players (Game.Player(1)..Game.Player(${playerCount}))`);
    }
    if (noAutoDefeat) {
      lines.push('    -- no_auto_defeat: call player:Win() or player:Defeat() explicitly when the match ends');
    }
    if (mp && mp.script) {
      lines.push(`    -- Custom map: ${basename(String(mp.script))} (base: ${mp.base || 'arena'})`);
    }
    if (visRes.length > 0) {
      const sample = visRes[0];
      const names = visRes.map(r => r.name || r.id).join(', ');
      lines.push(`    -- Mode resources (${names}): e.g. me:AddResource("${luaStringLit(sample.id)}", 0)`);
    }
    lines.push(
      `    UI.Message("${luaStringLit(title)} started!")`,
      'end)',
      '',
      'Game.OnSecond(function()',
      '    if not active then return end',
      '    -- Per-second mode logic goes here',
      'end)'
    );
    return lines.join('\n') + '\n';
  }

  function renderQuickPresetsBar(compact) {
    if (compact) {
      return `<div class="sc-more-wrap">
        <button type="button" class="btn btn-wood btn-sm" data-more-toggle aria-haspopup="true" aria-expanded="false">Presets &#9662;</button>
        <div class="sc-more-menu" hidden>
          ${GAME_MODE_PRESETS.map(p =>
            `<button type="button" class="sc-more-item" data-act="mode-preset" data-preset="${esc(p.key)}" title="${esc(p.summary)}">${esc(p.label)}</button>`
          ).join('')}
        </div>
      </div>`;
    }
    return `<div class="gm-empty-presets">
      <div class="gm-empty-presets-head">Quick-start presets</div>
      <div class="gm-preset-grid">
        ${GAME_MODE_PRESETS.map(p => `
          <button type="button" class="gm-preset-card" data-act="mode-preset" data-preset="${esc(p.key)}">
            <span class="gm-preset-title">${esc(p.label)}</span>
            <span class="gm-preset-sub">${esc(p.summary)}</span>
          </button>`).join('')}
      </div>
    </div>`;
  }

  function renderGameModeCard(m, i) {
    const item = (m && typeof m === 'object') ? m : {};
    const id = typeof item.id === 'string' ? item.id : '';
    const title = (typeof item.name === 'string' && item.name) ? item.name : (id || '(unnamed)');
    const desc = typeof item.description === 'string' ? item.description.trim() : '';
    const oneTeam = typeof item.teams === 'string' && item.teams.toLowerCase() === 'all';
    const enemy = item.enemy_player === true;
    const revealMap = item.reveal_map === true;
    const noAutoDefeat = item.no_auto_defeat === true;
    const pcRaw = (typeof item.player_count === 'number' && Number.isFinite(item.player_count)) ? Math.trunc(item.player_count) : 0;
    const playerCount = (pcRaw >= 2 && pcRaw <= 8) ? pcRaw : 0;
    const scriptEv = item.script_event !== false;
    const mp = (item.map && typeof item.map === 'object' && !Array.isArray(item.map)) ? item.map : null;
    const rawMapScript = mp && typeof mp.script === 'string' ? mp.script.trim() : '';
    const normMapScript = rawMapScript.replace(/\\/g, '/').replace(/^\.\//, '');
    const hasMapScript = !!normMapScript;
    const mapTreeFile = hasMapScript ? treeList().find(f => f.path.toLowerCase() === normMapScript.toLowerCase()) : null;

    // 1. Players & Teams
    const ptParts = [];
    if (playerCount) ptParts.push(`${playerCount} Players`);
    if (oneTeam) ptParts.push('All One Team');
    if (enemy) ptParts.push('Enemy AI');
    const ptText = ptParts.length ? ptParts.join(' · ') : 'Standard Lobby';

    // 2. Visibility & Rules
    const ruleParts = [];
    if (revealMap) ruleParts.push('All Visible (No Fog)');
    if (noAutoDefeat) ruleParts.push('No Auto-Defeat');
    if (!scriptEv) ruleParts.push('No Script Event');
    const rulesText = ruleParts.length ? ruleParts.join(' + ') : 'Standard Rules';

    // 3. Map
    let mapHtml = '<span class="gm-spec-muted">Lobby default</span>';
    if (mp) {
      const mapName = hasMapScript ? basename(normMapScript) : 'Custom map';
      const metaParts = [];
      if (mp.base) metaParts.push('base: ' + mp.base);
      if (mp.min_size) metaParts.push('min: ' + mp.min_size);
      if (mp.lock || item.map_lock) metaParts.push('lobby locked');
      const missingNote = (hasMapScript && !mapTreeFile)
        ? '<span class="gm-spec-warn">Not created yet — click RMS Map</span>'
        : '';
      mapHtml = `<span class="gm-spec-primary mono" title="${esc(normMapScript || mapName)}">${esc(mapName)}</span>`
        + (metaParts.length ? `<span class="gm-spec-sub">(${esc(metaParts.join(' · '))})</span>` : '')
        + missingNote;
    }

    // 4. Mode Resources
    const modeRes = modeResourcesFor(id);
    const visRes = modeRes.filter(r => r.hidden !== true);
    const hidRes = modeRes.filter(r => r.hidden === true);
    let resHtml = '<span class="gm-spec-muted">None</span>';
    if (modeRes.length > 0) {
      const allNames = modeRes.map(r => r.name || r.id).join(', ');
      if (visRes.length > 0) {
        const visNames = visRes.map(r => r.name || r.id).join(', ');
        resHtml = `<span class="gm-spec-primary" title="${esc(allNames)}">${esc(visNames)}</span>`
          + (hidRes.length ? `<span class="gm-spec-sub">+${hidRes.length} internal</span>` : '');
      } else {
        resHtml = `<span class="gm-spec-sub" title="${esc(allNames)}">${hidRes.length} internal (${esc(allNames)})</span>`;
      }
    }

    const idChip = id
      ? `<button type="button" class="gm-id-chip" data-act="mode-copy-id" data-idx="${i}" title="Copy: Game.Mode() == &quot;${esc(id)}&quot;">
          <code>${esc(id)}</code>${COPY_SVG}
        </button>`
      : '<span class="gm-id-chip missing">missing id</span>';

    return `<article class="gm-card" data-idx="${i}">
      <header class="gm-card-head">
        <div class="gm-card-identity">
          <div class="gm-icon" aria-hidden="true">${NAV_ICONS.game_modes}</div>
          <div class="gm-title-block">
            <div class="gm-title-row">
              <h3 class="gm-title">${esc(title)}</h3>
              ${idChip}
              ${!scriptEv ? '<span class="pill muted" title="script_event is disabled">Manual</span>' : ''}
            </div>
          </div>
        </div>
        <div class="gm-card-actions">
          <button type="button" class="btn btn-wood btn-xs" data-act="mode-edit" data-idx="${i}">Edit</button>
          ${hasMapScript ? `<button type="button" class="btn btn-wood btn-xs${!mapTreeFile ? ' gm-btn-rms-new' : ''}" data-act="mode-rms" data-idx="${i}" title="${mapTreeFile ? 'View or edit custom .rms map script' : 'Create custom .rms map script'}">RMS Map</button>` : ''}
          <div class="sc-more-wrap">
            <button type="button" class="btn btn-ghost btn-xs" data-more-toggle aria-haspopup="true" aria-expanded="false" title="More actions">&#8943;</button>
            <div class="sc-more-menu" hidden>
              <button type="button" class="sc-more-item" data-act="mode-duplicate" data-idx="${i}">Duplicate</button>
              <button type="button" class="sc-more-item" data-act="mode-copy-lua" data-idx="${i}">Copy Lua starter</button>
              <button type="button" class="sc-more-item danger" data-act="mode-delete" data-idx="${i}">Delete</button>
            </div>
          </div>
        </div>
      </header>
      ${desc ? `<p class="gm-desc" title="${esc(desc)}">${esc(desc)}</p>` : ''}
      <div class="gm-specs">
        <div class="gm-spec">
          <span class="gm-spec-label">Players &amp; Teams</span>
          <span class="gm-spec-val">${ptParts.length ? `<span class="gm-spec-primary">${esc(ptText)}</span>` : `<span class="gm-spec-muted">${esc(ptText)}</span>`}</span>
        </div>
        <div class="gm-spec">
          <span class="gm-spec-label">Visibility &amp; Rules</span>
          <span class="gm-spec-val">${ruleParts.length ? `<span class="gm-spec-primary">${esc(rulesText)}</span>` : `<span class="gm-spec-muted">${esc(rulesText)}</span>`}</span>
        </div>
        <div class="gm-spec">
          <span class="gm-spec-label">Map</span>
          <span class="gm-spec-val">${mapHtml}</span>
        </div>
        <div class="gm-spec">
          <span class="gm-spec-label">Mode Resources</span>
          <span class="gm-spec-val">${resHtml}</span>
        </div>
      </div>
    </article>`;
  }

  function renderGameModes(body) {
    const modes = modList('game_modes');
    if (modes.length === 0) {
      body.innerHTML = `
        <div class="empty-card gm-empty-card">
          <div class="empty-card-icon">${NAV_ICONS.game_modes}</div>
          <h3 class="empty-card-title">No game modes yet</h3>
          <p class="empty-card-text">Add a custom lobby game mode to appear in the Game Mode dropdown and run your Lua rules.</p>
          <div class="empty-card-actions"><button class="btn btn-save btn-sm" data-act="mode-add">Add game mode</button></div>
          ${renderQuickPresetsBar(false)}
        </div>`;
      return;
    }
    body.innerHTML = `
      <div class="list-head gm-list-head">
        <span><strong>${modes.length}</strong> game mode${modes.length === 1 ? '' : 's'}</span>
        <span class="gm-head-hint">Click any ID chip to copy its <code class="mono">Game.Mode()</code> check</span>
      </div>
      <div class="gm-cards">
        ${modes.map((m, i) => renderGameModeCard(m, i)).join('')}
      </div>
      <p class="list-note">Mode rules are written in a Lua script that checks <code class="mono">Game.Mode() == "&lt;id&gt;"</code> inside <code class="mono">Game.OnStart</code>. Toggling a mode mod in game requires restarting AoE2 HD.</p>`;
  }

  function openPresetDialog(presetKey) {
    const preset = GAME_MODE_PRESETS.find(p => p.key === presetKey);
    if (!preset) {
      gameModeDialog(null);
      return;
    }
    const list = modList('game_modes');
    const pre = JSON.parse(JSON.stringify(preset.defaults));
    pre.id = uniqueModeId(pre.id, list, '');
    pre.name = uniqueModeName(pre.name, list, false);
    gameModeDialog(null, pre);
  }

  function duplicateGameMode(idx) {
    const mod = modOf();
    const list = modList('game_modes');
    if (!mod || !Number.isInteger(idx) || idx < 0 || idx >= list.length) return;
    const orig = (list[idx] && typeof list[idx] === 'object') ? list[idx] : {};
    const clone = JSON.parse(JSON.stringify(orig));
    const origId = typeof orig.id === 'string' ? orig.id : '';
    const origTitle = (typeof orig.name === 'string' && orig.name) ? orig.name : (origId || 'Mode');
    clone.id = uniqueModeId(origId || 'mode', list, 'copy');
    clone.name = uniqueModeName(origTitle, list, true);
    mod.game_modes.splice(idx + 1, 0, clone);
    afterModJsonChange();
    S.showToast(`Duplicated "${origTitle}" as ${clone.id}`, 'success');
  }

  function copyModeLua(idx) {
    const list = modList('game_modes');
    if (!Number.isInteger(idx) || idx < 0 || idx >= list.length) return;
    const item = (list[idx] && typeof list[idx] === 'object') ? list[idx] : {};
    const snippet = buildModeLuaBoilerplate(item);
    const label = item.name || item.id || 'mode';
    copyToClipboard(snippet, `Copied Lua starter for "${label}"`);
  }

  function copyModeId(idx) {
    const list = modList('game_modes');
    if (!Number.isInteger(idx) || idx < 0 || idx >= list.length) return;
    const item = (list[idx] && typeof list[idx] === 'object') ? list[idx] : {};
    const id = typeof item.id === 'string' ? item.id : '';
    if (!id) {
      S.showToast('This game mode has no ID set yet.', 'error');
      return;
    }
    const expr = `Game.Mode() == "${luaStringLit(id)}"`;
    copyToClipboard(expr, `Copied: ${expr}`);
  }

  function checkRmsFilePath(path) {
    if (!path) return 'No map script path configured for this game mode.';
    if (path.length > 200) return 'Map script path exceeds 200 characters.';
    if (path.includes('..') || path === 'mod.json' || /^civs(\/|$)/i.test(path)) {
      return 'Map script path must be a relative path inside the mod folder (not mod.json or civs/).';
    }
    const segs = path.split('/');
    if (segs.length > 6 || segs.some(s => !s || !/^[A-Za-z0-9_.\- ]+$/.test(s))) {
      return 'Map script path may use 1..6 folders/segments with letters, digits, spaces, ".", "-", and "_".';
    }
    return '';
  }

  function rmsStarterTemplate(item) {
    const title = (item && typeof item.name === 'string' && item.name) ? item.name : (item && item.id ? item.id : 'Custom Mode Map');
    const mp = (item && item.map && typeof item.map === 'object') ? item.map : {};
    const base = mp.base || 'arena';
    return `/* ************ ${title.toUpperCase()} ************ */
/* Custom random map script for game mode "${item && item.id ? item.id : 'custom_mode'}" (fallback base: ${base}). */

<PLAYER_SETUP>
  random_placement

<LAND_GENERATION>
base_terrain GRASS

create_player_lands
{
  terrain_type                  GRASS3
  land_percent                  35
  base_size                     12
  border_fuzziness              12
  other_zone_avoidance_distance 4
}

<ELEVATION_GENERATION>
create_elevation 4
{
  base_terrain                  GRASS
  number_of_clumps              10
  number_of_tiles               400
  set_scale_by_groups
  set_scale_by_size
}

<TERRAIN_GENERATION>
create_terrain FOREST
{
  base_terrain                  GRASS
  spacing_to_other_terrain_types 5
  land_percent                  12
  number_of_clumps              14
  set_avoid_player_start_areas
  set_scale_by_groups
}

create_terrain GRASS2
{
  base_terrain                  GRASS
  number_of_clumps              18
  spacing_to_other_terrain_types 1
  land_percent                  6
  set_scale_by_size
}

<OBJECTS_GENERATION>
create_object TOWN_CENTER
{
  set_place_for_every_player
  group_placement_radius        18
  min_distance_to_players       0
  max_distance_to_players       0
}

create_object VILLAGER
{
  set_place_for_every_player
  min_distance_to_players       6
  max_distance_to_players       6
}

create_object SCOUT
{
  number_of_objects             1
  set_place_for_every_player
  min_distance_to_players       7
  max_distance_to_players       9
}

create_object OAKTREE
{
  number_of_objects             10
  group_placement_radius        3
  set_tight_grouping
  set_gaia_object_only
  set_place_for_every_player
  min_distance_to_players       8
  max_distance_to_players       11
}

create_object FORAGE
{
  number_of_objects             6
  group_placement_radius        3
  set_tight_grouping
  set_gaia_object_only
  set_place_for_every_player
  min_distance_to_players       8
  max_distance_to_players       10
}

create_object SHEEP
{
  number_of_objects             6
  set_loose_grouping
  set_gaia_object_only
  set_place_for_every_player
  min_distance_to_players       6
  max_distance_to_players       9
}

create_object GOLD
{
  number_of_objects             7
  group_placement_radius        3
  set_tight_grouping
  set_gaia_object_only
  set_place_for_every_player
  min_distance_to_players       10
  max_distance_to_players       13
}

create_object STONE
{
  number_of_objects             5
  group_placement_radius        2
  set_tight_grouping
  set_gaia_object_only
  set_place_for_every_player
  min_distance_to_players       10
  max_distance_to_players       13
}
`;
  }

  let rmsLoading = false;
  async function editRmsDialog(idx) {
    if (rmsLoading) return;
    const list = modList('game_modes');
    if (!Number.isInteger(idx) || idx < 0 || idx >= list.length) return;
    const item = (list[idx] && typeof list[idx] === 'object') ? list[idx] : {};
    const mp = (item.map && typeof item.map === 'object' && !Array.isArray(item.map)) ? item.map : null;
    const rawPath = mp && typeof mp.script === 'string' ? mp.script.trim().replace(/\\/g, '/').replace(/^\.\//, '') : '';
    const bad = checkRmsFilePath(rawPath);
    if (bad) {
      S.showToast(bad, 'error');
      return;
    }

    const existingEntry = treeList().find(f => f.path.toLowerCase() === rawPath.toLowerCase());
    const targetPath = existingEntry ? existingEntry.path : rawPath;
    let isNew = !existingEntry;
    let initialText = '';

    rmsLoading = true;
    try {
      if (existingEntry) {
        try {
          initialText = await readText(targetPath);
        } catch (err) {
          isNew = true;
        }
      }
      if (isNew) {
        initialText = rmsStarterTemplate(item);
      }
    } catch (e) {
      S.showToast('Could not read RMS file: ' + (e.message || e), 'error');
      return;
    } finally {
      rmsLoading = false;
    }

    const starter = rmsStarterTemplate(item);
    openDialog({
      title: isNew ? `Create RMS Map — ${item.name || item.id || 'Mode'}` : `Edit RMS Map — ${item.name || item.id || 'Mode'}`,
      width: 'xl',
      body: `
        <div class="rms-meta-bar">
          <div class="rms-meta-left">
            <code class="rms-path mono">${esc(targetPath)}</code>
            <span class="pill ${isNew ? 'warn' : 'ok'}" data-role="rms-status">${isNew ? 'New file — starter template' : 'Existing file'}</span>
          </div>
          <button type="button" class="btn btn-wood btn-xs" data-role="rms-template">Insert starter template</button>
        </div>
        <textarea class="rms-editor-input" data-role="rms-input" spellcheck="false" wrap="off" autocapitalize="off" autocomplete="off" autocorrect="off" aria-label="RMS script source">${esc(initialText)}</textarea>
        <div class="rms-foot-bar">
          <span class="mono" data-role="rms-pos">Ln 1, Col 1</span>
          <span class="rms-dirty" data-role="rms-dirty"></span>
        </div>
        <p class="form-error" role="alert"></p>`,
      buttons: [
        { label: 'Cancel' },
        {
          label: isNew ? 'Create RMS file' : 'Save RMS file',
          primary: true,
          onClick: async (dialog) => {
            const ta = dialog.querySelector('[data-role="rms-input"]');
            const text = ta ? ta.value : '';
            try {
              await writeFile(targetPath, text);
            } catch (e) {
              return dialogError(dialog, e.message || 'Could not save RMS file.'), false;
            }
            S.showToast(`Saved ${basename(targetPath)}`, 'success');
            await afterFileChange(targetPath);
            return true;
          }
        }
      ],
      onOpen: (dialog) => {
        const ta = dialog.querySelector('[data-role="rms-input"]');
        const posEl = dialog.querySelector('[data-role="rms-pos"]');
        const dirtyEl = dialog.querySelector('[data-role="rms-dirty"]');
        const tplBtn = dialog.querySelector('[data-role="rms-template"]');
        let confirmReset = false;

        const updateMeta = () => {
          const val = ta.value;
          const before = val.slice(0, ta.selectionStart);
          const ln = before.split('\n').length;
          const col = before.length - before.lastIndexOf('\n');
          const totalLines = val.split('\n').length;
          posEl.textContent = `Ln ${ln}, Col ${col} · ${totalLines} lines`;
          if (isNew) {
            dirtyEl.textContent = 'Not saved on disk yet';
            dirtyEl.className = 'rms-dirty warn';
          } else if (val !== initialText) {
            dirtyEl.textContent = 'Unsaved edits';
            dirtyEl.className = 'rms-dirty dirty';
          } else {
            dirtyEl.textContent = 'Matches disk · Tab inserts 2 spaces';
            dirtyEl.className = 'rms-dirty';
          }
        };

        ta.addEventListener('input', () => {
          confirmReset = false;
          tplBtn.textContent = 'Insert starter template';
          updateMeta();
        });
        ta.addEventListener('keyup', updateMeta);
        ta.addEventListener('click', updateMeta);
        ta.addEventListener('keydown', (e) => {
          if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
            e.preventDefault();
            e.stopPropagation();
            const saveBtn = dialog.querySelector('.dialog-foot [data-i="1"]');
            if (saveBtn) saveBtn.click();
            return;
          }
          if (e.key === 'Tab' && !e.ctrlKey && !e.metaKey && !e.altKey) {
            e.preventDefault();
            if (e.shiftKey) {
              const before = ta.value.slice(0, ta.selectionStart);
              const lineStart = before.lastIndexOf('\n') + 1;
              const spaces = (/^ {1,2}/.exec(ta.value.slice(lineStart)) || [''])[0];
              if (spaces) {
                ta.setSelectionRange(lineStart, lineStart + spaces.length);
                insertText(ta, '');
                ta.dispatchEvent(new Event('input'));
              }
            } else {
              insertText(ta, '  ');
              ta.dispatchEvent(new Event('input'));
            }
          } else if (e.key === 'Enter' && !e.ctrlKey && !e.metaKey) {
            const before = ta.value.slice(0, ta.selectionStart);
            const lineStart = before.lastIndexOf('\n') + 1;
            const indent = (/^[ \t]*/.exec(before.slice(lineStart)) || [''])[0];
            e.preventDefault();
            insertText(ta, '\n' + indent);
            ta.dispatchEvent(new Event('input'));
          }
        });

        tplBtn.addEventListener('click', () => {
          if (ta.value.trim() && ta.value !== starter && !confirmReset) {
            confirmReset = true;
            tplBtn.textContent = 'Click again to replace text';
            return;
          }
          confirmReset = false;
          tplBtn.textContent = 'Insert starter template';
          ta.value = starter;
          ta.setSelectionRange(0, 0);
          updateMeta();
          ta.focus();
        });

        updateMeta();
        setTimeout(() => ta.focus(), 30);
      }
    });
  }

  function gameModeDialog(editIdx, preset) {
    const mod = modOf();
    if (!mod) return;
    const list = modList('game_modes');
    const isEdit = Number.isInteger(editIdx) && editIdx >= 0 && editIdx < list.length;
    const cur = isEdit && list[editIdx] && typeof list[editIdx] === 'object' ? list[editIdx] : null;
    const seed = cur || (preset && typeof preset === 'object' ? preset : null);

    const curId = seed && typeof seed.id === 'string' ? seed.id : '';
    const curName = seed && typeof seed.name === 'string' ? seed.name : '';
    const curDesc = seed && typeof seed.description === 'string' ? seed.description : '';
    const curTeamsAll = !!(seed && typeof seed.teams === 'string' && seed.teams.toLowerCase() === 'all');
    const curEnemy = !!(seed && seed.enemy_player === true);
    const curRevealMap = !!(seed && seed.reveal_map === true);
    const curNoAutoDefeat = !!(seed && seed.no_auto_defeat === true);
    const curPcRaw = seed && typeof seed.player_count === 'number' && Number.isFinite(seed.player_count) ? Math.trunc(seed.player_count) : 0;
    const curPlayerCount = (curPcRaw >= 2 && curPcRaw <= 8) ? curPcRaw : 0;
    const curScriptEv = !seed || seed.script_event !== false;
    const curMap = (seed && seed.map && typeof seed.map === 'object' && !Array.isArray(seed.map)) ? seed.map : null;
    const curMapScript = curMap && typeof curMap.script === 'string' ? curMap.script : '';
    const curMapBase = curMap && typeof curMap.base === 'string' ? curMap.base.toLowerCase() : 'arena';
    const curMapMin = curMap && typeof curMap.min_size === 'string' ? curMap.min_size.toLowerCase() : '';
    const curMapLock = !!(curMap && curMap.lock === true) || !!(seed && seed.map_lock === true);

    const rms = rmsFiles();
    const baseOpts = [...MAP_BASES];
    if (curMap && curMap.base && !baseOpts.some(b => b.id === curMapBase)) {
      baseOpts.push({ id: String(curMap.base), label: String(curMap.base) });
    }
    const sizeOpts = [...MAP_SIZES];
    if (curMap && curMap.min_size && !sizeOpts.some(z => z.id === curMapMin)) {
      sizeOpts.push({ id: String(curMap.min_size), label: String(curMap.min_size) });
    }

    openDialog({
      title: isEdit ? 'Edit game mode' : (preset ? `New game mode — ${curName || 'Preset'}` : 'Add game mode'),
      width: 'lg',
      body: `
        <div class="gm-tabs" role="tablist">
          <button type="button" class="gm-tab active" role="tab" data-gm-tab="identity">Identity</button>
          <button type="button" class="gm-tab" role="tab" data-gm-tab="rules">Lobby &amp; Rules</button>
          <button type="button" class="gm-tab" role="tab" data-gm-tab="map">Map</button>
        </div>
        <section class="gm-form-section" data-gm-pane="identity">
          <div class="gm-form-sec-head">
            <h4 class="gm-form-sec-title">Identity</h4>
            <code class="gm-form-sec-chip mono" data-role="mode-hint">Game.Mode() == "${esc(curId || 'my_mode')}"</code>
          </div>
          <div class="gm-form-sec-body">
            <div class="form-grid-2">
              <div class="form-field">
                <label for="gm-name">Display name</label>
                <input id="gm-name" type="text" class="form-input" placeholder="e.g. Last Stand" value="${esc(curName)}" autocomplete="off">
              </div>
              <div class="form-field">
                <label for="gm-id">ID <span class="muted">lowercase, digits, _</span></label>
                <input id="gm-id" type="text" class="form-input mono" maxlength="63" placeholder="e.g. last_stand" value="${esc(curId)}" autocomplete="off" spellcheck="false">
              </div>
            </div>
            <div class="form-field gm-field-last">
              <label for="gm-desc">Description <span class="muted">shown on lobby hover</span></label>
              <textarea id="gm-desc" class="form-textarea" rows="3" placeholder="Explain how the mode plays and how to win...">${esc(curDesc)}</textarea>
            </div>
          </div>
        </section>

        <section class="gm-form-section" data-gm-pane="rules" hidden>
          <div class="gm-form-sec-head">
            <h4 class="gm-form-sec-title">Lobby &amp; Rules</h4>
            <span class="gm-form-sec-sub">Match setup &amp; Lua flags</span>
          </div>
          <div class="gm-form-sec-body">
            <div class="form-field">
              <label for="gm-player-count">Player count <span class="muted mono">player_count</span></label>
              <select id="gm-player-count" class="form-select">
                <option value="0" ${curPlayerCount === 0 ? 'selected' : ''}>Unchanged (use lobby setting)</option>
                ${[2, 3, 4, 5, 6, 7, 8].map(n => `<option value="${n}" ${curPlayerCount === n ? 'selected' : ''}>${n} players</option>`).join('')}
              </select>
              <p class="form-hint gm-hint-tight">Lock SP lobbies to 2&ndash;8 players. With 2 players + Enemy AI, P1 is Team 1 and P2 is Team 2.</p>
            </div>
            <div class="gm-rule-grid">
              <label class="gm-rule-card">
                <input type="checkbox" id="gm-script-event" ${curScriptEv ? 'checked' : ''}>
                <span class="form-check-text">
                  <span>Script driven <span class="muted mono">script_event</span></span>
                  <span class="form-check-sub">Fires Lua <code class="mono">Game.OnStart</code> rules (default on).</span>
                </span>
              </label>
              <label class="gm-rule-card">
                <input type="checkbox" id="gm-teams-all" ${curTeamsAll ? 'checked' : ''}>
                <span class="form-check-text">
                  <span>All on one team <span class="muted mono">teams: "all"</span></span>
                  <span class="form-check-sub">Locks every player onto one shared team (SP).</span>
                </span>
              </label>
              <label class="gm-rule-card">
                <input type="checkbox" id="gm-enemy-player" ${curEnemy ? 'checked' : ''}>
                <span class="form-check-text">
                  <span>Enemy player <span class="muted mono">enemy_player</span></span>
                  <span class="form-check-sub">Adds an extra AI opponent via <code class="mono">Game.ModeEnemy()</code>.</span>
                </span>
              </label>
              <label class="gm-rule-card">
                <input type="checkbox" id="gm-reveal-map" ${curRevealMap ? 'checked' : ''}>
                <span class="form-check-text">
                  <span>Reveal whole map <span class="muted mono">reveal_map</span></span>
                  <span class="form-check-sub">Starts with All Visible (no fog of war).</span>
                </span>
              </label>
              <label class="gm-rule-card gm-rule-span">
                <input type="checkbox" id="gm-no-auto-defeat" ${curNoAutoDefeat ? 'checked' : ''}>
                <span class="form-check-text">
                  <span>Disable auto-defeat <span class="muted mono">no_auto_defeat</span></span>
                  <span class="form-check-sub">Prevents automatic defeat with 0 units/buildings; Lua <code class="mono">p:Defeat()</code> still works.</span>
                </span>
              </label>
            </div>
          </div>
        </section>

        <section class="gm-form-section" data-gm-pane="map" hidden>
          <div class="gm-form-sec-head">
            <h4 class="gm-form-sec-title">Custom Map</h4>
            <label class="gm-inline-toggle">
              <input type="checkbox" id="gm-own-map" ${curMap ? 'checked' : ''}>
              <span>Lock lobby to custom <code class="mono">.rms</code></span>
            </label>
          </div>
          <div class="gm-form-sec-body" data-role="map-fields" ${curMap ? '' : 'hidden'}>
            <div class="form-field">
              <label for="gm-map-script">Map script (.rms)</label>
              <div class="picker-row">
                <input id="gm-map-script" type="text" class="form-input mono" placeholder="resources/_common/random-map-scripts/MyMap.rms" value="${esc(curMapScript)}" autocomplete="off" spellcheck="false">
                ${rms.length ? `<select id="gm-map-pick" class="form-select" aria-label="Pick a map script from this mod">
                  <option value="">Pick .rms from mod...</option>
                  ${rms.map(f => `<option value="${esc(f.path)}" ${f.path === curMapScript ? 'selected' : ''}>${esc(basename(f.path))}</option>`).join('')}
                </select>` : ''}
              </div>
              <p class="form-hint gm-hint-tight" data-role="map-script-hint"></p>
            </div>
            <div class="form-grid-2 gm-grid-last">
              <div class="form-field">
                <label for="gm-map-base">Base map <span class="muted">fallback</span></label>
                <select id="gm-map-base" class="form-select">
                  ${baseOpts.map(b => `<option value="${esc(b.id)}" ${b.id.toLowerCase() === curMapBase ? 'selected' : ''}>${esc(b.label)}</option>`).join('')}
                </select>
              </div>
              <div class="form-field">
                <label for="gm-map-minsize" data-role="map-size-label">${curMapLock ? 'Locked map size' : 'Minimum map size'}</label>
                <select id="gm-map-minsize" class="form-select">
                  ${sizeOpts.map(z => `<option value="${esc(z.id)}" ${z.id.toLowerCase() === curMapMin ? 'selected' : ''}>${esc(z.label)}</option>`).join('')}
                </select>
              </div>
            </div>
            <label class="gm-rule-card" style="margin-top:8px">
              <input type="checkbox" id="gm-map-lock" ${curMapLock ? 'checked' : ''}>
              <span class="form-check-text">
                <span>Hide map &amp; size in lobby <span class="muted mono">map.lock</span></span>
                <span class="form-check-sub">Hides Map Style and Location in the lobby (and Map Size when a minimum size is set).</span>
              </span>
            </label>
          </div>
          <p class="gm-map-off-hint" data-role="map-off-hint" ${curMap ? 'hidden' : ''}>Uses whichever map the player picks in the lobby.</p>
        </section>

        <p class="form-error" role="alert"></p>`,
      buttons: [
        { label: 'Cancel' },
        {
          label: isEdit ? 'Save changes' : 'Add game mode',
          primary: true,
          onClick: (dialog) => {
            const id = dialog.querySelector('#gm-id').value.trim();
            const name = dialog.querySelector('#gm-name').value.trim();
            const desc = dialog.querySelector('#gm-desc').value;
            const scriptEv = dialog.querySelector('#gm-script-event').checked;
            const teamsAll = dialog.querySelector('#gm-teams-all').checked;
            const enemy = dialog.querySelector('#gm-enemy-player').checked;
            const revealMap = dialog.querySelector('#gm-reveal-map').checked;
            const noAutoDefeat = dialog.querySelector('#gm-no-auto-defeat').checked;
            const playerCount = Number(dialog.querySelector('#gm-player-count').value);
            const ownMap = dialog.querySelector('#gm-own-map').checked;
            const mapScript = dialog.querySelector('#gm-map-script').value.trim().replace(/\\/g, '/');
            const mapBase = dialog.querySelector('#gm-map-base').value;
            const mapMin = dialog.querySelector('#gm-map-minsize').value;
            const mapLock = dialog.querySelector('#gm-map-lock').checked;

            if (!id) return dialogError(dialog, 'Enter a mode ID.'), false;
            if (id.length > 63) return dialogError(dialog, 'The mode ID must be at most 63 characters.'), false;
            if (!GAME_MODE_ID_RULE.test(id)) return dialogError(dialog, 'The mode ID may use lowercase letters, digits and "_" only.'), false;
            const dup = list.some((m, idx) => idx !== editIdx && m && typeof m.id === 'string' && m.id === id);
            if (dup) return dialogError(dialog, `A game mode with ID "${id}" already exists in this mod.`), false;

            if (!Number.isInteger(playerCount) || (playerCount !== 0 && (playerCount < 2 || playerCount > 8))) {
              return dialogError(dialog, 'Player count must be 0 (unchanged) or between 2 and 8.'), false;
            }

            if (ownMap) {
              if (!mapScript) return dialogError(dialog, 'Pick or type a map script path, or turn off "Lock lobby to custom .rms".'), false;
              const why = checkRelPath(mapScript, 'Map script path');
              if (why) return dialogError(dialog, why), false;
              if (!mapBase) return dialogError(dialog, 'Choose a base map.'), false;
            }

            const entry = cur ? { ...cur } : {};
            entry.id = id;
            if (name) entry.name = name;
            else delete entry.name;
            if (desc.trim()) entry.description = desc;
            else if (!cur || cur.description !== '') delete entry.description;
            entry.script_event = scriptEv;
            if (teamsAll) {
              entry.teams = 'all';
            } else if (typeof entry.teams === 'string' && entry.teams.toLowerCase() === 'all') {
              delete entry.teams;
            }
            if (enemy) {
              entry.enemy_player = true;
            } else if (entry.enemy_player === true) {
              delete entry.enemy_player;
            }
            if (revealMap) {
              entry.reveal_map = true;
            } else if (entry.reveal_map === true) {
              delete entry.reveal_map;
            }
            if (noAutoDefeat) {
              entry.no_auto_defeat = true;
            } else if (entry.no_auto_defeat === true) {
              delete entry.no_auto_defeat;
            }
            if (playerCount >= 2 && playerCount <= 8) {
              entry.player_count = playerCount;
            } else if (Object.prototype.hasOwnProperty.call(entry, 'player_count')) {
              delete entry.player_count;
            }
            if (ownMap) {
              const mp = curMap ? { ...curMap } : {};
              mp.base = mapBase;
              if (mapMin) mp.min_size = mapMin;
              else delete mp.min_size;
              if (mapLock) mp.lock = true;
              else delete mp.lock;
              mp.script = mapScript;
              entry.map = mp;
            } else {
              delete entry.map;
            }
            delete entry.map_lock;

            if (!Array.isArray(mod.game_modes)) mod.game_modes = [];
            if (isEdit) mod.game_modes[editIdx] = entry;
            else mod.game_modes.push(entry);
            afterModJsonChange();
            return true;
          }
        }
      ],
      onOpen: (dialog) => {
        dialog.querySelectorAll('.gm-tab').forEach(tab => tab.addEventListener('click', () => {
          dialog.querySelectorAll('.gm-tab').forEach(t => t.classList.toggle('active', t === tab));
          dialog.querySelectorAll('[data-gm-pane]').forEach(p => { p.hidden = p.dataset.gmPane !== tab.dataset.gmTab; });
        }));
        const nameIn = dialog.querySelector('#gm-name');
        const idIn = dialog.querySelector('#gm-id');
        const hintEl = dialog.querySelector('[data-role="mode-hint"]');
        const ownMapChk = dialog.querySelector('#gm-own-map');
        const mapBody = dialog.querySelector('[data-role="map-fields"]');
        const mapOffHint = dialog.querySelector('[data-role="map-off-hint"]');
        const scriptIn = dialog.querySelector('#gm-map-script');
        const scriptPick = dialog.querySelector('#gm-map-pick');
        const scriptHint = dialog.querySelector('[data-role="map-script-hint"]');
        let idTouched = isEdit;

        const updateHint = () => {
          const id = idIn.value.trim() || 'my_mode';
          hintEl.textContent = `Game.Mode() == "${id}"`;
        };
        const updateScriptHint = () => {
          const val = scriptIn.value.trim().replace(/\\/g, '/');
          if (scriptPick) {
            const match = rms.some(f => f.path === val);
            scriptPick.value = match ? val : '';
          }
          if (!val) {
            scriptHint.textContent = rms.length
              ? 'Pick an .rms file from resources/_common/random-map-scripts/ or type a path.'
              : 'Type a relative .rms path (after saving, click RMS Map on the card to create it).';
          } else if (!treeList().some(f => f.path.toLowerCase() === val.toLowerCase())) {
            scriptHint.textContent = 'Not in mod folder yet — save the mode, then click RMS Map on the card to create it.';
          } else {
            scriptHint.textContent = 'Found in the mod folder.';
          }
        };

        nameIn.addEventListener('input', () => {
          if (!idTouched) {
            idIn.value = slugId(nameIn.value, true).slice(0, 63);
            updateHint();
          }
        });
        idIn.addEventListener('input', () => { idTouched = true; updateHint(); });
        ownMapChk.addEventListener('change', () => {
          mapBody.hidden = !ownMapChk.checked;
          if (mapOffHint) mapOffHint.hidden = ownMapChk.checked;
        });
        const mapLockChk = dialog.querySelector('#gm-map-lock');
        const mapSizeLbl = dialog.querySelector('[data-role="map-size-label"]');
        if (mapLockChk && mapSizeLbl) {
          mapLockChk.addEventListener('change', () => {
            mapSizeLbl.textContent = mapLockChk.checked ? 'Locked map size' : 'Minimum map size';
          });
        }
        scriptIn.addEventListener('input', updateScriptHint);
        if (scriptPick) {
          scriptPick.addEventListener('change', () => {
            if (scriptPick.value) {
              scriptIn.value = scriptPick.value;
              updateScriptHint();
            }
          });
        }
        updateHint();
        updateScriptHint();
        setTimeout(() => nameIn.focus(), 30);
      }
    });
  }

  function deleteGameMode(idx) {
    const mod = modOf();
    const list = modList('game_modes');
    if (!mod || idx < 0 || idx >= list.length) return;
    const item = list[idx] || {};
    const label = item.name || item.id || 'this game mode';
    S.showConfirm('Delete game mode', `Remove "${label}" from mod.json?`, 'Delete', () => {
      mod.game_modes.splice(idx, 1);
      if (mod.game_modes.length === 0 && !st.hadKey.game_modes) delete mod.game_modes;
      afterModJsonChange();
    });
  }

  // ==========================================================================
  // Section: Resources (mod.json "resources")
  // ==========================================================================
  const VANILLA_RES_NAMES = new Set(['food', 'wood', 'stone', 'gold']);
  const PLAIN_INT_RE = /^[+-]?\d+$/;

  function hydrateResourceIcons(container) {
    if (!container) return;
    container.querySelectorAll('[data-res-icon]').forEach(el => {
      const path = el.dataset.resIcon;
      if (!path) return;
      loadImgUrl(path).then(url => {
        if (url && el.isConnected && el.dataset.resIcon === path) {
          el.innerHTML = `<img src="${url}" alt="">`;
        }
      });
    });
  }

  function renderResources(body) {
    const resources = modList('resources');
    if (resources.length === 0) {
      body.innerHTML = `
        <div class="empty-card">
          <div class="empty-card-icon">${NAV_ICONS.resources}</div>
          <h3 class="empty-card-title">No custom resources yet</h3>
          <p class="empty-card-text">Declare custom resources for the top HUD bar, unit and tech costs, effects, and Lua scripts.</p>
          <div class="empty-card-actions"><button class="btn btn-save btn-sm" data-act="res-add">Add resource</button></div>
        </div>`;
      return;
    }
    body.innerHTML = `
      <div class="list-head">
        <span><strong>${resources.length}</strong> custom resource${resources.length === 1 ? '' : 's'}</span>
      </div>
      <div class="list">
        ${resources.map((r, i) => {
          const item = (r && typeof r === 'object') ? r : {};
          const id = typeof item.id === 'string' ? item.id : '';
          const title = (typeof item.name === 'string' && item.name) ? item.name : (id || '(unnamed)');
          const icon = typeof item.icon === 'string' ? item.icon : '';
          const start = (typeof item.start === 'number' && Number.isFinite(item.start)) ? item.start : 0;
          const max = (typeof item.max === 'number' && Number.isFinite(item.max)) ? item.max : 0;
          const show = item.show !== false;
          const hidden = item.hidden === true;
          const gmode = typeof item.game_mode === 'string' ? item.game_mode : '';
          const hasGather = !!(item.gather && typeof item.gather === 'object');
          const desc = typeof item.description === 'string' ? item.description : '';
          const letter = esc((title.trim().charAt(0) || 'R').toUpperCase());
          const metaParts = [id || 'missing id', `start ${start}`, max > 0 ? `max ${max}` : 'no cap'];
          if (icon) metaParts.push(icon);
          return `<div class="modmeta-row">
            <button type="button" class="modmeta-row-hit" data-act="res-edit" data-idx="${i}">
              <span class="res-icon-box" ${icon ? `data-res-icon="${esc(icon)}"` : ''}>${letter}</span>
              <span class="list-main">
                <span class="list-title">${esc(title)}</span>
                <span class="list-sub mono">${esc(metaParts.join(' · '))}</span>
                ${desc ? `<span class="modmeta-desc" title="${esc(desc)}">${esc(desc)}</span>` : ''}
              </span>
              <span class="list-tags">
                ${hasGather ? '<span class="pill">Gatherable</span>' : ''}
                ${gmode ? `<span class="pill">Mode: ${esc(gmode)}</span>` : ''}
                ${hidden ? '<span class="pill muted">Hidden</span>' : (show ? '<span class="pill ok">Top bar</span>' : '<span class="pill muted">Panel only</span>')}
              </span>
            </button>
            <span class="row-actions">
              <button type="button" class="btn btn-wood btn-sm" data-act="res-edit" data-idx="${i}">Edit</button>
              <button type="button" class="btn btn-ghost danger btn-sm" data-act="res-delete" data-idx="${i}">Delete</button>
            </span>
          </div>`;
        }).join('')}
      </div>
      <p class="list-note">Reference custom resources by ID in civ JSON costs and effects, in Visual Script nodes (<code class="mono">Get Resource</code> / <code class="mono">Add Custom Resource</code> / <code class="mono">Resource Deposit ID</code>), or in Lua.</p>`;
    hydrateResourceIcons(body);
  }

  function resourceDialog(editIdx) {
    const mod = modOf();
    if (!mod) return;
    const list = modList('resources');
    const isEdit = Number.isInteger(editIdx) && editIdx >= 0 && editIdx < list.length;
    const cur = isEdit && list[editIdx] && typeof list[editIdx] === 'object' ? list[editIdx] : null;

    const curId = cur && typeof cur.id === 'string' ? cur.id : '';
    const curName = cur && typeof cur.name === 'string' ? cur.name : '';
    const curIcon = cur && typeof cur.icon === 'string' ? cur.icon : '';
    const curStart = cur && typeof cur.start === 'number' && Number.isFinite(cur.start) ? cur.start : 0;
    const curMax = cur && typeof cur.max === 'number' && Number.isFinite(cur.max) ? cur.max : 0;
    const curShow = !cur || cur.show !== false;
    const curHidden = !!(cur && cur.hidden === true);
    const curGameMode = cur && typeof cur.game_mode === 'string' ? cur.game_mode : '';
    const curDesc = cur && typeof cur.description === 'string' ? cur.description : '';

    const curG = cur && cur.gather && typeof cur.gather === 'object' ? cur.gather : null;
    const gEnabled = !!curG;
    const gDepName = curG && typeof curG.deposit_name === 'string' ? curG.deposit_name : '';
    const gMinerName = curG && typeof curG.miner_name === 'string' ? curG.miner_name : '';
    const gAmount = curG && typeof curG.amount === 'number' ? curG.amount : 800;
    const gCopyFrom = curG && typeof curG.copy_from === 'number' ? curG.copy_from : 66;
    const gWorkRate = curG && typeof curG.work_rate === 'number' ? curG.work_rate : 0.45;
    const gCarry = curG && typeof curG.carry === 'number' ? curG.carry : 10;
    const gDropSites = curG && Array.isArray(curG.drop_sites) ? curG.drop_sites.join(', ') : '109, 584';
    const gMinimap = curG && typeof curG.minimap_color === 'number' ? String(curG.minimap_color) : '';
    const gDepSlp = curG && typeof curG.deposit_slp === 'number' ? String(curG.deposit_slp) : '';
    const gMaleSlp = curG && typeof curG.carry_male_slp === 'number' ? String(curG.carry_male_slp) : '';
    const gFemaleSlp = curG && typeof curG.carry_female_slp === 'number' ? String(curG.carry_female_slp) : '';

    const pngs = pngFiles();
    const modes = modList('game_modes').filter(m => m && typeof m.id === 'string' && m.id);
    const slpIds = treeList()
      .map(f => {
        const m = /^resources\/_common\/drs\/graphics\/(\d+)\.slp$/i.exec(f.path);
        return m ? m[1] : null;
      })
      .filter(Boolean);

    openDialog({
      title: isEdit ? 'Edit resource' : 'Add resource',
      width: 'lg',
      body: `
        <div class="form-grid-2">
          <div class="form-field">
            <label for="res-name">Display name</label>
            <input id="res-name" type="text" class="form-input" maxlength="128" placeholder="e.g. Faith" value="${esc(curName)}" autocomplete="off">
          </div>
          <div class="form-field">
            <label for="res-id">ID <span class="muted">unique key</span></label>
            <input id="res-id" type="text" class="form-input mono" maxlength="64" placeholder="e.g. faith" value="${esc(curId)}" autocomplete="off" spellcheck="false">
          </div>
        </div>
        <p class="form-hint">Short key used in civ JSON, visual scripts, and Lua (no <code class="mono">:</code>, tabs or line breaks; cannot be <code class="mono">food</code>, <code class="mono">wood</code>, <code class="mono">stone</code>, <code class="mono">gold</code> or a number).</p>
        <div class="form-field">
          <label for="res-icon">Icon PNG <span class="muted">relative path, optional</span></label>
          <div class="picker-row">
            <span class="res-icon-box" data-role="icon-preview">?</span>
            <input id="res-icon" type="text" class="form-input mono" maxlength="256" placeholder="icons/faith.png" value="${esc(curIcon)}" autocomplete="off" spellcheck="false">
            ${pngs.length ? `<select id="res-icon-pick" class="form-select" aria-label="Pick a PNG from this mod">
              <option value="">Pick PNG from mod...</option>
              ${pngs.map(f => `<option value="${esc(f.path)}" ${f.path === curIcon ? 'selected' : ''}>${esc(f.path)}</option>`).join('')}
            </select>` : ''}
            <button type="button" class="btn btn-wood btn-sm" id="res-icon-lib">Pick from library</button>
          </div>
        </div>
        <p class="form-hint" data-role="icon-hint"></p>
        <div class="form-grid-2">
          <div class="form-field">
            <label for="res-start">Starting amount</label>
            <input id="res-start" type="number" step="any" class="form-input" value="${esc(curStart)}">
          </div>
          <div class="form-field">
            <label for="res-max">Maximum cap <span class="muted">0 = no cap</span></label>
            <input id="res-max" type="number" min="0" step="any" class="form-input" value="${esc(curMax)}">
          </div>
        </div>
        <div class="form-field">
          <label for="res-gmode">Only show in game mode <span class="muted">optional mode ID</span></label>
          <div class="picker-row">
            <input id="res-gmode" type="text" class="form-input mono" maxlength="64" placeholder="Leave empty for all modes (e.g. last_stand)" value="${esc(curGameMode)}" autocomplete="off" spellcheck="false">
            ${modes.length ? `<select id="res-gmode-pick" class="form-select" aria-label="Pick a game mode from this mod">
              <option value="">All modes</option>
              ${modes.map(m => `<option value="${esc(m.id)}" ${m.id === curGameMode ? 'selected' : ''}>${esc(m.name || m.id)} (${esc(m.id)})</option>`).join('')}
            </select>` : ''}
          </div>
        </div>
        <div class="form-checks">
          <label class="form-check">
            <input type="checkbox" id="res-show" ${curShow ? 'checked' : ''}>
            <span class="form-check-text">
              <span>Show in top resource bar <span class="muted mono">show</span></span>
              <span class="form-check-sub">Uncheck to keep this resource inside the "Show all" panel only.</span>
            </span>
          </label>
          <label class="form-check">
            <input type="checkbox" id="res-hidden" ${curHidden ? 'checked' : ''}>
            <span class="form-check-text">
              <span>Hide completely from HUD <span class="muted mono">hidden</span></span>
              <span class="form-check-sub">Never shown in the top bar, +N badge, or "Show all" panel (for internal / save-game data).</span>
            </span>
          </label>
          <label class="form-check">
            <input type="checkbox" id="res-gather-on" ${gEnabled ? 'checked' : ''}>
            <span class="form-check-text">
              <span>Gatherable on map <span class="muted mono">gather</span></span>
              <span class="form-check-sub">Generates a Gaia map deposit and villager miners automatically — no dat editing needed.</span>
            </span>
          </label>
        </div>
        <div id="res-gather-box" class="form-subbox" ${gEnabled ? '' : 'hidden'} style="border:1px solid var(--border);border-radius:6px;padding:12px;margin:8px 0 12px;background:rgba(0,0,0,0.16);">
          <div class="form-grid-2">
            <div class="form-field">
              <label for="res-g-depname">Deposit name</label>
              <input id="res-g-depname" type="text" class="form-input" maxlength="128" placeholder="e.g. Crystal Deposit" value="${esc(gDepName)}">
            </div>
            <div class="form-field">
              <label for="res-g-minername">Miner name</label>
              <input id="res-g-minername" type="text" class="form-input" maxlength="128" placeholder="e.g. Crystal Miner" value="${esc(gMinerName)}">
            </div>
          </div>
          <div class="form-grid-2">
            <div class="form-field">
              <label for="res-g-amount">Deposit amount <span class="muted">1..65535</span></label>
              <input id="res-g-amount" type="number" min="1" max="65535" step="any" class="form-input" value="${esc(gAmount)}">
            </div>
            <div class="form-field">
              <label for="res-g-copy">Deposit template <span class="muted">unit ID</span></label>
              <div class="picker-row">
                <input id="res-g-copy" type="number" min="0" max="65535" class="form-input mono" value="${esc(gCopyFrom)}">
                <select id="res-g-copy-pick" class="form-select" aria-label="Deposit template preset">
                  <option value="66" ${gCopyFrom === 66 ? 'selected' : ''}>Gold Mine (66)</option>
                  <option value="102" ${gCopyFrom === 102 ? 'selected' : ''}>Stone Mine (102)</option>
                  <option value="59" ${gCopyFrom === 59 ? 'selected' : ''}>Forage Bush (59)</option>
                </select>
              </div>
            </div>
          </div>
          <div class="form-grid-2">
            <div class="form-field">
              <label for="res-g-rate">Gather speed <span class="muted">work_rate</span></label>
              <input id="res-g-rate" type="number" min="0.01" max="1000" step="any" class="form-input" value="${esc(gWorkRate)}">
            </div>
            <div class="form-field">
              <label for="res-g-carry">Carry capacity <span class="muted">1..32767</span></label>
              <input id="res-g-carry" type="number" min="1" max="32767" class="form-input" value="${esc(gCarry)}">
            </div>
          </div>
          <div class="form-grid-2">
            <div class="form-field">
              <label for="res-g-drops">Drop-off buildings <span class="muted">1 or 2 unit IDs</span></label>
              <input id="res-g-drops" type="text" class="form-input mono" placeholder="109, 584" value="${esc(gDropSites)}">
              <p class="form-hint">109 = Town Center, 584 = Mining Camp, 562 = Lumber Camp, 68 = Mill.</p>
            </div>
            <div class="form-field">
              <label for="res-g-mm">Minimap color <span class="muted">0..255, optional</span></label>
              <input id="res-g-mm" type="number" min="0" max="255" class="form-input mono" placeholder="e.g. 92" value="${esc(gMinimap)}">
            </div>
          </div>
          ${slpIds.length ? `<datalist id="res-slp-list">${slpIds.map(s => `<option value="${esc(s)}">`).join('')}</datalist>` : ''}
          <div class="form-grid-2" style="grid-template-columns:repeat(3,minmax(0,1fr));">
            <div class="form-field">
              <label for="res-g-depslp">Deposit SLP <span class="muted">optional</span></label>
              <input id="res-g-depslp" type="number" min="1" class="form-input mono" list="res-slp-list" placeholder="e.g. 90001" value="${esc(gDepSlp)}">
            </div>
            <div class="form-field">
              <label for="res-g-mslp">Male carry SLP <span class="muted">optional</span></label>
              <input id="res-g-mslp" type="number" min="1" class="form-input mono" list="res-slp-list" placeholder="e.g. 90002" value="${esc(gMaleSlp)}">
            </div>
            <div class="form-field">
              <label for="res-g-fslp">Female carry SLP <span class="muted">optional</span></label>
              <input id="res-g-fslp" type="number" min="1" class="form-input mono" list="res-slp-list" placeholder="e.g. 90003" value="${esc(gFemaleSlp)}">
            </div>
          </div>
        </div>
        <div class="form-field">
          <label for="res-desc">Description <span class="muted">tooltip on hover, optional</span></label>
          <textarea id="res-desc" class="form-textarea" rows="3" placeholder="Explain how players earn and spend this resource...">${esc(curDesc)}</textarea>
        </div>
        <p class="form-error" role="alert"></p>`,
      buttons: [
        { label: 'Cancel' },
        {
          label: isEdit ? 'Save changes' : 'Add resource',
          primary: true,
          onClick: (dialog) => {
            const id = dialog.querySelector('#res-id').value.trim();
            const name = dialog.querySelector('#res-name').value.trim();
            const icon = dialog.querySelector('#res-icon').value.trim();
            const startRaw = dialog.querySelector('#res-start').value.trim();
            const maxRaw = dialog.querySelector('#res-max').value.trim();
            const gmode = dialog.querySelector('#res-gmode').value.trim();
            const show = dialog.querySelector('#res-show').checked;
            const hidden = dialog.querySelector('#res-hidden').checked;
            const gatherOn = dialog.querySelector('#res-gather-on').checked;
            const desc = dialog.querySelector('#res-desc').value;

            if (!id) return dialogError(dialog, 'Enter a resource ID.'), false;
            if (id.length > 64) return dialogError(dialog, 'The resource ID must be at most 64 characters.'), false;
            if (/[:\t\r\n]/.test(id)) return dialogError(dialog, 'The resource ID cannot contain ":", tabs, or line breaks.'), false;
            if (VANILLA_RES_NAMES.has(id.toLowerCase()) || PLAIN_INT_RE.test(id)) {
              return dialogError(dialog, `ID "${id}" conflicts with vanilla or numeric resource IDs.`), false;
            }
            const dup = list.some((r, idx) => idx !== editIdx && r && typeof r.id === 'string' && r.id.toLowerCase() === id.toLowerCase());
            if (dup) return dialogError(dialog, `A resource with ID "${id}" already exists in this mod.`), false;

            if (name.length > 128 || /[\t\r\n]/.test(name)) {
              return dialogError(dialog, 'Display name must be at most 128 characters with no tabs or line breaks.'), false;
            }
            if (gmode.length > 64 || /[\t\r\n]/.test(gmode)) {
              return dialogError(dialog, 'Game mode ID must be at most 64 characters with no tabs or line breaks.'), false;
            }
            if (icon) {
              const why = checkRelPath(icon, 'Icon path');
              if (why) return dialogError(dialog, why), false;
            }

            const startVal = startRaw === '' ? 0 : Number(startRaw);
            if (!Number.isFinite(startVal)) return dialogError(dialog, 'Starting amount must be a valid number.'), false;
            const maxVal = maxRaw === '' ? 0 : Number(maxRaw);
            if (!Number.isFinite(maxVal) || maxVal < 0) return dialogError(dialog, 'Maximum cap must be 0 or greater.'), false;

            let gatherObj = null;
            if (gatherOn) {
              const depName = dialog.querySelector('#res-g-depname').value.trim();
              const minerName = dialog.querySelector('#res-g-minername').value.trim();
              const amtRaw = dialog.querySelector('#res-g-amount').value.trim();
              const copyRaw = dialog.querySelector('#res-g-copy').value.trim();
              const rateRaw = dialog.querySelector('#res-g-rate').value.trim();
              const carryRaw = dialog.querySelector('#res-g-carry').value.trim();
              const dropsRaw = dialog.querySelector('#res-g-drops').value.trim();
              const mmRaw = dialog.querySelector('#res-g-mm').value.trim();
              const depSlpRaw = dialog.querySelector('#res-g-depslp').value.trim();
              const mSlpRaw = dialog.querySelector('#res-g-mslp').value.trim();
              const fSlpRaw = dialog.querySelector('#res-g-fslp').value.trim();

              gatherObj = {};
              if (depName) gatherObj.deposit_name = depName;
              if (minerName) gatherObj.miner_name = minerName;
              if (amtRaw !== '') {
                const a = Number(amtRaw);
                if (!Number.isFinite(a) || a < 1 || a > 65535) return dialogError(dialog, 'Deposit amount must be 1..65535.'), false;
                gatherObj.amount = a;
              }
              if (copyRaw !== '') {
                const c = parseInt(copyRaw, 10);
                if (!Number.isInteger(c) || c < 0 || c > 65535) return dialogError(dialog, 'Deposit template must be a valid unit ID.'), false;
                gatherObj.copy_from = c;
              }
              if (rateRaw !== '') {
                const wr = Number(rateRaw);
                if (!Number.isFinite(wr) || wr <= 0 || wr > 1000) return dialogError(dialog, 'Gather speed must be > 0 and <= 1000.'), false;
                gatherObj.work_rate = wr;
              }
              if (carryRaw !== '') {
                const ca = parseInt(carryRaw, 10);
                if (!Number.isInteger(ca) || ca < 1 || ca > 32767) return dialogError(dialog, 'Carry capacity must be an integer 1..32767.'), false;
                gatherObj.carry = ca;
              }
              if (dropsRaw !== '') {
                const parts = dropsRaw.split(',').map(s => s.trim()).filter(Boolean);
                if (parts.length < 1 || parts.length > 2) return dialogError(dialog, 'Drop-off buildings must be 1 or 2 unit IDs separated by a comma.'), false;
                const ds = parts.map(s => Number(s));
                if (ds.some(v => !Number.isInteger(v) || v < 0 || v > 32767)) return dialogError(dialog, 'Each drop-off building must be a non-negative integer unit ID.'), false;
                gatherObj.drop_sites = ds;
              }
              if (mmRaw !== '') {
                const mm = parseInt(mmRaw, 10);
                if (!Number.isInteger(mm) || mm < 0 || mm > 255) return dialogError(dialog, 'Minimap color must be an integer 0..255.'), false;
                gatherObj.minimap_color = mm;
              }
              for (const [k, raw, label] of [
                ['deposit_slp', depSlpRaw, 'Deposit SLP'],
                ['carry_male_slp', mSlpRaw, 'Male carry SLP'],
                ['carry_female_slp', fSlpRaw, 'Female carry SLP'],
              ]) {
                if (raw !== '') {
                  const v = parseInt(raw, 10);
                  if (!Number.isInteger(v) || v <= 0) return dialogError(dialog, `${label} must be a positive integer.`), false;
                  gatherObj[k] = v;
                }
              }
            }

            const entry = cur ? { ...cur } : {};
            entry.id = id;
            if (name) entry.name = name;
            else delete entry.name;
            if (icon) entry.icon = icon;
            else if (!cur || cur.icon !== '') delete entry.icon;
            entry.start = startVal;
            entry.max = maxVal;
            entry.show = show;
            if (hidden) entry.hidden = true;
            else delete entry.hidden;
            if (gmode) entry.game_mode = gmode;
            else delete entry.game_mode;
            if (desc.trim()) entry.description = desc;
            else if (!cur || cur.description !== '') delete entry.description;
            if (gatherObj) entry.gather = gatherObj;
            else delete entry.gather;

            if (!Array.isArray(mod.resources)) mod.resources = [];
            if (isEdit) mod.resources[editIdx] = entry;
            else mod.resources.push(entry);
            afterModJsonChange();
            return true;
          }
        }
      ],
      onOpen: (dialog) => {
        const nameIn = dialog.querySelector('#res-name');
        const idIn = dialog.querySelector('#res-id');
        const iconIn = dialog.querySelector('#res-icon');
        const iconPick = dialog.querySelector('#res-icon-pick');
        const gmodeIn = dialog.querySelector('#res-gmode');
        const gmodePick = dialog.querySelector('#res-gmode-pick');
        const gatherOn = dialog.querySelector('#res-gather-on');
        const gatherBox = dialog.querySelector('#res-gather-box');
        const copyIn = dialog.querySelector('#res-g-copy');
        const copyPick = dialog.querySelector('#res-g-copy-pick');
        const preview = dialog.querySelector('[data-role="icon-preview"]');
        const iconHint = dialog.querySelector('[data-role="icon-hint"]');
        let idTouched = isEdit || !!curId;

        const updateIcon = () => {
          const path = iconIn.value.trim();
          const fallback = ((nameIn.value.trim() || idIn.value.trim() || 'R').charAt(0)).toUpperCase();
          if (iconPick) {
            iconPick.value = pngs.some(f => f.path === path) ? path : '';
          }
          preview.dataset.resIcon = path;
          if (!path) {
            preview.textContent = fallback;
            iconHint.textContent = pngs.length
              ? 'Drawn at 16x16 in the top bar and 18x18 in the "Show all" panel.'
              : 'No PNG files in the mod folder yet — add one in Files or type a relative path.';
            return;
          }
          const exists = treeList().some(f => f.path.toLowerCase() === path.toLowerCase());
          iconHint.textContent = exists
            ? 'Found in the mod folder (drawn at 16x16 in the top bar).'
            : 'Note: this PNG is not in the mod folder yet (you can add it in Files).';
          preview.textContent = fallback;
          if (exists) {
            loadImgUrl(path).then(url => {
              if (url && preview.isConnected && preview.dataset.resIcon === path) {
                preview.innerHTML = `<img src="${url}" alt="">`;
              }
            });
          }
        };

        nameIn.addEventListener('input', () => {
          if (!idTouched) idIn.value = slugId(nameIn.value, true).slice(0, 64);
          if (!iconIn.value.trim()) updateIcon();
        });
        idIn.addEventListener('input', () => {
          idTouched = true;
          if (!iconIn.value.trim()) updateIcon();
        });
        iconIn.addEventListener('input', updateIcon);
        if (iconPick) {
          iconPick.addEventListener('change', () => {
            if (iconPick.value) {
              iconIn.value = iconPick.value;
              updateIcon();
            }
          });
        }
        dialog.querySelector('#res-icon-lib').addEventListener('click', () => {
          if (!window.ScIconLib) return;
          window.ScIconLib.pick({
            title: 'Pick a resource icon',
            dest: entry => `icons/${entry.id}.png`,
            onPicked: async (entry, info) => {
              // the file is in the mod now: refresh the file list so the preview finds it
              forgetImgUrl(info.path);
              await loadTree();
              iconIn.value = info.path;
              updateIcon();
            }
          });
        });
        if (gmodePick) {
          gmodePick.addEventListener('change', () => {
            gmodeIn.value = gmodePick.value;
          });
        }
        gatherOn.addEventListener('change', () => {
          gatherBox.hidden = !gatherOn.checked;
        });
        if (copyPick) {
          copyPick.addEventListener('change', () => {
            copyIn.value = copyPick.value;
          });
        }
        updateIcon();
        setTimeout(() => nameIn.focus(), 30);
      }
    });
  }

  function deleteResource(idx) {
    const mod = modOf();
    const list = modList('resources');
    if (!mod || idx < 0 || idx >= list.length) return;
    const item = list[idx] || {};
    const label = item.name || item.id || 'this resource';
    S.showConfirm('Delete resource', `Remove "${label}" from mod.json?`, 'Delete', () => {
      mod.resources.splice(idx, 1);
      if (mod.resources.length === 0 && !st.hadKey.resources) delete mod.resources;
      afterModJsonChange();
    });
  }

  // ==========================================================================
  // Section: Settings (mod.json "settings")
  // ==========================================================================
  const SETTING_ID_RULE = /^[A-Za-z0-9_]+$/;
  const SETTING_MAX = 32;        // WL_MAX_SETTINGS in modsettings.h
  const CHOICE_MAX = 12;         // WL_MAX_CHOICES in modsettings.h
  const CHOICE_MAX_LEN = 39;     // choices[WL_MAX_CHOICES][40] in modsettings.h

  function settingTypeOf(s) {
    const t = s && typeof s.type === 'string' ? s.type : 'bool';
    return (t === 'number' || t === 'choice' || t === 'bool') ? t : 'bool';
  }

  function renderSettings(body) {
    const settings = modList('settings');
    if (settings.length === 0) {
      body.innerHTML = `
        <div class="empty-card">
          <div class="empty-card-icon">${NAV_ICONS.settings}</div>
          <h3 class="empty-card-title">No mod settings yet</h3>
          <p class="empty-card-text">Add options that players can change on the in-game Mods screen and your Lua scripts read from MOD.settings.</p>
          <div class="empty-card-actions"><button class="btn btn-save btn-sm" data-act="setting-add">Add setting</button></div>
        </div>`;
      return;
    }
    body.innerHTML = `
      <div class="list-head">
        <span><strong>${settings.length}</strong> setting${settings.length === 1 ? '' : 's'}${settings.length > SETTING_MAX ? ` &middot; <span class="warn">the game loads the first ${SETTING_MAX}</span>` : ''}</span>
      </div>
      <div class="list">
        ${settings.map((s, i) => {
          const item = (s && typeof s === 'object') ? s : {};
          const id = typeof item.id === 'string' ? item.id : '';
          const title = (typeof item.name === 'string' && item.name) ? item.name : (id || '(unnamed)');
          const kind = settingTypeOf(item);
          const glyphLabel = kind === 'number' ? 'num' : kind === 'choice' ? 'pick' : 'bool';
          let summary = '';
          if (kind === 'bool') {
            summary = `default: ${item.default === true ? 'On' : 'Off'}`;
          } else if (kind === 'number') {
            const min = typeof item.min === 'number' ? item.min : 0;
            const max = typeof item.max === 'number' ? item.max : 100;
            const step = typeof item.step === 'number' ? item.step : 1;
            const def = typeof item.default === 'number' ? item.default : min;
            summary = `default: ${def} · ${min}..${max} (step ${step})`;
          } else {
            const ch = Array.isArray(item.choices) ? item.choices : [];
            const def = typeof item.default === 'string' ? item.default : (ch[0] || '');
            summary = `default: ${def || '—'} · ${ch.length ? ch.join(', ') : 'no choices'}`;
          }
          return `<div class="modmeta-row">
            <button type="button" class="modmeta-row-hit" data-act="setting-edit" data-idx="${i}">
              <span class="list-glyph ${kind}">${glyphLabel}</span>
              <span class="list-main">
                <span class="list-title">${esc(title)}</span>
                <span class="list-sub mono">MOD.settings.${esc(id || '?')} &middot; ${esc(summary)}</span>
              </span>
              <span class="list-tags"></span>
            </button>
            <span class="row-actions">
              <button type="button" class="btn btn-ghost btn-icon-sm" data-act="setting-up" data-idx="${i}" ${i === 0 ? 'disabled' : ''} title="Move up" aria-label="Move up">&#8593;</button>
              <button type="button" class="btn btn-ghost btn-icon-sm" data-act="setting-down" data-idx="${i}" ${i === settings.length - 1 ? 'disabled' : ''} title="Move down" aria-label="Move down">&#8595;</button>
              <button type="button" class="btn btn-wood btn-sm" data-act="setting-edit" data-idx="${i}">Edit</button>
              <button type="button" class="btn btn-ghost danger btn-sm" data-act="setting-delete" data-idx="${i}">Delete</button>
            </span>
          </div>`;
        }).join('')}
      </div>
      <p class="list-note">Players change these on the in-game Mods screen (<strong>Settings</strong> button). Values are read in Lua as <code class="mono">MOD.settings.&lt;id&gt;</code>.</p>`;
  }

  function settingDialog(editIdx) {
    const mod = modOf();
    if (!mod) return;
    const list = modList('settings');
    const isEdit = Number.isInteger(editIdx) && editIdx >= 0 && editIdx < list.length;
    const cur = isEdit && list[editIdx] && typeof list[editIdx] === 'object' ? list[editIdx] : null;

    const curId = cur && typeof cur.id === 'string' ? cur.id : '';
    const curName = cur && typeof cur.name === 'string' ? cur.name : '';
    const curType = cur ? settingTypeOf(cur) : 'bool';
    const curBoolDef = curType === 'bool' ? (cur ? cur.default === true : true) : true;
    const curMin = cur && typeof cur.min === 'number' && Number.isFinite(cur.min) ? cur.min : 0;
    const curMax = cur && typeof cur.max === 'number' && Number.isFinite(cur.max) ? cur.max : 100;
    const curStep = cur && typeof cur.step === 'number' && Number.isFinite(cur.step) ? cur.step : 1;
    const curNumDef = curType === 'number' && cur && typeof cur.default === 'number' && Number.isFinite(cur.default) ? cur.default : curMin;
    let choiceValues = (cur && Array.isArray(cur.choices) && cur.choices.length)
      ? cur.choices.map(c => String(c))
      : ['Easy', 'Normal', 'Hard'];
    let choiceDefault = (curType === 'choice' && cur && typeof cur.default === 'string')
      ? cur.default
      : (choiceValues[0] || '');

    openDialog({
      title: isEdit ? 'Edit setting' : 'Add setting',
      width: 'lg',
      body: `
        <div class="form-grid-2">
          <div class="form-field">
            <label for="set-name">Label <span class="muted">shown on Mods screen</span></label>
            <input id="set-name" type="text" class="form-input" maxlength="63" placeholder="e.g. Difficulty" value="${esc(curName)}" autocomplete="off">
          </div>
          <div class="form-field">
            <label for="set-id">ID <span class="muted">letters, digits, _</span></label>
            <input id="set-id" type="text" class="form-input mono" maxlength="47" placeholder="e.g. difficulty" value="${esc(curId)}" autocomplete="off" spellcheck="false">
          </div>
        </div>
        <p class="form-hint">Read in Lua as <code class="mono" data-role="set-hint">MOD.settings.${esc(curId || 'my_setting')}</code>.</p>
        <div class="form-field">
          <label for="set-type">Type</label>
          <select id="set-type" class="form-select">
            <option value="bool" ${curType === 'bool' ? 'selected' : ''}>Toggle (bool)</option>
            <option value="number" ${curType === 'number' ? 'selected' : ''}>Number</option>
            <option value="choice" ${curType === 'choice' ? 'selected' : ''}>Choice</option>
          </select>
        </div>

        <div class="form-fieldset" data-panel="bool" ${curType === 'bool' ? '' : 'hidden'}>
          <label class="form-check" style="margin-bottom: 8px;">
            <input type="checkbox" id="set-bool-default" ${curBoolDef ? 'checked' : ''}>
            <span class="form-check-text">
              <span data-role="bool-label">Default: ${curBoolDef ? 'On (true)' : 'Off (false)'}</span>
              <span class="form-check-sub">Initial value until the player toggles it on the Mods screen.</span>
            </span>
          </label>
        </div>

        <div class="form-fieldset" data-panel="number" ${curType === 'number' ? '' : 'hidden'}>
          <div class="form-grid-2">
            <div class="form-field">
              <label for="set-num-min">Minimum <span class="muted mono">min</span></label>
              <input id="set-num-min" type="number" step="any" class="form-input" value="${esc(curMin)}">
            </div>
            <div class="form-field">
              <label for="set-num-max">Maximum <span class="muted mono">max</span></label>
              <input id="set-num-max" type="number" step="any" class="form-input" value="${esc(curMax)}">
            </div>
            <div class="form-field">
              <label for="set-num-step">Step <span class="muted mono">step</span></label>
              <input id="set-num-step" type="number" step="any" class="form-input" value="${esc(curStep)}">
            </div>
            <div class="form-field">
              <label for="set-num-default">Default <span class="muted mono">default</span></label>
              <input id="set-num-default" type="number" step="any" class="form-input" value="${esc(curNumDef)}">
            </div>
          </div>
        </div>

        <div class="form-fieldset" data-panel="choice" ${curType === 'choice' ? '' : 'hidden'}>
          <div class="form-field">
            <label>Choices <span class="muted" data-role="choices-count"></span></label>
            <div class="choice-editor-list" data-role="choices-list"></div>
            <div class="choice-editor-foot">
              <button type="button" class="btn btn-wood btn-sm" data-role="choice-add">+ Add choice</button>
              <span class="muted" style="font-size:11.5px;">Up to ${CHOICE_MAX} choices, max ${CHOICE_MAX_LEN} chars each</span>
            </div>
          </div>
          <div class="form-field">
            <label for="set-choice-default">Default choice <span class="muted mono">default</span></label>
            <select id="set-choice-default" class="form-select"></select>
          </div>
        </div>

        <p class="form-error" role="alert"></p>`,
      buttons: [
        { label: 'Cancel' },
        {
          label: isEdit ? 'Save changes' : 'Add setting',
          primary: true,
          onClick: (dialog) => {
            const id = dialog.querySelector('#set-id').value.trim();
            const name = dialog.querySelector('#set-name').value.trim();
            const type = dialog.querySelector('#set-type').value;

            if (!id) return dialogError(dialog, 'Enter a setting ID.'), false;
            if (id.length > 47) return dialogError(dialog, 'The setting ID must be at most 47 characters.'), false;
            if (!SETTING_ID_RULE.test(id)) return dialogError(dialog, 'The setting ID may use letters, digits and "_" only.'), false;
            const dup = list.some((s, idx) => idx !== editIdx && s && typeof s.id === 'string' && s.id === id);
            if (dup) return dialogError(dialog, `A setting with ID "${id}" already exists in this mod.`), false;

            const entry = cur ? { ...cur } : {};
            entry.id = id;
            if (name) entry.name = name;
            else delete entry.name;
            entry.type = type;

            if (type === 'bool') {
              entry.default = dialog.querySelector('#set-bool-default').checked;
              delete entry.min;
              delete entry.max;
              delete entry.step;
              delete entry.choices;
            } else if (type === 'number') {
              const minRaw = dialog.querySelector('#set-num-min').value.trim();
              const maxRaw = dialog.querySelector('#set-num-max').value.trim();
              const stepRaw = dialog.querySelector('#set-num-step').value.trim();
              const defRaw = dialog.querySelector('#set-num-default').value.trim();
              if (!minRaw || !maxRaw || !stepRaw || !defRaw) {
                return dialogError(dialog, 'Fill in min, max, step, and default.'), false;
              }
              const minVal = Number(minRaw);
              const maxVal = Number(maxRaw);
              const stepVal = Number(stepRaw);
              const defVal = Number(defRaw);
              if (!Number.isFinite(minVal) || !Number.isFinite(maxVal) || !Number.isFinite(stepVal) || !Number.isFinite(defVal)) {
                return dialogError(dialog, 'Min, max, step, and default must be valid numbers.'), false;
              }
              if (maxVal < minVal) return dialogError(dialog, 'Maximum must be greater than or equal to minimum.'), false;
              if (stepVal <= 0) return dialogError(dialog, 'Step must be greater than 0.'), false;
              if (defVal < minVal || defVal > maxVal) {
                return dialogError(dialog, `Default must be between ${minVal} and ${maxVal}.`), false;
              }
              entry.min = minVal;
              entry.max = maxVal;
              entry.step = stepVal;
              entry.default = defVal;
              delete entry.choices;
            } else if (type === 'choice') {
              const trimmed = choiceValues.map(c => c.trim());
              if (trimmed.length === 0) return dialogError(dialog, 'Add at least one choice.'), false;
              if (trimmed.length > CHOICE_MAX) return dialogError(dialog, `At most ${CHOICE_MAX} choices are supported.`), false;
              if (trimmed.some(c => !c)) return dialogError(dialog, 'Choices cannot be blank. Remove or fill in empty choices.'), false;
              if (trimmed.some(c => c.length > CHOICE_MAX_LEN)) {
                return dialogError(dialog, `Each choice must be at most ${CHOICE_MAX_LEN} characters.`), false;
              }
              if (new Set(trimmed).size !== trimmed.length) {
                return dialogError(dialog, 'Choices must be unique.'), false;
              }
              const defVal = dialog.querySelector('#set-choice-default').value;
              if (!trimmed.includes(defVal)) {
                return dialogError(dialog, 'Pick a default choice from the list.'), false;
              }
              entry.choices = trimmed;
              entry.default = defVal;
              delete entry.min;
              delete entry.max;
              delete entry.step;
            }

            if (!Array.isArray(mod.settings)) mod.settings = [];
            if (isEdit) mod.settings[editIdx] = entry;
            else mod.settings.push(entry);
            afterModJsonChange();
            return true;
          }
        }
      ],
      onOpen: (dialog) => {
        const nameIn = dialog.querySelector('#set-name');
        const idIn = dialog.querySelector('#set-id');
        const hintEl = dialog.querySelector('[data-role="set-hint"]');
        const typeSel = dialog.querySelector('#set-type');
        const boolChk = dialog.querySelector('#set-bool-default');
        const boolLbl = dialog.querySelector('[data-role="bool-label"]');
        const choicesList = dialog.querySelector('[data-role="choices-list"]');
        const choicesCount = dialog.querySelector('[data-role="choices-count"]');
        const addChoiceBtn = dialog.querySelector('[data-role="choice-add"]');
        const defSel = dialog.querySelector('#set-choice-default');
        let idTouched = isEdit || !!curId;

        const updateHint = () => {
          hintEl.textContent = `MOD.settings.${idIn.value.trim() || 'my_setting'}`;
        };
        const syncPanels = () => {
          const t = typeSel.value;
          dialog.querySelectorAll('[data-panel]').forEach(p => { p.hidden = p.dataset.panel !== t; });
        };
        const syncDefaultSelect = () => {
          const nonEmpty = choiceValues.map(c => c.trim()).filter(Boolean);
          const unique = [...new Set(nonEmpty)];
          if (!unique.includes(choiceDefault)) choiceDefault = unique[0] || '';
          defSel.innerHTML = unique.length
            ? unique.map(c => `<option value="${esc(c)}" ${c === choiceDefault ? 'selected' : ''}>${esc(c)}</option>`).join('')
            : '<option value="">(Add a choice first)</option>';
        };
        const paintChoices = (focusIdx) => {
          choicesCount.textContent = `${choiceValues.length} / ${CHOICE_MAX}`;
          addChoiceBtn.disabled = choiceValues.length >= CHOICE_MAX;
          choicesList.innerHTML = choiceValues.map((val, idx) => `
            <div class="choice-editor-row" data-choice-idx="${idx}">
              <input type="text" class="form-input" maxlength="${CHOICE_MAX_LEN}" value="${esc(val)}" placeholder="Choice ${idx + 1}" autocomplete="off">
              <button type="button" class="btn btn-ghost btn-icon-sm" data-choice-act="up" ${idx === 0 ? 'disabled' : ''} title="Move up" aria-label="Move up">&#8593;</button>
              <button type="button" class="btn btn-ghost btn-icon-sm" data-choice-act="down" ${idx === choiceValues.length - 1 ? 'disabled' : ''} title="Move down" aria-label="Move down">&#8595;</button>
              <button type="button" class="btn btn-ghost danger btn-icon-sm" data-choice-act="rm" title="Remove choice" aria-label="Remove choice">&times;</button>
            </div>`).join('');
          syncDefaultSelect();
          if (Number.isInteger(focusIdx)) {
            const inp = choicesList.querySelector(`[data-choice-idx="${focusIdx}"] input`);
            if (inp) inp.focus();
          }
        };

        choicesList.addEventListener('input', (e) => {
          const row = e.target.closest('[data-choice-idx]');
          if (!row) return;
          const idx = Number(row.dataset.choiceIdx);
          const prev = choiceValues[idx].trim();
          const next = e.target.value;
          choiceValues[idx] = next;
          if (choiceDefault === prev) choiceDefault = next.trim();
          syncDefaultSelect();
        });
        choicesList.addEventListener('click', (e) => {
          const btn = e.target.closest('[data-choice-act]');
          const row = e.target.closest('[data-choice-idx]');
          if (!btn || !row || btn.disabled) return;
          const idx = Number(row.dataset.choiceIdx);
          const act = btn.dataset.choiceAct;
          if (act === 'up' && idx > 0) {
            [choiceValues[idx - 1], choiceValues[idx]] = [choiceValues[idx], choiceValues[idx - 1]];
            paintChoices(idx - 1);
          } else if (act === 'down' && idx < choiceValues.length - 1) {
            [choiceValues[idx], choiceValues[idx + 1]] = [choiceValues[idx + 1], choiceValues[idx]];
            paintChoices(idx + 1);
          } else if (act === 'rm') {
            choiceValues.splice(idx, 1);
            paintChoices();
          }
        });
        addChoiceBtn.addEventListener('click', () => {
          if (choiceValues.length >= CHOICE_MAX) return;
          choiceValues.push('');
          paintChoices(choiceValues.length - 1);
        });
        defSel.addEventListener('change', () => { choiceDefault = defSel.value; });
        boolChk.addEventListener('change', () => {
          boolLbl.textContent = boolChk.checked ? 'Default: On (true)' : 'Default: Off (false)';
        });
        typeSel.addEventListener('change', () => {
          dialogError(dialog, '');
          syncPanels();
        });
        nameIn.addEventListener('input', () => {
          if (!idTouched) {
            idIn.value = slugId(nameIn.value, true).slice(0, 47);
            updateHint();
          }
        });
        idIn.addEventListener('input', () => { idTouched = true; updateHint(); });

        updateHint();
        syncPanels();
        paintChoices();
        setTimeout(() => nameIn.focus(), 30);
      }
    });
  }

  function deleteSetting(idx) {
    const mod = modOf();
    const list = modList('settings');
    if (!mod || idx < 0 || idx >= list.length) return;
    const item = list[idx] || {};
    const label = item.name || item.id || 'this setting';
    S.showConfirm('Delete setting', `Remove "${label}" from mod.json?`, 'Delete', () => {
      mod.settings.splice(idx, 1);
      if (mod.settings.length === 0 && !st.hadKey.settings) delete mod.settings;
      afterModJsonChange();
    });
  }

  function moveSetting(idx, dir) {
    const mod = modOf();
    if (!mod || !Array.isArray(mod.settings)) return;
    const target = idx + dir;
    if (idx < 0 || idx >= mod.settings.length || target < 0 || target >= mod.settings.length) return;
    [mod.settings[idx], mod.settings[target]] = [mod.settings[target], mod.settings[idx]];
    afterModJsonChange();
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
      id: 'game_modes', label: 'Game modes',
      sub: 'Custom lobby game modes added to the Game Mode dropdown.',
      count: () => modList('game_modes').length,
      actions: () => {
        const hasModes = modList('game_modes').length > 0;
        return (hasModes ? renderQuickPresetsBar(true) : '')
          + '<button class="btn btn-save btn-sm" data-act="mode-add">Add game mode</button>';
      },
      render: renderGameModes
    },
    {
      id: 'resources', label: 'Resources',
      sub: 'Custom resources shown in the top bar and used in costs, effects and Lua scripts.',
      count: () => modList('resources').length,
      actions: () => '<button class="btn btn-save btn-sm" data-act="res-add">Add resource</button>',
      render: renderResources
    },
    {
      id: 'settings', label: 'Settings',
      sub: 'Options players can change on the in-game Mods screen (read from MOD.settings).',
      count: () => modList('settings').length,
      actions: () => '<button class="btn btn-save btn-sm" data-act="setting-add">Add setting</button>',
      render: renderSettings
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
    'mode-add': () => gameModeDialog(null),
    'mode-preset': (el) => openPresetDialog(el.dataset.preset),
    'mode-edit': (el) => gameModeDialog(Number(el.dataset.idx)),
    'mode-duplicate': (el) => duplicateGameMode(Number(el.dataset.idx)),
    'mode-rms': (el) => editRmsDialog(Number(el.dataset.idx)).catch(taskFailed),
    'mode-copy-lua': (el) => copyModeLua(Number(el.dataset.idx)),
    'mode-copy-id': (el) => copyModeId(Number(el.dataset.idx)),
    'mode-delete': (el) => deleteGameMode(Number(el.dataset.idx)),
    'res-add': () => resourceDialog(null),
    'res-edit': (el) => resourceDialog(Number(el.dataset.idx)),
    'res-delete': (el) => deleteResource(Number(el.dataset.idx)),
    'setting-add': () => settingDialog(null),
    'setting-edit': (el) => settingDialog(Number(el.dataset.idx)),
    'setting-delete': (el) => deleteSetting(Number(el.dataset.idx)),
    'setting-up': (el) => moveSetting(Number(el.dataset.idx), -1),
    'setting-down': (el) => moveSetting(Number(el.dataset.idx), 1),
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
    'file-preview': (el) => previewFileDialog(el.dataset.path).catch(taskFailed),
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
    clearImgUrls();
    st.nowPlaying = null;
    st.codes.clear();
    st.code = null;
    st.tree = null;
    st.filter = '';
    const loadedMod = modOf() || {};
    st.hadKey = {
      game_modes: Object.prototype.hasOwnProperty.call(loadedMod, 'game_modes'),
      resources: Object.prototype.hasOwnProperty.call(loadedMod, 'resources'),
      settings: Object.prototype.hasOwnProperty.call(loadedMod, 'settings')
    };
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

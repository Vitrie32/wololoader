/**
 * Scriptorium v2 — Front End Application
 * Vanilla ES2020, zero dependencies, offline-first.
 */

// Error logging into hidden DOM element for headless chrome verification
window.onerror = function(message, source, lineno, colno, error) {
  const errDiv = document.getElementById('errors');
  if (errDiv) {
    const p = document.createElement('p');
    p.textContent = `[${new Date().toISOString()}] ${message} at ${source}:${lineno}:${colno}`;
    errDiv.appendChild(p);
  }
  console.error("Scriptorium Error:", message, source, lineno, colno, error);
};

(function() {
  'use strict';

  // ==========================================================================
  // Application State
  // ==========================================================================
  const state = {
    token: '',
    info: { game_dir: '', mods_dir: '', version: '2.0' },
    catalog: {
      civs: [],
      units: [],
      techs: [],
      stats: []
    },
    mods: [],
    currentModFolder: null,
    currentModData: null,      // { folder, mod: {...}, civs: { ... }, deleted_civs: [] }
    snapshot: '',              // JSON string of saved state
    isDirty: false,
    pane: 'main',              // editor pane: 'main' (the Basic workspace) or 'sprites'
    pingInterval: null,
    lastPingSuccess: true
  };

  // ==========================================================================
  // Utility & Security Functions
  // ==========================================================================
  function escapeHtml(str) {
    if (str === null || str === undefined) return '';
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }

  function showToast(message, type = 'info', duration = 3500) {
    const container = document.getElementById('toast-container');
    if (!container) return;
    const toast = document.createElement('div');
    toast.className = `toast toast-${type}`;
    toast.innerHTML = `
      <span>${escapeHtml(message)}</span>
      <button class="btn-toast-close" title="Close">&times;</button>
    `;
    const closeBtn = toast.querySelector('.btn-toast-close');
    closeBtn.addEventListener('click', () => toast.remove());
    container.appendChild(toast);
    setTimeout(() => {
      if (toast.parentNode) {
        toast.style.transition = 'opacity 200ms ease, transform 200ms ease';
        toast.style.opacity = '0';
        toast.style.transform = 'translateY(10px)';
        setTimeout(() => toast.remove(), 200);
      }
    }, duration);
  }

  function showConfirm(title, message, confirmText = 'Confirm', onConfirm = null) {
    const modal = document.getElementById('modal-container');
    if (!modal) return;
    modal.innerHTML = `
      <div class="modal-dialog">
        <div class="modal-header">
          <h4 class="modal-title">${escapeHtml(title)}</h4>
          <button class="btn-modal-close" id="modal-btn-close">&times;</button>
        </div>
        <div class="modal-body">
          <p>${escapeHtml(message)}</p>
        </div>
        <div class="modal-footer">
          <button class="btn btn-secondary btn-sm" id="modal-btn-cancel">Cancel</button>
          <button class="btn btn-gold btn-sm" id="modal-btn-confirm">${escapeHtml(confirmText)}</button>
        </div>
      </div>
    `;
    modal.style.display = 'flex';

    const close = () => { modal.style.display = 'none'; modal.innerHTML = ''; };
    modal.querySelector('#modal-btn-close').onclick = close;
    modal.querySelector('#modal-btn-cancel').onclick = close;
    modal.querySelector('#modal-btn-confirm').onclick = () => {
      close();
      if (onConfirm) onConfirm();
    };
  }

  function parseHashAndParams() {
    const params = new URLSearchParams(window.location.search);
    const hash = window.location.hash.replace(/^#/, '');
    if (hash) {
      const hashParams = new URLSearchParams(hash);
      for (const [k, v] of hashParams.entries()) {
        params.set(k, v);
      }
    }
    return params;
  }

  function updateUrlState() {
    const params = new URLSearchParams();
    if (state.token) params.set('token', state.token);
    if (state.currentModFolder) params.set('mod', state.currentModFolder);
    history.replaceState(null, '', '#' + params.toString());
  }

  // ==========================================================================
  // HTTP / API Client
  // ==========================================================================
  async function api(path, options = {}) {
    const headers = options.headers || {};
    if (state.token) {
      headers['X-Scriptorium-Token'] = state.token;
    }

    // binary bodies (ArrayBuffer, Uint8Array and other views, Blob) go as they are; objects as JSON
    const b = options.body;
    if (b && typeof b === 'object' && !(b instanceof ArrayBuffer || ArrayBuffer.isView(b) || b instanceof Blob)) {
      headers['Content-Type'] = 'application/json';
      options.body = JSON.stringify(options.body);
    }

    options.headers = headers;
    const res = await fetch(path, options);
    if (!res.ok) {
      let errText = `HTTP ${res.status}`;
      try {
        const data = await res.json();
        if (data && data.error) errText = data.error;
      } catch (e) {
        // ignore json parse error
      }
      throw new Error(errText);
    }

    const contentType = res.headers.get('content-type');
    if (contentType && contentType.includes('application/json')) {
      return await res.json();
    }
    return res;
  }

  // Heartbeat ping every 2 seconds
  // The status dot and text appear on the home header and in the editor top bar (class ping-dot / ping-text).
  function setConnection(ok) {
    document.querySelectorAll('.ping-dot').forEach(el => { el.className = 'status-dot ' + (ok ? 'connected' : 'disconnected'); });
    document.querySelectorAll('.ping-text').forEach(el => { el.textContent = ok ? 'Connected' : 'Disconnected'; });
  }

  function startPing() {
    if (state.pingInterval) clearInterval(state.pingInterval);
    state.pingInterval = setInterval(async () => {
      try {
        await api('/api/ping', { method: 'POST' });
        if (!state.lastPingSuccess) {
          state.lastPingSuccess = true;
          setConnection(true);
        }
      } catch (err) {
        if (state.lastPingSuccess) {
          state.lastPingSuccess = false;
          setConnection(false);
        }
      }
    }, 2000);
  }

  // ==========================================================================
  // Mod State Management (Direct Mutation & Dirty Checking)
  // ==========================================================================
  function takeSnapshot() {
    if (!state.currentModData) {
      state.snapshot = '';
      setDirty(false);
      return;
    }
    state.snapshot = JSON.stringify({
      mod: state.currentModData.mod,
      civs: state.currentModData.civs,
      deleted_civs: state.currentModData.deleted_civs || []
    });
    updateDirty();
  }

  // Unsaved edits that are not part of mod.json or civs/ (a Lua file in the code editor, a visual
  // script): their owners call setExtraDirty(key, true) and clear it when they save.
  const extraDirty = new Set();
  function setExtraDirty(key, on) {
    if (on) extraDirty.add(key);
    else extraDirty.delete(key);
    updateDirty();
  }

  function updateDirty() {
    if (!state.currentModData) {
      setDirty(false);
      return;
    }
    const current = JSON.stringify({
      mod: state.currentModData.mod,
      civs: state.currentModData.civs,
      deleted_civs: state.currentModData.deleted_civs || []
    });
    setDirty(extraDirty.size > 0 || current !== state.snapshot);
  }

  function checkDirty() {
    updateDirty();
  }

  // Save and revert hooks: the other editors register what they save (or drop) with the mod.
  // Save hooks run in `order` (lowest first) before mod.json and civs/ are written.
  const saveHooks = [];
  const revertHooks = [];
  function onSave(fn, order = 50) {
    saveHooks.push({ fn, order });
    saveHooks.sort((a, b) => a.order - b.order);
  }
  function onRevert(fn) {
    revertHooks.push(fn);
  }

  function setDirty(isDirty) {
    state.isDirty = isDirty;
    const badge = document.getElementById('unsaved-badge');
    const revertBtn = document.getElementById('btn-revert');
    if (badge) badge.style.display = isDirty ? 'inline-flex' : 'none';
    if (revertBtn) revertBtn.disabled = !isDirty;
  }

  // ==========================================================================
  // Catalog & Initial Loading
  // ==========================================================================
  async function init() {
    const params = parseHashAndParams();
    state.token = params.get('token') || '';

    try {
      // 1. Fetch backend info
      try {
        state.info = await api('/api/info');
        if (state.info.version) {
          const vEl = document.getElementById('app-version');
          if (vEl) vEl.textContent = 'v' + state.info.version;
        }
      } catch (e) {
        console.warn('api/info failed, continuing...', e);
      }

      // 2. Fetch catalog
      state.catalog = await api('/api/catalog');

      // 3. Fetch mods list
      await refreshModsList();

      // 4. Deep-linking: a mod named in the URL opens its editor; otherwise the mod list is the start page
      const reqMod = params.get('mod');
      if (reqMod && state.mods.some(m => m.folder === reqMod)) {
        await loadMod(reqMod);
      } else {
        showHome();
      }

      const reqModal = params.get('modal');
      if (reqModal === 'new-mod') openNewModModal();

      // 5. Start ping heartbeat
      startPing();
    } catch (err) {
      showToast('Initialization error: ' + err.message, 'error', 6000);
      console.error(err);
    }
  }

  async function refreshModsList() {
    state.mods = await api('/api/mods');
    renderModCards();
  }

  function formatDate(unixSeconds) {
    return new Date(unixSeconds * 1000).toLocaleString(undefined, {
      year: 'numeric', month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit'
    });
  }

  function modCardHtml(m) {
    const enabled = m.enabled !== false;
    const civCount = (m.civs || []).length;
    const changed = m.modified ? formatDate(m.modified) : '';
    return `
      <div class="mod-tile" role="button" tabindex="0" data-folder="${escapeHtml(m.folder)}">
        <div class="mod-tile-top">
          <div class="mod-tile-icon${m.has_icon ? '' : ' is-generated'}">${modIconHtml(m)}</div>
          <span class="mod-state ${enabled ? 'on' : 'off'}">${enabled ? 'Enabled' : 'Disabled'}</span>
        </div>
        <div class="mod-tile-name">${escapeHtml(m.name)}</div>
        <div class="mod-tile-id">${escapeHtml(m.folder)}</div>
        <div class="mod-tile-meta">
          <span>v${escapeHtml(m.version || '0.1')}</span>
          <span>${civCount} civ${civCount === 1 ? '' : 's'}</span>
        </div>
        ${changed ? `<div class="mod-tile-changed">Last changed ${escapeHtml(changed)}</div>` : ''}
      </div>`;
  }

  // Home screen: one card per mod, then the New mod card. Nothing else is on this page.
  // (The server leaves WololoaderBase out of /api/mods.)
  function renderModCards() {
    const grid = document.getElementById('mods-grid');
    if (!grid) return;

    const searchTerm = (document.getElementById('mod-search')?.value || '').toLowerCase().trim();
    const filtered = state.mods.filter(m =>
      (m.name || '').toLowerCase().includes(searchTerm) || m.folder.toLowerCase().includes(searchTerm)
    );

    const countEl = document.getElementById('home-count');
    if (countEl) countEl.textContent = `${state.mods.length} installed`;

    const note = filtered.length === 0 && state.mods.length > 0
      ? '<p class="mods-grid-note">No mods match the search.</p>' : '';
    grid.innerHTML = filtered.map(modCardHtml).join('') + note + `
      <button type="button" class="mod-tile mod-tile-new" id="btn-new-mod">
        <span class="mod-tile-plus">+</span>
        <span class="mod-tile-name">New mod</span>
        <span class="mod-tile-hint">Create an empty mod folder and open it</span>
      </button>`;
    hydrateModIcons(grid);

    grid.querySelectorAll('.mod-tile[data-folder]').forEach(el => {
      const open = () => loadMod(el.dataset.folder);
      el.addEventListener('click', open);
      el.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); open(); }
      });
    });
    grid.querySelector('#btn-new-mod')?.addEventListener('click', openNewModModal);
  }

  function showEditor() {
    document.getElementById('home-view').hidden = true;
    document.getElementById('editor-view').hidden = false;
  }

  // Back to the mod list. Leaving the editor with unsaved changes asks first.
  function goHome() {
    if (state.isDirty) {
      showConfirm(
        'Unsaved Changes',
        'You have unsaved changes in this mod. Discard them and go back to the mod list?',
        'Discard & Go Back',
        showHome
      );
    } else {
      showHome();
    }
  }

  function showHome() {
    state.currentModFolder = null;
    state.currentModData = null;
    state.snapshot = '';
    extraDirty.clear();
    setDirty(false);
    showPane('main');
    document.getElementById('editor-view').hidden = true;
    document.getElementById('home-view').hidden = false;
    // the editors (Basic workspace, script graphs, Sprites) drop the state of the mod they had open
    if (window.Scriptorium) window.Scriptorium._modLoaded();
    renderModCards();
    updateUrlState();
  }

  // The editor shows either the Basic workspace ('main') or the mod's Sprites.
  function showPane(pane) {
    state.pane = pane === 'sprites' ? 'sprites' : 'main';
    const ws = document.getElementById('mod-workspace');
    const spritesView = document.getElementById('sprites-view');
    if (ws) ws.classList.toggle('pane-sprites', state.pane === 'sprites');
    if (spritesView) spritesView.hidden = state.pane !== 'sprites';
    if (window.SlpViewer) {
      if (state.pane === 'sprites') window.SlpViewer.show();
      else window.SlpViewer.hide();
    }
  }

  async function loadMod(folder) {
    try {
      const data = await api(`/api/mod/${encodeURIComponent(folder)}`);
      state.currentModFolder = folder;
      state.currentModData = {
        folder: data.folder,
        mod: data.mod || {},
        civs: data.civs || {},
        deleted_civs: []
      };

      extraDirty.clear();
      takeSnapshot();
      const params = parseHashAndParams();
      if (params.get('dirty') === '1') {
        state.isDirty = true;
        setDirty(true);
      }

      showEditor();
      showPane('main');
      renderModHeader();
      if (window.Scriptorium) window.Scriptorium._modLoaded();
      updateUrlState();
    } catch (err) {
      showToast('Failed to load mod: ' + err.message, 'error');
    }
  }

  // ==========================================================================
  // Mod Icons
  // ==========================================================================
  // The icon endpoint needs the session token header, and <img src> cannot send it. So the icon
  // is fetched through api() and shown as a blob URL, cached per mod folder. A mod without
  // icon.png gets a generated shield (generatedIconSvg): its colour comes from the folder id, so
  // it is stable. New mods get it written as icon.png so the in-game Mods screen shows it too.
  const ICON_SIZE = 256;
  const iconUrlCache = new Map();   // folder -> Promise<blob URL | null>

  function loadModIconUrl(folder) {
    if (!iconUrlCache.has(folder)) {
      const p = api(`/api/icon/${encodeURIComponent(folder)}`)
        .then(res => res.blob())
        .then(blob => URL.createObjectURL(blob))
        .catch(() => null);
      iconUrlCache.set(folder, p);
    }
    return iconUrlCache.get(folder);
  }

  // After a new icon.png is written: the next view fetches it again.
  function forgetModIcon(folder) {
    const p = iconUrlCache.get(folder);
    iconUrlCache.delete(folder);
    if (p) p.then(url => { if (url) URL.revokeObjectURL(url); });
  }

  function hashString(str) {
    let h = 5381;
    for (let i = 0; i < str.length; i++) h = ((h * 33) ^ str.charCodeAt(i)) >>> 0;
    return h;
  }

  // "Roman Empire" -> "RE", "Monklanians" -> "M"
  function modInitials(name) {
    const words = String(name || '').trim().split(/[\s_\-]+/).filter(Boolean);
    if (words.length === 0) return 'M';
    if (words.length === 1) return Array.from(words[0])[0].toUpperCase();
    return (Array.from(words[0])[0] + Array.from(words[1])[0]).toUpperCase();
  }

  // The generated default: a shield on the dark theme, muted colour from the folder hash.
  function generatedIconSvg(folder, name) {
    const hue = hashString(folder || name || 'mod') % 360;
    const initials = modInitials(name || folder);
    const fontSize = initials.length > 1 ? 20 : 25;
    return `<svg class="mod-icon-generated" viewBox="0 0 64 64" aria-hidden="true" focusable="false">` +
      `<path d="M32 3 L56 11 V30 C56 46 46 56 32 61 C18 56 8 46 8 30 V11 Z" fill="hsl(${hue} 40% 28%)" stroke="hsl(${hue} 50% 50%)" stroke-width="2.5" stroke-linejoin="round"/>` +
      `<path d="M32 9 L50 15 V30 C50 42 43 50 32 54.5 C21 50 14 42 14 30 V15 Z" fill="none" stroke="rgba(255,255,255,0.14)" stroke-width="1.5"/>` +
      `<text x="32" y="40" text-anchor="middle" font-family="Georgia, 'Times New Roman', serif" font-size="${fontSize}" font-weight="700" fill="#f1e6c8">${escapeHtml(initials)}</text>` +
      `</svg>`;
  }

  // Markup of a mod's icon: an <img> for a custom icon.png (filled in by hydrateModIcons), else the
  // generated shield. m = { folder, name, has_icon }.
  function modIconHtml(m) {
    if (m.has_icon) {
      return `<img data-mod-icon="${escapeHtml(m.folder)}" data-mod-name="${escapeHtml(m.name || '')}" alt="">`;
    }
    return generatedIconSvg(m.folder, m.name);
  }

  // Fills the <img data-mod-icon> elements under root. A fetch that fails falls back to the generated icon.
  function hydrateModIcons(root) {
    if (!root) return;
    root.querySelectorAll('img[data-mod-icon]').forEach(img => {
      const folder = img.dataset.modIcon;
      const name = img.dataset.modName || '';
      loadModIconUrl(folder).then(url => {
        if (url) { img.src = url; return; }
        const tmp = document.createElement('div');
        tmp.innerHTML = generatedIconSvg(folder, name);
        if (img.parentNode) img.replaceWith(tmp.firstElementChild);
      });
    });
  }

  // Reads a PNG/JPG (any browser-decodable image) and returns a 256 x 256 PNG Blob: the largest
  // centred square, scaled with smoothing. Rejects when the file is not an image.
  function imageFileToIconBlob(file) {
    return new Promise((resolve, reject) => {
      const isImage = file && (/^image\//.test(file.type) || /\.(png|jpe?g|gif|bmp|webp)$/i.test(file.name || ''));
      if (!isImage) { reject(new Error('Choose an image file (PNG or JPG)')); return; }
      const url = URL.createObjectURL(file);
      const img = new Image();
      img.onload = () => {
        URL.revokeObjectURL(url);
        const side = Math.min(img.naturalWidth, img.naturalHeight);
        if (!side) { reject(new Error('The image is empty')); return; }
        const canvas = document.createElement('canvas');
        canvas.width = ICON_SIZE;
        canvas.height = ICON_SIZE;
        const ctx = canvas.getContext('2d');
        ctx.imageSmoothingEnabled = true;
        ctx.imageSmoothingQuality = 'high';
        const sx = (img.naturalWidth - side) / 2;
        const sy = (img.naturalHeight - side) / 2;
        ctx.drawImage(img, sx, sy, side, side, 0, 0, ICON_SIZE, ICON_SIZE);
        canvas.toBlob(blob => (blob ? resolve(blob) : reject(new Error('Could not encode the PNG'))), 'image/png');
      };
      img.onerror = () => { URL.revokeObjectURL(url); reject(new Error('Could not read this image')); };
      img.src = url;
    });
  }

  // The generated shield as a 256x256 PNG blob (same look as tools/make_default_icon.py).
  function generatedIconPng(folder, name) {
    const svg = generatedIconSvg(folder, name).replace('<svg ', '<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" ');
    return new Promise((resolve, reject) => {
      const img = new Image();
      img.onload = () => {
        const c = document.createElement('canvas');
        c.width = c.height = 256;
        c.getContext('2d').drawImage(img, 0, 0, 256, 256);
        c.toBlob(b => b ? resolve(b) : reject(new Error('PNG encoding failed')), 'image/png');
      };
      img.onerror = () => reject(new Error('icon render failed'));
      img.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg);
    });
  }

  // Writes icon.png into the mod folder through the upload endpoint (the file API has no root files).
  async function saveModIcon(folder, pngBlob) {
    await api(`/api/upload?folder=${encodeURIComponent(folder)}&name=icon.png`, {
      method: 'POST',
      headers: { 'Content-Type': 'image/png' },
      body: pngBlob
    });
    forgetModIcon(folder);
  }

  // The picker shared by the icon dialog and the New mod dialog. onPicked(blob | null) runs when
  // the user chooses a file (null when it cannot be read).
  function iconPickerHtml(previewHtml) {
    return `
      <div class="icon-picker">
        <div class="icon-picker-preview" id="icon-picker-preview">${previewHtml}</div>
        <div class="icon-picker-side">
          <label class="btn btn-wood btn-sm btn-upload">Choose image...
            <input type="file" id="icon-picker-file" accept="image/png,image/jpeg,image/gif,image/bmp,image/webp" style="display:none;">
          </label>
          <p class="icon-picker-hint">PNG or JPG. The centre square is used and saved as a 256 x 256 PNG.</p>
          <p class="icon-picker-error" id="icon-picker-error"></p>
        </div>
      </div>`;
  }

  function bindIconPicker(modal, onPicked) {
    const input = modal.querySelector('#icon-picker-file');
    if (!input) return;
    let previewUrl = null;
    input.addEventListener('change', async () => {
      const file = input.files[0];
      input.value = '';
      if (!file) return;
      const errEl = modal.querySelector('#icon-picker-error');
      try {
        const blob = await imageFileToIconBlob(file);
        if (previewUrl) URL.revokeObjectURL(previewUrl);
        previewUrl = URL.createObjectURL(blob);
        modal.querySelector('#icon-picker-preview').innerHTML = `<img src="${previewUrl}" alt="">`;
        if (errEl) errEl.textContent = '';
        onPicked(blob);
      } catch (err) {
        if (errEl) errEl.textContent = err.message;
        onPicked(null);
      }
    });
  }

  // "Change icon" in the editor header.
  function openChangeIconDialog() {
    if (!state.currentModData) return;
    const folder = state.currentModFolder;
    const modal = document.getElementById('modal-container');
    if (!modal) return;
    const meta = state.mods.find(m => m.folder === folder) || { folder, name: state.currentModData.mod.name, has_icon: false };
    let picked = null;

    modal.innerHTML = `
      <div class="modal-dialog">
        <div class="modal-header">
          <h4 class="modal-title">Mod icon</h4>
          <button class="btn-modal-close" id="modal-btn-close">&times;</button>
        </div>
        <div class="modal-body">${iconPickerHtml(modIconHtml(meta))}</div>
        <div class="modal-footer">
          <button class="btn btn-secondary btn-sm" id="modal-btn-cancel">Cancel</button>
          <button class="btn btn-gold btn-sm" id="modal-btn-save" disabled>Save icon</button>
        </div>
      </div>`;
    modal.style.display = 'flex';
    hydrateModIcons(modal);

    const close = () => { modal.style.display = 'none'; modal.innerHTML = ''; };
    modal.querySelector('#modal-btn-close').onclick = close;
    modal.querySelector('#modal-btn-cancel').onclick = close;
    const saveBtn = modal.querySelector('#modal-btn-save');
    bindIconPicker(modal, blob => { picked = blob; saveBtn.disabled = !blob; });

    saveBtn.onclick = async () => {
      if (!picked) return;
      saveBtn.disabled = true;
      try {
        await saveModIcon(folder, picked);
        close();
        showToast('Mod icon saved.');
        await refreshModsList();
        renderModHeader();
      } catch (err) {
        saveBtn.disabled = false;
        showToast('Could not save the icon: ' + err.message, 'error');
      }
    };
  }

  // Mod details (name, version, author, description) are in mod.json. Edits apply at once and Save
  // writes them, like every other change.
  function openDetailsDialog() {
    if (!state.currentModData) return;
    const modal = document.getElementById('modal-container');
    if (!modal) return;
    const mod = state.currentModData.mod;
    modal.innerHTML = `
      <div class="modal-dialog">
        <div class="modal-header">
          <h4 class="modal-title">Mod details</h4>
          <button class="btn-modal-close" id="modal-btn-close">&times;</button>
        </div>
        <div class="modal-body">
          <div class="form-grid-3">
            <div class="form-group">
              <label for="mod-input-name">Mod Name</label>
              <input type="text" id="mod-input-name" class="form-input" placeholder="e.g. Monklanians" value="${escapeHtml(mod.name || '')}">
            </div>
            <div class="form-group">
              <label for="mod-input-version">Version</label>
              <input type="text" id="mod-input-version" class="form-input" placeholder="0.1" value="${escapeHtml(mod.version || '')}">
            </div>
            <div class="form-group">
              <label for="mod-input-author">Author</label>
              <input type="text" id="mod-input-author" class="form-input" placeholder="Your name or handle" value="${escapeHtml(mod.author || '')}">
            </div>
          </div>
          <div class="form-group mt-3">
            <label for="mod-input-description">Mod Description</label>
            <textarea id="mod-input-description" class="form-textarea" rows="3" placeholder="Describe the mod and its changes...">${escapeHtml(mod.description || '')}</textarea>
          </div>
        </div>
        <div class="modal-footer">
          <button class="btn btn-gold btn-sm" id="modal-btn-done">Done</button>
        </div>
      </div>`;
    modal.style.display = 'flex';

    const close = () => { modal.style.display = 'none'; modal.innerHTML = ''; };
    modal.querySelector('#modal-btn-close').onclick = close;
    modal.querySelector('#modal-btn-done').onclick = () => { close(); refreshEditor(); };

    const field = (id, key) => {
      modal.querySelector(id).addEventListener('input', (e) => {
        if (!state.currentModData) return;
        state.currentModData.mod[key] = e.target.value;
        renderModHeader();
        checkDirty();
      });
    };
    field('#mod-input-name', 'name');
    field('#mod-input-version', 'version');
    field('#mod-input-author', 'author');
    field('#mod-input-description', 'description');
  }

  // ==========================================================================
  // Mod Header
  // ==========================================================================
  function renderModHeader() {
    const mod = state.currentModData.mod;
    const folder = state.currentModFolder;
    const nameEl = document.getElementById('mod-header-name');
    const versionEl = document.getElementById('mod-header-version');
    const authorEl = document.getElementById('mod-header-author');
    const enabledToggle = document.getElementById('mod-enabled-toggle');
    const iconEl = document.getElementById('mod-header-icon');

    if (nameEl) nameEl.textContent = mod.name || folder;
    if (versionEl) versionEl.textContent = 'v' + (mod.version || '0.1');
    if (authorEl) authorEl.textContent = mod.author ? `by ${mod.author}` : 'No author specified';
    if (enabledToggle) enabledToggle.checked = mod.enabled !== false;

    if (iconEl) {
      const meta = state.mods.find(m => m.folder === folder) || { folder, has_icon: false };
      iconEl.innerHTML = modIconHtml({ folder, name: mod.name || folder, has_icon: meta.has_icon });
      hydrateModIcons(iconEl);
    }
  }

  // Re-renders the header and the open editor (Basic workspace) after the mod data changed.
  function refreshEditor() {
    if (!state.currentModData) return;
    renderModHeader();
    checkDirty();
    refreshListeners.forEach(fn => { try { fn(state.currentModData); } catch (e) { console.error(e); } });
  }

  // ==========================================================================
  // Modals & New Mod
  // ==========================================================================
  function openNewModModal() {
    const modal = document.getElementById('modal-container');
    if (!modal) return;
    // the preview shows the generated icon for the name typed so far, until a picture is chosen
    const previewFor = (name) => generatedIconSvg(name.replace(/[^A-Za-z0-9_\- ]/g, ''), name);
    modal.innerHTML = `
      <div class="modal-dialog">
        <div class="modal-header">
          <h4 class="modal-title">Create New Mod</h4>
          <button class="btn-modal-close" id="modal-btn-close">&times;</button>
        </div>
        <div class="modal-body">
          <div class="form-group">
            <label for="new-mod-name-input">Mod Name <span class="required">*</span></label>
            <input type="text" id="new-mod-name-input" class="form-input" placeholder="e.g. Roman Empire" autofocus>
          </div>
          <div class="form-group mt-3">
            <label>Icon <span class="text-muted">(optional)</span></label>
            ${iconPickerHtml(previewFor(''))}
          </div>
        </div>
        <div class="modal-footer">
          <button class="btn btn-secondary btn-sm" id="modal-btn-cancel">Cancel</button>
          <button class="btn btn-gold btn-sm" id="modal-btn-create">Create Mod</button>
        </div>
      </div>
    `;
    modal.style.display = 'flex';

    const close = () => { modal.style.display = 'none'; modal.innerHTML = ''; };
    modal.querySelector('#modal-btn-close').onclick = close;
    modal.querySelector('#modal-btn-cancel').onclick = close;

    const input = modal.querySelector('#new-mod-name-input');
    let picked = null;
    input.addEventListener('input', () => {
      if (!picked) modal.querySelector('#icon-picker-preview').innerHTML = previewFor(input.value.trim());
    });
    bindIconPicker(modal, blob => {
      picked = blob;
      if (!blob) modal.querySelector('#icon-picker-preview').innerHTML = previewFor(input.value.trim());
    });

    const create = async () => {
      const name = input.value.trim();
      if (!name) {
        showToast('Please enter a mod name', 'error');
        return;
      }
      try {
        const res = await api('/api/mods', {
          method: 'POST',
          body: { name }
        });
        {
          // No pick: save the generated shield, so the in-game Mods screen shows it too.
          try {
            await saveModIcon(res.folder, picked || await generatedIconPng(res.folder, name));
          } catch (err) {
            showToast('Mod created, but the icon could not be saved: ' + err.message, 'error', 6000);
          }
        }
        close();
        showToast(`Created mod "${name}"!`);
        await refreshModsList();
        loadMod(res.folder);
      } catch (err) {
        showToast('Failed to create mod: ' + err.message, 'error');
      }
    };

    modal.querySelector('#modal-btn-create').onclick = create;
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') create();
    });
  }

  // ==========================================================================
  // Save, Validate, Revert Actions
  // ==========================================================================
  async function saveMod(opts = {}) {
    if (!state.currentModFolder || !state.currentModData) return false;

    try {
      const folder = state.currentModFolder;
      const payload = {
        mod: state.currentModData.mod,
        civs: state.currentModData.civs,
        deleted_civs: state.currentModData.deleted_civs || []
      };

      await api(`/api/mod/${encodeURIComponent(folder)}`, {
        method: 'PUT',
        body: payload
      });

      state.currentModData.deleted_civs = [];
      takeSnapshot();
      const params = parseHashAndParams();
      if (params.get('dirty') === '1') {
        state.isDirty = true;
        setDirty(true);
      }
      await refreshModsList();
      if (!opts.silent) showToast('Mod saved successfully!', 'success');
      return true;
    } catch (err) {
      showToast('Save failed: ' + err.message, 'error');
      return false;
    }
  }

  // Save everything: the registered hooks (code, visual scripts), then mod.json and civs/.
  async function saveAll() {
    if (!state.currentModFolder || !state.currentModData) return;
    try {
      for (const h of saveHooks) await h.fn();
    } catch (err) {
      showToast('Save failed: ' + err.message, 'error');
      return;
    }
    if (await saveMod({ silent: true })) showToast('All changes saved', 'success');
  }

  async function validateMod() {
    if (!state.currentModFolder || !state.currentModData) return;

    const valBtn = document.getElementById('btn-validate');
    if (valBtn) {
      valBtn.disabled = true;
      valBtn.querySelector('span').textContent = 'Validating...';
    }

    try {
      const payload = {
        folder: state.currentModFolder,
        mod: state.currentModData.mod,
        civs: state.currentModData.civs,
        deleted_civs: state.currentModData.deleted_civs || []
      };

      const res = await api('/api/validate', {
        method: 'POST',
        body: payload
      });

      if (res.ok) {
        showToast(`Validation Passed! Built ${res.civs_built || 1} civilization(s) with zero errors.`, 'success', 5000);
      } else {
        showToast(`Validation Failed: ${res.error || 'Unknown error'}`, 'error', 7000);
      }
    } catch (err) {
      showToast('Validation failed: ' + err.message, 'error');
    } finally {
      if (valBtn) {
        valBtn.disabled = false;
        valBtn.querySelector('span').textContent = 'Validate';
      }
    }
  }

  function revertMod() {
    if (!state.isDirty || !state.snapshot) return;

    showConfirm(
      'Revert Changes',
      'Are you sure you want to discard all unsaved changes and reload the last saved state?',
      'Revert',
      () => {
        try {
          const restored = JSON.parse(state.snapshot);
          state.currentModData.mod = restored.mod;
          state.currentModData.civs = restored.civs;
          state.currentModData.deleted_civs = restored.deleted_civs || [];
          revertHooks.forEach(fn => { try { fn(); } catch (e) { console.error(e); } });
          extraDirty.clear();
          refreshEditor();
          showToast('Changes reverted.');
        } catch (e) {
          showToast('Failed to revert: ' + e.message, 'error');
        }
      }
    );
  }

  // ==========================================================================
  // Event Listeners Setup
  // ==========================================================================
  function setupEventListeners() {
    // Editor top bar: back to the mod list, Sprites pane (the New mod card is bound in renderModCards)
    document.getElementById('btn-back-mods')?.addEventListener('click', goHome);

    const openModsDir = () => api('/api/open-folder', { method: 'POST', body: {} });
    document.getElementById('btn-open-mods-dir')?.addEventListener('click', openModsDir);

    document.getElementById('btn-open-mod-folder')?.addEventListener('click', () => {
      if (state.currentModFolder) {
        api('/api/open-folder', { method: 'POST', body: { folder: state.currentModFolder } });
      }
    });

    // Mod Search
    const modSearch = document.getElementById('mod-search');
    const modSearchClear = document.getElementById('mod-search-clear');
    if (modSearch) {
      modSearch.addEventListener('input', () => {
        if (modSearchClear) modSearchClear.style.display = modSearch.value ? 'block' : 'none';
        renderModCards();
      });
    }
    if (modSearchClear) {
      modSearchClear.addEventListener('click', () => {
        if (modSearch) modSearch.value = '';
        modSearchClear.style.display = 'none';
        renderModCards();
      });
    }

    // Mod Header Actions
    document.getElementById('btn-save')?.addEventListener('click', saveAll);
    document.getElementById('btn-validate')?.addEventListener('click', validateMod);
    document.getElementById('btn-revert')?.addEventListener('click', revertMod);
    document.getElementById('btn-change-icon')?.addEventListener('click', openChangeIconDialog);
    document.getElementById('btn-mod-details')?.addEventListener('click', openDetailsDialog);

    // Mod Enabled Toggle
    document.getElementById('mod-enabled-toggle')?.addEventListener('change', (e) => {
      if (state.currentModData && state.currentModData.mod) {
        state.currentModData.mod.enabled = e.target.checked;
        document.getElementById('mod-enabled-label').textContent = e.target.checked ? 'Enabled' : 'Disabled';
        checkDirty();
      }
    });

    // Global Keyboard Shortcuts (Ctrl+S / Cmd+S)
    window.addEventListener('keydown', (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
        e.preventDefault();
        saveAll();
      }
      if (e.key === 'Escape') {
        const modal = document.getElementById('modal-container');
        if (modal && modal.style.display === 'flex') {
          modal.style.display = 'none';
          modal.innerHTML = '';
        }
      }
    });

    // Confirm navigation / close if unsaved changes
    window.addEventListener('beforeunload', (e) => {
      if (state.isDirty) {
        e.preventDefault();
        e.returnValue = '';
      }
    });
  }

  // Bridge for the other modules (basic.js: the Basic workspace, graph.js: script graphs).
  // They edit the same state.currentModData objects and call refresh() after a change.
  const listeners = [];
  const refreshListeners = [];
  window.Scriptorium = {
    state, api, loadMod, saveMod, refreshModsList, showToast, showConfirm,
    setDirty, checkDirty, showPane, goHome, showHome,
    setExtraDirty, onSave, onRevert, saveAll,
    refresh: refreshEditor,
    onModLoaded(fn) { listeners.push(fn); },
    onRefresh(fn) { refreshListeners.push(fn); },
    _modLoaded() { listeners.forEach(fn => { try { fn(state.currentModData); } catch (e) { console.error(e); } }); },
  };

  // DOM Content Loaded entry point
  document.addEventListener('DOMContentLoaded', () => {
    setupEventListeners();
    init();
  });
})();

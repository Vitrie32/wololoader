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
    activeCivFile: null,       // e.g. "monklanians.json"
    activeTab: 'overview',     // 'overview', 'identity', 'bonuses', 'units', 'techtree'
    selectedUnitId: null,
    statsCache: new Map(),     // "civId:unitId" -> stats object
    filteredUnits: [],
    unitsVirtual: {
      rowHeight: 44,
      overscan: 6,
      startIndex: 0,
      endIndex: 20
    },
    pingInterval: null,
    lastPingSuccess: true
  };

  // Preset bonus definitions as specified in SPEC.md
  const PRESET_DEFS = [
    {
      id: 'food',
      title: 'Starting Food Bonus',
      category: 'Economy',
      unitLabel: 'Food',
      defaultVal: 100,
      matches: (e) => e.type === 1 && e.a === 0 && e.b === 1,
      getVal: (e) => e.d || 0,
      create: (v) => ({ type: 1, a: 0, b: 1, c: -1, d: Number(v) })
    },
    {
      id: 'wood',
      title: 'Starting Wood Bonus',
      category: 'Economy',
      unitLabel: 'Wood',
      defaultVal: 100,
      matches: (e) => e.type === 1 && e.a === 1 && e.b === 1,
      getVal: (e) => e.d || 0,
      create: (v) => ({ type: 1, a: 1, b: 1, c: -1, d: Number(v) })
    },
    {
      id: 'stone',
      title: 'Starting Stone Bonus',
      category: 'Economy',
      unitLabel: 'Stone',
      defaultVal: 100,
      matches: (e) => e.type === 1 && e.a === 2 && e.b === 1,
      getVal: (e) => e.d || 0,
      create: (v) => ({ type: 1, a: 2, b: 1, c: -1, d: Number(v) })
    },
    {
      id: 'gold',
      title: 'Starting Gold Bonus',
      category: 'Economy',
      unitLabel: 'Gold',
      defaultVal: 100,
      matches: (e) => e.type === 1 && e.a === 3 && e.b === 1,
      getVal: (e) => e.d || 0,
      create: (v) => ({ type: 1, a: 3, b: 1, c: -1, d: Number(v) })
    },
    {
      id: 'vill_work',
      title: 'Villager Work Rate',
      category: 'Economy',
      unitLabel: '%',
      defaultVal: 15,
      matches: (e) => e.type === 5 && (e.a === -1 || e.a === undefined) && e.b === 4 && e.c === 13,
      getVal: (e) => Math.round(((e.d || 1) - 1.0) * 100),
      create: (v) => ({ type: 5, a: -1, b: 4, c: 13, d: Number((1.0 + v / 100).toFixed(4)) })
    },
    {
      id: 'monk_hp',
      title: 'Monk Hit Points',
      category: 'Military',
      unitLabel: '%',
      defaultVal: 50,
      matches: (e) => e.type === 5 && (e.a === -1 || e.a === undefined) && e.b === 18 && e.c === 0,
      getVal: (e) => Math.round(((e.d || 1) - 1.0) * 100),
      create: (v) => ({ type: 5, a: -1, b: 18, c: 0, d: Number((1.0 + v / 100).toFixed(4)) })
    },
    {
      id: 'infantry_hp',
      title: 'Infantry Hit Points',
      category: 'Military',
      unitLabel: '%',
      defaultVal: 20,
      matches: (e) => e.type === 5 && (e.a === -1 || e.a === undefined) && e.b === 6 && e.c === 0,
      getVal: (e) => Math.round(((e.d || 1) - 1.0) * 100),
      create: (v) => ({ type: 5, a: -1, b: 6, c: 0, d: Number((1.0 + v / 100).toFixed(4)) })
    },
    {
      id: 'archer_hp',
      title: 'Archer Hit Points',
      category: 'Military',
      unitLabel: '%',
      defaultVal: 15,
      matches: (e) => e.type === 5 && (e.a === -1 || e.a === undefined) && e.b === 0 && e.c === 0,
      getVal: (e) => Math.round(((e.d || 1) - 1.0) * 100),
      create: (v) => ({ type: 5, a: -1, b: 0, c: 0, d: Number((1.0 + v / 100).toFixed(4)) })
    },
    {
      id: 'cavalry_hp',
      title: 'Cavalry Hit Points',
      category: 'Military',
      unitLabel: '%',
      defaultVal: 20,
      matches: (e) => e.type === 5 && (e.a === -1 || e.a === undefined) && e.b === 12 && e.c === 0,
      getVal: (e) => Math.round(((e.d || 1) - 1.0) * 100),
      create: (v) => ({ type: 5, a: -1, b: 12, c: 0, d: Number((1.0 + v / 100).toFixed(4)) })
    },
    {
      id: 'team_monastery',
      title: 'Team: Monastery Work Rate',
      category: 'Team Bonus',
      unitLabel: '%',
      isTeam: true,
      defaultVal: 20,
      matches: (e) => e.type === 5 && (e.a === 104 || e.a === 30) && e.c === 13,
      getVal: (e) => Math.round(((e.d || 1) - 1.0) * 100),
      create: (v) => [
        { type: 5, a: 104, b: -1, c: 13, d: Number((1.0 + v / 100).toFixed(4)) },
        { type: 5, a: 30,  b: -1, c: 13, d: Number((1.0 + v / 100).toFixed(4)) }
      ]
    }
  ];

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

  function renderCivDescriptionPreview(text) {
    if (!text || !text.trim()) {
      return '<span class="text-muted" style="font-style:italic;">No description entered yet.</span>';
    }
    let escaped = escapeHtml(text);
    // AoE2 format: <b>bold<b>
    escaped = escaped.replace(/&lt;b&gt;([\s\S]*?)&lt;b&gt;/g, '<strong>$1</strong>');
    escaped = escaped.replace(/&lt;b&gt;([\s\S]*?)&lt;\/b&gt;/g, '<strong>$1</strong>');
    escaped = escaped.replace(/\n/g, '<br>');
    return escaped;
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
    if (state.activeCivFile) params.set('civ', state.activeCivFile);
    if (state.activeTab) params.set('view', state.activeTab);
    if (state.selectedUnitId) params.set('unit', state.selectedUnitId);
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

    if (options.body && typeof options.body === 'object' && !(options.body instanceof ArrayBuffer || options.body instanceof Blob)) {
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
  function startPing() {
    if (state.pingInterval) clearInterval(state.pingInterval);
    state.pingInterval = setInterval(async () => {
      try {
        await api('/api/ping', { method: 'POST' });
        if (!state.lastPingSuccess) {
          state.lastPingSuccess = true;
          document.getElementById('ping-dot').className = 'status-dot connected';
          document.getElementById('ping-text').textContent = 'Connected';
        }
      } catch (err) {
        if (state.lastPingSuccess) {
          state.lastPingSuccess = false;
          document.getElementById('ping-dot').className = 'status-dot disconnected';
          document.getElementById('ping-text').textContent = 'Disconnected';
        }
      }
    }, 2000);
  }

  // ==========================================================================
  // Mod & Civ State Management (Direct Mutation & Dirty Checking)
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
    setDirty(false);
  }

  function checkDirty() {
    if (!state.currentModData) {
      setDirty(false);
      return;
    }
    const current = JSON.stringify({
      mod: state.currentModData.mod,
      civs: state.currentModData.civs,
      deleted_civs: state.currentModData.deleted_civs || []
    });
    setDirty(current !== state.snapshot);
  }

  function setDirty(isDirty) {
    state.isDirty = isDirty;
    const badge = document.getElementById('unsaved-badge');
    const revertBtn = document.getElementById('btn-revert');
    if (badge) badge.style.display = isDirty ? 'inline-flex' : 'none';
    if (revertBtn) revertBtn.disabled = !isDirty;
  }

  function getActiveCiv() {
    if (!state.currentModData || !state.currentModData.civs || !state.activeCivFile) {
      return null;
    }
    return state.currentModData.civs[state.activeCivFile] || null;
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

      // 3. Populate base civ dropdowns & unique tech selects
      populateCivDropdowns();

      // 4. Fetch mods list
      await refreshModsList();

      // 5. Deep-linking: load requested mod or first available mod
      const reqMod = params.get('mod');
      const reqCiv = params.get('civ');
      const reqView = params.get('view');
      const reqUnit = params.get('unit');

      if (reqMod && state.mods.some(m => m.folder === reqMod)) {
        await loadMod(reqMod, reqCiv, reqView, reqUnit);
      } else if (state.mods.length > 0) {
        await loadMod(state.mods[0].folder, reqCiv, reqView, reqUnit);
      } else {
        showEmptyState();
      }

      const reqModal = params.get('modal');
      if (reqModal === 'new-civ') openNewCivModal();
      else if (reqModal === 'new-mod') openNewModModal();

      // 6. Start ping heartbeat
      startPing();
    } catch (err) {
      showToast('Initialization error: ' + err.message, 'error', 6000);
      console.error(err);
    }
  }

  function populateCivDropdowns() {
    const baseSelect = document.getElementById('civ-select-base');
    if (baseSelect) {
      baseSelect.innerHTML = state.catalog.civs.map(c => 
        `<option value="${c.id}">${c.id}. ${escapeHtml(c.name)}</option>`
      ).join('');
    }

    const castleSelect = document.getElementById('select-unique-castle');
    const impSelect = document.getElementById('select-unique-imp');
    const techOptions = '<option value="-1">(None)</option>' + state.catalog.techs.map(t =>
      `<option value="${t.id}">${t.id}. ${escapeHtml(t.name || t.internal)}</option>`
    ).join('');

    if (castleSelect) castleSelect.innerHTML = techOptions;
    if (impSelect) impSelect.innerHTML = techOptions;
  }

  async function refreshModsList() {
    state.mods = await api('/api/mods');
    renderSidebarMods();
  }

  function renderSidebarMods() {
    const container = document.getElementById('mods-list');
    if (!container) return;

    const searchTerm = (document.getElementById('mod-search')?.value || '').toLowerCase().trim();
    const filtered = state.mods.filter(m => 
      m.name.toLowerCase().includes(searchTerm) || m.folder.toLowerCase().includes(searchTerm)
    );

    if (filtered.length === 0) {
      container.innerHTML = '<div class="table-empty-msg" style="padding:16px; font-size:12px; color:var(--text-light-dim);">No mods found</div>';
      return;
    }

    container.innerHTML = filtered.map(m => {
      const isActive = state.currentModFolder === m.folder;
      const initial = (m.name || m.folder || 'M').charAt(0).toUpperCase();
      const civCount = (m.civs || []).length;
      return `
        <div class="mod-card ${isActive ? 'active' : ''}" data-folder="${escapeHtml(m.folder)}">
          <div class="mod-card-icon">
            ${m.has_icon ? `<img src="/api/icon/${encodeURIComponent(m.folder)}" alt="">` : initial}
          </div>
          <div class="mod-card-info">
            <div class="mod-card-name">${escapeHtml(m.name)}</div>
            <div class="mod-card-meta">
              <span class="${m.enabled !== false ? 'status-dot-enabled' : 'status-dot-disabled'}" title="${m.enabled !== false ? 'Enabled' : 'Disabled'}"></span>
              <span>v${escapeHtml(m.version || '0.1')}</span>
              <span>&bull;</span>
              <span>${civCount} civ${civCount === 1 ? '' : 's'}</span>
            </div>
          </div>
        </div>
      `;
    }).join('');

    container.querySelectorAll('.mod-card').forEach(el => {
      el.addEventListener('click', () => {
        const folder = el.dataset.folder;
        if (folder !== state.currentModFolder) {
          trySwitchMod(folder);
        }
      });
    });
  }

  function trySwitchMod(folder) {
    if (state.isDirty) {
      showConfirm(
        'Unsaved Changes',
        'You have unsaved changes in the current mod. Discard them and load another mod?',
        'Discard & Switch',
        () => loadMod(folder)
      );
    } else {
      loadMod(folder);
    }
  }

  async function loadMod(folder, targetCiv = null, targetView = null, targetUnit = null) {
    try {
      const data = await api(`/api/mod/${encodeURIComponent(folder)}`);
      state.currentModFolder = folder;
      state.currentModData = {
        folder: data.folder,
        mod: data.mod || {},
        civs: data.civs || {},
        deleted_civs: []
      };

      // Select active civ: targetCiv -> or first civ in data.civs -> or null
      const civFiles = Object.keys(state.currentModData.civs);
      if (targetCiv && state.currentModData.civs[targetCiv]) {
        state.activeCivFile = targetCiv;
      } else if (civFiles.length > 0) {
        state.activeCivFile = civFiles[0];
      } else {
        state.activeCivFile = null;
      }

      takeSnapshot();
      const params = parseHashAndParams();
      if (params.get('dirty') === '1') {
        state.isDirty = true;
        setDirty(true);
      }

      // Show workspace, hide empty state
      document.getElementById('empty-state').style.display = 'none';
      document.getElementById('mod-workspace').style.display = 'flex';

      // Render Mod Header
      renderModHeader();

      // Render Civ Switcher in nav bar
      renderCivSwitcher();

      // Update Sidebar active state
      renderSidebarMods();

      // Switch to target view (or overview)
      const viewToLoad = targetView || (state.activeCivFile ? 'identity' : 'overview');
      switchTab(viewToLoad);

      if (targetUnit) {
        selectUnit(Number(targetUnit), true);
      }

      updateUrlState();
    } catch (err) {
      showToast('Failed to load mod: ' + err.message, 'error');
    }
  }

  function showEmptyState() {
    state.currentModFolder = null;
    state.currentModData = null;
    state.activeCivFile = null;
    document.getElementById('empty-state').style.display = 'flex';
    document.getElementById('mod-workspace').style.display = 'none';
  }

  // ==========================================================================
  // Mod Header & Overview
  // ==========================================================================
  function renderModHeader() {
    const mod = state.currentModData.mod;
    const nameEl = document.getElementById('mod-header-name');
    const versionEl = document.getElementById('mod-header-version');
    const authorEl = document.getElementById('mod-header-author');
    const enabledToggle = document.getElementById('mod-enabled-toggle');
    const badgeEl = document.getElementById('mod-header-badge');

    if (nameEl) nameEl.textContent = mod.name || state.currentModFolder;
    if (versionEl) versionEl.textContent = 'v' + (mod.version || '0.1');
    if (authorEl) authorEl.textContent = mod.author ? `by ${mod.author}` : 'No author specified';
    if (enabledToggle) enabledToggle.checked = mod.enabled !== false;

    if (badgeEl) {
      const initial = (mod.name || state.currentModFolder || 'M').charAt(0).toUpperCase();
      badgeEl.textContent = initial;
    }

    // Populate Overview Tab fields
    const inputName = document.getElementById('mod-input-name');
    const inputVersion = document.getElementById('mod-input-version');
    const inputAuthor = document.getElementById('mod-input-author');
    const inputDesc = document.getElementById('mod-input-description');

    if (inputName) inputName.value = mod.name || '';
    if (inputVersion) inputVersion.value = mod.version || '';
    if (inputAuthor) inputAuthor.value = mod.author || '';
    if (inputDesc) inputDesc.value = mod.description || '';

    renderCivCardsGrid();
  }

  function renderCivCardsGrid() {
    const container = document.getElementById('civs-cards-grid');
    const countLabel = document.getElementById('civs-count-label');
    if (!container) return;

    const civsObj = state.currentModData.civs || {};
    const civFiles = Object.keys(civsObj);
    if (countLabel) {
      countLabel.textContent = `${civFiles.length} civilization${civFiles.length === 1 ? '' : 's'} in this mod`;
    }

    if (civFiles.length === 0) {
      container.innerHTML = `
        <div class="empty-state-card" style="grid-column: 1 / -1; padding: 24px;">
          <h4>No civilizations created yet</h4>
          <p style="font-size:12px; margin-bottom:14px;">This mod does not have any custom civilizations. Add your first civ now!</p>
          <button class="btn btn-gold btn-sm" id="btn-empty-grid-new-civ">+ Create Civilization</button>
        </div>
      `;
      const btn = document.getElementById('btn-empty-grid-new-civ');
      if (btn) btn.onclick = openNewCivModal;
      return;
    }

    container.innerHTML = civFiles.map(file => {
      const civ = civsObj[file];
      const baseCiv = state.catalog.civs.find(c => c.id === civ.base);
      const baseName = baseCiv ? baseCiv.name : `Civ #${civ.base || 1}`;
      const unitOverrides = Object.keys(civ.units || {}).length;
      const effectCount = (civ.effects || []).length + (civ.team_bonus_effects || []).length;
      const isSelected = file === state.activeCivFile;

      return `
        <div class="civ-card ${isSelected ? 'active' : ''}" data-file="${escapeHtml(file)}">
          <div class="civ-card-top">
            <div>
              <div class="civ-card-name">${escapeHtml(civ.name || file)}</div>
              <div class="civ-card-dat">DAT: ${escapeHtml(civ.dat_name || 'CIV')} &bull; ${file}</div>
            </div>
            <span class="civ-base-tag">Base: ${escapeHtml(baseName)}</span>
          </div>
          <div class="civ-card-meta">
            <span class="civ-meta-tag">${unitOverrides} unit override${unitOverrides === 1 ? '' : 's'}</span>
            <span class="civ-meta-tag">${effectCount} effect${effectCount === 1 ? '' : 's'}</span>
            <span class="civ-meta-tag">${(civ.ai_names || []).length} AI leader${(civ.ai_names || []).length === 1 ? '' : 's'}</span>
          </div>
          <div class="civ-card-actions">
            <button class="btn btn-wood btn-xs btn-delete-civ" data-file="${escapeHtml(file)}" title="Delete civilization">
              Delete
            </button>
            <button class="btn btn-wood btn-xs btn-edit-civ" data-file="${escapeHtml(file)}">
              Edit Civilization &rarr;
            </button>
          </div>
        </div>
      `;
    }).join('');

    container.querySelectorAll('.btn-edit-civ').forEach(btn => {
      btn.addEventListener('click', (e) => {
        e.stopPropagation();
        const file = btn.dataset.file;
        selectCiv(file);
        switchTab('identity');
      });
    });

    container.querySelectorAll('.btn-delete-civ').forEach(btn => {
      btn.addEventListener('click', (e) => {
        e.stopPropagation();
        const file = btn.dataset.file;
        deleteCiv(file);
      });
    });
  }

  function renderCivSwitcher() {
    const dropdown = document.getElementById('civ-select-dropdown');
    const container = document.getElementById('civ-selector-container');
    const civTabs = document.querySelectorAll('.civ-tab');

    if (!dropdown || !container) return;

    const civFiles = Object.keys(state.currentModData.civs || {});
    if (civFiles.length === 0) {
      container.style.display = 'none';
      civTabs.forEach(t => t.style.display = 'none');
      return;
    }

    container.style.display = 'flex';
    civTabs.forEach(t => t.style.display = 'inline-flex');

    dropdown.innerHTML = civFiles.map(file => {
      const civ = state.currentModData.civs[file];
      const isSelected = file === state.activeCivFile;
      return `<option value="${escapeHtml(file)}" ${isSelected ? 'selected' : ''}>${escapeHtml(civ.name || file)}</option>`;
    }).join('');
  }

  function selectCiv(file) {
    if (!state.currentModData.civs[file]) return;
    state.activeCivFile = file;
    renderCivSwitcher();
    renderCivCardsGrid();
    if (state.activeTab !== 'overview') {
      renderActiveTab();
    }
    updateUrlState();
  }

  function deleteCiv(file) {
    const civ = state.currentModData.civs[file];
    const name = civ ? (civ.name || file) : file;
    showConfirm(
      'Delete Civilization',
      `Are you sure you want to delete "${name}"? This file will be removed when you save.`,
      'Delete',
      () => {
        delete state.currentModData.civs[file];
        if (!state.currentModData.deleted_civs) {
          state.currentModData.deleted_civs = [];
        }
        if (!state.currentModData.deleted_civs.includes(file)) {
          state.currentModData.deleted_civs.push(file);
        }

        const remaining = Object.keys(state.currentModData.civs);
        if (state.activeCivFile === file) {
          state.activeCivFile = remaining.length > 0 ? remaining[0] : null;
        }

        checkDirty();
        renderCivSwitcher();
        renderCivCardsGrid();
        if (!state.activeCivFile) {
          switchTab('overview');
        } else {
          renderActiveTab();
        }
        showToast(`Civilization "${name}" marked for deletion.`);
      }
    );
  }

  // ==========================================================================
  // Tab Switching
  // ==========================================================================
  function switchTab(tabName) {
    state.activeTab = tabName;

    // Update nav tab buttons
    document.querySelectorAll('.nav-tab').forEach(btn => {
      const isCurrent = btn.dataset.tab === tabName;
      btn.classList.toggle('active', isCurrent);
      btn.setAttribute('aria-selected', isCurrent ? 'true' : 'false');
    });

    // Update views visibility
    document.querySelectorAll('.tab-view').forEach(view => {
      view.classList.remove('active');
    });

    const activeView = document.getElementById(`view-${tabName}`);
    if (activeView) activeView.classList.add('active');

    renderActiveTab();
    updateUrlState();
  }

  function renderActiveTab() {
    const civ = getActiveCiv();
    if (!civ && state.activeTab !== 'overview') {
      switchTab('overview');
      return;
    }

    switch (state.activeTab) {
      case 'overview':
        renderModHeader();
        break;
      case 'identity':
        renderIdentityTab();
        break;
      case 'bonuses':
        renderBonusesTab();
        break;
      case 'units':
        renderUnitsTab();
        break;
      case 'techtree':
        renderTechTreeTab();
        break;
    }
  }

  // ==========================================================================
  // Tab: Identity
  // ==========================================================================
  function renderIdentityTab() {
    const civ = getActiveCiv();
    if (!civ) return;

    const inputName = document.getElementById('civ-input-name');
    const inputDat = document.getElementById('civ-input-dat-name');
    const selectBase = document.getElementById('civ-select-base');
    const inputDesc = document.getElementById('civ-input-description');
    const previewEl = document.getElementById('civ-desc-preview');

    if (inputName) inputName.value = civ.name || '';
    if (inputDat) inputDat.value = civ.dat_name || '';
    if (selectBase) selectBase.value = civ.base || 1;
    if (inputDesc) inputDesc.value = civ.description || '';
    if (previewEl) previewEl.innerHTML = renderCivDescriptionPreview(civ.description);

    renderAiNamesChips();
    renderAssetStatus();
  }

  function renderAiNamesChips() {
    const civ = getActiveCiv();
    const container = document.getElementById('civ-ai-names-chips');
    if (!container || !civ) return;

    const names = civ.ai_names || [];
    if (names.length === 0) {
      container.innerHTML = '<span class="text-muted" style="font-size:12px; font-style:italic;">No AI names configured</span>';
      return;
    }

    container.innerHTML = names.map((name, idx) => `
      <div class="chip">
        <span>${escapeHtml(name)}</span>
        <button class="btn-chip-remove" data-index="${idx}" title="Remove name">&times;</button>
      </div>
    `).join('');

    container.querySelectorAll('.btn-chip-remove').forEach(btn => {
      btn.addEventListener('click', () => {
        const idx = Number(btn.dataset.index);
        civ.ai_names.splice(idx, 1);
        checkDirty();
        renderAiNamesChips();
      });
    });
  }

  function renderAssetStatus() {
    const civ = getActiveCiv();
    if (!civ) return;

    const soundLabel = document.getElementById('civ-sound-filename');
    const slpLabel = document.getElementById('civ-slp-filename');
    const clearSoundBtn = document.getElementById('btn-clear-civ-sound');
    const clearSlpBtn = document.getElementById('btn-clear-civ-slp');

    if (soundLabel) {
      soundLabel.textContent = civ.sound ? civ.sound : '(None configured)';
    }
    if (clearSoundBtn) {
      clearSoundBtn.style.display = civ.sound ? 'inline-flex' : 'none';
    }

    if (slpLabel) {
      slpLabel.textContent = civ.interface_slp ? civ.interface_slp : '(None configured)';
    }
    if (clearSlpBtn) {
      clearSlpBtn.style.display = civ.interface_slp ? 'inline-flex' : 'none';
    }
  }

  // ==========================================================================
  // Tab: Bonuses & Advanced Effects
  // ==========================================================================
  function renderBonusesTab() {
    const civ = getActiveCiv();
    if (!civ) return;

    if (!Array.isArray(civ.effects)) civ.effects = [];
    if (!Array.isArray(civ.team_bonus_effects)) civ.team_bonus_effects = [];

    renderPresetCards();
    renderAdvancedEffectsTable();
  }

  function renderPresetCards() {
    const civ = getActiveCiv();
    const container = document.getElementById('presets-grid');
    if (!container || !civ) return;

    container.innerHTML = PRESET_DEFS.map(preset => {
      const list = preset.isTeam ? civ.team_bonus_effects : civ.effects;
      const matched = list.find(preset.matches);
      const isChecked = Boolean(matched);
      const currentVal = matched ? preset.getVal(matched) : preset.defaultVal;

      return `
        <div class="preset-card ${isChecked ? 'active' : ''}" data-preset-id="${preset.id}">
          <div class="preset-info">
            <input type="checkbox" class="preset-checkbox" id="preset-chk-${preset.id}" ${isChecked ? 'checked' : ''}>
            <div>
              <label for="preset-chk-${preset.id}" class="preset-title" style="cursor:pointer;">${escapeHtml(preset.title)}</label>
              <div class="preset-category">${escapeHtml(preset.category)}</div>
            </div>
          </div>
          <div class="preset-input-wrap">
            <span style="font-weight:600; color:var(--text-muted); font-size:12px;">+</span>
            <input type="number" class="preset-input" value="${currentVal}" ${isChecked ? '' : 'disabled'}>
            <span style="font-size:11px; color:var(--text-muted);">${escapeHtml(preset.unitLabel)}</span>
          </div>
        </div>
      `;
    }).join('');

    container.querySelectorAll('.preset-card').forEach(card => {
      const presetId = card.dataset.presetId;
      const preset = PRESET_DEFS.find(p => p.id === presetId);
      const chk = card.querySelector('.preset-checkbox');
      const input = card.querySelector('.preset-input');

      chk.addEventListener('change', () => {
        const active = chk.checked;
        input.disabled = !active;
        card.classList.toggle('active', active);

        const list = preset.isTeam ? civ.team_bonus_effects : civ.effects;
        if (active) {
          const val = Number(input.value) || preset.defaultVal;
          const created = preset.create(val);
          if (Array.isArray(created)) {
            list.push(...created);
          } else {
            list.push(created);
          }
        } else {
          // Remove all matching effects
          const remaining = list.filter(e => !preset.matches(e));
          if (preset.isTeam) {
            civ.team_bonus_effects = remaining;
          } else {
            civ.effects = remaining;
          }
        }
        checkDirty();
        renderAdvancedEffectsTable();
      });

      input.addEventListener('input', () => {
        if (!chk.checked) return;
        const val = Number(input.value) || 0;
        const list = preset.isTeam ? civ.team_bonus_effects : civ.effects;

        // Remove old and re-add updated
        const filtered = list.filter(e => !preset.matches(e));
        const created = preset.create(val);
        if (Array.isArray(created)) {
          filtered.push(...created);
        } else {
          filtered.push(created);
        }

        if (preset.isTeam) {
          civ.team_bonus_effects = filtered;
        } else {
          civ.effects = filtered;
        }
        checkDirty();
        renderAdvancedEffectsTable();
      });
    });
  }

  function renderAdvancedEffectsTable() {
    const civ = getActiveCiv();
    const tbody = document.getElementById('effects-tbody');
    const emptyMsg = document.getElementById('effects-empty');
    if (!tbody || !civ) return;

    // Filter effects that are NOT claimed by any preset
    const customCivEffects = (civ.effects || []).map((eff, idx) => ({ eff, isTeam: false, origIdx: idx }))
      .filter(({ eff }) => !PRESET_DEFS.some(p => !p.isTeam && p.matches(eff)));

    const customTeamEffects = (civ.team_bonus_effects || []).map((eff, idx) => ({ eff, isTeam: true, origIdx: idx }))
      .filter(({ eff }) => !PRESET_DEFS.some(p => p.isTeam && p.matches(eff)));

    const allCustom = [...customCivEffects, ...customTeamEffects];

    if (allCustom.length === 0) {
      tbody.innerHTML = '';
      if (emptyMsg) emptyMsg.style.display = 'block';
      return;
    }

    if (emptyMsg) emptyMsg.style.display = 'none';

    tbody.innerHTML = allCustom.map((item, rowIdx) => {
      const e = item.eff;
      return `
        <tr data-row="${rowIdx}">
          <td>
            <select class="form-select eff-scope" data-isteam="${item.isTeam}" data-origidx="${item.origIdx}">
              <option value="civ" ${!item.isTeam ? 'selected' : ''}>Civ Bonus</option>
              <option value="team" ${item.isTeam ? 'selected' : ''}>Team Bonus</option>
            </select>
          </td>
          <td>
            <select class="form-select eff-type" data-isteam="${item.isTeam}" data-origidx="${item.origIdx}">
              <option value="0" ${e.type === 0 ? 'selected' : ''}>0 - Set Attribute</option>
              <option value="1" ${e.type === 1 ? 'selected' : ''}>1 - Resource</option>
              <option value="2" ${e.type === 2 ? 'selected' : ''}>2 - Enable Unit</option>
              <option value="3" ${e.type === 3 ? 'selected' : ''}>3 - Upgrade Unit</option>
              <option value="4" ${e.type === 4 ? 'selected' : ''}>4 - Add Attribute</option>
              <option value="5" ${e.type === 5 ? 'selected' : ''}>5 - Multiply Attribute</option>
              <option value="6" ${e.type === 6 ? 'selected' : ''}>6 - Multiply Resource</option>
              <option value="101" ${e.type === 101 ? 'selected' : ''}>101 - Tech Cost</option>
              <option value="102" ${e.type === 102 ? 'selected' : ''}>102 - Disable Tech</option>
              <option value="103" ${e.type === 103 ? 'selected' : ''}>103 - Tech Time</option>
            </select>
          </td>
          <td><input type="number" class="form-input eff-a" value="${e.a ?? -1}"></td>
          <td><input type="number" class="form-input eff-b" value="${e.b ?? -1}"></td>
          <td><input type="number" class="form-input eff-c" value="${e.c ?? -1}"></td>
          <td><input type="number" step="any" class="form-input eff-d" value="${e.d ?? 0}"></td>
          <td style="text-align: center;">
            <button class="btn btn-secondary btn-xs btn-delete-eff" title="Delete effect" style="color:var(--color-red);">
              &times;
            </button>
          </td>
        </tr>
      `;
    }).join('');

    // Bind inputs
    tbody.querySelectorAll('tr').forEach((tr, rowIdx) => {
      const item = allCustom[rowIdx];
      const eff = item.eff;
      const isTeam = item.isTeam;
      const origIdx = item.origIdx;

      const scopeSelect = tr.querySelector('.eff-scope');
      const typeSelect = tr.querySelector('.eff-type');
      const inputA = tr.querySelector('.eff-a');
      const inputB = tr.querySelector('.eff-b');
      const inputC = tr.querySelector('.eff-c');
      const inputD = tr.querySelector('.eff-d');
      const delBtn = tr.querySelector('.btn-delete-eff');

      const updateEff = () => {
        eff.type = Number(typeSelect.value);
        eff.a = Number(inputA.value);
        eff.b = Number(inputB.value);
        eff.c = Number(inputC.value);
        eff.d = Number(inputD.value);
        checkDirty();
      };

      typeSelect.addEventListener('change', updateEff);
      inputA.addEventListener('input', updateEff);
      inputB.addEventListener('input', updateEff);
      inputC.addEventListener('input', updateEff);
      inputD.addEventListener('input', updateEff);

      scopeSelect.addEventListener('change', () => {
        const newIsTeam = scopeSelect.value === 'team';
        if (newIsTeam === isTeam) return;

        // Move effect between arrays
        if (isTeam) {
          civ.team_bonus_effects.splice(origIdx, 1);
          civ.effects.push(eff);
        } else {
          civ.effects.splice(origIdx, 1);
          civ.team_bonus_effects.push(eff);
        }
        checkDirty();
        renderAdvancedEffectsTable();
      });

      delBtn.addEventListener('click', () => {
        if (isTeam) {
          civ.team_bonus_effects.splice(origIdx, 1);
        } else {
          civ.effects.splice(origIdx, 1);
        }
        checkDirty();
        renderAdvancedEffectsTable();
      });
    });
  }

  // ==========================================================================
  // Tab: Units (Virtualized Catalog & Stats Detail)
  // ==========================================================================
  function renderUnitsTab() {
    filterUnitsList();
    renderUnitsVirtualList();
    if (state.selectedUnitId) {
      renderUnitDetail();
    }
  }

  function filterUnitsList() {
    const civ = getActiveCiv();
    const searchInput = document.getElementById('units-search');
    const filterModified = document.getElementById('units-filter-modified')?.checked;
    const term = (searchInput?.value || '').toLowerCase().trim();

    state.filteredUnits = state.catalog.units.filter(u => {
      const isMod = civ && civ.units && civ.units[u.id] && Object.keys(civ.units[u.id]).length > 0;
      if (filterModified && !isMod) return false;

      if (!term) return true;
      const idMatch = String(u.id).includes(term);
      const nameMatch = (u.name || '').toLowerCase().includes(term);
      const internalMatch = (u.internal || '').toLowerCase().includes(term);
      return idMatch || nameMatch || internalMatch;
    });

    const countLabel = document.getElementById('units-count-display');
    if (countLabel) {
      countLabel.textContent = `${state.filteredUnits.length.toLocaleString()} unit${state.filteredUnits.length === 1 ? '' : 's'}`;
    }

    // Default select first unit if none selected
    if (!state.selectedUnitId && state.filteredUnits.length > 0) {
      state.selectedUnitId = state.filteredUnits[0].id;
    }
  }

  function renderUnitsVirtualList() {
    const viewport = document.getElementById('units-virtual-viewport');
    const spacer = document.getElementById('units-phantom-spacer');
    const rowsContainer = document.getElementById('units-visible-rows');
    if (!viewport || !spacer || !rowsContainer) return;

    const totalCount = state.filteredUnits.length;
    const rowHeight = state.unitsVirtual.rowHeight;
    const totalHeight = totalCount * rowHeight;
    spacer.style.height = `${totalHeight}px`;

    const scrollTop = viewport.scrollTop;
    const viewportHeight = viewport.clientHeight || 500;
    const startIndex = Math.max(0, Math.floor(scrollTop / rowHeight) - state.unitsVirtual.overscan);
    const endIndex = Math.min(totalCount, Math.ceil((scrollTop + viewportHeight) / rowHeight) + state.unitsVirtual.overscan);

    state.unitsVirtual.startIndex = startIndex;
    state.unitsVirtual.endIndex = endIndex;

    const visibleItems = state.filteredUnits.slice(startIndex, endIndex);
    const offsetY = startIndex * rowHeight;
    rowsContainer.style.transform = `translateY(${offsetY}px)`;

    const civ = getActiveCiv();
    const overrides = civ ? (civ.units || {}) : {};

    rowsContainer.innerHTML = visibleItems.map(unit => {
      const isSelected = unit.id === state.selectedUnitId;
      const isModified = overrides[unit.id] && Object.keys(overrides[unit.id]).length > 0;

      return `
        <div class="unit-row ${isSelected ? 'selected' : ''}" data-id="${unit.id}">
          <div class="unit-row-left">
            <span class="unit-row-id">#${unit.id}</span>
            <div class="unit-row-names">
              <div class="unit-row-name">${escapeHtml(unit.name || unit.internal)}</div>
              <div class="unit-row-internal">${escapeHtml(unit.internal)}</div>
            </div>
          </div>
          ${isModified ? '<span class="badge-modified" title="Overridden in this civilization"></span>' : ''}
        </div>
      `;
    }).join('');

    rowsContainer.querySelectorAll('.unit-row').forEach(row => {
      row.addEventListener('click', () => {
        const id = Number(row.dataset.id);
        selectUnit(id);
      });
    });
  }

  function selectUnit(id, scrollIntoView = false) {
    state.selectedUnitId = id;
    if (scrollIntoView && state.filteredUnits) {
      const idx = state.filteredUnits.findIndex(u => u.id === id);
      if (idx >= 0) {
        const viewport = document.getElementById('units-virtual-viewport');
        if (viewport) {
          viewport.scrollTop = Math.max(0, (idx - 2) * state.unitsVirtual.rowHeight);
        }
      }
    }
    renderUnitsVirtualList();
    renderUnitDetail();
    updateUrlState();
  }

  async function renderUnitDetail() {
    const emptyEl = document.getElementById('unit-detail-empty');
    const contentEl = document.getElementById('unit-detail-content');
    if (!emptyEl || !contentEl) return;

    const unit = state.catalog.units.find(u => u.id === state.selectedUnitId);
    if (!unit) {
      emptyEl.style.display = 'flex';
      contentEl.style.display = 'none';
      return;
    }

    emptyEl.style.display = 'none';
    contentEl.style.display = 'block';

    const civ = getActiveCiv();
    const baseCivId = civ ? (civ.base || 1) : 1;

    // Header info
    document.getElementById('unit-detail-name').textContent = unit.name || unit.internal;
    document.getElementById('unit-detail-id').textContent = `#${unit.id}`;
    document.getElementById('unit-detail-internal').textContent = unit.internal;
    document.getElementById('unit-detail-class').textContent = `Class: ${unit.class_ ?? 'N/A'}`;

    // Fetch base stats from cache or backend
    const cacheKey = `${baseCivId}:${unit.id}`;
    let baseStats = state.statsCache.get(cacheKey);
    if (!baseStats) {
      try {
        baseStats = await api(`/api/stats?civ=${baseCivId}&unit=${unit.id}`);
        state.statsCache.set(cacheKey, baseStats);
      } catch (e) {
        baseStats = {};
      }
    }

    const fieldsList = document.getElementById('unit-stats-fields-list');
    if (!fieldsList) return;

    const overrides = (civ && civ.units && civ.units[unit.id]) ? civ.units[unit.id] : {};
    const availableStats = state.catalog.stats.filter(s => baseStats[s.field] !== undefined);

    if (availableStats.length === 0) {
      fieldsList.innerHTML = '<div class="table-empty-msg" style="padding:16px;">This unit has no editable numeric stats.</div>';
      return;
    }

    fieldsList.innerHTML = availableStats.map(stat => {
      const baseVal = baseStats[stat.field];
      const hasOverride = overrides[stat.field] !== undefined;
      const currentVal = hasOverride ? overrides[stat.field] : baseVal;
      const isFloat = stat.field === 'speed' || stat.field === 'work_rate';

      return `
        <div class="stat-field-row ${hasOverride ? 'modified' : ''}" data-field="${stat.field}">
          <div class="stat-name-col">${escapeHtml(stat.label)}</div>
          <div class="stat-base-col">${baseVal}</div>
          <div>
            <input type="number" 
                   step="${isFloat ? '0.01' : '1'}" 
                   class="stat-override-input ${hasOverride ? 'is-override' : ''}" 
                   value="${currentVal}">
          </div>
          <div>
            <button class="btn btn-secondary btn-xs btn-reset-stat" 
                    title="Reset to base value" 
                    ${hasOverride ? '' : 'disabled'}>
              Reset
            </button>
          </div>
        </div>
      `;
    }).join('');

    fieldsList.querySelectorAll('.stat-field-row').forEach(row => {
      const field = row.dataset.field;
      const input = row.querySelector('.stat-override-input');
      const resetBtn = row.querySelector('.btn-reset-stat');
      const baseVal = baseStats[field];

      input.addEventListener('input', () => {
        const val = input.value.trim() === '' ? NaN : Number(input.value);
        if (isNaN(val) || val === baseVal) {
          // Remove override
          if (civ.units && civ.units[unit.id]) {
            delete civ.units[unit.id][field];
            if (Object.keys(civ.units[unit.id]).length === 0) {
              delete civ.units[unit.id];
            }
          }
          row.classList.remove('modified');
          input.classList.remove('is-override');
          resetBtn.disabled = true;
        } else {
          // Add/update override
          if (!civ.units) civ.units = {};
          if (!civ.units[unit.id]) civ.units[unit.id] = {};
          civ.units[unit.id][field] = val;
          row.classList.add('modified');
          input.classList.add('is-override');
          resetBtn.disabled = false;
        }
        checkDirty();
        renderUnitsVirtualList();
      });

      resetBtn.addEventListener('click', () => {
        if (civ.units && civ.units[unit.id]) {
          delete civ.units[unit.id][field];
          if (Object.keys(civ.units[unit.id]).length === 0) {
            delete civ.units[unit.id];
          }
        }
        input.value = baseVal;
        row.classList.remove('modified');
        input.classList.remove('is-override');
        resetBtn.disabled = true;
        checkDirty();
        renderUnitsVirtualList();
      });
    });

    const resetUnitBtn = document.getElementById('btn-reset-unit');
    if (resetUnitBtn) {
      resetUnitBtn.onclick = () => {
        if (civ.units && civ.units[unit.id]) {
          delete civ.units[unit.id];
          checkDirty();
          renderUnitDetail();
          renderUnitsVirtualList();
          showToast(`Reset all overrides for ${unit.name || unit.internal}`);
        }
      };
    }
  }

  // ==========================================================================
  // Tab: Tech Tree (Unique Techs & 3-State Researches/Units)
  // ==========================================================================
  function renderTechTreeTab() {
    const civ = getActiveCiv();
    if (!civ) return;

    if (!Array.isArray(civ.enabled_techs)) civ.enabled_techs = [];
    if (!Array.isArray(civ.disabled_techs)) civ.disabled_techs = [];
    if (!Array.isArray(civ.enabled_units)) civ.enabled_units = [];
    if (!Array.isArray(civ.disabled_units)) civ.disabled_units = [];
    if (!Array.isArray(civ.unique_techs)) civ.unique_techs = [-1, -1];

    const params = parseHashAndParams();
    if (params.get('ttfilter') === 'changed') {
      document.querySelector('.segmented-filter .seg-btn[data-filter="tech-all"]')?.classList.remove('active');
      document.querySelector('.segmented-filter .seg-btn[data-filter="tech-changed"]')?.classList.add('active');
      document.querySelector('.segmented-filter .seg-btn[data-filter="unit-all"]')?.classList.remove('active');
      document.querySelector('.segmented-filter .seg-btn[data-filter="unit-changed"]')?.classList.add('active');
    }

    // Unique techs dropdowns
    const castleSelect = document.getElementById('select-unique-castle');
    const impSelect = document.getElementById('select-unique-imp');
    if (castleSelect) castleSelect.value = civ.unique_techs[0] ?? -1;
    if (impSelect) impSelect.value = civ.unique_techs[1] ?? -1;

    renderTechsAvailabilityList();
    renderUnitsAvailabilityList();
  }

  function renderTechsAvailabilityList() {
    const civ = getActiveCiv();
    const container = document.getElementById('tt-techs-list');
    if (!container || !civ) return;

    const searchTerm = (document.getElementById('tt-techs-search')?.value || '').toLowerCase().trim();
    const activeFilter = document.querySelector('.segmented-filter .seg-btn[data-filter="tech-changed"]')?.classList.contains('active');

    const filtered = state.catalog.techs.filter(tech => {
      const isEnabled = civ.enabled_techs.includes(tech.id);
      const isDisabled = civ.disabled_techs.includes(tech.id);
      const isChanged = isEnabled || isDisabled;
      if (activeFilter && !isChanged) return false;

      if (!searchTerm) return true;
      return String(tech.id).includes(searchTerm) ||
             (tech.name || '').toLowerCase().includes(searchTerm) ||
             (tech.internal || '').toLowerCase().includes(searchTerm);
    });

    if (filtered.length === 0) {
      container.innerHTML = '<div class="table-empty-msg" style="padding:16px;">No technologies match search.</div>';
      return;
    }

    container.innerHTML = filtered.map(tech => {
      const isEnabled = civ.enabled_techs.includes(tech.id);
      const isDisabled = civ.disabled_techs.includes(tech.id);
      const isBase = !isEnabled && !isDisabled;

      return `
        <div class="tt-row" data-id="${tech.id}">
          <div class="tt-info">
            <span class="tt-id">#${tech.id}</span>
            <div>
              <div class="tt-name">${escapeHtml(tech.name || tech.internal)}</div>
              <div class="tt-internal">${escapeHtml(tech.internal)}</div>
            </div>
          </div>
          <div class="state-tri-toggle">
            <button class="state-tri-btn ${isBase ? 'active-base' : ''}" data-state="base">Base</button>
            <button class="state-tri-btn ${isEnabled ? 'active-enabled' : ''}" data-state="enabled">Enabled</button>
            <button class="state-tri-btn ${isDisabled ? 'active-disabled' : ''}" data-state="disabled">Disabled</button>
          </div>
        </div>
      `;
    }).join('');

    container.querySelectorAll('.tt-row').forEach(row => {
      const id = Number(row.dataset.id);
      row.querySelectorAll('.state-tri-btn').forEach(btn => {
        btn.addEventListener('click', () => {
          const targetState = btn.dataset.state;
          // Clear both
          civ.enabled_techs = civ.enabled_techs.filter(x => x !== id);
          civ.disabled_techs = civ.disabled_techs.filter(x => x !== id);

          if (targetState === 'enabled') {
            civ.enabled_techs.push(id);
          } else if (targetState === 'disabled') {
            civ.disabled_techs.push(id);
          }
          checkDirty();
          renderTechsAvailabilityList();
        });
      });
    });
  }

  function renderUnitsAvailabilityList() {
    const civ = getActiveCiv();
    const container = document.getElementById('tt-units-list');
    if (!container || !civ) return;

    const searchTerm = (document.getElementById('tt-units-search')?.value || '').toLowerCase().trim();
    const activeFilter = document.querySelector('.segmented-filter .seg-btn[data-filter="unit-changed"]')?.classList.contains('active');

    const filtered = state.catalog.units.filter(unit => {
      const isEnabled = civ.enabled_units.includes(unit.id);
      const isDisabled = civ.disabled_units.includes(unit.id);
      const isChanged = isEnabled || isDisabled;
      if (activeFilter && !isChanged) return false;

      if (!searchTerm) return true;
      return String(unit.id).includes(searchTerm) ||
             (unit.name || '').toLowerCase().includes(searchTerm) ||
             (unit.internal || '').toLowerCase().includes(searchTerm);
    });

    if (filtered.length === 0) {
      container.innerHTML = '<div class="table-empty-msg" style="padding:16px;">No units match search.</div>';
      return;
    }

    container.innerHTML = filtered.map(unit => {
      const isEnabled = civ.enabled_units.includes(unit.id);
      const isDisabled = civ.disabled_units.includes(unit.id);
      const isBase = !isEnabled && !isDisabled;

      return `
        <div class="tt-row" data-id="${unit.id}">
          <div class="tt-info">
            <span class="tt-id">#${unit.id}</span>
            <div>
              <div class="tt-name">${escapeHtml(unit.name || unit.internal)}</div>
              <div class="tt-internal">${escapeHtml(unit.internal)}</div>
            </div>
          </div>
          <div class="state-tri-toggle">
            <button class="state-tri-btn ${isBase ? 'active-base' : ''}" data-state="base">Base</button>
            <button class="state-tri-btn ${isEnabled ? 'active-enabled' : ''}" data-state="enabled">Enabled</button>
            <button class="state-tri-btn ${isDisabled ? 'active-disabled' : ''}" data-state="disabled">Disabled</button>
          </div>
        </div>
      `;
    }).join('');

    container.querySelectorAll('.tt-row').forEach(row => {
      const id = Number(row.dataset.id);
      row.querySelectorAll('.state-tri-btn').forEach(btn => {
        btn.addEventListener('click', () => {
          const targetState = btn.dataset.state;
          // Clear both
          civ.enabled_units = civ.enabled_units.filter(x => x !== id);
          civ.disabled_units = civ.disabled_units.filter(x => x !== id);

          if (targetState === 'enabled') {
            civ.enabled_units.push(id);
          } else if (targetState === 'disabled') {
            civ.disabled_units.push(id);
          }
          checkDirty();
          renderUnitsAvailabilityList();
        });
      });
    });
  }

  // ==========================================================================
  // Generate Description from Bonuses
  // ==========================================================================
  function generateDescriptionFromBonuses() {
    const civ = getActiveCiv();
    if (!civ) return;

    const bullets = [];
    const teamBullets = [];

    // Check presets
    for (const preset of PRESET_DEFS) {
      const list = preset.isTeam ? (civ.team_bonus_effects || []) : (civ.effects || []);
      const matched = list.find(preset.matches);
      if (matched) {
        const val = preset.getVal(matched);
        if (preset.id === 'food') bullets.push(`Start with +${val} food`);
        else if (preset.id === 'wood') bullets.push(`Start with +${val} wood`);
        else if (preset.id === 'stone') bullets.push(`Start with +${val} stone`);
        else if (preset.id === 'gold') bullets.push(`Start with +${val} gold`);
        else if (preset.id === 'vill_work') bullets.push(`Villagers work ${val}% faster`);
        else if (preset.id === 'monk_hp') bullets.push(`Monks have +${val}% hit points`);
        else if (preset.id === 'infantry_hp') bullets.push(`Infantry have +${val}% hit points`);
        else if (preset.id === 'archer_hp') bullets.push(`Archers have +${val}% hit points`);
        else if (preset.id === 'cavalry_hp') bullets.push(`Cavalry have +${val}% hit points`);
        else if (preset.id === 'team_monastery') teamBullets.push(`Monasteries work ${val}% faster`);
      }
    }

    // Unit stat overrides
    const overrides = civ.units || {};
    for (const [unitId, stats] of Object.entries(overrides)) {
      const unit = state.catalog.units.find(u => u.id === Number(unitId));
      const uName = unit ? (unit.name || unit.internal) : `Unit #${unitId}`;
      const statParts = Object.entries(stats).map(([k, v]) => `${k.replace('_', ' ')} ${v}`);
      if (statParts.length > 0) {
        bullets.push(`${uName}: ${statParts.join(', ')}`);
      }
    }

    // Tech tree changes
    if (civ.enabled_techs && civ.enabled_techs.length > 0) {
      const names = civ.enabled_techs.map(id => {
        const t = state.catalog.techs.find(x => x.id === id);
        return t ? (t.name || t.internal) : `#${id}`;
      });
      bullets.push(`${names.join(', ')} enabled`);
    }

    if (civ.disabled_techs && civ.disabled_techs.length > 0) {
      const names = civ.disabled_techs.map(id => {
        const t = state.catalog.techs.find(x => x.id === id);
        return t ? (t.name || t.internal) : `#${id}`;
      });
      bullets.push(`${names.join(', ')} disabled`);
    }

    if (civ.disabled_units && civ.disabled_units.length > 0) {
      const names = civ.disabled_units.map(id => {
        const u = state.catalog.units.find(x => x.id === id);
        return u ? (u.name || u.internal) : `#${id}`;
      });
      bullets.push(`${names.join(', ')} disabled`);
    }

    const civName = civ.name || 'Custom Civilization';
    let text = `<b>${civName}<b> \nCustom civilization \n\n`;

    if (bullets.length > 0) {
      text += bullets.map(b => `· ${b}`).join(' \n');
    } else {
      text += '· Standard civilization bonuses';
    }

    if (teamBullets.length > 0) {
      text += ` \n\n<b>Team Bonus:<b> \n${teamBullets.join(' \n')}`;
    }

    civ.description = text;
    checkDirty();
    renderIdentityTab();
    showToast('Generated description from bonuses.');
  }

  // ==========================================================================
  // Modals & New Mod / Civ Creation
  // ==========================================================================
  function openNewModModal() {
    const modal = document.getElementById('modal-container');
    if (!modal) return;
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

  function openNewCivModal() {
    if (!state.currentModData) return;
    const modal = document.getElementById('modal-container');
    if (!modal) return;

    const civOptions = state.catalog.civs.map(c => 
      `<option value="${c.id}">${c.id}. ${escapeHtml(c.name)}</option>`
    ).join('');

    modal.innerHTML = `
      <div class="modal-dialog">
        <div class="modal-header">
          <h4 class="modal-title">New Civilization Wizard</h4>
          <button class="btn-modal-close" id="modal-btn-close">&times;</button>
        </div>
        <div class="modal-body">
          <div class="form-group">
            <label for="new-civ-name-input">Civilization Name <span class="required">*</span></label>
            <input type="text" id="new-civ-name-input" class="form-input" placeholder="e.g. Romans" autofocus>
          </div>
          <div class="form-group mt-3">
            <label for="new-civ-dat-input">DAT Name (max 19 ASCII chars) <span class="required">*</span></label>
            <input type="text" id="new-civ-dat-input" class="form-input font-mono" maxlength="19" placeholder="Romans">
          </div>
          <div class="form-group mt-3">
            <label for="new-civ-base-select">Base Civilization <span class="required">*</span></label>
            <select id="new-civ-base-select" class="form-select">
              ${civOptions}
            </select>
          </div>
        </div>
        <div class="modal-footer">
          <button class="btn btn-secondary btn-sm" id="modal-btn-cancel">Cancel</button>
          <button class="btn btn-gold btn-sm" id="modal-btn-create">Add Civilization</button>
        </div>
      </div>
    `;
    modal.style.display = 'flex';

    const close = () => { modal.style.display = 'none'; modal.innerHTML = ''; };
    modal.querySelector('#modal-btn-close').onclick = close;
    modal.querySelector('#modal-btn-cancel').onclick = close;

    const nameInput = modal.querySelector('#new-civ-name-input');
    const datInput = modal.querySelector('#new-civ-dat-input');
    const baseSelect = modal.querySelector('#new-civ-base-select');

    nameInput.addEventListener('input', () => {
      // Auto-populate DAT name
      datInput.value = nameInput.value.replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 19);
    });

    const create = () => {
      const name = nameInput.value.trim();
      const datName = datInput.value.trim() || name.replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 19);
      const baseId = Number(baseSelect.value) || 1;

      if (!name) {
        showToast('Please enter a civilization name', 'error');
        return;
      }

      // Generate a clean filename: e.g. "romans.json"
      let filename = name.toLowerCase().replace(/[^a-z0-9_\-]/g, '_') + '.json';
      let counter = 2;
      while (state.currentModData.civs[filename]) {
        filename = `${name.toLowerCase().replace(/[^a-z0-9_\-]/g, '_')}_${counter}.json`;
        counter++;
      }

      const newCiv = {
        name: name,
        dat_name: datName,
        base: baseId,
        description: `<b>${name}<b> \nCustom civilization \n\n· Standard civilization bonuses`,
        ai_names: [`Leader of ${name}`, `General ${name}`],
        enabled_techs: [],
        disabled_techs: [],
        enabled_units: [],
        disabled_units: [],
        effects: [],
        team_bonus_effects: [],
        unique_techs: [-1, -1],
        units: {}
      };

      state.currentModData.civs[filename] = newCiv;
      close();
      checkDirty();
      selectCiv(filename);
      switchTab('identity');
      showToast(`Added civilization "${name}"!`);
    };

    modal.querySelector('#modal-btn-create').onclick = create;
    nameInput.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') create();
    });
  }

  // ==========================================================================
  // Save, Validate, Revert Actions
  // ==========================================================================
  async function saveMod() {
    if (!state.currentModFolder || !state.currentModData) return;

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
      showToast('Mod saved successfully!', 'success');
    } catch (err) {
      showToast('Save failed: ' + err.message, 'error');
    }
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

          const civFiles = Object.keys(state.currentModData.civs);
          if (!state.currentModData.civs[state.activeCivFile]) {
            state.activeCivFile = civFiles.length > 0 ? civFiles[0] : null;
          }

          checkDirty();
          renderModHeader();
          renderCivSwitcher();
          renderActiveTab();
          showToast('Changes reverted.');
        } catch (e) {
          showToast('Failed to revert: ' + e.message, 'error');
        }
      }
    );
  }

  // File upload handler
  async function uploadCivAsset(fileInput, propertyKey) {
    const file = fileInput.files[0];
    if (!file || !state.currentModFolder) return;

    const civ = getActiveCiv();
    if (!civ) return;

    const safeName = file.name.replace(/[^A-Za-z0-9._\-]/g, '_');
    try {
      const res = await api(`/api/upload?folder=${encodeURIComponent(state.currentModFolder)}&name=${encodeURIComponent(safeName)}`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/octet-stream'
        },
        body: file
      });

      civ[propertyKey] = res.file || safeName;
      checkDirty();
      renderAssetStatus();
      showToast(`Uploaded ${safeName}!`);
    } catch (err) {
      showToast('Upload failed: ' + err.message, 'error');
    } finally {
      fileInput.value = '';
    }
  }

  // ==========================================================================
  // Event Listeners Setup
  // ==========================================================================
  function setupEventListeners() {
    // Sidebar actions
    document.getElementById('btn-new-mod')?.addEventListener('click', openNewModModal);
    document.getElementById('btn-empty-new-mod')?.addEventListener('click', openNewModModal);
    document.getElementById('btn-overview-new-civ')?.addEventListener('click', openNewCivModal);
    document.getElementById('btn-quick-new-civ')?.addEventListener('click', openNewCivModal);

    const openModsDir = () => api('/api/open-folder', { method: 'POST', body: {} });
    document.getElementById('btn-open-mods-dir')?.addEventListener('click', openModsDir);
    document.getElementById('btn-empty-open-folder')?.addEventListener('click', openModsDir);

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
        renderSidebarMods();
      });
    }
    if (modSearchClear) {
      modSearchClear.addEventListener('click', () => {
        if (modSearch) modSearch.value = '';
        modSearchClear.style.display = 'none';
        renderSidebarMods();
      });
    }

    // Mod Header Actions
    document.getElementById('btn-save')?.addEventListener('click', saveMod);
    document.getElementById('btn-validate')?.addEventListener('click', validateMod);
    document.getElementById('btn-revert')?.addEventListener('click', revertMod);

    // Mod Enabled Toggle
    document.getElementById('mod-enabled-toggle')?.addEventListener('change', (e) => {
      if (state.currentModData && state.currentModData.mod) {
        state.currentModData.mod.enabled = e.target.checked;
        document.getElementById('mod-enabled-label').textContent = e.target.checked ? 'Enabled' : 'Disabled';
        checkDirty();
        renderSidebarMods();
      }
    });

    // Mod Overview Fields
    const inputName = document.getElementById('mod-input-name');
    const inputVersion = document.getElementById('mod-input-version');
    const inputAuthor = document.getElementById('mod-input-author');
    const inputDesc = document.getElementById('mod-input-description');

    if (inputName) {
      inputName.addEventListener('input', () => {
        if (state.currentModData && state.currentModData.mod) {
          state.currentModData.mod.name = inputName.value;
          document.getElementById('mod-header-name').textContent = inputName.value || state.currentModFolder;
          checkDirty();
          renderSidebarMods();
        }
      });
    }

    if (inputVersion) {
      inputVersion.addEventListener('input', () => {
        if (state.currentModData && state.currentModData.mod) {
          state.currentModData.mod.version = inputVersion.value;
          document.getElementById('mod-header-version').textContent = 'v' + (inputVersion.value || '0.1');
          checkDirty();
        }
      });
    }

    if (inputAuthor) {
      inputAuthor.addEventListener('input', () => {
        if (state.currentModData && state.currentModData.mod) {
          state.currentModData.mod.author = inputAuthor.value;
          document.getElementById('mod-header-author').textContent = inputAuthor.value ? `by ${inputAuthor.value}` : 'No author specified';
          checkDirty();
        }
      });
    }

    if (inputDesc) {
      inputDesc.addEventListener('input', () => {
        if (state.currentModData && state.currentModData.mod) {
          state.currentModData.mod.description = inputDesc.value;
          checkDirty();
        }
      });
    }

    // Nav Bar Tabs
    document.querySelectorAll('.nav-tab').forEach(tab => {
      tab.addEventListener('click', () => {
        const tabName = tab.dataset.tab;
        switchTab(tabName);
      });
    });

    // Civ Switcher Dropdown
    document.getElementById('civ-select-dropdown')?.addEventListener('change', (e) => {
      selectCiv(e.target.value);
    });

    // Identity Tab Inputs
    const civName = document.getElementById('civ-input-name');
    const civDat = document.getElementById('civ-input-dat-name');
    const civBase = document.getElementById('civ-select-base');
    const civDesc = document.getElementById('civ-input-description');

    if (civName) {
      civName.addEventListener('input', () => {
        const civ = getActiveCiv();
        if (civ) {
          civ.name = civName.value;
          checkDirty();
          renderCivSwitcher();
        }
      });
    }

    if (civDat) {
      civDat.addEventListener('input', () => {
        const civ = getActiveCiv();
        if (civ) {
          civ.dat_name = civDat.value.slice(0, 19);
          checkDirty();
        }
      });
    }

    document.getElementById('btn-sync-dat-name')?.addEventListener('click', () => {
      const civ = getActiveCiv();
      if (civ && civ.name) {
        const dat = civ.name.replace(/[^A-Za-z0-9_\-]/g, '').slice(0, 19);
        civ.dat_name = dat;
        if (civDat) civDat.value = dat;
        checkDirty();
      }
    });

    if (civBase) {
      civBase.addEventListener('change', () => {
        const civ = getActiveCiv();
        if (civ) {
          civ.base = Number(civBase.value) || 1;
          checkDirty();
        }
      });
    }

    if (civDesc) {
      civDesc.addEventListener('input', () => {
        const civ = getActiveCiv();
        if (civ) {
          civ.description = civDesc.value;
          document.getElementById('civ-desc-preview').innerHTML = renderCivDescriptionPreview(civDesc.value);
          checkDirty();
        }
      });
    }

    // Quick insertion tags in description editor
    document.querySelectorAll('.editor-quick-inserts .btn-tag').forEach(btn => {
      btn.addEventListener('click', () => {
        const textToInsert = btn.dataset.insert;
        if (!civDesc || !textToInsert) return;
        const start = civDesc.selectionStart || 0;
        const end = civDesc.selectionEnd || 0;
        const val = civDesc.value;
        civDesc.value = val.substring(0, start) + textToInsert + val.substring(end);
        civDesc.focus();
        civDesc.selectionStart = civDesc.selectionEnd = start + textToInsert.length;
        civDesc.dispatchEvent(new Event('input'));
      });
    });

    document.getElementById('btn-generate-description')?.addEventListener('click', generateDescriptionFromBonuses);

    // AI Names Add
    const aiInput = document.getElementById('civ-new-ai-name');
    const addAiBtn = document.getElementById('btn-add-ai-name');
    const doAddAi = () => {
      const civ = getActiveCiv();
      if (!civ || !aiInput) return;
      const name = aiInput.value.trim();
      if (!name) return;
      if (!Array.isArray(civ.ai_names)) civ.ai_names = [];
      civ.ai_names.push(name);
      aiInput.value = '';
      checkDirty();
      renderAiNamesChips();
    };

    if (addAiBtn) addAiBtn.addEventListener('click', doAddAi);
    if (aiInput) {
      aiInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') doAddAi();
      });
    }

    // Sound and SLP upload
    const soundInput = document.getElementById('civ-sound-file-input');
    const slpInput = document.getElementById('civ-slp-file-input');
    if (soundInput) {
      soundInput.addEventListener('change', () => uploadCivAsset(soundInput, 'sound'));
    }
    if (slpInput) {
      slpInput.addEventListener('change', () => uploadCivAsset(slpInput, 'interface_slp'));
    }

    document.getElementById('btn-clear-civ-sound')?.addEventListener('click', () => {
      const civ = getActiveCiv();
      if (civ) {
        delete civ.sound;
        checkDirty();
        renderAssetStatus();
      }
    });

    document.getElementById('btn-clear-civ-slp')?.addEventListener('click', () => {
      const civ = getActiveCiv();
      if (civ) {
        delete civ.interface_slp;
        checkDirty();
        renderAssetStatus();
      }
    });

    // Bonuses: Add Custom Effect
    document.getElementById('btn-add-effect')?.addEventListener('click', () => {
      const civ = getActiveCiv();
      if (!civ) return;
      if (!Array.isArray(civ.effects)) civ.effects = [];
      civ.effects.push({ type: 5, a: -1, b: -1, c: 0, d: 1.1 });
      checkDirty();
      renderAdvancedEffectsTable();
    });

    // Units Tab Virtual Scroll & Filters
    const unitsViewport = document.getElementById('units-virtual-viewport');
    if (unitsViewport) {
      unitsViewport.addEventListener('scroll', () => {
        requestAnimationFrame(renderUnitsVirtualList);
      });
    }

    const unitsSearch = document.getElementById('units-search');
    const unitsSearchClear = document.getElementById('units-search-clear');
    if (unitsSearch) {
      unitsSearch.addEventListener('input', () => {
        if (unitsSearchClear) unitsSearchClear.style.display = unitsSearch.value ? 'block' : 'none';
        filterUnitsList();
        renderUnitsVirtualList();
      });
    }
    if (unitsSearchClear) {
      unitsSearchClear.addEventListener('click', () => {
        if (unitsSearch) unitsSearch.value = '';
        unitsSearchClear.style.display = 'none';
        filterUnitsList();
        renderUnitsVirtualList();
      });
    }

    document.getElementById('units-filter-modified')?.addEventListener('change', () => {
      filterUnitsList();
      renderUnitsVirtualList();
    });

    // Tech Tree Filters
    document.getElementById('tt-techs-search')?.addEventListener('input', renderTechsAvailabilityList);
    document.getElementById('tt-units-search')?.addEventListener('input', renderUnitsAvailabilityList);

    document.querySelectorAll('.segmented-filter .seg-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const parent = btn.parentElement;
        parent.querySelectorAll('.seg-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        const filter = btn.dataset.filter;
        if (filter.startsWith('tech')) renderTechsAvailabilityList();
        if (filter.startsWith('unit')) renderUnitsAvailabilityList();
      });
    });

    // Unique Techs
    document.getElementById('select-unique-castle')?.addEventListener('change', (e) => {
      const civ = getActiveCiv();
      if (civ) {
        if (!Array.isArray(civ.unique_techs)) civ.unique_techs = [-1, -1];
        civ.unique_techs[0] = Number(e.target.value);
        checkDirty();
      }
    });

    document.getElementById('select-unique-imp')?.addEventListener('change', (e) => {
      const civ = getActiveCiv();
      if (civ) {
        if (!Array.isArray(civ.unique_techs)) civ.unique_techs = [-1, -1];
        civ.unique_techs[1] = Number(e.target.value);
        checkDirty();
      }
    });

    // Global Keyboard Shortcuts (Ctrl+S / Cmd+S)
    window.addEventListener('keydown', (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
        e.preventDefault();
        saveMod();
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

  // DOM Content Loaded entry point
  document.addEventListener('DOMContentLoaded', () => {
    setupEventListeners();
    init();
  });
})();

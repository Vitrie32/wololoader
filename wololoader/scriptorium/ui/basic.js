/**
 * Scriptorium Basic workspace — MCreator-style mod element workspace
 * Vanilla ES2020, no dependencies, offline-first
 */

(function() {
  'use strict';

  if (!window.Scriptorium) {
    console.error('[Basic] Scriptorium bridge not found');
    return;
  }

  const S = window.Scriptorium;
  const CHEV_SVG = '<svg class="sc-chev" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 3 11 8 6 13"/></svg>';
  const wireDisclosures = (el) => { if (S.wireDisclosures) S.wireDisclosures(el); };
  let loadedGraphs = new Map();
  let modifiedHistoryTexts = new Map();
  let currentElement = null;
  let renderToken = 0;   // bumped when an editor opens: a pending draw of the page must not overwrite it

  // ==========================================================================
  // "Test it" — Quick-Launch Single-Player Match (POST /api/testit)
  // ==========================================================================
  const TESTIT_STORAGE_KEY = 'scriptorium.testit.opts';
  const TESTIT_DEFAULTS = {
    map: 'Arabia',
    size: 'tiny',
    ai: 1,
    ai_civ: 'random',
    difficulty: 'easiest',
    resources: 'standard',
    reveal_map: false,
    cheats: true
  };
  const TESTIT_MAPS = [
    'Arabia', 'Arena', 'Black Forest', 'Islands', 'Coastal', 'Continental',
    'Fortress', 'Gold Rush', 'Highland', 'Mediterranean', 'Nomad', 'Oasis',
    'Rivers', 'Yucatan'
  ];
  const PLAY_SVG = '<svg class="testit-play-ico" width="12" height="12" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true"><polygon points="4,2.5 13.5,8 4,13.5"/></svg>';
  const CARET_SVG = '<svg width="11" height="11" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polyline points="4 6 8 10 12 6"/></svg>';

  function loadTestItOpts() {
    const out = { ...TESTIT_DEFAULTS };
    try {
      const raw = window.localStorage.getItem(TESTIT_STORAGE_KEY);
      if (raw) {
        const parsed = JSON.parse(raw);
        if (parsed && typeof parsed === 'object') {
          if (typeof parsed.map === 'string' && parsed.map) out.map = parsed.map;
          if (typeof parsed.size === 'string' && parsed.size) out.size = parsed.size;
          if (typeof parsed.ai === 'number' && parsed.ai >= 0 && parsed.ai <= 7) out.ai = Math.round(parsed.ai);
          if (parsed.ai_civ !== undefined && parsed.ai_civ !== null && parsed.ai_civ !== '') out.ai_civ = parsed.ai_civ;
          if (typeof parsed.difficulty === 'string' && parsed.difficulty) out.difficulty = parsed.difficulty;
          if (typeof parsed.resources === 'string' && parsed.resources) out.resources = parsed.resources;
          if (typeof parsed.reveal_map === 'boolean') out.reveal_map = parsed.reveal_map;
          if (typeof parsed.cheats === 'boolean') out.cheats = parsed.cheats;
        }
      }
    } catch (_) {
      // ignore localStorage errors
    }
    return out;
  }

  function saveTestItOpts(opts) {
    try {
      window.localStorage.setItem(TESTIT_STORAGE_KEY, JSON.stringify(opts));
    } catch (_) {
      // ignore localStorage errors
    }
  }

  function closeTestItPopover() {
    const existing = document.getElementById('testit-popover');
    if (existing) existing.remove();
  }

  function testItControlHtml(civFile) {
    return `
      <div class="testit-wrap" data-testit-file="${escapeHtml(civFile)}">
        <span class="testit-status" aria-live="polite" hidden></span>
        <div class="testit-split">
          <button type="button" class="btn btn-save btn-sm testit-go-btn" data-testit-go="${escapeHtml(civFile)}" title="Launch AoE2 HD straight into a test match with this civilization">
            ${PLAY_SVG}<span>Test it</span>
          </button>
          <button type="button" class="btn btn-save btn-sm testit-opts-btn" data-testit-opts="${escapeHtml(civFile)}" title="Test match options" aria-haspopup="dialog" aria-expanded="false">
            ${CARET_SVG}
          </button>
        </div>
      </div>`;
  }

  function setTestItStatus(wrap, text, isError) {
    if (!wrap) return;
    const el = wrap.querySelector('.testit-status');
    if (!el) return;
    if (!text) {
      el.hidden = true;
      el.textContent = '';
      el.classList.remove('is-error');
      return;
    }
    el.hidden = false;
    el.textContent = text;
    el.classList.toggle('is-error', !!isError);
  }

  async function runTestIt(civFile, wrap) {
    const folder = S.state.currentModFolder;
    if (!folder || !civFile) return;
    closeTestItPopover();

    const goBtn = wrap?.querySelector('.testit-go-btn');
    const optsBtn = wrap?.querySelector('.testit-opts-btn');
    if (goBtn) goBtn.disabled = true;
    if (optsBtn) optsBtn.disabled = true;

    try {
      if (S.state.isDirty && S.saveAll) {
        setTestItStatus(wrap, 'Saving changes…', false);
        await S.saveAll();
      }
      setTestItStatus(wrap, 'Starting the game…', false);
      const opts = loadTestItOpts();
      const payload = {
        mod: folder,
        civ: civFile,
        map: opts.map,
        size: opts.size,
        ai: Number(opts.ai),
        ai_civ: /^\d+$/.test(String(opts.ai_civ)) ? Number(opts.ai_civ) : opts.ai_civ,
        difficulty: opts.difficulty,
        resources: opts.resources,
        reveal_map: !!opts.reveal_map,
        cheats: !!opts.cheats
      };
      const res = await S.api('/api/testit', { method: 'POST', body: payload });

      if (res && res.mod_was_enabled && S.state.currentModData && S.state.currentModData.mod) {
        S.state.currentModData.mod.enabled = true;
        const tog = document.getElementById('mod-enabled-toggle');
        const lbl = document.getElementById('mod-enabled-label');
        if (tog) tog.checked = true;
        if (lbl) lbl.textContent = 'Enabled';
        if (S.refreshModsList) S.refreshModsList();
      }

      if (res && res.game_running) {
        setTestItStatus(wrap, 'Waiting for the main menu…', false);
        S.showToast(res.message || 'Game is already running — return to the Main Menu to start the test match.', 'info', 6000);
      } else {
        setTestItStatus(wrap, 'Starting the game…', false);
        S.showToast(res.message || 'Starting the game…', 'success', 4500);
        setTimeout(() => {
          if (wrap && wrap.isConnected) setTestItStatus(wrap, 'Waiting for the main menu…', false);
        }, 2800);
      }
    } catch (err) {
      setTestItStatus(wrap, 'Failed: ' + err.message, true);
      S.showToast('Test it failed: ' + err.message, 'error', 5500);
    } finally {
      if (goBtn) goBtn.disabled = false;
      if (optsBtn) optsBtn.disabled = false;
    }
  }

  function openTestItPopover(anchorBtn, civFile, wrap) {
    const existing = document.getElementById('testit-popover');
    if (existing && existing.dataset.anchorFile === civFile && existing.dataset.anchorId === (anchorBtn.id || '')) {
      closeTestItPopover();
      return;
    }
    closeTestItPopover();

    const opts = loadTestItOpts();
    const vanillaCivs = (S.state.catalog?.civs || []).filter(c => c.id >= 1 && c.id <= 31);

    const pop = document.createElement('div');
    pop.id = 'testit-popover';
    pop.className = 'testit-popover';
    pop.dataset.anchorFile = civFile;
    pop.dataset.anchorId = anchorBtn.id || '';
    pop.setAttribute('role', 'dialog');
    pop.setAttribute('aria-label', 'Test match options');
    pop.innerHTML = `
      <div class="testit-pop-head">
        <span class="testit-pop-title">Test Match Options</span>
        <button type="button" class="testit-pop-close" title="Close">&times;</button>
      </div>
      <div class="testit-pop-grid">
        <label class="testit-pop-field">
          <span>Map</span>
          <select id="ti-map" class="form-select">
            ${TESTIT_MAPS.map(m => `<option value="${escapeHtml(m)}" ${opts.map === m ? 'selected' : ''}>${escapeHtml(m)}</option>`).join('')}
          </select>
        </label>
        <label class="testit-pop-field">
          <span>Map size</span>
          <select id="ti-size" class="form-select">
            ${[
              ['tiny', 'Tiny (2p)'],
              ['small', 'Small (3p)'],
              ['medium', 'Medium (4p)'],
              ['normal', 'Normal (6p)'],
              ['large', 'Large (8p)'],
              ['giant', 'Giant']
            ].map(([v, lbl]) => `<option value="${v}" ${opts.size === v ? 'selected' : ''}>${lbl}</option>`).join('')}
          </select>
        </label>
        <label class="testit-pop-field">
          <span>AI opponents</span>
          <select id="ti-ai" class="form-select">
            ${[0, 1, 2, 3, 4, 5, 6, 7].map(n => `<option value="${n}" ${Number(opts.ai) === n ? 'selected' : ''}>${n === 0 ? '0 (Solo)' : String(n)}</option>`).join('')}
          </select>
        </label>
        <label class="testit-pop-field">
          <span>AI civilization</span>
          <select id="ti-ai-civ" class="form-select">
            <option value="random" ${String(opts.ai_civ) === 'random' ? 'selected' : ''}>Random</option>
            <option value="mirror" ${String(opts.ai_civ) === 'mirror' ? 'selected' : ''}>Mirror</option>
            ${vanillaCivs.map(c => `<option value="${c.id}" ${String(opts.ai_civ) === String(c.id) ? 'selected' : ''}>${escapeHtml(c.name)}</option>`).join('')}
          </select>
        </label>
        <label class="testit-pop-field">
          <span>Difficulty</span>
          <select id="ti-diff" class="form-select">
            ${[
              ['easiest', 'Easiest'],
              ['standard', 'Standard'],
              ['moderate', 'Moderate'],
              ['hard', 'Hard'],
              ['hardest', 'Hardest']
            ].map(([v, lbl]) => `<option value="${v}" ${opts.difficulty === v ? 'selected' : ''}>${lbl}</option>`).join('')}
          </select>
        </label>
        <label class="testit-pop-field">
          <span>Resources</span>
          <select id="ti-res" class="form-select">
            ${[
              ['standard', 'Standard'],
              ['low', 'Low'],
              ['medium', 'Medium'],
              ['high', 'High']
            ].map(([v, lbl]) => `<option value="${v}" ${opts.resources === v ? 'selected' : ''}>${lbl}</option>`).join('')}
          </select>
        </label>
      </div>
      <div class="testit-pop-checks">
        <label class="testit-pop-check"><input type="checkbox" id="ti-reveal" ${opts.reveal_map ? 'checked' : ''}> <span>Reveal map</span></label>
        <label class="testit-pop-check"><input type="checkbox" id="ti-cheats" ${opts.cheats ? 'checked' : ''}> <span>Allow cheats</span></label>
      </div>
      <div class="testit-pop-foot">
        <button type="button" class="btn btn-wood btn-sm" id="ti-reset">Defaults</button>
        <button type="button" class="btn btn-save btn-sm" id="ti-launch">${PLAY_SVG}<span>Start test match</span></button>
      </div>
    `;

    document.body.appendChild(pop);

    // Position cleanly below (or above if near bottom) anchor button, clamped to viewport
    const r = anchorBtn.getBoundingClientRect();
    const pw = pop.offsetWidth || 320;
    const ph = pop.offsetHeight || 290;
    let left = Math.min(window.innerWidth - pw - 12, Math.max(12, r.right - pw));
    let top = r.bottom + 6;
    if (top + ph > window.innerHeight - 12 && r.top - ph - 6 > 12) {
      top = r.top - ph - 6;
    }
    pop.style.left = `${Math.round(left)}px`;
    pop.style.top = `${Math.round(top)}px`;

    const readForm = () => {
      const next = {
        map: pop.querySelector('#ti-map').value,
        size: pop.querySelector('#ti-size').value,
        ai: Number(pop.querySelector('#ti-ai').value),
        ai_civ: pop.querySelector('#ti-ai-civ').value,
        difficulty: pop.querySelector('#ti-diff').value,
        resources: pop.querySelector('#ti-res').value,
        reveal_map: !!pop.querySelector('#ti-reveal').checked,
        cheats: !!pop.querySelector('#ti-cheats').checked
      };
      saveTestItOpts(next);
      return next;
    };

    pop.querySelectorAll('select, input').forEach(el => el.addEventListener('change', readForm));
    pop.querySelector('.testit-pop-close').addEventListener('click', closeTestItPopover);
    pop.querySelector('#ti-reset').addEventListener('click', () => {
      saveTestItOpts({ ...TESTIT_DEFAULTS });
      closeTestItPopover();
      openTestItPopover(anchorBtn, civFile, wrap);
    });
    pop.querySelector('#ti-launch').addEventListener('click', () => {
      readForm();
      runTestIt(civFile, wrap);
    });
  }

  function wireTestItControls(container) {
    if (!container) return;
    container.querySelectorAll('.testit-wrap').forEach(wrap => {
      const file = wrap.dataset.testitFile;
      wrap.querySelector('.testit-go-btn')?.addEventListener('click', (e) => {
        e.stopPropagation();
        runTestIt(file, wrap);
      });
      wrap.querySelector('.testit-opts-btn')?.addEventListener('click', (e) => {
        e.stopPropagation();
        openTestItPopover(e.currentTarget, file, wrap);
      });
    });
  }

  if (typeof document !== 'undefined') {
    document.addEventListener('click', (e) => {
      const pop = document.getElementById('testit-popover');
      if (pop && !pop.contains(e.target) && !e.target.closest('.testit-opts-btn')) {
        closeTestItPopover();
      }
    });
    document.addEventListener('keydown', (e) => {
      if (e.key === 'Escape') closeTestItPopover();
    });
  }

  // ==========================================================================
  // Element Derivation from State
  // ==========================================================================

  function deriveElements() {
    const data = S.state.currentModData;
    if (!data) return [];

    const elements = [];

    Object.keys(data.civs || {}).forEach(file => {
      const civ = data.civs[file];
      elements.push({
        type: 'civilization',
        id: `civ:${file}`,
        title: civLabel(civ, file),
        summary: civ.extend ? 'Changes to a civilization of the game' : `Based on ${getCivName(civ.base || 1)}`,
        file
      });
    });

    Object.keys(data.civs || {}).forEach(file => {
      const civ = data.civs[file];
      if (civ.unique_unit) {
        elements.push({
          type: 'unique_unit',
          id: `unique_unit:${file}`,
          title: civ.unique_unit.name || 'Unique Unit',
          summary: `${civLabel(civ, file)}`,
          file
        });
      }
    });

    Object.keys(data.civs || {}).forEach(file => {
      const civ = data.civs[file];
      if (civ.unique_building) {
        elements.push({
          type: 'unique_building',
          id: `unique_building:${file}`,
          title: civ.unique_building.name || 'Unique Building',
          summary: `${civLabel(civ, file)}`,
          file
        });
      }
    });

    Object.keys(data.civs || {}).forEach(file => {
      const civ = data.civs[file];
      const ids = Object.keys(civ.units || {});
      if (ids.length) {
        // one card per civ: a civ often changes a whole unit family (all villager types)
        const names = [...new Set(ids.map(id => { const u = findUnit(Number(id)); return u ? u.name : 'Unit ' + id; }))];
        elements.push({
          type: 'unit_changes',
          id: `unit_changes:${file}`,
          title: 'Unit changes',
          summary: `${civLabel(civ, file)}: ${ids.length} unit${ids.length > 1 ? 's' : ''} (${names.slice(0, 3).join(', ')}${names.length > 3 ? ', ...' : ''})`,
          file
        });
      }
    });

    Object.keys(data.civs || {}).forEach(file => {
      const civ = data.civs[file];
      if ((civ.effects && civ.effects.length > 0) || (civ.team_bonus_effects && civ.team_bonus_effects.length > 0)) {
        elements.push({
          type: 'bonuses',
          id: `bonuses:${file}`,
          title: 'Bonuses',
          summary: `${civLabel(civ, file)}`,
          file
        });
      }
    });

    return elements;
  }

  // A civ file either makes a new civ, or changes a civ of the game ("extend": 1..31).
  function civLabel(civ, file) {
    return civ && civ.extend ? `${getCivName(civ.extend)} (game civ)` : ((civ && civ.name) || file);
  }

  function getCivName(id) {
    const civ = S.state.catalog.civs.find(c => c.id === id);
    return civ ? civ.name : `Civ ${id}`;
  }

  // Game icon of a catalog unit or building (gamedata.js); filled in by hydrateIcons().
  function unitIconHtml(unit, size) {
    return window.GameIcons ? GameIcons.unitMarkup(unit, size) : '';
  }

  // Fills the game icons that were just rendered (the markup is plain HTML, so this runs after
  // every render through a MutationObserver instead of after each call site).
  // Fills the game icons and custom SLP / civ icons that were just rendered
  function hydrateIcons() {
    if (window.GameIcons) {
      GameIcons.hydrate(document.body);
    }

    // Custom SLP icons (unique building icon, unique unit icon, civ icon)
    document.querySelectorAll('[data-custom-slp]:not([data-custom-done])').forEach(async el => {
      el.dataset.customDone = '1';
      const slpNum = Number(el.dataset.customSlp);
      if (!slpNum) return;
      try {
        const url = await loadSlpThumbnail(slpNum);
        if (url && el.isConnected) {
          el.innerHTML = `<img src="${url}" class="game-icon-img" style="image-rendering: pixelated; width: 100%; height: 100%; object-fit: contain;" alt="">`;
          el.classList.add('has-icon');
        }
      } catch (_) {}
    });

    // Civilization icons (civ.icon, civ.history.image, or mod icon.png)
    document.querySelectorAll('[data-civ-icon-file]:not([data-civ-done])').forEach(async el => {
      el.dataset.civDone = '1';
      const file = el.dataset.civIconFile;
      const data = S.state.currentModData;
      if (!data || !data.civs || !data.civs[file]) return;
      const civ = data.civs[file];
      const folder = data.folder;

      // 1. If civ.icon is an image file path (e.g. icon.png or history/xyz.png)
      if (civ.icon && typeof civ.icon === 'string') {
        try {
          const res = await S.api(`/api/file?folder=${encodeURIComponent(folder)}&path=${encodeURIComponent(civ.icon)}`);
          const blob = await res.blob();
          const url = URL.createObjectURL(blob);
          if (el.isConnected) {
            el.innerHTML = `<img src="${url}" style="width: 100%; height: 100%; object-fit: contain; border-radius: 6px;" alt="">`;
            return;
          }
        } catch (_) {}
      }

      // 2. If civ has a history image
      if (civ.history && civ.history.image) {
        try {
          const res = await S.api(`/api/file?folder=${encodeURIComponent(folder)}&path=${encodeURIComponent(civ.history.image)}`);
          const blob = await res.blob();
          const url = URL.createObjectURL(blob);
          if (el.isConnected) {
            el.innerHTML = `<img src="${url}" style="width: 100%; height: 100%; object-fit: cover; border-radius: 6px;" alt="">`;
            return;
          }
        } catch (_) {}
      }

      // 3. Mod's own icon.png (/api/icon/folder)
      if (folder) {
        try {
          const res = await S.api(`/api/icon/${encodeURIComponent(folder)}`);
          const blob = await res.blob();
          const url = URL.createObjectURL(blob);
          if (el.isConnected) {
            el.innerHTML = `<img src="${url}" style="width: 100%; height: 100%; object-fit: contain; border-radius: 6px;" alt="">`;
            return;
          }
        } catch (_) {}
      }

      // 4. Fallback to base civ unit / town center if nothing else found
      const fallbackUnit = civ.unique_unit ? findUnit(civ.unique_unit.copy_from) : findUnit(CIV_TOWN_CENTER);
      if (fallbackUnit && el.isConnected) {
        el.innerHTML = unitIconHtml(fallbackUnit, 40);
        if (window.GameIcons) GameIcons.hydrate(el);
      }
    });
  }
  if (typeof MutationObserver !== 'undefined') {
    new MutationObserver(hydrateIcons).observe(document.body, { childList: true, subtree: true });
  }

  function findUnit(id) {
    return S.state.catalog.units.find(u => u.id === id);
  }

  // ==========================================================================
  // Civilizations section: one group per civilization with its elements as rows.
  // sections.js draws the page around it (BasicWorkspace.renderCivCards fills the body).
  // ==========================================================================

  const CIV_TOWN_CENTER = 109;   // icon of a civilization without a unique unit

  function renderCivCards(body) {
    const data = S.state.currentModData;
    if (!data || !body) return;
    const civFiles = Object.keys(data.civs || {});
    if (civFiles.length === 0) {
      body.innerHTML = emptyCard({
        icon: getElementIcon('civilization'),
        title: 'No civilizations yet',
        text: 'A new civilization has its own name, units and bonuses. A game civilization change adds a building, unit changes or bonuses to one of the 31 civilizations of the game.',
        buttons: [
          { act: 'civ-new', label: 'New civilization', primary: true },
          { act: 'civ-game', label: 'Change a game civilization' }
        ]
      });
      body.querySelector('[data-act="civ-new"]').addEventListener('click', createNewCivilization);
      body.querySelector('[data-act="civ-game"]').addEventListener('click', createGameCivExtension);
      return;
    }
    const elements = deriveElements();
    body.innerHTML = `<div class="civ-groups">${civFiles.map(file =>
      civGroupHtml(file, elements.filter(e => e.file === file))).join('')}</div>`;
    body.querySelectorAll('[data-element-id]').forEach(row => {
      row.addEventListener('click', () => {
        const element = elements.find(e => e.id === row.dataset.elementId);
        if (element) openElementEditor(element);
      });
    });
    wireTestItControls(body);
    hydrateIcons();
  }

  function civGroupIconHtml(civ, file) {
    const name = civ.name || file || 'Civ';
    const letter = escapeHtml(name.trim().charAt(0).toUpperCase() || 'C');

    // If civ.icon is an SLP number
    if (civ.icon && (typeof civ.icon === 'number' || /^\d+$/.test(civ.icon))) {
      return `<span class="game-icon" style="--icon-size:40px" data-custom-slp="${civ.icon}"><span class="game-icon-fallback">${letter}</span></span>`;
    }

    // Default container for async loading of civ icon/mod shield
    return `<span class="civ-icon-container" style="width: 40px; height: 40px; display: grid; place-items: center;" data-civ-icon-file="${escapeHtml(file)}">` +
      `<span class="game-icon-fallback" style="font-size: 16px; font-weight: bold; color: var(--accent);">${letter}</span>` +
    `</span>`;
  }

  function civGroupHtml(file, els) {
    const civ = S.state.currentModData.civs[file];
    const civEl = els.find(e => e.type === 'civilization');
    const isGame = !!civ.extend;
    const name = isGame ? getCivName(civ.extend) : (civ.name || file);
    const sub = isGame
      ? 'Changes to a civilization of the game'
      : `New civilization, based on ${getCivName(civ.base || 1)}`;
    const rows = els.filter(e => e.type !== 'civilization');
    const headIcon = civGroupIconHtml(civ, file);
    return `
      <section class="civ-group">
        <div class="civ-group-head-bar">
          <button class="civ-group-head" data-element-id="${escapeHtml(civEl.id)}">
            <span class="civ-group-icon">${headIcon}</span>
            <span class="civ-group-text">
              <span class="civ-group-name">${escapeHtml(name)}</span>
              <span class="civ-group-sub">${escapeHtml(sub)}</span>
            </span>
            <span class="civ-group-edit">Edit civilization <span aria-hidden="true">&rsaquo;</span></span>
          </button>
          <div class="civ-group-testit">${testItControlHtml(file)}</div>
        </div>
        <div class="civ-group-rows">
          ${rows.length ? rows.map(el => elementRowHtml(el, civ)).join('') : `
            <p class="civ-group-empty">Nothing else yet. Add a unique unit, a building, unit changes or bonuses with <strong>New element</strong>.</p>`}
        </div>
      </section>`;
  }

  function elementRowHtml(el, civ) {
    return `
      <button class="el-row" data-element-id="${escapeHtml(el.id)}">
        <span class="el-row-icon">${elementIconHtml(el, civ)}</span>
        <span class="el-row-text">
          <span class="el-row-title">${escapeHtml(el.title)}</span>
          <span class="el-row-sub"><span class="el-row-type">${escapeHtml(getElementTypeName(el.type))}</span>${escapeHtml(rowSubText(el, civ))}</span>
        </span>
        <span class="el-row-chev" aria-hidden="true">&rsaquo;</span>
      </button>`;
  }

  // The line under an element's title. The civilization is the group's heading, so it is not repeated.
  function rowSubText(el, civ) {
    const nameOf = (id) => { const u = findUnit(id); return u ? u.name : 'unit ' + id; };
    if (el.type === 'unique_unit') return 'Based on ' + nameOf(civ.unique_unit.copy_from);
    if (el.type === 'unique_building') return 'Based on ' + nameOf(civ.unique_building.copy_from);
    if (el.type === 'unit_changes') {
      const ids = Object.keys(civ.units || {});
      const names = [...new Set(ids.map(id => nameOf(Number(id))))];
      return `${ids.length} unit${ids.length === 1 ? '' : 's'}: ${names.slice(0, 3).join(', ')}${names.length > 3 ? ', ...' : ''}`;
    }
    if (el.type === 'bonuses') {
      const n = (civ.effects || []).length, t = (civ.team_bonus_effects || []).length;
      const parts = [];
      if (n) parts.push(`${n} effect${n === 1 ? '' : 's'}`);
      if (t) parts.push(`${t} team effect${t === 1 ? '' : 's'}`);
      return parts.join(', ') || 'No effects yet';
    }
    return el.summary;
  }

  // Game icon of the unit or building an element is built from; a generic glyph for the rest.
  function elementIconHtml(el, civ) {
    if (el.type === 'unique_unit') {
      const uu = civ.unique_unit;
      if (uu && uu.icon) {
        return `<span class="game-icon" style="--icon-size:30px" data-custom-slp="${uu.icon}"><span class="game-icon-fallback">U</span></span>`;
      }
      return unitIconHtml(findUnit(civ.unique_unit.copy_from), 30);
    }
    if (el.type === 'unique_building') {
      const ub = civ.unique_building;
      if (ub && ub.icon) {
        return `<span class="game-icon" style="--icon-size:30px" data-custom-slp="${ub.icon}"><span class="game-icon-fallback">B</span></span>`;
      }
      if (ub && ub.slp) {
        return `<span class="game-icon" style="--icon-size:30px" data-custom-slp="${ub.slp}"><span class="game-icon-fallback">B</span></span>`;
      }
      return unitIconHtml(findUnit(civ.unique_building.copy_from), 30);
    }
    if (el.type === 'unit_changes') {
      const first = Number(Object.keys(civ.units || {})[0]);
      return unitIconHtml(findUnit(first), 30);
    }
    return `<span class="el-row-glyph">${getElementIcon(el.type)}</span>`;
  }

  // A centred box with a title, a line of text and optional buttons (data-act marks each one).
  function emptyCard({ icon = '', title, text, buttons = [] }) {
    return `
      <div class="empty-card">
        <div class="empty-card-icon">${icon}</div>
        <h3 class="empty-card-title">${escapeHtml(title)}</h3>
        <p class="empty-card-text">${escapeHtml(text)}</p>
        ${buttons.length ? `<div class="empty-card-actions">${buttons.map(b =>
          `<button class="btn btn-sm ${b.primary ? 'btn-save' : 'btn-wood'}" data-act="${b.act}">${escapeHtml(b.label)}</button>`).join('')}</div>` : ''}
      </div>`;
  }

  function getElementIcon(type) {
    const icons = {
      civilization: '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M3 9l9-7 9 7v11a2 2 0 01-2 2H5a2 2 0 01-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>',
      unique_unit: '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2L2 7l10 5 10-5-10-5z"/><path d="M2 17l10 5 10-5"/><path d="M2 12l10 5 10-5"/></svg>',
      unique_building: '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="2" width="16" height="20" rx="2"/><path d="M9 22V12h6v10"/></svg>',
      unit_changes: '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M8 12h8"/><path d="M12 8v8"/></svg>',
      bonuses: '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2"/></svg>',
      script: '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/></svg>'
    };
    return icons[type] || '';
  }

  function getElementTypeName(type) {
    const names = {
      civilization: 'Civilization',
      unique_unit: 'Unique unit',
      unique_building: 'Mod building',
      unit_changes: 'Unit changes',
      bonuses: 'Bonuses',
      script: 'Script'
    };
    return names[type] || type;
  }

  function escapeHtml(str) {
    if (str === null || str === undefined) return '';
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }

  // ==========================================================================
  // New Element Dialog
  // ==========================================================================

  function showNewElementDialog() {
    const modal = document.getElementById('modal-container');
    if (!modal) return;
    modal.style.display = 'flex';
    modal.innerHTML = `
      <div class="modal-dialog">
        <div class="modal-header">
          <h3 class="modal-title">New Element</h3>
          <button class="btn-modal-close">&times;</button>
        </div>
        <div class="modal-body">
          <div class="sc-modal-group-label">Civilization</div>
          <div class="new-element-types">
            <div class="new-element-type-card" data-type="civilization">
              <strong>Civilization</strong>
              <p>New custom civilization with its own name, units and bonuses</p>
            </div>
            <div class="new-element-type-card" data-type="game_civ">
              <strong>Game Civilization</strong>
              <p>Modify an existing game civ (Britons, Franks, ...)</p>
            </div>
          </div>

          <div class="sc-modal-group-label">Add to a civilization</div>
          <div class="new-element-types">
            <div class="new-element-type-card" data-type="unique_unit">
              <strong>Unique Unit</strong>
              <p>Custom Castle unit for a civilization</p>
            </div>
            <div class="new-element-type-card" data-type="unique_building">
              <strong>Mod Building</strong>
              <p>Custom building with train or research buttons</p>
            </div>
            <div class="new-element-type-card" data-type="unit_changes">
              <strong>Unit Changes</strong>
              <p>Adjust stats or visuals of existing units</p>
            </div>
            <div class="new-element-type-card" data-type="bonuses">
              <strong>Bonuses</strong>
              <p>Starting resources, unit buffs and team bonus</p>
            </div>
          </div>

          <div class="sc-modal-group-label">Automation</div>
          <div class="new-element-types">
            <div class="new-element-type-card" data-type="script">
              <strong>Visual Script</strong>
              <p>Node graph triggered by in-game events</p>
            </div>
          </div>
        </div>
      </div>
    `;

    modal.querySelector('.btn-modal-close')?.addEventListener('click', () => {
      modal.style.display = 'none';
      modal.innerHTML = '';
    });

    modal.querySelectorAll('.new-element-type-card').forEach(card => {
      card.addEventListener('click', () => {
        const type = card.dataset.type;
        modal.style.display = 'none';
        modal.innerHTML = '';
        promptNewElement(type);
      });
    });
  }

  function promptNewElement(type) {
    if (type === 'civilization') {
      createNewCivilization();
    } else if (type === 'game_civ') {
      createGameCivExtension();
    } else if (type === 'script') {
      createNewScript();
    } else if (type === 'unique_unit' || type === 'unique_building' || type === 'unit_changes' || type === 'bonuses') {
      createElementForCiv(type);
    } else {
      S.showToast(`Unknown element type`, 'error');
    }
  }

  function createElementForCiv(type) {
    const data = S.state.currentModData;
    if (!data || !data.civs || Object.keys(data.civs).length === 0) {
      S.showToast('Create a civilization first', 'error');
      return;
    }

    const civFiles = Object.keys(data.civs);
    if (civFiles.length === 1) {
      const file = civFiles[0];
      if (type === 'unique_unit' && data.civs[file].extend) {
        S.showToast('A civilization of the game keeps its own unique unit', 'error');
        return;
      }
      if (type === 'unique_unit') createUniqueUnit(file);
      else if (type === 'unique_building') createUniqueBuilding(file);
      else if (type === 'unit_changes') createUnitChanges(file);
      else if (type === 'bonuses') createBonuses(file);
    } else {
      showCivPickerDialog(type, civFiles);
    }
  }

  function showCivPickerDialog(type, civFiles) {
    const modal = document.getElementById('modal-container');
    if (!modal) return;
    const data = S.state.currentModData;
    modal.style.display = 'flex';
    modal.innerHTML = `
      <div class="modal-dialog">
        <div class="modal-header">
          <h3 class="modal-title">Choose Civilization</h3>
          <button class="btn-modal-close">&times;</button>
        </div>
        <div class="modal-body">
          <p style="margin-bottom: 12px; font-size: 13px; color: var(--text-dark);">Select which civilization this element belongs to:</p>
          <div style="display: flex; flex-direction: column; gap: 8px;">
            ${civFiles.map(file => {
              const civ = data.civs[file];
              const gameUU = type === 'unique_unit' && civ.extend;
              const disabled = gameUU || (type === 'unique_unit' && civ.unique_unit) || (type === 'unique_building' && civ.unique_building);
              return `<button class="btn btn-wood btn-block" data-file="${escapeHtml(file)}" ${disabled ? 'disabled' : ''}>${escapeHtml(civLabel(civ, file))}${gameUU ? ' (game civs keep their own)' : disabled ? ' (already has)' : ''}</button>`;
            }).join('')}
          </div>
        </div>
      </div>
    `;

    modal.querySelector('.btn-modal-close')?.addEventListener('click', () => {
      modal.style.display = 'none';
      modal.innerHTML = '';
    });

    modal.querySelectorAll('button[data-file]').forEach(btn => {
      btn.addEventListener('click', () => {
        const file = btn.dataset.file;
        modal.style.display = 'none';
        modal.innerHTML = '';
        if (type === 'unique_unit') createUniqueUnit(file);
        else if (type === 'unique_building') createUniqueBuilding(file);
        else if (type === 'unit_changes') createUnitChanges(file);
        else if (type === 'bonuses') createBonuses(file);
      });
    });
  }

  function createUniqueUnit(file) {
    const data = S.state.currentModData;
    const civ = data.civs[file];
    if (civ.unique_unit) {
      S.showToast('This civilization already has a unique unit', 'error');
      return;
    }
    civ.unique_unit = {
      copy_from: 125,
      name: 'Unique Unit',
      help: '',
      units: {},
      elite_units: {}
    };
    S.setDirty(true);
    renderBasicWorkspace();
    S.showToast('Unique unit created', 'success');
  }

  function createUniqueBuilding(file) {
    const data = S.state.currentModData;
    const civ = data.civs[file];
    if (civ.unique_building) {
      S.showToast('This civilization already has a unique building', 'error');
      return;
    }
    civ.unique_building = {
      copy_from: 104,
      name: 'Unique Building',
      help: '',
      page: 'mods',
      button: 1,
      age: 2,
      units: {},
      commands: []
    };
    S.setDirty(true);
    renderBasicWorkspace();
    S.showToast('Unique building created', 'success');
  }

  function createUnitChanges(file) {
    showUnitPicker(null, unit => {
      const civ = S.state.currentModData.civs[file];
      if (!civ.units) civ.units = {};
      if (!civ.units[unit.id]) civ.units[unit.id] = {};
      S.setDirty(true);
      openElementEditor({ type: 'unit_changes', id: `unit_changes:${file}`, title: 'Unit changes', file, unitId: String(unit.id) });
    });
  }

  function createBonuses(file) {
    const data = S.state.currentModData;
    const civ = data.civs[file];
    if (!civ.effects) civ.effects = [];
    if (!civ.team_bonus_effects) civ.team_bonus_effects = [];
    S.setDirty(true);
    renderBasicWorkspace();
    S.showToast('Bonuses element created', 'success');
  }

  // In-app text prompt (window.prompt is unthemed and blocked in some browser windows).
  function askName(title, label, placeholder) {
    return new Promise(resolve => {
      const modal = document.getElementById('modal-container');
      if (!modal) return resolve(null);
      modal.style.display = 'flex';
      modal.innerHTML = `
        <div class="modal-dialog">
          <div class="modal-header">
            <h3 class="modal-title">${escapeHtml(title)}</h3>
            <button class="btn-modal-close">&times;</button>
          </div>
          <div class="modal-body">
            <label class="basic-ask-label">${escapeHtml(label)}</label>
            <input type="text" class="form-input basic-ask-input" placeholder="${escapeHtml(placeholder || '')}" maxlength="60">
            <div class="basic-ask-actions">
              <button class="btn btn-wood btn-sm" data-act="cancel">Cancel</button>
              <button class="btn btn-wood btn-sm btn-save" data-act="ok">Create</button>
            </div>
          </div>
        </div>`;
      const input = modal.querySelector('.basic-ask-input');
      const close = val => { modal.style.display = 'none'; modal.innerHTML = ''; resolve(val); };
      const ok = () => { const v = input.value.trim(); if (v) close(v); else input.focus(); };
      modal.querySelector('.btn-modal-close').onclick = () => close(null);
      modal.querySelector('[data-act="cancel"]').onclick = () => close(null);
      modal.querySelector('[data-act="ok"]').onclick = ok;
      input.addEventListener('keydown', e => { if (e.key === 'Enter') ok(); if (e.key === 'Escape') close(null); });
      setTimeout(() => input.focus(), 0);
    });
  }

  // A civ file with "extend": <game civ id>: the builder changes that civ in place.
  function createGameCivExtension() {
    const data = S.state.currentModData;
    if (!data) return;
    const modal = document.getElementById('modal-container');
    const civs = (S.state.catalog.civs || []).filter(c => c.id >= 1 && c.id <= 31);
    modal.style.display = 'flex';
    modal.innerHTML = `
      <div class="modal-dialog" style="max-height: 80vh;">
        <div class="modal-header">
          <h3 class="modal-title">Which civilization of the game?</h3>
          <button class="btn-modal-close">&times;</button>
        </div>
        <div class="modal-body" style="overflow-y: auto;">
          <div class="basic-civ-grid">${civs.map(c => {
            const taken = Object.values(data.civs).some(v => v.extend === c.id);
            return `<button class="btn btn-wood" data-id="${c.id}" ${taken ? 'disabled title="Already in this mod"' : ''}>${escapeHtml(c.name)}</button>`;
          }).join('')}</div>
        </div>
      </div>`;
    const close = () => { modal.style.display = 'none'; modal.innerHTML = ''; };
    modal.querySelector('.btn-modal-close').onclick = close;
    modal.querySelectorAll('[data-id]').forEach(btn => btn.onclick = () => {
      const id = Number(btn.dataset.id);
      const base = (getCivName(id).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'civ' + id);
      let file = base + '-changes.json', k = 2;
      while (data.civs[file]) file = `${base}-changes-${k++}.json`;
      data.civs[file] = { extend: id };
      S.setDirty(true);
      close();
      currentElement = null;
      renderBasicWorkspace();
      S.showToast(`${getCivName(id)} added: now add a Mod Building, Unit Changes or Bonuses to it`, 'success', 6000);
    });
  }

  async function createNewCivilization() {
    const data = S.state.currentModData;
    if (!data) return;

    const name = await askName('New Civilization', 'Name of the civilization', 'e.g. Monklanians');
    if (!name) return;

    const datName = name.trim().replace(/[^A-Za-z0-9]/g, '').substring(0, 19) || 'Civ' + (Object.keys(data.civs).length + 1);
    const fileName = datName.toLowerCase() + '.json';

    if (data.civs[fileName]) {
      S.showToast('A civilization with this name already exists', 'error');
      return;
    }

    data.civs[fileName] = {
      name: name.trim(),
      dat_name: datName,
      base: 1,
      description: '',
      ai_names: [],
      effects: [],
      team_bonus_effects: [],
      units: {},
      enabled_techs: [],
      disabled_techs: [],
      enabled_units: [],
      disabled_units: []
    };

    S.setDirty(true);
    renderBasicWorkspace();
    S.showToast('Civilization created', 'success');
  }

  async function createNewScript() {
    if (!window.ScGraph) {
      S.showToast('Script editor not available', 'error');
      return;
    }

    const name = await askName('New Script', 'Name of the script', 'e.g. Bless button');
    if (!name) return;

    const slug = name.trim().toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || ('script-' + Date.now());
    const graph = window.ScGraph.newGraph(name.trim());

    loadedGraphs.set(slug, graphEntry(slug, graph, true));
    S.setExtraDirty('graph:' + slug, true);
    openElementEditor({ type: 'script', id: `script:${slug}.json`, title: name.trim(), file: `${slug}.json` });
  }

  // ==========================================================================
  // Element Editors
  // ==========================================================================

  function openElementEditor(element) {
    currentElement = element;
    renderToken++;   // a workspace render still waiting for the file list must not draw over the editor
    const root = document.getElementById('basic-root');
    if (!root) return;

    root.innerHTML = '<div class="basic-editor-loading">Loading editor...</div>';

    setTimeout(() => {
      if (element.type === 'civilization') {
        renderCivilizationEditor(element);
      } else if (element.type === 'script') {
        renderScriptEditor(element);
      } else if (element.type === 'unique_unit') {
        renderUniqueUnitEditor(element);
      } else if (element.type === 'unique_building') {
        renderUniqueBuildingEditor(element);
      } else if (element.type === 'unit_changes') {
        renderUnitChangesEditor(element);
      } else if (element.type === 'bonuses') {
        renderBonusesEditor(element);
      } else {
        renderPlaceholderEditor(element);
      }
    }, 10);
  }

  function closeElementEditor() {
    const audio = document.getElementById('history-sound-audio');
    if (audio) {
      audio.pause();
      audio.src = '';
    }
    currentElement = null;
    renderBasicWorkspace();
  }

  // Redraws the current section page (sections.js owns the page).
  function renderBasicWorkspace() {
    if (window.ModSections) window.ModSections.refresh();
  }

  function renderGameCivEditor(element, civ) {
    const root = document.getElementById('basic-root');
    const opts = (S.state.catalog.civs || []).filter(c => c.id >= 1 && c.id <= 31)
      .map(c => `<option value="${c.id}" ${c.id === civ.extend ? 'selected' : ''}>${escapeHtml(c.name)}</option>`).join('');
    root.innerHTML = `
      <div class="basic-editor">
        <div class="basic-editor-header">
          <button class="btn btn-wood btn-sm" id="basic-back">← Back</button>
          <h2 class="basic-editor-title">${escapeHtml(civLabel(civ, element.file))}</h2>
          <div class="basic-editor-actions">
            ${testItControlHtml(element.file)}
            <button class="btn btn-wood btn-sm" id="basic-delete-element">Delete</button>
          </div>
        </div>
        <div class="basic-editor-content"><div class="basic-form">
          <div class="basic-form-section">
            <h3 class="basic-form-section-title">Civilization of the game</h3>
            <div class="basic-form-field"><label>Civilization</label><select id="gc-civ" class="form-input">${opts}</select></div>
            <p class="basic-muted">This mod changes this civilization of the game instead of making a new one.
              Add a Mod Building, Unit Changes or Bonuses for it from "+ New Element". Its name, AI names and
              unique unit stay the game's own.</p>
          </div>
          <details class="sc-section" data-pref-key="civ-custom-units" ${(civ.custom_units && civ.custom_units.length) ? 'open' : ''}>
            <summary class="sc-section-summary">
              ${CHEV_SVG}
              <span class="sc-section-title">Custom Units</span>
              <span class="sc-section-meta" id="civ-cu-meta">${(civ.custom_units && civ.custom_units.length) ? `${civ.custom_units.length} unit${civ.custom_units.length === 1 ? '' : 's'}` : 'None'}</span>
            </summary>
            <div class="sc-section-body">
              <div id="civ-cu-list-container"></div>
            </div>
          </details>
        </div></div>
      </div>`;
    root.querySelector('#basic-back').onclick = closeElementEditor;
    root.querySelector('#gc-civ').onchange = e => { civ.extend = Number(e.target.value); S.setDirty(true); };
    wireTestItControls(root);
    renderCustomUnitsSection(element, civ);
    wireDisclosures(root);
    root.querySelector('#basic-delete-element').onclick = () => S.showConfirm('Delete', `Remove all changes to ${getCivName(civ.extend)}?`, 'Delete', () => {
      delete S.state.currentModData.civs[element.file];
      if (!S.state.currentModData.deleted_civs) S.state.currentModData.deleted_civs = [];
      S.state.currentModData.deleted_civs.push(element.file);
      S.setDirty(true);
      closeElementEditor();
    });
  }

  function getCivSlug(civ, file) {
    let s = (civ && civ.dat_name) || (civ && civ.name) || '';
    if (!s && file) s = file.replace(/\.json$/i, '');
    s = s.toLowerCase().replace(/[^a-z0-9_-]+/g, '_').replace(/^_+|_+$/g, '');
    return s || 'civ';
  }

  function formatHistoryMarkup(raw) {
    if (!raw || !raw.trim()) {
      return '<span style="color:var(--text-3); font-style:italic;">No history article text written yet.</span>';
    }
    let escaped = escapeHtml(raw);
    escaped = escaped.replace(/&lt;b&gt;([\s\S]*?)&lt;\/?b&gt;/gi, '<b>$1</b>');
    return escaped;
  }

  function openCropDialog({ file, targetW, targetH, slotName, onApply }) {
    const modal = document.getElementById('modal-container');
    if (!modal) return;

    const reader = new FileReader();
    reader.onload = (e) => {
      const img = new Image();
      img.onload = () => {
        renderCropper(img);
      };
      img.onerror = () => {
        S.showToast('Failed to load image file', 'error');
      };
      img.src = e.target.result;
    };
    reader.readAsDataURL(file);

    function renderCropper(img) {
      modal.innerHTML = `
        <div class="modal-dialog crop-modal-dialog" style="max-width: 650px; width: 95%;">
          <div class="modal-header">
            <h4 class="modal-title">Crop ${escapeHtml(slotName)} (${targetW} × ${targetH})</h4>
            <button class="btn-modal-close" id="crop-close-btn">&times;</button>
          </div>
          <div class="modal-body">
            <div class="crop-canvas-wrapper" style="display:flex; justify-content:center; align-items:center; background:#0c0c0e; border:1px solid var(--line); border-radius:var(--radius); padding:12px; overflow:hidden; margin-bottom:12px;">
              <canvas id="crop-canvas" width="${targetW}" height="${targetH}" style="max-width:100%; height:auto; border:1px solid var(--accent); cursor:grab; display:block;"></canvas>
            </div>
            <div class="crop-toolbar" style="display:flex; align-items:center; gap:12px; margin-bottom:8px;">
              <label style="font-size:12px; color:var(--text-2); flex:none;">Zoom:</label>
              <input type="range" id="crop-zoom" min="1" max="3" step="0.02" value="1" style="flex:1;">
              <span id="crop-zoom-label" style="font-size:12px; color:var(--text-3); min-width:40px; text-align:right;">100%</span>
              <button type="button" class="btn btn-wood btn-xs" id="crop-reset-btn">Reset</button>
            </div>
            <p style="font-size:11.5px; color:var(--text-3); margin:0;">Drag image on the canvas to adjust position. Cover-fit with exact size ${targetW} × ${targetH}.</p>
          </div>
          <div class="modal-footer">
            <button class="btn btn-secondary btn-sm" id="crop-cancel-btn">Cancel</button>
            <button class="btn btn-gold btn-sm" id="crop-apply-btn">Crop &amp; Upload</button>
          </div>
        </div>
      `;
      modal.style.display = 'flex';

      const canvas = modal.querySelector('#crop-canvas');
      const ctx = canvas.getContext('2d');
      const zoomInput = modal.querySelector('#crop-zoom');
      const zoomLabel = modal.querySelector('#crop-zoom-label');

      const baseScale = Math.max(targetW / img.naturalWidth, targetH / img.naturalHeight);
      let zoom = 1.0;
      let offsetX = (targetW - img.naturalWidth * baseScale) / 2;
      let offsetY = (targetH - img.naturalHeight * baseScale) / 2;

      function draw() {
        const currentScale = baseScale * zoom;
        const curW = img.naturalWidth * currentScale;
        const curH = img.naturalHeight * currentScale;

        offsetX = Math.min(0, Math.max(targetW - curW, offsetX));
        offsetY = Math.min(0, Math.max(targetH - curH, offsetY));

        ctx.clearRect(0, 0, targetW, targetH);
        ctx.drawImage(img, offsetX, offsetY, curW, curH);
      }

      draw();

      let dragging = false;
      let dragStartX = 0, dragStartY = 0;
      let initialOffsetX = 0, initialOffsetY = 0;

      const onMouseDown = (e) => {
        dragging = true;
        dragStartX = e.clientX;
        dragStartY = e.clientY;
        initialOffsetX = offsetX;
        initialOffsetY = offsetY;
        canvas.style.cursor = 'grabbing';
        e.preventDefault();
      };

      const onMouseMove = (e) => {
        if (!dragging) return;
        const rect = canvas.getBoundingClientRect();
        const scaleFactor = targetW / rect.width;
        const dx = (e.clientX - dragStartX) * scaleFactor;
        const dy = (e.clientY - dragStartY) * scaleFactor;
        offsetX = initialOffsetX + dx;
        offsetY = initialOffsetY + dy;
        draw();
      };

      const onMouseUp = () => {
        if (dragging) {
          dragging = false;
          canvas.style.cursor = 'grab';
        }
      };

      canvas.addEventListener('mousedown', onMouseDown);
      window.addEventListener('mousemove', onMouseMove);
      window.addEventListener('mouseup', onMouseUp);

      zoomInput.addEventListener('input', () => {
        const prevZoom = zoom;
        zoom = parseFloat(zoomInput.value);
        zoomLabel.textContent = Math.round(zoom * 100) + '%';
        const factor = zoom / prevZoom;
        offsetX = targetW / 2 - (targetW / 2 - offsetX) * factor;
        offsetY = targetH / 2 - (targetH / 2 - offsetY) * factor;
        draw();
      });

      modal.querySelector('#crop-reset-btn').addEventListener('click', () => {
        zoom = 1.0;
        zoomInput.value = '1';
        zoomLabel.textContent = '100%';
        offsetX = (targetW - img.naturalWidth * baseScale) / 2;
        offsetY = (targetH - img.naturalHeight * baseScale) / 2;
        draw();
      });

      const cleanup = () => {
        window.removeEventListener('mousemove', onMouseMove);
        window.removeEventListener('mouseup', onMouseUp);
        modal.style.display = 'none';
        modal.innerHTML = '';
      };

      modal.querySelector('#crop-close-btn').onclick = cleanup;
      modal.querySelector('#crop-cancel-btn').onclick = cleanup;

      modal.querySelector('#crop-apply-btn').onclick = () => {
        draw();
        canvas.toBlob(async (blob) => {
          if (!blob) {
            S.showToast('Canvas export failed', 'error');
            return;
          }
          cleanup();
          try {
            await onApply(blob);
          } catch (err) {
            S.showToast('Upload failed: ' + err.message, 'error');
          }
        }, 'image/png');
      };
    }
  }

  function renderCivilizationEditor(element) {
    const data = S.state.currentModData;
    const civ = data.civs[element.file];
    if (!civ) return closeElementEditor();
    if (civ.extend) return renderGameCivEditor(element, civ);

    const historyTitle = (civ.history && civ.history.title) || '';
    const defaultTextFile = (civ.history && civ.history.text_file) || ('history/' + getCivSlug(civ, element.file) + '.txt');
    const aiCount = (civ.ai_names || []).length;

    const root = document.getElementById('basic-root');
    root.innerHTML = `
      <div class="basic-editor">
        <div class="basic-editor-header">
          <button class="btn btn-wood btn-sm" id="basic-back">← Back</button>
          <h2 class="basic-editor-title">${escapeHtml(element.title)}</h2>
          <div class="basic-editor-actions">
            ${testItControlHtml(element.file)}
            <button class="btn btn-ghost danger btn-sm" id="basic-delete-element">Delete</button>
          </div>
        </div>
        <div class="basic-editor-tab-bar">
          <button type="button" class="basic-tab-btn active" id="civ-tab-btn-general" data-tab="general">General</button>
          <button type="button" class="basic-tab-btn" id="civ-tab-btn-history" data-tab="history">History Book</button>
        </div>
        <div class="basic-editor-content">
          <!-- General Tab Pane -->
          <div id="civ-pane-general" class="civ-tab-pane">
            <div class="basic-form">
              <!-- Section 1: Identity (essential — open by default) -->
              <details class="sc-section" data-pref-key="civ-identity" open>
                <summary class="sc-section-summary">
                  ${CHEV_SVG}
                  <span class="sc-section-title">Identity</span>
                  <span class="sc-section-meta">Name, base template &amp; emblem</span>
                </summary>
                <div class="sc-section-body">
                  <div class="basic-form-row">
                    <div class="basic-form-field">
                      <label for="civ-name">Display Name <span class="required">*</span></label>
                      <input type="text" id="civ-name" value="${escapeHtml(civ.name || '')}" class="form-input" placeholder="e.g. The Monklanians">
                    </div>
                    <div class="basic-form-field">
                      <label for="civ-base">Base Civilization <span class="required">*</span></label>
                      <select id="civ-base" class="form-select">
                        ${S.state.catalog.civs.filter(c => c.id >= 1 && c.id <= 31).map(c =>
                          `<option value="${c.id}" ${c.id === civ.base ? 'selected' : ''}>${escapeHtml(c.name)} (ID ${c.id})</option>`
                        ).join('')}
                      </select>
                      <span class="basic-field-hint">Inherits tech tree, architecture, and base units.</span>
                    </div>
                  </div>
                  <div class="basic-form-field">
                    <label>Civilization Icon / Emblem</label>
                    <div id="civ-icon-field-container"></div>
                  </div>
                  <details class="sc-advanced" data-pref-key="civ-identity-adv">
                    <summary>${CHEV_SVG}<span>Advanced (Internal DAT ID)</span></summary>
                    <div class="sc-advanced-body">
                      <div class="basic-form-field">
                        <label for="civ-dat-name">Internal DAT Name (max 19 ASCII) <span class="required">*</span></label>
                        <input type="text" id="civ-dat-name" value="${escapeHtml(civ.dat_name || '')}" maxlength="19" class="form-input font-mono" placeholder="e.g. Monklanians">
                        <span class="basic-field-hint">Auto-filled from the name; only change if you need a specific DAT identifier.</span>
                      </div>
                    </div>
                  </details>
                </div>
              </details>

              <!-- Section 2: In-Game Description -->
              <details class="sc-section" data-pref-key="civ-description">
                <summary class="sc-section-summary">
                  ${CHEV_SVG}
                  <span class="sc-section-title">In-Game Description</span>
                  <span class="sc-section-meta">${civ.description ? 'Set' : 'Optional'}</span>
                </summary>
                <div class="sc-section-body">
                  <div class="basic-form-field">
                    <textarea id="civ-description" class="form-textarea" rows="4" placeholder="Shown in the civilization picker. Supports <b>bold</b> and line breaks.">${escapeHtml(civ.description || '')}</textarea>
                  </div>
                </div>
              </details>

              <!-- Section 2b: Custom Units -->
              <details class="sc-section" data-pref-key="civ-custom-units" ${(civ.custom_units && civ.custom_units.length) ? 'open' : ''}>
                <summary class="sc-section-summary">
                  ${CHEV_SVG}
                  <span class="sc-section-title">Custom Units</span>
                  <span class="sc-section-meta" id="civ-cu-meta">${(civ.custom_units && civ.custom_units.length) ? `${civ.custom_units.length} unit${civ.custom_units.length === 1 ? '' : 's'}` : 'None'}</span>
                </summary>
                <div class="sc-section-body">
                  <div id="civ-cu-list-container"></div>
                </div>
              </details>

              <!-- Section 3: Civilization Audio -->
              <details class="sc-section" data-pref-key="civ-audio">
                <summary class="sc-section-summary">
                  ${CHEV_SVG}
                  <span class="sc-section-title">Theme Audio</span>
                  <span class="sc-section-meta" id="civ-audio-section-meta">${(civ.sound || civ.victory_sound) ? 'Custom audio' : 'None'}</span>
                </summary>
                <div class="sc-section-body">
                  <div class="general-sound-card" id="civ-sound-card">
                    <div class="general-sound-left">
                      <div class="general-sound-icon-box">
                        <span class="general-sound-icon">&#9835;</span>
                      </div>
                      <div class="general-sound-info">
                        <div class="general-sound-name font-mono" id="civ-sound-name" title="${escapeHtml(civ.sound || '')}">
                          ${escapeHtml(civ.sound || '(no audio set)')}
                        </div>
                        <div class="general-sound-status" id="civ-sound-status">
                          ${civ.sound ? 'Ready to play' : 'Upload an MP3, WAV or OGG menu jingle'}
                        </div>
                      </div>
                    </div>
                    <div class="general-sound-actions">
                      <button type="button" class="btn btn-wood btn-sm" id="civ-sound-play-btn" ${civ.sound ? '' : 'disabled'}>&#9654; Play</button>
                      <button type="button" class="btn btn-wood btn-sm" id="civ-sound-pick-btn">Upload Sound...</button>
                      <button type="button" class="btn btn-ghost danger btn-sm" id="civ-sound-remove-btn" ${civ.sound ? '' : 'disabled'}>Remove</button>
                      <input type="file" id="civ-sound-file-input" accept=".mp3,.wav,.ogg,audio/*" style="display:none;">
                      <audio id="civ-sound-audio" style="display:none;"></audio>
                    </div>
                  </div>
                  <div class="basic-form-field" style="margin-top:12px;">
                    <label class="basic-field-label" for="civ-victory-sound-pick-btn">Victory sound</label>
                    <div class="general-sound-card" id="civ-victory-sound-card">
                      <div class="general-sound-left">
                        <div class="general-sound-icon-box">
                          <span class="general-sound-icon">&#9835;</span>
                        </div>
                        <div class="general-sound-info">
                          <div class="general-sound-name font-mono" id="civ-victory-sound-name" title="${escapeHtml(civ.victory_sound || '')}">
                            ${escapeHtml(civ.victory_sound || '(no audio set)')}
                          </div>
                          <div class="general-sound-status" id="civ-victory-sound-status">
                            ${civ.victory_sound ? 'Ready to play' : 'Upload an MP3 or WAV victory theme (local player wins)'}
                          </div>
                        </div>
                      </div>
                      <div class="general-sound-actions">
                        <button type="button" class="btn btn-wood btn-sm" id="civ-victory-sound-play-btn" ${civ.victory_sound ? '' : 'disabled'}>&#9654; Play</button>
                        <button type="button" class="btn btn-wood btn-sm" id="civ-victory-sound-pick-btn">Upload Sound...</button>
                        <button type="button" class="btn btn-ghost danger btn-sm" id="civ-victory-sound-remove-btn" ${civ.victory_sound ? '' : 'disabled'}>Remove</button>
                        <input type="file" id="civ-victory-sound-file-input" accept=".mp3,.wav,audio/*" style="display:none;">
                        <audio id="civ-victory-sound-audio" style="display:none;"></audio>
                      </div>
                    </div>
                  </div>
                </div>
              </details>

              <!-- Section 4: AI Player Names -->
              <details class="sc-section" data-pref-key="civ-ai-names">
                <summary class="sc-section-summary">
                  ${CHEV_SVG}
                  <span class="sc-section-title">AI Player Names</span>
                  <span class="sc-section-meta">${aiCount ? `${aiCount} name${aiCount === 1 ? '' : 's'}` : 'Default'}</span>
                </summary>
                <div class="sc-section-body">
                  <div class="basic-form-field">
                    <textarea id="civ-ai-names" class="form-textarea font-mono" rows="4" placeholder="King Arthur&#10;Lancelot&#10;Gawain">${escapeHtml((civ.ai_names || []).join('\n'))}</textarea>
                    <span class="basic-field-hint">One name per line; picked randomly for AI players.</span>
                  </div>
                </div>
              </details>
            </div>
          </div>

          <!-- History Tab Pane: Authentic AoE2 In-Game History Book -->
          <div id="civ-pane-history" class="civ-tab-pane" style="display: none;">
            <div class="history-book-stage">
              <div class="history-book-frame">
                <!-- Left Page: Civ List inside blue ornate frame, Side Portrait below -->
                <div class="history-book-left-page">
                  <div class="history-book-left-top">
                    <div class="history-civ-list-header">Civilizations</div>
                    <div class="history-civ-list">
                      ${Object.keys(data.civs).map(f => {
                        const c = data.civs[f];
                        const isSelected = f === element.file;
                        return `
                          <div class="history-civ-list-item ${isSelected ? 'selected' : ''}" data-file="${escapeHtml(f)}" title="Switch to ${escapeHtml(civLabel(c, f))}">
                            <span class="history-civ-item-name">${escapeHtml(civLabel(c, f))}</span>
                          </div>
                        `;
                      }).join('')}
                    </div>

                    <div class="history-quick-info">
                      <div class="history-info-row">
                        <span class="history-info-label">File:</span>
                        <span class="history-info-val font-mono" title="${escapeHtml(defaultTextFile)}">${escapeHtml(defaultTextFile)}</span>
                      </div>
                      <div class="history-info-tip">
                        Write history directly on the right page. Use &lt;B&gt;headings&lt;/B&gt; to style sections.
                      </div>
                    </div>
                  </div>

                  <!-- Side Illustration Slot (280x206) in bottom-left corner -->
                  <div class="history-book-side-slot-container">
                    <div class="history-slot-card" id="history-side-slot" title="Click to choose or crop side portrait">
                      <div class="history-slot-thumb" id="history-side-thumb">
                        <div class="history-thumb-placeholder">
                          <span class="placeholder-title">Side Portrait</span>
                          <span class="placeholder-dim">280 × 206</span>
                          <span class="placeholder-hint">Click to choose image</span>
                        </div>
                      </div>
                      <div class="history-slot-overlay">
                        <button type="button" class="btn btn-wood btn-xs" id="history-side-pick-btn">Choose Image</button>
                        <button type="button" class="btn btn-wood btn-xs" id="history-side-remove-btn" ${(civ.history && civ.history.side_image) ? '' : 'disabled'}>Remove</button>
                      </div>
                      <input type="file" id="history-side-file-input" accept="image/png,image/jpeg,image/webp" style="display:none;">
                    </div>
                  </div>
                </div>

                <!-- Right Page: Title, Main Illustration, History Text -->
                <div class="history-book-right-page">
                  <div class="history-book-right-content">
                    <!-- Title centered at top of right page -->
                    <div class="history-book-title-wrapper">
                      <input type="text" id="history-title" class="history-book-title-input" value="${escapeHtml(historyTitle)}" placeholder="${escapeHtml(civ.name || 'Civilization Name')}" title="Click to edit history title">
                    </div>

                    <!-- Main Illustration Slot (490x263) centered under title -->
                    <div class="history-book-main-slot-container">
                      <div class="history-slot-card history-main-aspect" id="history-main-slot" title="Click to choose or crop main illustration">
                        <div class="history-slot-thumb" id="history-main-thumb">
                          <div class="history-thumb-placeholder">
                            <span class="placeholder-title">Main Illustration</span>
                            <span class="placeholder-dim">490 × 263</span>
                            <span class="placeholder-hint">Click to choose &amp; crop image</span>
                          </div>
                        </div>
                        <div class="history-slot-overlay">
                          <button type="button" class="btn btn-wood btn-xs" id="history-main-pick-btn">Choose Image</button>
                          <button type="button" class="btn btn-wood btn-xs" id="history-main-remove-btn" ${(civ.history && civ.history.image) ? '' : 'disabled'}>Remove</button>
                        </div>
                        <input type="file" id="history-main-file-input" accept="image/png,image/jpeg,image/webp" style="display:none;">
                      </div>
                    </div>

                    <!-- History Text Toolbar & Text Area -->
                    <div class="history-book-text-area-container">
                      <div class="history-book-text-toolbar">
                        <button type="button" class="btn btn-wood btn-xs" id="history-bold-btn" title="Wrap selection in <B>..</B>"><b>B</b> Bold &lt;B&gt;</button>
                        <label class="history-mono-toggle"><input type="checkbox" id="history-mono-check"> Monospace</label>
                        <button type="button" class="btn btn-wood btn-xs" id="history-view-toggle-btn" title="Toggle between raw editor and book view">Book View</button>
                      </div>

                      <div class="history-text-wrapper">
                        <textarea id="history-text-input" class="history-book-textarea" placeholder="Write civilization history here. Supports <B>bold</B> headings and newlines..."></textarea>
                        <div id="book-article-body" class="history-book-formatted-view" style="display:none;"></div>
                      </div>
                    </div>

                    <!-- Back button at bottom right page -->
                    <div class="history-book-footer">
                      <button type="button" class="btn btn-wood btn-sm" id="history-back-btn">Back</button>
                    </div>
                  </div>
                </div>

              </div>
            </div>
          </div>
        </div>
      </div>
    `;

    setupCivEditorEvents(element, civ);
    wireTestItControls(root);
    renderCustomUnitsSection(element, civ);
    wireDisclosures(root);
  }

  function customUnitIconHtml(cu, size) {
    const baseUnit = findUnit(cu.copy_from);
    if (cu.icon !== undefined && cu.icon !== null && cu.icon !== '') {
      if (typeof cu.icon === 'number' && cu.icon < 1000 && window.GameIcons) {
        return GameIcons.markup(GameIcons.UNIT_SHEET, cu.icon, cu.name || 'U', size);
      }
      return `<span class="game-icon" style="--icon-size:${size}px" data-custom-slp="${escapeHtml(String(cu.icon))}"><span class="game-icon-fallback">${escapeHtml((cu.name || 'U').charAt(0).toUpperCase())}</span></span>`;
    }
    return unitIconHtml(baseUnit, size);
  }

  function customUnitSummary(cu, civ) {
    const baseUnit = findUnit(cu.copy_from);
    const parts = [`Based on ${baseUnit ? baseUnit.name : 'Unit ' + cu.copy_from}`];
    if (cu.cost) {
      const c = [];
      if (cu.cost.food) c.push(`${cu.cost.food}F`);
      if (cu.cost.wood) c.push(`${cu.cost.wood}W`);
      if (cu.cost.stone) c.push(`${cu.cost.stone}S`);
      if (cu.cost.gold) c.push(`${cu.cost.gold}G`);
      if (c.length) parts.push(c.join(' '));
    }
    if (cu.trained_at && cu.trained_at.building !== undefined) {
      const b = cu.trained_at.building;
      let bName = '';
      if (b === 'unique_building') {
        bName = (civ.unique_building && civ.unique_building.name) || 'Unique Building';
      } else {
        const bu = findUnit(Number(b));
        bName = bu ? bu.name : 'Building ' + b;
      }
      parts.push(`Trained at ${bName}`);
    }
    return parts.join(' · ');
  }

  function makeCustomUnitId(name, existing) {
    const base = (name || 'unit').toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '').slice(0, 40) || 'custom_unit';
    const taken = new Set((existing || []).map(u => u.id));
    if (!taken.has(base)) return base;
    let n = 2;
    while (taken.has(`${base}_${n}`)) n++;
    return `${base}_${n}`;
  }

  function renderCustomUnitsSection(element, civ) {
    const container = document.getElementById('civ-cu-list-container');
    if (!container) return;
    const list = civ.custom_units || [];
    const meta = document.getElementById('civ-cu-meta');
    if (meta) meta.textContent = list.length ? `${list.length} unit${list.length === 1 ? '' : 's'}` : 'None';

    const rowsHtml = list.map((cu, idx) => `
      <div class="basic-list-row" data-cu-idx="${idx}">
        ${customUnitIconHtml(cu, 32)}
        <div class="basic-list-main">
          <strong>${escapeHtml(cu.name || cu.id || 'Custom Unit')}</strong>
          <span class="basic-muted font-mono">${escapeHtml(cu.id || '')}</span>
          <div class="basic-muted">${escapeHtml(customUnitSummary(cu, civ))}</div>
        </div>
        <button type="button" class="btn btn-wood btn-sm" data-cu-act="edit" data-cu-idx="${idx}">Edit</button>
        <button type="button" class="btn btn-ghost danger btn-sm" data-cu-act="delete" data-cu-idx="${idx}">Delete</button>
      </div>`).join('');

    container.innerHTML = `
      <p class="basic-field-hint" style="margin-top:0;">Brand-new trainable units available only to this civilization.</p>
      <div class="basic-list">${rowsHtml || '<p class="basic-muted">No custom units yet.</p>'}</div>
      <div class="cu-list-actions"><button type="button" class="btn btn-save btn-sm" id="civ-cu-add">+ Add Custom Unit</button></div>
    `;

    container.querySelector('#civ-cu-add')?.addEventListener('click', () => {
      showUnitPicker(70, (unit) => {
        if (!civ.custom_units) civ.custom_units = [];
        civ.custom_units.push({
          id: makeCustomUnitId(unit.name, civ.custom_units),
          name: unit.name,
          copy_from: unit.id
        });
        S.setDirty(true);
        renderCustomUnitEditor(element, civ.custom_units.length - 1);
      });
    });

    container.querySelectorAll('[data-cu-act="edit"]').forEach(btn => {
      btn.addEventListener('click', () => renderCustomUnitEditor(element, Number(btn.dataset.cuIdx)));
    });

    container.querySelectorAll('[data-cu-act="delete"]').forEach(btn => {
      btn.addEventListener('click', () => {
        const idx = Number(btn.dataset.cuIdx);
        const target = list[idx];
        S.showConfirm('Delete Custom Unit', `Remove "${target?.name || target?.id || 'Custom Unit'}"?`, 'Delete', () => {
          civ.custom_units.splice(idx, 1);
          if (civ.custom_units.length === 0) delete civ.custom_units;
          S.setDirty(true);
          renderCustomUnitsSection(element, civ);
        });
      });
    });

    hydrateIcons();
  }

  function renderCustomUnitEditor(element, cuIndex) {
    const data = S.state.currentModData;
    const civ = data && data.civs[element.file];
    if (!civ || !Array.isArray(civ.custom_units) || !civ.custom_units[cuIndex]) {
      return renderCivilizationEditor(element);
    }
    const cu = civ.custom_units[cuIndex];
    const baseUnit = findUnit(cu.copy_from);
    const ages = ['Dark Age', 'Feudal Age', 'Castle Age', 'Imperial Age'];
    const commonBuildings = [
      { id: 12, label: 'Barracks (12)' },
      { id: 87, label: 'Archery Range (87)' },
      { id: 101, label: 'Stable (101)' },
      { id: 49, label: 'Siege Workshop (49)' },
      { id: 104, label: 'Monastery (104)' },
      { id: 82, label: 'Castle (82)' },
      { id: 109, label: 'Town Center (109)' },
      { id: 45, label: 'Dock (45)' },
      { id: 84, label: 'Market (84)' }
    ];

    const curBldg = cu.trained_at && cu.trained_at.building !== undefined ? String(cu.trained_at.building) : '';
    const hasCustomBldgOpt = curBldg && curBldg !== 'unique_building' && !commonBuildings.some(b => String(b.id) === curBldg);
    const customBldgUnit = hasCustomBldgOpt ? findUnit(Number(curBldg)) : null;

    const meleeArmor = (cu.armor && typeof cu.armor === 'object') ? (cu.armor.melee ?? '') : (cu.armor ?? '');
    const pierceArmor = (cu.armor && typeof cu.armor === 'object') ? (cu.armor.pierce ?? '') : (cu.pierce_armor ?? '');

    const curAge = cu.available_in_age ?? 0;
    const gfxCount = cu.graphics ? Object.keys(cu.graphics).length : 0;
    const lookMeta = (cu.icon !== undefined ? 1 : 0) + gfxCount;
    const advStatCount = ['train_time', 'speed', 'range', 'line_of_sight'].filter(k => cu[k] !== undefined).length +
      (meleeArmor !== '' ? 1 : 0) + (pierceArmor !== '' ? 1 : 0);

    const root = document.getElementById('basic-root');
    root.innerHTML = `
      <div class="basic-editor">
        <div class="basic-editor-header">
          <button class="btn btn-wood btn-sm" id="cu-back">← Back to ${escapeHtml(civLabel(civ, element.file))}</button>
          <h2 class="basic-editor-title" id="cu-editor-title">${escapeHtml(cu.name || cu.id || 'Custom Unit')}</h2>
          <div class="basic-editor-actions">
            <button class="btn btn-ghost danger btn-sm" id="cu-delete">Delete</button>
          </div>
        </div>
        <div class="basic-editor-content">
          <div class="basic-form">
            <details class="sc-section" data-pref-key="cu-essentials" open>
              <summary class="sc-section-summary">
                ${CHEV_SVG}
                <span class="sc-section-title">Essentials</span>
                <span class="sc-section-meta">${baseUnit ? escapeHtml(baseUnit.name) : 'Pick base unit'}</span>
              </summary>
              <div class="sc-section-body">
                <div class="basic-form-row">
                  <div class="basic-form-field">
                    <label for="cu-name">Unit Name <span class="required">*</span></label>
                    <input type="text" id="cu-name" value="${escapeHtml(cu.name || '')}" class="form-input" placeholder="e.g. Forest Tracker">
                  </div>
                  <div class="basic-form-field">
                    <label>Based On <span class="required">*</span></label>
                    <button type="button" class="btn btn-wood btn-sm btn-block" id="cu-pick-base">${baseUnit ? unitIconHtml(baseUnit, 20) + escapeHtml(baseUnit.name) + ' (' + baseUnit.id + ')' : 'Pick unit'}</button>
                  </div>
                </div>
                <div class="basic-form-field">
                  <label>Cost</label>
                  <div class="basic-cost-grid">
                    <div class="basic-form-field">
                      <label for="cu-cost-food">Food</label>
                      <input type="number" min="0" max="32767" id="cu-cost-food" value="${cu.cost?.food ?? ''}" placeholder="0" class="form-input">
                    </div>
                    <div class="basic-form-field">
                      <label for="cu-cost-wood">Wood</label>
                      <input type="number" min="0" max="32767" id="cu-cost-wood" value="${cu.cost?.wood ?? ''}" placeholder="0" class="form-input">
                    </div>
                    <div class="basic-form-field">
                      <label for="cu-cost-stone">Stone</label>
                      <input type="number" min="0" max="32767" id="cu-cost-stone" value="${cu.cost?.stone ?? ''}" placeholder="0" class="form-input">
                    </div>
                    <div class="basic-form-field">
                      <label for="cu-cost-gold">Gold</label>
                      <input type="number" min="0" max="32767" id="cu-cost-gold" value="${cu.cost?.gold ?? ''}" placeholder="0" class="form-input">
                    </div>
                  </div>
                </div>
                <div class="basic-form-row">
                  <div class="basic-form-field">
                    <label for="cu-hp">Hit Points</label>
                    <input type="number" min="1" max="32767" id="cu-hp" value="${cu.hit_points ?? ''}" placeholder="Base" class="form-input">
                  </div>
                  <div class="basic-form-field">
                    <label for="cu-attack">Attack</label>
                    <input type="number" min="0" max="32767" id="cu-attack" value="${cu.attack ?? ''}" placeholder="Base" class="form-input">
                  </div>
                </div>
                <details class="sc-advanced" data-pref-key="cu-essentials-adv">
                  <summary>${CHEV_SVG}<span>ID &amp; tooltip help text</span></summary>
                  <div class="sc-advanced-body">
                    <div class="basic-form-field">
                      <label for="cu-id">Unit Key (ID) <span class="required">*</span></label>
                      <input type="text" id="cu-id" value="${escapeHtml(cu.id || '')}" maxlength="64" class="form-input font-mono" placeholder="e.g. forest_tracker">
                      <span class="basic-field-hint">Short unique key inside this civilization.</span>
                    </div>
                    <div class="basic-form-field">
                      <label for="cu-help">Help Text</label>
                      <textarea id="cu-help" class="form-textarea" rows="2" placeholder="Tooltip shown on the train button">${escapeHtml(cu.help || '')}</textarea>
                    </div>
                  </div>
                </details>
              </div>
            </details>

            <details class="sc-section" data-pref-key="cu-training">
              <summary class="sc-section-summary">
                ${CHEV_SVG}
                <span class="sc-section-title">Training Building &amp; Age</span>
                <span class="sc-section-meta">${curBldg ? `${ages[curAge] || 'Dark Age'}` : 'Not placed in a building'}</span>
              </summary>
              <div class="sc-section-body">
                <div class="basic-form-field">
                  <label for="cu-trained-building">Trained At Building</label>
                  <div class="cu-bldg-picker-row">
                    <select id="cu-trained-building" class="form-select">
                      <option value="" ${!curBldg ? 'selected' : ''}>None (not trained in a building)</option>
                      ${(civ.unique_building || curBldg === 'unique_building') ? `<option value="unique_building" ${curBldg === 'unique_building' ? 'selected' : ''}>${escapeHtml((civ.unique_building && civ.unique_building.name) || 'Unique Building')} (this civ's unique building)</option>` : ''}
                      ${commonBuildings.map(b => `<option value="${b.id}" ${curBldg === String(b.id) ? 'selected' : ''}>${escapeHtml(b.label)}</option>`).join('')}
                      ${hasCustomBldgOpt ? `<option value="${escapeHtml(curBldg)}" selected>${escapeHtml(customBldgUnit ? `${customBldgUnit.name} (${curBldg})` : `Building ${curBldg}`)}</option>` : ''}
                    </select>
                    <button type="button" class="btn btn-wood btn-sm" id="cu-pick-building">Pick Building...</button>
                  </div>
                </div>
                <div class="basic-form-row" id="cu-trained-placement" style="${curBldg ? '' : 'display:none;'}">
                  <div class="basic-form-field">
                    <label for="cu-trained-page">Command Page</label>
                    <input type="number" min="1" max="10" id="cu-trained-page" value="${cu.trained_at?.page ?? 1}" class="form-input">
                  </div>
                  <div class="basic-form-field">
                    <label for="cu-trained-button">Button Slot (1-14)</label>
                    <input type="number" min="1" max="14" id="cu-trained-button" value="${cu.trained_at?.button ?? 1}" class="form-input">
                    <span class="basic-field-hint">Slot 15 is reserved for Next Page.</span>
                  </div>
                </div>
                <div class="basic-form-field">
                  <label for="cu-age">Available in Age</label>
                  <select id="cu-age" class="form-select">
                    ${ages.map((label, i) => `<option value="${i}" ${curAge === i ? 'selected' : ''}>${label}</option>`).join('')}
                  </select>
                </div>
              </div>
            </details>

            <details class="sc-section" data-pref-key="cu-look">
              <summary class="sc-section-summary">
                ${CHEV_SVG}
                <span class="sc-section-title">Icon &amp; Graphics</span>
                <span class="sc-section-meta">${lookMeta ? `${lookMeta} custom` : 'Using base art'}</span>
              </summary>
              <div class="sc-section-body">
                <div class="building-visual-subfield">
                  <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:6px;gap:8px;flex-wrap:wrap;">
                    <label style="font-size:12px;font-weight:600;color:var(--text);">Unit Icon</label>
                    <span class="basic-form-hint">Train button and selection portrait (icons/units/)</span>
                  </div>
                  <div id="cu-icon-field"></div>
                </div>
                <div class="building-visual-subfield">
                  <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:6px;gap:8px;flex-wrap:wrap;">
                    <label style="font-size:12px;font-weight:600;color:var(--text);">Unit Animations</label>
                    <span class="basic-form-hint">Own SLPs for standing, walking, attack and dying</span>
                  </div>
                  <div id="cu-look-container"></div>
                </div>
              </div>
            </details>

            <details class="sc-section" data-pref-key="cu-stats">
              <summary class="sc-section-summary">
                ${CHEV_SVG}
                <span class="sc-section-title">Advanced Stats</span>
                <span class="sc-section-meta">${advStatCount ? `${advStatCount} modified` : 'Using base stats'}</span>
              </summary>
              <div class="sc-section-body">
                <div class="basic-form-row">
                  <div class="basic-form-field">
                    <label for="cu-train-time">Train Time (seconds)</label>
                    <input type="number" min="0" max="32767" id="cu-train-time" value="${cu.train_time ?? ''}" placeholder="Base" class="form-input">
                  </div>
                  <div class="basic-form-field">
                    <label for="cu-speed">Movement Speed</label>
                    <input type="number" step="0.01" min="0" id="cu-speed" value="${cu.speed ?? ''}" placeholder="Base" class="form-input">
                  </div>
                </div>
                <div class="basic-form-row">
                  <div class="basic-form-field">
                    <label for="cu-melee-armor">Melee Armor</label>
                    <input type="number" min="0" max="32767" id="cu-melee-armor" value="${meleeArmor}" placeholder="Base" class="form-input">
                  </div>
                  <div class="basic-form-field">
                    <label for="cu-pierce-armor">Pierce Armor</label>
                    <input type="number" min="0" max="32767" id="cu-pierce-armor" value="${pierceArmor}" placeholder="Base" class="form-input">
                  </div>
                </div>
                <div class="basic-form-row">
                  <div class="basic-form-field">
                    <label for="cu-range">Max Range</label>
                    <input type="number" step="0.1" min="0" id="cu-range" value="${cu.range ?? ''}" placeholder="Base" class="form-input">
                  </div>
                  <div class="basic-form-field">
                    <label for="cu-los">Line of Sight</label>
                    <input type="number" step="0.5" min="0" id="cu-los" value="${cu.line_of_sight ?? ''}" placeholder="Base" class="form-input">
                  </div>
                </div>
              </div>
            </details>
          </div>
        </div>
      </div>
    `;

    root.querySelector('#cu-back')?.addEventListener('click', () => renderCivilizationEditor(element));
    root.querySelector('#cu-delete')?.addEventListener('click', () => {
      S.showConfirm('Delete Custom Unit', `Remove "${cu.name || cu.id || 'Custom Unit'}"?`, 'Delete', () => {
        civ.custom_units.splice(cuIndex, 1);
        if (civ.custom_units.length === 0) delete civ.custom_units;
        S.setDirty(true);
        renderCivilizationEditor(element);
      });
    });

    root.querySelector('#cu-pick-base')?.addEventListener('click', () => {
      showUnitPicker(70, (unit) => {
        cu.copy_from = unit.id;
        S.setDirty(true);
        renderCustomUnitEditor(element, cuIndex);
      });
    });

    root.querySelector('#cu-name')?.addEventListener('input', (e) => {
      cu.name = e.target.value;
      const t = document.getElementById('cu-editor-title');
      if (t) t.textContent = cu.name || cu.id || 'Custom Unit';
      S.setDirty(true);
    });

    root.querySelector('#cu-id')?.addEventListener('input', (e) => {
      cu.id = e.target.value.trim();
      S.setDirty(true);
    });

    root.querySelector('#cu-help')?.addEventListener('input', (e) => {
      if (e.target.value) cu.help = e.target.value;
      else delete cu.help;
      S.setDirty(true);
    });

    ['food', 'wood', 'stone', 'gold'].forEach(res => {
      root.querySelector(`#cu-cost-${res}`)?.addEventListener('input', (e) => {
        if (!cu.cost) cu.cost = {};
        if (e.target.value !== '') cu.cost[res] = Math.max(0, Math.round(Number(e.target.value)));
        else delete cu.cost[res];
        if (Object.keys(cu.cost).length === 0) delete cu.cost;
        S.setDirty(true);
      });
    });

    const numKeys = [
      ['#cu-hp', 'hit_points', true],
      ['#cu-attack', 'attack', true],
      ['#cu-train-time', 'train_time', true],
      ['#cu-speed', 'speed', false],
      ['#cu-range', 'range', false],
      ['#cu-los', 'line_of_sight', false]
    ];
    numKeys.forEach(([sel, key, isInt]) => {
      root.querySelector(sel)?.addEventListener('input', (e) => {
        if (e.target.value !== '') {
          const v = Number(e.target.value);
          cu[key] = isInt ? Math.round(v) : v;
        } else {
          delete cu[key];
        }
        S.setDirty(true);
      });
    });

    const syncArmor = () => {
      const mVal = root.querySelector('#cu-melee-armor')?.value;
      const pVal = root.querySelector('#cu-pierce-armor')?.value;
      const hasM = mVal !== undefined && mVal !== '';
      const hasP = pVal !== undefined && pVal !== '';
      delete cu.armor;
      delete cu.pierce_armor;
      if (hasM && hasP) {
        cu.armor = { melee: Math.round(Number(mVal)), pierce: Math.round(Number(pVal)) };
      } else if (hasM) {
        cu.armor = Math.round(Number(mVal));
      } else if (hasP) {
        cu.pierce_armor = Math.round(Number(pVal));
      }
      S.setDirty(true);
    };
    root.querySelector('#cu-melee-armor')?.addEventListener('input', syncArmor);
    root.querySelector('#cu-pierce-armor')?.addEventListener('input', syncArmor);

    const bldgSel = root.querySelector('#cu-trained-building');
    const placementEl = root.querySelector('#cu-trained-placement');
    const pageEl = root.querySelector('#cu-trained-page');
    const btnEl = root.querySelector('#cu-trained-button');

    bldgSel?.addEventListener('change', () => {
      const v = bldgSel.value;
      if (!v) {
        delete cu.trained_at;
        if (placementEl) placementEl.style.display = 'none';
      } else {
        const b = v === 'unique_building' ? 'unique_building' : Number(v);
        cu.trained_at = {
          building: b,
          page: Math.max(1, Math.round(Number(pageEl?.value) || 1)),
          button: Math.max(1, Math.min(14, Math.round(Number(btnEl?.value) || 1)))
        };
        if (placementEl) placementEl.style.display = '';
      }
      S.setDirty(true);
    });

    root.querySelector('#cu-pick-building')?.addEventListener('click', () => {
      showUnitPicker(80, (unit) => {
        cu.trained_at = {
          building: unit.id,
          page: Math.max(1, Math.round(Number(pageEl?.value) || 1)),
          button: Math.max(1, Math.min(14, Math.round(Number(btnEl?.value) || 1)))
        };
        S.setDirty(true);
        renderCustomUnitEditor(element, cuIndex);
      });
    });

    pageEl?.addEventListener('input', () => {
      if (cu.trained_at) {
        cu.trained_at.page = Math.max(1, Math.round(Number(pageEl.value) || 1));
        S.setDirty(true);
      }
    });
    btnEl?.addEventListener('input', () => {
      if (cu.trained_at) {
        cu.trained_at.button = Math.max(1, Math.min(14, Math.round(Number(btnEl.value) || 1)));
        S.setDirty(true);
      }
    });

    root.querySelector('#cu-age')?.addEventListener('change', (e) => {
      const a = Number(e.target.value);
      if (a === 0) delete cu.available_in_age;
      else cu.available_in_age = a;
      S.setDirty(true);
    });

    renderSpriteField(document.getElementById('cu-icon-field'), {
      value: cu.icon,
      kind: 'icon',
      iconFolder: 'units',
      sheet: window.GameIcons ? GameIcons.UNIT_SHEET : '50730',
      title: cu.icon !== undefined ? `Custom Icon (${cu.icon})` : (baseUnit ? `${baseUnit.name} (Base Icon #${baseUnit.icon || ''})` : 'Default Base Icon'),
      hint: cu.icon !== undefined ? 'Saved in icons/units/' : 'Inherited from template unit',
      fallbackUnit: baseUnit,
      onChange: (newIcon) => {
        cu.icon = newIcon;
        slpThumbCache.delete(newIcon);
        S.setDirty(true);
        renderCustomUnitEditor(element, cuIndex);
      },
      onRemove: cu.icon !== undefined ? () => {
        delete cu.icon;
        S.setDirty(true);
        renderCustomUnitEditor(element, cuIndex);
      } : null
    });

    renderLookForm('cu-look-container', cu, 'graphics', element);
    wireDisclosures(root);
    hydrateIcons();
  }

  function setupCivEditorEvents(element, civ) {
    const folder = S.state.currentModFolder;
    document.getElementById('basic-back')?.addEventListener('click', closeElementEditor);
    document.getElementById('basic-delete-element')?.addEventListener('click', () => {
      deleteElement(element);
    });

    // General inputs
    const inputs = ['civ-name', 'civ-dat-name', 'civ-base', 'civ-description', 'civ-ai-names'];
    inputs.forEach(id => {
      const el = document.getElementById(id);
      if (el) {
        el.addEventListener('input', () => updateCivFromForm(civ));
        el.addEventListener('change', () => updateCivFromForm(civ));
      }
    });

    // Civilization Icon / Emblem Field
    renderSpriteField('civ-icon-field-container', {
      value: (typeof civ.icon === 'number' || /^\d+$/.test(civ.icon)) ? Number(civ.icon) : null,
      kind: 'icon',
      title: civ.icon ? `Civilization Icon (#${civ.icon})` : 'Mod Default Shield',
      hint: civ.icon ? 'Custom civilization emblem/icon' : 'Using mod emblem (icon.png)',
      onChange: (newSlp) => {
        civ.icon = newSlp;
        S.setDirty(true);
        renderCivilizationEditor(element);
        S.showToast(`Updated civilization icon to #${newSlp}`, 'success');
      },
      onClear: () => {
        delete civ.icon;
        S.setDirty(true);
        renderCivilizationEditor(element);
        S.showToast('Civilization icon reset to default mod emblem', 'info');
      }
    });

    // Tabs switching
    const tabBtnGeneral = document.getElementById('civ-tab-btn-general');
    const tabBtnHistory = document.getElementById('civ-tab-btn-history');
    const paneGeneral = document.getElementById('civ-pane-general');
    const paneHistory = document.getElementById('civ-pane-history');

    function switchTab(tab) {
      if (tab === 'history') {
        tabBtnGeneral?.classList.remove('active');
        tabBtnHistory?.classList.add('active');
        if (paneGeneral) paneGeneral.style.display = 'none';
        if (paneHistory) paneHistory.style.display = 'block';
      } else {
        tabBtnHistory?.classList.remove('active');
        tabBtnGeneral?.classList.add('active');
        if (paneHistory) paneHistory.style.display = 'none';
        if (paneGeneral) paneGeneral.style.display = 'block';
      }
    }

    tabBtnGeneral?.addEventListener('click', () => switchTab('general'));
    tabBtnHistory?.addEventListener('click', () => switchTab('history'));

    // History elements
    const titleInput = document.getElementById('history-title');
    const textInput = document.getElementById('history-text-input');
    const boldBtn = document.getElementById('history-bold-btn');
    const monoCheck = document.getElementById('history-mono-check');
    const bookArticleBody = document.getElementById('book-article-body');
    const viewToggleBtn = document.getElementById('history-view-toggle-btn');
    const historyBackBtn = document.getElementById('history-back-btn');
    let isPreviewMode = false;

    // Title input
    titleInput?.addEventListener('input', () => {
      const val = titleInput.value.trim();
      if (val) {
        civ.history = civ.history || {};
        civ.history.title = val;
      } else if (civ.history) {
        delete civ.history.title;
        if (Object.keys(civ.history).length === 0) delete civ.history;
      }
      S.setDirty(true);
    });

    // View toggle (Edit Raw <-> Book View)
    viewToggleBtn?.addEventListener('click', () => {
      isPreviewMode = !isPreviewMode;
      if (isPreviewMode) {
        if (textInput) textInput.style.display = 'none';
        if (bookArticleBody) {
          bookArticleBody.style.display = 'block';
          bookArticleBody.innerHTML = formatHistoryMarkup(textInput ? textInput.value : '');
        }
        viewToggleBtn.textContent = 'Edit Raw';
      } else {
        if (bookArticleBody) bookArticleBody.style.display = 'none';
        if (textInput) {
          textInput.style.display = 'block';
          textInput.focus();
        }
        viewToggleBtn.textContent = 'Book View';
      }
    });

    // Back button switches to General tab
    historyBackBtn?.addEventListener('click', () => {
      switchTab('general');
    });

    // Bold button
    boldBtn?.addEventListener('click', () => {
      if (!textInput) return;
      const start = textInput.selectionStart;
      const end = textInput.selectionEnd;
      const val = textInput.value;
      const sel = val.slice(start, end);
      const repl = `<B>${sel}</B>`;
      textInput.setRangeText(repl, start, end, 'end');
      if (start === end) {
        textInput.selectionStart = textInput.selectionEnd = start + 3;
      }
      textInput.focus();
      textInput.dispatchEvent(new Event('input'));
    });

    // Monospace toggle
    monoCheck?.addEventListener('change', () => {
      textInput?.classList.toggle('font-mono', monoCheck.checked);
    });

    // History text input
    textInput?.addEventListener('input', () => {
      civ.history = civ.history || {};
      if (!civ.history.text_file) {
        civ.history.text_file = 'history/' + getCivSlug(civ, element.file) + '.txt';
        const destPathEl = document.getElementById('history-dest-path');
        if (destPathEl) destPathEl.textContent = 'Saved to ' + civ.history.text_file;
      }
      delete civ.history.text; // Ensure saved to text_file, not inline
      modifiedHistoryTexts.set(element.file, {
        path: civ.history.text_file,
        text: textInput.value
      });
      if (bookArticleBody) {
        bookArticleBody.innerHTML = formatHistoryMarkup(textInput.value);
      }
      S.setDirty(true);
    });

    // Load history text
    if (modifiedHistoryTexts.has(element.file)) {
      const loadedTxt = modifiedHistoryTexts.get(element.file).text;
      if (textInput) textInput.value = loadedTxt;
      if (bookArticleBody) bookArticleBody.innerHTML = formatHistoryMarkup(loadedTxt);
    } else if (civ.history && civ.history.text_file) {
      S.api(`/api/file?folder=${encodeURIComponent(folder)}&path=${encodeURIComponent(civ.history.text_file)}`)
        .then(res => res.text())
        .then(txt => {
          if (textInput) textInput.value = txt;
          if (bookArticleBody) bookArticleBody.innerHTML = formatHistoryMarkup(txt);
        })
        .catch(() => {
          const fallbackTxt = civ.history.text || '';
          if (textInput) textInput.value = fallbackTxt;
          if (bookArticleBody) bookArticleBody.innerHTML = formatHistoryMarkup(fallbackTxt);
        });
    } else if (civ.history && civ.history.text) {
      if (textInput) textInput.value = civ.history.text;
      if (bookArticleBody) bookArticleBody.innerHTML = formatHistoryMarkup(civ.history.text);
    } else {
      if (bookArticleBody) bookArticleBody.innerHTML = formatHistoryMarkup('');
    }

    // Sound controls in General Tab
    const audioSectionMeta = document.getElementById('civ-audio-section-meta');
    function updateAudioSectionMeta() {
      if (audioSectionMeta) audioSectionMeta.textContent = (civ.sound || civ.victory_sound) ? 'Custom audio' : 'None';
    }

    function wireSoundCard({ prop, prefix, cardId, slugSuffix, emptyStatus, toastUploaded, toastRemoved }) {
      const playBtn = document.getElementById(`${prefix}-play-btn`);
      const audio = document.getElementById(`${prefix}-audio`);
      const pickBtn = document.getElementById(`${prefix}-pick-btn`);
      const removeBtn = document.getElementById(`${prefix}-remove-btn`);
      const fileInput = document.getElementById(`${prefix}-file-input`);
      const nameEl = document.getElementById(`${prefix}-name`);
      const statusEl = document.getElementById(`${prefix}-status`);
      const card = document.getElementById(cardId);
      let blobUrl = null;

      function updateUI() {
        const val = civ[prop];
        if (val) {
          if (nameEl) { nameEl.textContent = val; nameEl.title = val; }
          if (statusEl) statusEl.textContent = 'Audio configured (' + val.split('.').pop().toUpperCase() + ')';
          if (playBtn) playBtn.disabled = false;
          if (removeBtn) removeBtn.disabled = false;
        } else {
          if (nameEl) { nameEl.textContent = '(no audio set)'; nameEl.title = ''; }
          if (statusEl) statusEl.textContent = emptyStatus;
          if (playBtn) playBtn.disabled = true;
          if (removeBtn) removeBtn.disabled = true;
        }
        updateAudioSectionMeta();
      }

      playBtn?.addEventListener('click', async () => {
        const val = civ[prop];
        if (!val) return;
        try {
          if (!audio.paused && !audio.ended && audio.currentTime > 0) {
            audio.pause();
            playBtn.textContent = '▶ Play';
            if (statusEl) statusEl.textContent = 'Paused';
            return;
          }
          if (!blobUrl) {
            if (statusEl) statusEl.textContent = 'Loading audio...';
            const res = await S.api(`/api/file?folder=${encodeURIComponent(folder)}&path=${encodeURIComponent(val)}`);
            const blob = await res.blob();
            blobUrl = URL.createObjectURL(blob);
            audio.src = blobUrl;
          }
          audio.onended = () => {
            playBtn.textContent = '▶ Play';
            if (statusEl) statusEl.textContent = 'Finished';
          };
          await audio.play();
          playBtn.textContent = '⏸ Pause';
          if (statusEl) statusEl.textContent = 'Playing...';
        } catch (err) {
          playBtn.textContent = '▶ Play';
          if (statusEl) statusEl.textContent = 'Playback error';
          S.showToast('Could not play sound: ' + err.message, 'error');
        }
      });

      async function uploadFile(file) {
        if (!file) return;
        const ext = file.name.slice(file.name.lastIndexOf('.')).toLowerCase() || '.mp3';
        const soundRelPath = 'sounds/' + getCivSlug(civ, element.file) + slugSuffix + ext;
        try {
          if (statusEl) statusEl.textContent = 'Uploading...';
          await S.api(`/api/file?folder=${encodeURIComponent(folder)}&path=${encodeURIComponent(soundRelPath)}`, {
            method: 'PUT',
            headers: { 'Content-Type': file.type || 'audio/mpeg' },
            body: file
          });
          civ[prop] = soundRelPath;
          S.setDirty(true);
          if (blobUrl) {
            URL.revokeObjectURL(blobUrl);
            blobUrl = null;
          }
          if (audio) audio.src = '';
          if (playBtn) playBtn.textContent = '▶ Play';
          updateUI();
          S.showToast(`${toastUploaded} ${soundRelPath}`, 'success');
        } catch (err) {
          updateUI();
          S.showToast('Failed to upload sound: ' + err.message, 'error');
        }
      }

      pickBtn?.addEventListener('click', () => fileInput?.click());
      fileInput?.addEventListener('change', async (e) => {
        const file = e.target.files && e.target.files[0];
        if (file) await uploadFile(file);
        fileInput.value = '';
      });
      if (card) {
        card.addEventListener('dragover', (e) => {
          e.preventDefault();
          card.classList.add('dragover');
        });
        card.addEventListener('dragleave', (e) => {
          if (!card.contains(e.relatedTarget)) card.classList.remove('dragover');
        });
        card.addEventListener('drop', async (e) => {
          e.preventDefault();
          card.classList.remove('dragover');
          const file = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
          if (file) await uploadFile(file);
        });
      }
      removeBtn?.addEventListener('click', () => {
        if (civ[prop]) {
          delete civ[prop];
          if (blobUrl) {
            URL.revokeObjectURL(blobUrl);
            blobUrl = null;
          }
          if (audio) {
            audio.pause();
            audio.src = '';
          }
          if (playBtn) playBtn.textContent = '▶ Play';
          S.setDirty(true);
          updateUI();
          S.showToast(toastRemoved, 'info');
        }
      });
    }

    wireSoundCard({
      prop: 'sound',
      prefix: 'civ-sound',
      cardId: 'civ-sound-card',
      slugSuffix: '',
      emptyStatus: 'Upload an MP3, WAV or OGG theme audio for this civ',
      toastUploaded: 'Sound uploaded and set to',
      toastRemoved: 'Civilization audio removed'
    });

    wireSoundCard({
      prop: 'victory_sound',
      prefix: 'civ-victory-sound',
      cardId: 'civ-victory-sound-card',
      slugSuffix: '_victory',
      emptyStatus: 'Upload an MP3 or WAV victory theme (local player wins)',
      toastUploaded: 'Victory sound uploaded and set to',
      toastRemoved: 'Victory sound removed'
    });

    // History Civ List item switcher
    document.querySelectorAll('.history-civ-list-item').forEach(item => {
      item.addEventListener('click', () => {
        const targetFile = item.dataset.file;
        if (targetFile && targetFile !== element.file) {
          openElementEditor({
            type: 'civilization',
            file: targetFile,
            title: civLabel(S.state.currentModData.civs[targetFile], targetFile)
          });
          setTimeout(() => {
            const hBtn = document.getElementById('civ-tab-btn-history');
            hBtn?.click();
          }, 0);
        }
      });
    });

    // Image previews and pickers
    const mainSlot = document.getElementById('history-main-slot');
    const mainPickBtn = document.getElementById('history-main-pick-btn');
    const mainFileInput = document.getElementById('history-main-file-input');
    const mainRemoveBtn = document.getElementById('history-main-remove-btn');
    const mainThumbBox = document.getElementById('history-main-thumb');

    const sideSlot = document.getElementById('history-side-slot');
    const sidePickBtn = document.getElementById('history-side-pick-btn');
    const sideFileInput = document.getElementById('history-side-file-input');
    const sideRemoveBtn = document.getElementById('history-side-remove-btn');
    const sideThumbBox = document.getElementById('history-side-thumb');

    function renderMainThumb(url) {
      if (url) {
        if (mainThumbBox) mainThumbBox.innerHTML = `<img src="${url}" alt="Main illustration">`;
        if (mainRemoveBtn) mainRemoveBtn.disabled = false;
        mainSlot?.classList.add('has-image');
      } else {
        if (mainThumbBox) mainThumbBox.innerHTML = `
          <div class="history-thumb-placeholder">
            <span class="placeholder-title">Main Illustration</span>
            <span class="placeholder-dim">490 × 263</span>
            <span class="placeholder-hint">Click to choose &amp; crop image</span>
          </div>`;
        if (mainRemoveBtn) mainRemoveBtn.disabled = true;
        mainSlot?.classList.remove('has-image');
      }
    }

    function renderSideThumb(url) {
      if (url) {
        if (sideThumbBox) sideThumbBox.innerHTML = `<img src="${url}" alt="Side portrait">`;
        if (sideRemoveBtn) sideRemoveBtn.disabled = false;
        sideSlot?.classList.add('has-image');
      } else {
        if (sideThumbBox) sideThumbBox.innerHTML = `
          <div class="history-thumb-placeholder">
            <span class="placeholder-title">Side Portrait</span>
            <span class="placeholder-dim">280 × 206</span>
            <span class="placeholder-hint">Click to choose image</span>
          </div>`;
        if (sideRemoveBtn) sideRemoveBtn.disabled = true;
        sideSlot?.classList.remove('has-image');
      }
    }

    if (civ.history && civ.history.image) {
      S.api(`/api/file?folder=${encodeURIComponent(folder)}&path=${encodeURIComponent(civ.history.image)}`)
        .then(res => res.blob())
        .then(blob => {
          const url = URL.createObjectURL(blob);
          renderMainThumb(url);
        })
        .catch(() => renderMainThumb(null));
    } else {
      renderMainThumb(null);
    }

    if (civ.history && civ.history.side_image) {
      S.api(`/api/file?folder=${encodeURIComponent(folder)}&path=${encodeURIComponent(civ.history.side_image)}`)
        .then(res => res.blob())
        .then(blob => {
          const url = URL.createObjectURL(blob);
          renderSideThumb(url);
        })
        .catch(() => renderSideThumb(null));
    } else {
      renderSideThumb(null);
    }

    mainPickBtn?.addEventListener('click', () => mainFileInput?.click());
    sidePickBtn?.addEventListener('click', () => sideFileInput?.click());

    mainSlot?.addEventListener('click', (e) => {
      if (e.target.closest('button')) return;
      mainFileInput?.click();
    });

    sideSlot?.addEventListener('click', (e) => {
      if (e.target.closest('button')) return;
      sideFileInput?.click();
    });

    function handleMainFile(file) {
      if (!file) return;
      openCropDialog({
        file,
        targetW: 490,
        targetH: 263,
        slotName: 'Main Illustration',
        onApply: async (blob) => {
          const destPath = (civ.history && civ.history.image) || ('history/' + getCivSlug(civ, element.file) + '.png');
          await S.api(`/api/file?folder=${encodeURIComponent(folder)}&path=${encodeURIComponent(destPath)}`, {
            method: 'PUT',
            headers: { 'Content-Type': 'image/png' },
            body: blob
          });
          civ.history = civ.history || {};
          civ.history.image = destPath;
          const blobUrl = URL.createObjectURL(blob);
          renderMainThumb(blobUrl);
          S.setDirty(true);
          S.showToast('Main illustration cropped and uploaded', 'success');
        }
      });
    }

    function handleSideFile(file) {
      if (!file) return;
      openCropDialog({
        file,
        targetW: 280,
        targetH: 206,
        slotName: 'Side Illustration',
        onApply: async (blob) => {
          const destPath = (civ.history && civ.history.side_image) || ('history/' + getCivSlug(civ, element.file) + '_side.png');
          await S.api(`/api/file?folder=${encodeURIComponent(folder)}&path=${encodeURIComponent(destPath)}`, {
            method: 'PUT',
            headers: { 'Content-Type': 'image/png' },
            body: blob
          });
          civ.history = civ.history || {};
          civ.history.side_image = destPath;
          const blobUrl = URL.createObjectURL(blob);
          renderSideThumb(blobUrl);
          S.setDirty(true);
          S.showToast('Side illustration cropped and uploaded', 'success');
        }
      });
    }

    mainSlot?.addEventListener('dragover', (e) => {
      e.preventDefault();
      mainSlot.classList.add('dragover');
    });
    mainSlot?.addEventListener('dragleave', (e) => {
      if (!mainSlot.contains(e.relatedTarget)) mainSlot.classList.remove('dragover');
    });
    mainSlot?.addEventListener('drop', (e) => {
      e.preventDefault();
      mainSlot.classList.remove('dragover');
      const file = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
      if (file) handleMainFile(file);
    });

    sideSlot?.addEventListener('dragover', (e) => {
      e.preventDefault();
      sideSlot.classList.add('dragover');
    });
    sideSlot?.addEventListener('dragleave', (e) => {
      if (!sideSlot.contains(e.relatedTarget)) sideSlot.classList.remove('dragover');
    });
    sideSlot?.addEventListener('drop', (e) => {
      e.preventDefault();
      sideSlot.classList.remove('dragover');
      const file = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
      if (file) handleSideFile(file);
    });

    mainFileInput?.addEventListener('change', (e) => {
      const file = e.target.files && e.target.files[0];
      if (file) handleMainFile(file);
      mainFileInput.value = '';
    });

    sideFileInput?.addEventListener('change', (e) => {
      const file = e.target.files && e.target.files[0];
      if (file) handleSideFile(file);
      sideFileInput.value = '';
    });

    mainRemoveBtn?.addEventListener('click', () => {
      if (civ.history && civ.history.image) {
        delete civ.history.image;
        if (Object.keys(civ.history).length === 0) delete civ.history;
        renderMainThumb(null);
        S.setDirty(true);
      }
    });

    sideRemoveBtn?.addEventListener('click', () => {
      if (civ.history && civ.history.side_image) {
        delete civ.history.side_image;
        if (Object.keys(civ.history).length === 0) delete civ.history;
        renderSideThumb(null);
        S.setDirty(true);
      }
    });
  }

  function updateCivFromForm(civ) {
    const nameEl = document.getElementById('civ-name');
    const datNameEl = document.getElementById('civ-dat-name');
    const baseEl = document.getElementById('civ-base');
    const descEl = document.getElementById('civ-description');
    const aiNamesEl = document.getElementById('civ-ai-names');
    const historyTitleInput = document.getElementById('history-title');

    if (nameEl) {
      civ.name = nameEl.value.trim();
      if (historyTitleInput && (!civ.history || !civ.history.title)) {
        historyTitleInput.placeholder = civ.name || 'Civilization Name';
      }
    }
    if (datNameEl) civ.dat_name = datNameEl.value.trim();
    if (baseEl) civ.base = Number(baseEl.value);
    if (descEl) civ.description = descEl.value;
    if (aiNamesEl) civ.ai_names = aiNamesEl.value.split('\n').map(s => s.trim()).filter(s => s);

    S.setDirty(true);
  }

  function deleteElement(element) {
    S.showConfirm(`Delete ${element.title}?`, 'This cannot be undone.', () => {
      const data = S.state.currentModData;
      if (element.type === 'civilization') {
        modifiedHistoryTexts.delete(element.file);
        delete data.civs[element.file];
        if (!data.deleted_civs) data.deleted_civs = [];
        data.deleted_civs.push(element.file);
      } else if (element.type === 'unique_unit') {
        delete data.civs[element.file].unique_unit;
      } else if (element.type === 'unique_building') {
        delete data.civs[element.file].unique_building;
      } else if (element.type === 'unit_changes') {
        delete data.civs[element.file].units[element.unitId];
      } else if (element.type === 'bonuses') {
        data.civs[element.file].effects = [];
        data.civs[element.file].team_bonus_effects = [];
      }
      S.setDirty(true);
      closeElementEditor();
    });
  }

  function renderPlaceholderEditor(element) {
    const root = document.getElementById('basic-root');
    root.innerHTML = `
      <div class="basic-editor">
        <div class="basic-editor-header">
          <button class="btn btn-wood btn-sm" id="basic-back">← Back</button>
          <h2 class="basic-editor-title">${escapeHtml(element.title)}</h2>
        </div>
        <div class="basic-editor-content">
          <p style="color: var(--text-dark); padding: 20px;">${getElementTypeName(element.type)} editor coming soon.</p>
        </div>
      </div>
    `;
    document.getElementById('basic-back')?.addEventListener('click', closeElementEditor);
  }

  // ==========================================================================
  // Unique Unit Editor
  // ==========================================================================

  function renderUniqueUnitEditor(element) {
    const data = S.state.currentModData;
    const civ = data.civs[element.file];
    const uniqueUnit = civ.unique_unit;
    if (!uniqueUnit) return closeElementEditor();

    const baseUnit = findUnit(uniqueUnit.copy_from || 125);
    const baseStats = { hit_points: 30, speed: 0.7, line_of_sight: 6, max_range: 9, reload_time: 2.0 };

    const root = document.getElementById('basic-root');
    const customStatCount = ['hit_points', 'speed', 'line_of_sight', 'max_range', 'reload_time']
      .filter(k => uniqueUnit.units && uniqueUnit.units[k] !== undefined).length;
    root.innerHTML = `
      <div class="basic-editor">
        <div class="basic-editor-header">
          <button class="btn btn-wood btn-sm" id="basic-back">← Back</button>
          <h2 class="basic-editor-title">${escapeHtml(civ.name || element.file)} - Unique Unit</h2>
          <div class="basic-editor-actions">
            <button class="btn btn-ghost danger btn-sm" id="basic-delete-element">Delete</button>
          </div>
        </div>
        <div class="basic-editor-content">
          <div class="basic-form">
            <details class="sc-section" data-pref-key="uu-basics" open>
              <summary class="sc-section-summary">
                ${CHEV_SVG}
                <span class="sc-section-title">Basics</span>
                <span class="sc-section-meta">${baseUnit ? escapeHtml(baseUnit.name) : 'Pick base unit'}</span>
              </summary>
              <div class="sc-section-body">
                <div class="basic-form-row">
                  <div class="basic-form-field">
                    <label>Name <span class="required">*</span></label>
                    <input type="text" id="uu-name" value="${escapeHtml(uniqueUnit.name || '')}" class="form-input">
                  </div>
                  <div class="basic-form-field">
                    <label>Based On</label>
                    <button class="btn btn-wood btn-sm btn-block" id="uu-pick-base">${baseUnit ? unitIconHtml(baseUnit, 20) + escapeHtml(baseUnit.name) + ' (' + baseUnit.id + ')' : 'Pick unit'}</button>
                  </div>
                </div>
                <details class="sc-advanced" data-pref-key="uu-basics-adv">
                  <summary>${CHEV_SVG}<span>Advanced (Elite name &amp; tooltip)</span></summary>
                  <div class="sc-advanced-body">
                    <div class="basic-form-field">
                      <label>Elite Name</label>
                      <input type="text" id="uu-elite-name" value="${escapeHtml(uniqueUnit.elite_name || '')}" placeholder="Elite ${escapeHtml(uniqueUnit.name || 'Unit')}" class="form-input">
                    </div>
                    <div class="basic-form-field">
                      <label>Help Text</label>
                      <textarea id="uu-help" class="form-textarea" rows="2" placeholder="In-game tooltip description">${escapeHtml(uniqueUnit.help || '')}</textarea>
                    </div>
                  </div>
                </details>
              </div>
            </details>

            <details class="sc-section" data-pref-key="uu-stats">
              <summary class="sc-section-summary">
                ${CHEV_SVG}
                <span class="sc-section-title">Stats</span>
                <span class="sc-section-meta">${customStatCount ? `${customStatCount} modified` : 'Using base stats'}</span>
              </summary>
              <div class="sc-section-body">
                <div class="basic-form-row">
                  <div class="basic-form-field">
                    <label>Hit Points (Base: ${baseStats.hit_points})</label>
                    <input type="number" id="uu-hp" value="${uniqueUnit.units?.hit_points ?? ''}" placeholder="${baseStats.hit_points}" class="form-input">
                  </div>
                  <div class="basic-form-field">
                    <label>Speed (Base: ${baseStats.speed})</label>
                    <input type="number" step="0.01" id="uu-speed" value="${uniqueUnit.units?.speed ?? ''}" placeholder="${baseStats.speed}" class="form-input">
                  </div>
                </div>
                <details class="sc-advanced" data-pref-key="uu-stats-adv">
                  <summary>${CHEV_SVG}<span>Advanced stats (Sight, Range, Reload)</span></summary>
                  <div class="sc-advanced-body">
                    <div class="basic-form-row">
                      <div class="basic-form-field">
                        <label>Line of Sight (Base: ${baseStats.line_of_sight})</label>
                        <input type="number" id="uu-los" value="${uniqueUnit.units?.line_of_sight ?? ''}" placeholder="${baseStats.line_of_sight}" class="form-input">
                      </div>
                      <div class="basic-form-field">
                        <label>Max Range (Base: ${baseStats.max_range})</label>
                        <input type="number" step="0.1" id="uu-range" value="${uniqueUnit.units?.max_range ?? ''}" placeholder="${baseStats.max_range}" class="form-input">
                      </div>
                    </div>
                    <div class="basic-form-row">
                      <div class="basic-form-field">
                        <label>Reload Time (Base: ${baseStats.reload_time})</label>
                        <input type="number" step="0.1" id="uu-reload" value="${uniqueUnit.units?.reload_time ?? ''}" placeholder="${baseStats.reload_time}" class="form-input">
                      </div>
                    </div>
                  </div>
                </details>
              </div>
            </details>

            <details class="sc-section" id="uu-look-section" data-pref-key="uu-look">
              <summary class="sc-section-summary">
                ${CHEV_SVG}
                <span class="sc-section-title">Look &amp; Animations</span>
                <span class="sc-section-meta">${uniqueUnit.graphics && Object.keys(uniqueUnit.graphics).length ? `${Object.keys(uniqueUnit.graphics).length} custom` : 'Default'}</span>
              </summary>
              <div class="sc-section-body">
                <div id="uu-look-container"></div>
              </div>
            </details>
          </div>
        </div>
      </div>
    `;

    document.getElementById('basic-back')?.addEventListener('click', closeElementEditor);
    document.getElementById('basic-delete-element')?.addEventListener('click', () => deleteElement(element));
    document.getElementById('uu-pick-base')?.addEventListener('click', () => showUnitPicker(70, (unit) => {
      uniqueUnit.copy_from = unit.id;
      S.setDirty(true);
      renderUniqueUnitEditor(element);
    }));

    setupUniqueUnitEvents(uniqueUnit);
    renderLookForm('uu-look-container', uniqueUnit, 'graphics', element);
    wireDisclosures(root);
  }

  function setupUniqueUnitEvents(uniqueUnit) {
    const fields = [
      { id: 'uu-name', key: 'name', type: 'string' },
      { id: 'uu-elite-name', key: 'elite_name', type: 'string' },
      { id: 'uu-help', key: 'help', type: 'string' },
      { id: 'uu-hp', unitKey: 'hit_points', type: 'number' },
      { id: 'uu-speed', unitKey: 'speed', type: 'number' },
      { id: 'uu-los', unitKey: 'line_of_sight', type: 'number' },
      { id: 'uu-range', unitKey: 'max_range', type: 'number' },
      { id: 'uu-reload', unitKey: 'reload_time', type: 'number' }
    ];

    fields.forEach(({ id, key, unitKey, type }) => {
      const el = document.getElementById(id);
      if (el) {
        el.addEventListener('input', () => {
          if (key) {
            uniqueUnit[key] = type === 'number' && el.value ? Number(el.value) : el.value;
          } else if (unitKey) {
            if (!uniqueUnit.units) uniqueUnit.units = {};
            if (el.value) {
              uniqueUnit.units[unitKey] = Number(el.value);
            } else {
              delete uniqueUnit.units[unitKey];
            }
          }
          S.setDirty(true);
        });
      }
    });
  }

  // ==========================================================================
  // Unique Building Editor
  // ==========================================================================

  let ubActivePage = 1;

  function getBuildingType(building) {
    if (building.building_type) return building.building_type;
    if (building.commands && building.commands.length > 0) {
      if (building.commands.some(c => c.train !== undefined)) return 'recruit';
      if (building.commands.some(c => c.research !== undefined)) return 'tech';
      if (building.commands.some(c => c.lua !== undefined)) return 'custom';
    }
    return 'recruit';
  }

  function renderUniqueBuildingEditor(element) {
    const data = S.state.currentModData;
    const civ = data.civs[element.file];
    const building = civ.unique_building;
    if (!building) return closeElementEditor();

    // Clean up any transient UI state keys from JSON
    if (building._activePage) {
      ubActivePage = building._activePage;
      delete building._activePage;
    }

    const buildingType = getBuildingType(building);
    const baseUnit = findUnit(building.copy_from || 104);
    const ages = ['Dark Age', 'Feudal Age', 'Castle Age', 'Imperial Age'];
    const pages = [{ id: 'mods', label: 'Mod Buildings' }, { id: 'military', label: 'Military' }];

    const root = document.getElementById('basic-root');
    const cmdCount = (building.commands || []).length;
    const visualsCount = (building.icon ? 1 : 0) + (building.slp ? 1 : 0);
    root.innerHTML = `
      <div class="basic-editor">
        <div class="basic-editor-header">
          <button class="btn btn-wood btn-sm" id="basic-back">← Back</button>
          <h2 class="basic-editor-title">${escapeHtml(civ.name || element.file)} - Unique Building</h2>
          <div class="basic-editor-actions">
            <button class="btn btn-ghost danger btn-sm" id="basic-delete-element">Delete</button>
          </div>
        </div>
        <div class="basic-editor-content">
          <div class="basic-form">
            <details class="sc-section" data-pref-key="ub-basics" open>
              <summary class="sc-section-summary">
                ${CHEV_SVG}
                <span class="sc-section-title">Basics</span>
                <span class="sc-section-meta">${ages[building.age || 0] || 'Castle Age'}</span>
              </summary>
              <div class="sc-section-body">
                <div class="basic-form-row">
                  <div class="basic-form-field">
                    <label>Name <span class="required">*</span></label>
                    <input type="text" id="ub-name" value="${escapeHtml(building.name || '')}" class="form-input">
                  </div>
                  <div class="basic-form-field">
                    <label>Based On</label>
                    <button class="btn btn-wood btn-sm btn-block" id="ub-pick-base">${baseUnit ? unitIconHtml(baseUnit, 20) + escapeHtml(baseUnit.name) + ' (' + baseUnit.id + ')' : 'Pick building'}</button>
                  </div>
                </div>

                <div class="basic-form-field">
                  <label>Building Type <span class="required">*</span></label>
                  <div class="building-type-grid">
                    <label class="building-type-card ${buildingType === 'recruit' ? 'active' : ''}" data-btype="recruit">
                      <input type="radio" name="ub-building-type" value="recruit" class="building-type-radio" ${buildingType === 'recruit' ? 'checked' : ''}>
                      <div class="btype-icon-box">
                        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                          <polyline points="14.5 17.5 3 6 3 3 6 3 17.5 14.5"/>
                          <line x1="13" y1="19" x2="19" y2="13"/>
                          <line x1="16" y1="16" x2="20" y2="20"/>
                        </svg>
                      </div>
                      <div class="btype-text">
                        <span class="btype-title">Recruit</span>
                        <span class="btype-desc">Trains units (reserves Slot 5 for Gather Point)</span>
                      </div>
                    </label>

                    <label class="building-type-card ${buildingType === 'tech' ? 'active' : ''}" data-btype="tech">
                      <input type="radio" name="ub-building-type" value="tech" class="building-type-radio" ${buildingType === 'tech' ? 'checked' : ''}>
                      <div class="btype-icon-box">
                        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                          <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/>
                          <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/>
                        </svg>
                      </div>
                      <div class="btype-text">
                        <span class="btype-title">Tech</span>
                        <span class="btype-desc">Researches upgrades &amp; technologies</span>
                      </div>
                    </label>

                    <label class="building-type-card ${buildingType === 'custom' ? 'active' : ''}" data-btype="custom">
                      <input type="radio" name="ub-building-type" value="custom" class="building-type-radio" ${buildingType === 'custom' ? 'checked' : ''}>
                      <div class="btype-icon-box">
                        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                          <polyline points="16 18 22 12 16 6"/>
                          <polyline points="8 6 2 12 8 18"/>
                        </svg>
                      </div>
                      <div class="btype-text">
                        <span class="btype-title">Custom</span>
                        <span class="btype-desc">Custom Lua or Visual Script buttons</span>
                      </div>
                    </label>
                  </div>
                </div>

                <div class="basic-form-row">
                  <div class="basic-form-field">
                    <label>Available in Age <span class="required">*</span></label>
                    <select id="ub-age" class="form-select">
                      ${ages.map((age, i) => `<option value="${i}" ${building.age === i ? 'selected' : ''}>${age}</option>`).join('')}
                    </select>
                  </div>
                  <div class="basic-form-field">
                    <label>Hit Points</label>
                    <input type="number" id="ub-hp" value="${building.units?.hit_points ?? ''}" placeholder="2100" class="form-input">
                  </div>
                </div>

                <details class="sc-advanced" data-pref-key="ub-basics-adv">
                  <summary>${CHEV_SVG}<span>Advanced (Build menu placement &amp; tooltip)</span></summary>
                  <div class="sc-advanced-body">
                    <div class="basic-form-field">
                      <label>Help Text</label>
                      <textarea id="ub-help" class="form-textarea" rows="2" placeholder="Tooltip shown in villager build menu">${escapeHtml(building.help || '')}</textarea>
                    </div>
                    <div class="basic-form-row">
                      <div class="basic-form-field">
                        <label>Build Menu Page</label>
                        <select id="ub-page" class="form-select">
                          ${pages.map(p => `<option value="${p.id}" ${building.page === p.id ? 'selected' : ''}>${p.label}</option>`).join('')}
                        </select>
                      </div>
                      <div class="basic-form-field">
                        <label>Button Slot (1-15) <span class="required">*</span></label>
                        <input type="number" id="ub-button" min="1" max="15" value="${building.button || 1}" class="form-input">
                      </div>
                    </div>
                  </div>
                </details>
              </div>
            </details>

            <!-- Building Visuals: Menu Icon & Battlefield Sprite -->
            <details class="sc-section" data-pref-key="ub-visuals">
              <summary class="sc-section-summary">
                ${CHEV_SVG}
                <span class="sc-section-title">Visuals &amp; Icon</span>
                <span class="sc-section-meta">${visualsCount ? `${visualsCount} custom` : 'Using base art'}</span>
              </summary>
              <div class="sc-section-body">
                <div class="building-visual-subfield">
                  <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 6px; gap: 8px; flex-wrap: wrap;">
                    <label style="font-size: 12px; font-weight: 600; color: var(--text);">1. Menu &amp; Build Button Icon</label>
                    <span class="basic-form-hint">Villager build menu and selection portrait</span>
                  </div>
                  <div id="ub-icon-field-container"></div>
                </div>

                <div class="building-visual-subfield">
                  <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 6px; gap: 8px; flex-wrap: wrap;">
                    <label style="font-size: 12px; font-weight: 600; color: var(--text);">2. Map Building Sprite</label>
                    <span class="basic-form-hint">Isometric building graphic on the map</span>
                  </div>
                  <div id="ub-sprite-field-container"></div>
                </div>
              </div>
            </details>

            <!-- AoE2 Command Grid (5x3 Slots) Section -->
            <details class="sc-section" data-pref-key="ub-commands">
              <summary class="sc-section-summary">
                ${CHEV_SVG}
                <span class="sc-section-title">Command Panel &amp; Buttons</span>
                <span class="sc-section-meta">${cmdCount} button${cmdCount === 1 ? '' : 's'}</span>
              </summary>
              <div class="sc-section-body">
                <div id="ub-command-panel-container"></div>
              </div>
            </details>
          </div>
        </div>
      </div>
    `;

    document.getElementById('basic-back')?.addEventListener('click', closeElementEditor);
    document.getElementById('basic-delete-element')?.addEventListener('click', () => deleteElement(element));
    document.getElementById('ub-pick-base')?.addEventListener('click', () => showUnitPicker(80, (unit) => {
      building.copy_from = unit.id;
      S.setDirty(true);
      renderUniqueBuildingEditor(element);
    }));

    setupUniqueBuildingEvents(building, element, baseUnit);
    renderCommandPanel(building, element);
    wireDisclosures(root);
  }

  function setupUniqueBuildingEvents(building, element, baseUnit) {
    if (!baseUnit) baseUnit = findUnit(building.copy_from || 104);
    const fields = [
      { id: 'ub-name', key: 'name', type: 'string' },
      { id: 'ub-help', key: 'help', type: 'string' },
      { id: 'ub-age', key: 'age', type: 'number' },
      { id: 'ub-page', key: 'page', type: 'string' },
      { id: 'ub-button', key: 'button', type: 'number' },
      { id: 'ub-hp', unitKey: 'hit_points', type: 'number' }
    ];

    fields.forEach(({ id, key, unitKey, type }) => {
      const el = document.getElementById(id);
      if (el) {
        el.addEventListener('input', () => {
          if (key) {
            building[key] = type === 'number' && el.value ? Number(el.value) : el.value;
          } else if (unitKey) {
            if (!building.units) building.units = {};
            if (el.value) {
              building.units[unitKey] = Number(el.value);
            } else {
              delete building.units[unitKey];
            }
          }
          S.setDirty(true);
        });
        el.addEventListener('change', () => {
          if (key) {
            building[key] = type === 'number' && el.value ? Number(el.value) : el.value;
          }
          S.setDirty(true);
        });
      }
    });

    // 1. Building Menu & Portrait Icon Field (Universal Sprite Component)
    renderSpriteField(document.getElementById('ub-icon-field-container'), {
      value: building.icon,
      kind: 'icon',
      title: building.icon ? `Custom Menu Icon (#${building.icon})` : (baseUnit ? `${baseUnit.name} (Base Icon #${baseUnit.icon_id || ''})` : 'Default Base Icon'),
      hint: building.icon ? 'Custom button/portrait icon' : 'Inherited from base building in villager build menu',
      fallbackUnit: baseUnit,
      onChange: (newIcon) => {
        building.icon = newIcon;
        slpThumbCache.delete(newIcon);
        S.setDirty(true);
        renderUniqueBuildingEditor(element);
      },
      onRemove: building.icon !== undefined ? () => {
        delete building.icon;
        S.setDirty(true);
        S.showToast('Building icon reset to base building icon', 'info');
        renderUniqueBuildingEditor(element);
      } : null
    });

    // 2. Building Battlefield Sprite Field (Universal Sprite Component)
    renderSpriteField(document.getElementById('ub-sprite-field-container'), {
      value: building.slp,
      kind: 'building',
      title: building.slp ? `Battlefield Sprite (#${building.slp})` : (baseUnit ? `${baseUnit.name} (Base Graphic)` : 'Default Building Art'),
      hint: building.slp ? 'Custom graphics DRS file for map structure' : 'Uses standard base building 3D visuals on map',
      fallbackUnit: baseUnit,
      onChange: (newSlp) => {
        building.slp = newSlp;
        slpThumbCache.delete(newSlp);
        S.setDirty(true);
        renderUniqueBuildingEditor(element);
      },
      onRemove: building.slp ? () => {
        delete building.slp;
        S.setDirty(true);
        S.showToast('Building sprite reset to base art', 'info');
        renderUniqueBuildingEditor(element);
      } : null
    });


    // Building Type Selector events
    document.querySelectorAll('.building-type-card').forEach(card => {
      card.addEventListener('click', () => {
        const btype = card.dataset.btype;
        if (btype) {
          building.building_type = btype;
          S.setDirty(true);
          renderUniqueBuildingEditor(element);
        }
      });
    });
  }

  function renderCommandPanel(building, element) {
    const container = document.getElementById('ub-command-panel-container');
    if (!container) return;

    if (!building.commands) building.commands = [];
    const buildingType = getBuildingType(building);

    // Track active page (1-based)
    if (!ubActivePage) ubActivePage = 1;
    let activePage = ubActivePage;

    // Discover contiguous pages 1..maxPage
    let maxPage = Math.max(1, ubActivePage || 1);
    const pageCmdCounts = {};
    building.commands.forEach(c => {
      const p = Number(c.page) || 1;
      pageCmdCounts[p] = (pageCmdCounts[p] || 0) + 1;
      if (p > maxPage) maxPage = p;
    });
    const pages = [];
    for (let p = 1; p <= maxPage; p++) {
      pages.push(p);
    }

    // Current page commands
    const pageCmds = building.commands
      .map((c, i) => ({ cmd: c, idx: i }))
      .filter(({ cmd }) => (Number(cmd.page) || 1) === activePage);

    // Build the 15 slots (5 cols x 3 rows) matching in-game AoE2 grid
    let slotsHtml = '';
    for (let slot = 1; slot <= 15; slot++) {
      const matches = pageCmds.filter(({ cmd }) => Number(cmd.button) === slot);
      const isDuplicate = matches.length > 1;
      const entry = matches[0];

      // Slot 5 in recruit building on page 1 is reserved for Gather Point
      const isGatherPoint = !entry && slot === 5 && buildingType === 'recruit' && activePage === 1;
      // Slot 15 is reserved for Next Page cycling when building has multiple pages
      const isNextPage = !entry && slot === 15 && maxPage > 1;

      if (entry) {
        const cmd = entry.cmd;
        let title = '';
        let iconHtml = '';

        if (cmd.train !== undefined) {
          const isComplex = typeof cmd.train === 'object';
          const unitId = isComplex ? cmd.train.copy_from : cmd.train;
          const unit = findUnit(unitId);
          title = (isComplex && cmd.train.name) ? cmd.train.name : (unit ? unit.name : 'Unit ' + unitId);
          iconHtml = unit ? unitIconHtml(unit, 40) : `<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#e2b354" stroke-width="1.8"><polyline points="14.5 17.5 3 6 3 3 6 3 17.5 14.5"/><line x1="13" y1="19" x2="19" y2="13"/><line x1="16" y1="16" x2="20" y2="20"/></svg>`;
        } else if (cmd.research) {
          title = cmd.research.name || 'Research';
          iconHtml = `<svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="#e6c88b" stroke-width="1.8"><path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/><path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/></svg>`;
        } else if (cmd.lua) {
          title = cmd.text || cmd.lua || 'Script';
          iconHtml = `<svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="#7ac4eb" stroke-width="1.8"><polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/></svg>`;
        }

        slotsHtml += `
          <div class="aoe2-cmd-slot slot-occupied ${isDuplicate ? 'slot-duplicate' : ''}" data-slot="${slot}" data-cmd-idx="${entry.idx}" title="Slot ${slot}: ${escapeHtml(title)} (Click to edit)">
            <span class="slot-number">${slot}</span>
            <div class="slot-actions-overlay">
              <button type="button" class="slot-mini-btn btn-edit" data-action="edit" data-cmd-idx="${entry.idx}" title="Edit">✎</button>
              <button type="button" class="slot-mini-btn btn-del" data-action="delete" data-cmd-idx="${entry.idx}" title="Delete">×</button>
            </div>
            <div class="slot-icon-wrapper">
              ${iconHtml}
            </div>
            <span class="slot-title">${escapeHtml(title)}</span>
          </div>
        `;
      } else if (isGatherPoint) {
        slotsHtml += `
          <div class="aoe2-cmd-slot slot-gather-point" data-slot="5" title="Slot 5: Set Gather Point flag (AoE2 default for military buildings. Click to configure or override)">
            <span class="slot-number">5</span>
            <div class="gather-point-art">
              <svg viewBox="0 0 36 36" fill="none">
                <line x1="7" y1="6" x2="27" y2="6" stroke="#c0a060" stroke-width="2.5" stroke-linecap="round"/>
                <line x1="17" y1="2" x2="17" y2="6" stroke="#e0caa0" stroke-width="2"/>
                <rect x="9" y="7" width="13" height="17" fill="#e8e8ea" stroke="#777780" stroke-width="1"/>
                <line x1="12" y1="8" x2="12" y2="23" stroke="#2b5bbb" stroke-width="1.5"/>
                <line x1="15" y1="8" x2="15" y2="23" stroke="#2b5bbb" stroke-width="1.5"/>
                <line x1="18" y1="8" x2="18" y2="23" stroke="#2b5bbb" stroke-width="1.5"/>
                <line x1="10" y1="24" x2="10" y2="26" stroke="#777780" stroke-width="1"/>
                <line x1="12" y1="24" x2="12" y2="26" stroke="#777780" stroke-width="1"/>
                <line x1="14" y1="24" x2="14" y2="26" stroke="#777780" stroke-width="1"/>
                <line x1="16" y1="24" x2="16" y2="26" stroke="#777780" stroke-width="1"/>
                <line x1="18" y1="24" x2="18" y2="26" stroke="#777780" stroke-width="1"/>
                <line x1="20" y1="24" x2="20" y2="26" stroke="#777780" stroke-width="1"/>
                <polygon points="26,10 30,10 30,17 33,17 28,25 23,17 26,17" fill="#e62222" stroke="#600" stroke-width="0.8"/>
              </svg>
            </div>
            <span class="gather-point-label">Gather Point</span>
            <span class="gather-point-sublabel">Reserved</span>
          </div>
        `;
      } else if (isNextPage) {
        slotsHtml += `
          <div class="aoe2-cmd-slot slot-gather-point slot-next-page" data-slot="15" title="Slot 15: Next Page button (Reserved when multi-page is active, automatically cycles command pages)">
            <span class="slot-number">15</span>
            <div class="gather-point-art">
              <svg viewBox="0 0 36 36" fill="none">
                <polygon points="12,9 25,18 12,27" fill="#e62222" stroke="#600" stroke-width="1.2"/>
              </svg>
            </div>
            <span class="gather-point-label">Next Page</span>
            <span class="gather-point-sublabel">Reserved</span>
          </div>
        `;
      } else {
        slotsHtml += `
          <div class="aoe2-cmd-slot slot-empty" data-slot="${slot}" title="Slot ${slot}: Empty (Click to add command button)">
            <span class="slot-number">${slot}</span>
            <svg class="slot-empty-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <line x1="12" y1="5" x2="12" y2="19"></line>
              <line x1="5" y1="12" x2="19" y2="12"></line>
            </svg>
          </div>
        `;
      }
    }

    container.innerHTML = `
      <div class="aoe2-command-panel-wrapper">
        <div class="aoe2-command-panel-header">
          <div class="aoe2-panel-title-group">
            <span class="aoe2-panel-title">Command Panel</span>
            <span class="aoe2-panel-hint">
              ${buildingType === 'recruit'
                ? 'AoE2 5×3 command layout. Top-right slot 5 is reserved for Set Gather Point on Page 1.'
                : (buildingType === 'tech'
                  ? 'AoE2 5×3 command layout for research and upgrade buttons.'
                  : 'AoE2 5×3 command layout for custom buttons and Lua triggers.')}
            </span>
          </div>

          <!-- Multi-Page Tabs -->
          <div class="aoe2-page-tabs">
            ${pages.map(p => `
              <button type="button" class="aoe2-page-tab ${p === activePage ? 'active' : ''}" data-page="${p}">
                Page ${p}${pageCmdCounts[p] ? ` <span class="aoe2-page-tab-count">(${pageCmdCounts[p]})</span>` : ''}
              </button>
            `).join('')}
            <button type="button" class="aoe2-page-add-btn" id="ub-add-page" title="Add Page">+ Page</button>
            ${activePage > 1 ? `
              <button type="button" class="aoe2-page-del-btn" id="ub-del-page" title="Delete Page ${activePage}">Delete Page</button>
            ` : ''}
          </div>
        </div>

        <!-- 5x3 Grid Frame -->
        <div class="aoe2-grid-container">
          <div class="aoe2-command-grid">
            ${slotsHtml}
          </div>
        </div>

        <!-- Clean Footer -->
        <div class="aoe2-grid-footer">
          <span class="aoe2-grid-footer-note">Click any slot to add or configure commands.</span>
          <button type="button" class="btn btn-wood btn-xs" id="ub-quick-add-btn">+ Add Button</button>
        </div>
      </div>
    `;

    // Page Switching
    container.querySelectorAll('.aoe2-page-tab').forEach(btn => {
      btn.addEventListener('click', () => {
        ubActivePage = Number(btn.dataset.page);
        renderCommandPanel(building, element);
      });
    });

    container.querySelector('#ub-add-page')?.addEventListener('click', () => {
      const nextP = Math.max(...pages) + 1;
      ubActivePage = nextP;
      renderCommandPanel(building, element);
    });

    // Delete Active Page
    container.querySelector('#ub-del-page')?.addEventListener('click', () => {
      const cmdsOnPage = building.commands.filter(c => (Number(c.page) || 1) === activePage);
      if (cmdsOnPage.length > 0) {
        if (!confirm(`Page ${activePage} contains ${cmdsOnPage.length} button(s). Delete this page and shift higher pages down?`)) {
          return;
        }
      }
      building.commands = building.commands.filter(c => (Number(c.page) || 1) !== activePage);
      building.commands.forEach(c => {
        const p = Number(c.page) || 1;
        if (p > activePage) {
          c.page = p - 1;
        }
      });
      ubActivePage = Math.max(1, activePage - 1);
      S.setDirty(true);
      renderCommandPanel(building, element);
    });

    // Quick Add Button
    container.querySelector('#ub-quick-add-btn')?.addEventListener('click', () => {
      showAddCommandDialog(building, element, getNextFreeSlot(building, activePage), activePage);
    });

    // Empty Slot Click -> Add Command to that slot
    container.querySelectorAll('.aoe2-cmd-slot.slot-empty').forEach(el => {
      el.addEventListener('click', () => {
        const slot = Number(el.dataset.slot);
        showAddCommandDialog(building, element, slot, activePage);
      });
    });

    // Occupied Slot Click -> Edit Command
    container.querySelectorAll('.aoe2-cmd-slot.slot-occupied').forEach(el => {
      el.addEventListener('click', (e) => {
        if (e.target.closest('.slot-mini-btn')) return;
        const cmdIdx = Number(el.dataset.cmdIdx);
        editCommand(building, cmdIdx, element);
      });
    });

    // Mini Edit / Delete buttons
    container.querySelectorAll('.slot-mini-btn[data-action="edit"]').forEach(btn => {
      btn.addEventListener('click', (e) => {
        e.stopPropagation();
        const cmdIdx = Number(btn.dataset.cmdIdx);
        editCommand(building, cmdIdx, element);
      });
    });

    container.querySelectorAll('.slot-mini-btn[data-action="delete"]').forEach(btn => {
      btn.addEventListener('click', (e) => {
        e.stopPropagation();
        const cmdIdx = Number(btn.dataset.cmdIdx);
        building.commands.splice(cmdIdx, 1);
        S.setDirty(true);
        renderCommandPanel(building, element);
      });
    });

    // Gather Point Click -> Explain & Option to override
    container.querySelector('.aoe2-cmd-slot.slot-gather-point')?.addEventListener('click', () => {
      showGatherPointModal(building, element, activePage);
    });
  }

  function showGatherPointModal(building, element, activePage) {
    const modal = document.getElementById('modal-container');
    if (!modal) return;

    modal.style.display = 'flex';
    modal.innerHTML = `
      <div class="modal-dialog" style="max-width: 480px;">
        <div class="modal-header">
          <h3 class="modal-title">Slot 5: Set Gather Point</h3>
          <button class="btn-modal-close">&times;</button>
        </div>
        <div class="modal-body">
          <div style="display: flex; gap: 14px; align-items: flex-start; margin-bottom: 14px;">
            <div style="width: 48px; height: 48px; background: #090705; border: 1px solid #4a3724; border-radius: 4px; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">
              <svg width="36" height="36" viewBox="0 0 36 36" fill="none">
                <line x1="7" y1="6" x2="27" y2="6" stroke="#c0a060" stroke-width="2.5" stroke-linecap="round"/>
                <line x1="17" y1="2" x2="17" y2="6" stroke="#e0caa0" stroke-width="2"/>
                <rect x="9" y="7" width="13" height="17" fill="#e8e8ea" stroke="#777780" stroke-width="1"/>
                <line x1="12" y1="8" x2="12" y2="23" stroke="#2b5bbb" stroke-width="1.5"/>
                <line x1="15" y1="8" x2="15" y2="23" stroke="#2b5bbb" stroke-width="1.5"/>
                <line x1="18" y1="8" x2="18" y2="23" stroke="#2b5bbb" stroke-width="1.5"/>
                <polygon points="26,10 30,10 30,17 33,17 28,25 23,17 26,17" fill="#e62222" stroke="#600" stroke-width="0.8"/>
              </svg>
            </div>
            <div style="font-size: 12.5px; color: var(--text-dark); line-height: 1.45;">
              <strong style="color: var(--wood-900);">Standard Military Slot</strong>
              <p style="margin-top: 4px; margin-bottom: 6px;">In Age of Empires II, recruit buildings automatically reserve <strong>Slot 5 (Top-Right)</strong> on Page 1 for the "Set Gather Point" command flag.</p>
              <p style="margin: 0; color: var(--text-dark-muted); font-size: 11.5px;">To place buttons without affecting gather points, you can switch to <strong>Page 2</strong> or override Slot 5 below.</p>
            </div>
          </div>
        </div>
        <div class="modal-footer" style="display: flex; justify-content: space-between; gap: 8px;">
          <button type="button" class="btn btn-wood" id="gp-cancel">Understood</button>
          <div style="display: flex; gap: 6px;">
            <button type="button" class="btn btn-wood" id="gp-switch-page">Go to Page 2</button>
            <button type="button" class="btn btn-wood" id="gp-override">Override Slot 5 Anyway</button>
          </div>
        </div>
      </div>
    `;

    const closeModal = () => {
      modal.style.display = 'none';
      modal.innerHTML = '';
    };

    modal.querySelector('.btn-modal-close')?.addEventListener('click', closeModal);
    modal.querySelector('#gp-cancel')?.addEventListener('click', closeModal);

    modal.querySelector('#gp-switch-page')?.addEventListener('click', () => {
      closeModal();
      ubActivePage = 2;
      renderCommandPanel(building, element);
    });

    modal.querySelector('#gp-override')?.addEventListener('click', () => {
      closeModal();
      showAddCommandDialog(building, element, 5, activePage);
    });
  }

  function showAddCommandDialog(building, element, defaultSlot, defaultPage) {
    const modal = document.getElementById('modal-container');
    if (!modal) return;

    defaultPage = defaultPage || ubActivePage || 1;
    defaultSlot = defaultSlot || getNextFreeSlot(building, defaultPage);
    const buildingType = getBuildingType(building);

    modal.style.display = 'flex';
    modal.innerHTML = `
      <div class="modal-dialog" style="max-width: 440px;">
        <div class="modal-header">
          <h3 class="modal-title">Add Button (Slot ${defaultSlot}, Page ${defaultPage})</h3>
          <button class="btn-modal-close">&times;</button>
        </div>
        <div class="modal-body">
          <p style="margin-bottom: 12px; font-size: 13px; color: var(--text-dark);">Choose button command type:</p>
          <div style="display: flex; flex-direction: column; gap: 8px;">
            <button class="btn btn-wood btn-block" data-type="train" style="text-align: left; display: flex; align-items: center; justify-content: space-between;">
              <span style="display:inline-flex;align-items:center;gap:8px;"><svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="14.5 17.5 3 6 3 3 6 3 17.5 14.5"/><line x1="13" y1="19" x2="19" y2="13"/><line x1="16" y1="16" x2="20" y2="20"/></svg>Train Unit</span>
              ${buildingType === 'recruit' ? '<span style="font-size: 10px; opacity: 0.8; font-weight: 600;">(Recommended)</span>' : ''}
            </button>
            <button class="btn btn-wood btn-block" data-type="research" style="text-align: left; display: flex; align-items: center; justify-content: space-between;">
              <span style="display:inline-flex;align-items:center;gap:8px;"><svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/><path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/></svg>Research Technology</span>
              ${buildingType === 'tech' ? '<span style="font-size: 10px; opacity: 0.8; font-weight: 600;">(Recommended)</span>' : ''}
            </button>
            <button class="btn btn-wood btn-block" data-type="lua" style="text-align: left; display: flex; align-items: center; justify-content: space-between;">
              <span style="display:inline-flex;align-items:center;gap:8px;"><svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/></svg>Script Button (Lua)</span>
              ${buildingType === 'custom' ? '<span style="font-size: 10px; opacity: 0.8; font-weight: 600;">(Recommended)</span>' : ''}
            </button>
          </div>
        </div>
      </div>
    `;

    const closeModal = () => {
      modal.style.display = 'none';
      modal.innerHTML = '';
    };

    modal.querySelector('.btn-modal-close')?.addEventListener('click', closeModal);

    modal.querySelectorAll('button[data-type]').forEach(btn => {
      btn.addEventListener('click', () => {
        const type = btn.dataset.type;
        closeModal();

        const newCmd = {
          button: defaultSlot,
          page: defaultPage
        };

        if (type === 'train') {
          newCmd.train = 125;
        } else if (type === 'research') {
          newCmd.research = { name: '', help: '', cost: {}, time: 30, effects: [] };
        } else if (type === 'lua') {
          newCmd.lua = 'Handler';
          newCmd.text = 'Button';
          newCmd.help = '';
          newCmd.icon = 49;
        }

        building.commands.push(newCmd);
        S.setDirty(true);
        renderCommandPanel(building, element);
        editCommand(building, building.commands.length - 1, element);
      });
    });
  }

  function getNextFreeSlot(building, page) {
    page = page || ubActivePage || 1;
    const buildingType = getBuildingType(building);
    const used = new Set(
      (building.commands || [])
        .filter(c => (Number(c.page) || 1) === page)
        .map(c => Number(c.button))
    );

    // In recruit buildings on page 1, slot 5 is reserved for gather point
    const isRecruitPage1 = buildingType === 'recruit' && page === 1;
    for (let i = 1; i <= 15; i++) {
      if (isRecruitPage1 && i === 5) continue;
      if (!used.has(i)) return i;
    }
    return isRecruitPage1 ? 1 : 15;
  }

  function editCommand(building, idx, element) {
    const cmd = building.commands[idx];
    const modal = document.getElementById('modal-container');
    if (!modal) return;

    const pageField = `
      <div class="basic-form-row">
        <div class="basic-form-field">
          <label>Page</label>
          <input type="number" id="cmd-page" min="1" max="10" value="${cmd.page || 1}" class="form-input">
        </div>
        <div class="basic-form-field">
          <label>Button Slot (1-14)</label>
          <input type="number" id="cmd-button" min="1" max="14" value="${Math.min(14, cmd.button || 1)}" class="form-input">
          <span style="font-size:11px;color:#8c7359;">Slot 15 is reserved for Next Page</span>
        </div>
      </div>
    `;

    let content = '';
    let advContent = '';
    if (cmd.train !== undefined) {
      const isComplex = typeof cmd.train === 'object';
      const unitId = isComplex ? cmd.train.copy_from : cmd.train;
      const unit = findUnit(unitId);

      content = `
        <div class="basic-form-field">
          <label>Unit to Train</label>
          <button class="btn btn-wood btn-sm btn-block" id="cmd-pick-unit">${unit ? unitIconHtml(unit, 20) + escapeHtml(unit.name) + ' (' + unit.id + ')' : 'Pick unit'}</button>
        </div>
        ${isComplex ? `
          <div class="basic-form-field">
            <label>Custom Name</label>
            <input type="text" id="cmd-train-name" value="${escapeHtml(cmd.train.name || '')}" class="form-input">
          </div>
        ` : `
          <div class="basic-form-field">
            <button class="btn btn-ghost btn-sm" id="cmd-make-complex" style="align-self:flex-start;">Customize name, tooltip or look...</button>
          </div>
        `}
      `;
      if (isComplex) {
        advContent = `
          <div class="basic-form-field">
            <label>Custom Help</label>
            <textarea id="cmd-train-help" class="form-textarea" rows="2">${escapeHtml(cmd.train.help || '')}</textarea>
          </div>
          <div class="basic-form-field">
            <label>Look (Graphics)</label>
            <div id="cmd-train-look"></div>
          </div>
        `;
      }
    } else if (cmd.research) {
      content = `
        <div class="basic-form-field">
          <label>Name</label>
          <input type="text" id="cmd-res-name" value="${escapeHtml(cmd.research.name || '')}" class="form-input">
        </div>
        <div class="basic-cost-grid">
          <div class="basic-form-field">
            <label>Food</label>
            <input type="number" id="cmd-res-food" value="${cmd.research.cost?.food ?? ''}" placeholder="0" class="form-input">
          </div>
          <div class="basic-form-field">
            <label>Wood</label>
            <input type="number" id="cmd-res-wood" value="${cmd.research.cost?.wood ?? ''}" placeholder="0" class="form-input">
          </div>
          <div class="basic-form-field">
            <label>Stone</label>
            <input type="number" id="cmd-res-stone" value="${cmd.research.cost?.stone ?? ''}" placeholder="0" class="form-input">
          </div>
          <div class="basic-form-field">
            <label>Gold</label>
            <input type="number" id="cmd-res-gold" value="${cmd.research.cost?.gold ?? ''}" placeholder="0" class="form-input">
          </div>
        </div>
        <div class="basic-form-field">
          <label>Effect</label>
          <select id="cmd-res-effect" class="form-select">
            <option value="">None</option>
            <option value="hp_monk">Monk +HP</option>
            <option value="hp_inf">Infantry +HP</option>
            <option value="hp_arch">Archer +HP</option>
            <option value="hp_cav">Cavalry +HP</option>
            <option value="speed_monk">Monk +Speed %</option>
            <option value="speed_inf">Infantry +Speed %</option>
            <option value="speed_arch">Archer +Speed %</option>
            <option value="speed_cav">Cavalry +Speed %</option>
          </select>
        </div>
        <div class="basic-form-field" id="cmd-res-effect-val-container" style="display: none;">
          <label id="cmd-res-effect-label">Value</label>
          <input type="number" id="cmd-res-effect-val" value="20" class="form-input">
        </div>
      `;
      advContent = `
        <div class="basic-form-field">
          <label>Research Time (seconds)</label>
          <input type="number" id="cmd-res-time" value="${cmd.research.time || 30}" class="form-input">
        </div>
        <div class="basic-form-field">
          <label>Help Text</label>
          <textarea id="cmd-res-help" class="form-textarea" rows="2">${escapeHtml(cmd.research.help || '')}</textarea>
        </div>
      `;
    } else if (cmd.lua) {
      content = `
        <div class="basic-form-field">
          <label>Button Text</label>
          <input type="text" id="cmd-lua-text" value="${escapeHtml(cmd.text || '')}" class="form-input">
        </div>
        <div class="basic-form-field">
          <label>Handler Name (Lua)</label>
          <input type="text" id="cmd-lua-handler" value="${escapeHtml(cmd.lua || '')}" class="form-input font-mono">
        </div>
        <div class="basic-form-field">
          <label>Button Icon</label>
          <div id="cmd-lua-icon-field"></div>
        </div>
      `;
      advContent = `
        <div class="basic-form-field">
          <label>Help Text</label>
          <textarea id="cmd-lua-help" class="form-textarea" rows="2">${escapeHtml(cmd.help || '')}</textarea>
        </div>
      `;
    }

    const conditionField = `
      <div class="basic-form-field">
        <label style="font-weight: 600;">Availability Condition</label>
        <div class="basic-form-row">
          <div class="basic-form-field" style="flex: 1;">
            <label>Condition Mode</label>
            <select id="cmd-cond-mode" class="form-select">
              <option value="none" ${!cmd.condition ? 'selected' : ''}>Always Available</option>
              <option value="condition" ${cmd.condition ? 'selected' : ''}>Conditional (Graph / Lua)</option>
            </select>
          </div>
          <div class="basic-form-field" id="cmd-cond-action-field" style="flex: 1; ${!cmd.condition ? 'display: none;' : ''}">
            <label>When Condition Fails</label>
            <select id="cmd-cond-action" class="form-select">
              <option value="disable" ${(cmd.condition?.action || 'disable') === 'disable' ? 'selected' : ''}>Gray out / Disable</option>
              <option value="hide" ${cmd.condition?.action === 'hide' ? 'selected' : ''}>Hide Button</option>
            </select>
          </div>
        </div>
        <div class="basic-form-field" id="cmd-cond-name-field" style="${!cmd.condition ? 'display: none;' : ''}; margin-top: 6px;">
          <label>Condition Name (Lua / Graph)</label>
          <input type="text" id="cmd-cond-name" class="form-input font-mono" placeholder="e.g. CanBless" value="${escapeHtml(cmd.condition?.name || '')}">
          <span class="basic-field-hint">Match a <strong>ButtonCondition</strong> node or <code>Game.CommandCondition(...)</code>.</span>
        </div>
      </div>
    `;

    modal.style.display = 'flex';
    modal.innerHTML = `
      <div class="modal-dialog" style="max-width: 560px; max-height: 82vh; overflow-y: auto;">
        <div class="modal-header">
          <h3 class="modal-title">Edit Button (Page ${cmd.page || 1}, Slot ${Math.min(14, cmd.button || 1)})</h3>
          <button class="btn-modal-close">&times;</button>
        </div>
        <div class="modal-body">
          <div style="display: flex; flex-direction: column; gap: 12px;">
            ${content}
            <details class="sc-advanced" ${cmd.condition ? 'open' : ''}>
              <summary>${CHEV_SVG}<span>Advanced (Slot, conditions${advContent ? ', details' : ''})</span></summary>
              <div class="sc-advanced-body">
                ${pageField}
                ${advContent}
                ${conditionField}
              </div>
            </details>
          </div>
        </div>
        <div class="modal-footer">
          <button class="btn btn-save btn-sm" id="cmd-save">Done</button>
        </div>
      </div>
    `;

    const closeModal = () => {
      modal.style.display = 'none';
      modal.innerHTML = '';
      renderCommandPanel(building, element);
    };

    modal.querySelector('.btn-modal-close')?.addEventListener('click', closeModal);
    modal.querySelector('#cmd-save')?.addEventListener('click', closeModal);

    document.getElementById('cmd-button')?.addEventListener('input', (e) => {
      cmd.button = Math.max(1, Math.min(14, Number(e.target.value) || 1));
      S.setDirty(true);
    });

    document.getElementById('cmd-page')?.addEventListener('input', (e) => {
      cmd.page = Number(e.target.value) || 1;
      S.setDirty(true);
    });

    if (cmd.train !== undefined) {
      document.getElementById('cmd-pick-unit')?.addEventListener('click', () => {
        showUnitPicker(70, (unit) => {
          if (typeof cmd.train === 'object') {
            cmd.train.copy_from = unit.id;
          } else {
            cmd.train = unit.id;
          }
          S.setDirty(true);
          closeModal();
          editCommand(building, idx, element);
        });
      });

      document.getElementById('cmd-make-complex')?.addEventListener('click', () => {
        const unitId = cmd.train;
        cmd.train = { copy_from: unitId, name: '', help: '', units: {} };
        S.setDirty(true);
        closeModal();
        editCommand(building, idx, element);
      });

      if (typeof cmd.train === 'object') {
        document.getElementById('cmd-train-name')?.addEventListener('input', (e) => {
          cmd.train.name = e.target.value;
          S.setDirty(true);
        });
        document.getElementById('cmd-train-help')?.addEventListener('input', (e) => {
          cmd.train.help = e.target.value;
          S.setDirty(true);
        });

        setTimeout(() => {
          renderLookForm('cmd-train-look', cmd.train, 'graphics', element);
        }, 10);
      }
    } else if (cmd.research) {
      ['name', 'help', 'time'].forEach(k => {
        const el = document.getElementById(`cmd-res-${k}`);
        if (el) {
          el.addEventListener('input', () => {
            cmd.research[k] = k === 'time' ? Number(el.value) : el.value;
            S.setDirty(true);
          });
        }
      });

      ['food', 'wood', 'stone', 'gold'].forEach(res => {
        const el = document.getElementById(`cmd-res-${res}`);
        if (el) {
          el.addEventListener('input', () => {
            if (!cmd.research.cost) cmd.research.cost = {};
            if (el.value) {
              cmd.research.cost[res] = Number(el.value);
            } else {
              delete cmd.research.cost[res];
            }
            S.setDirty(true);
          });
        }
      });

      const effectSel = document.getElementById('cmd-res-effect');
      const effectValContainer = document.getElementById('cmd-res-effect-val-container');
      const effectLabel = document.getElementById('cmd-res-effect-label');
      const effectVal = document.getElementById('cmd-res-effect-val');

      if (effectSel && effectValContainer && effectLabel && effectVal) {
        effectSel.addEventListener('change', () => {
          if (effectSel.value) {
            effectValContainer.style.display = 'block';
            effectLabel.textContent = effectSel.value.includes('hp') ? 'Hit Points' : 'Percent';
          } else {
            effectValContainer.style.display = 'none';
          }
        });

        const applyEffect = () => {
          const effectType = effectSel.value;
          const val = Number(effectVal.value);

          if (!effectType) {
            cmd.research.effects = [];
          } else {
            const classMap = { monk: 18, inf: 6, arch: 0, cav: 12 };
            const [attr, unit] = effectType.split('_');
            const classId = classMap[unit];

            if (attr === 'hp') {
              cmd.research.effects = [{ type: 4, a: -1, b: classId, c: 0, d: val }];
            } else if (attr === 'speed') {
              cmd.research.effects = [{ type: 5, a: -1, b: classId, c: 5, d: Number((1.0 + val / 100).toFixed(4)) }];
            }
          }
          S.setDirty(true);
        };

        effectVal.addEventListener('input', applyEffect);
        effectSel.addEventListener('change', applyEffect);
      }
    } else if (cmd.lua) {
      ['text', 'help', 'lua'].forEach(k => {
        const el = document.getElementById(`cmd-lua-${k === 'lua' ? 'handler' : k}`);
        if (el) {
          el.addEventListener('input', () => {
            if (k === 'lua') {
              cmd.lua = el.value;
            } else {
              cmd[k] = el.value;
            }
            S.setDirty(true);
          });
        }
      });

      const iconContainer = document.getElementById('cmd-lua-icon-field');
      if (iconContainer) {
        renderSpriteField(iconContainer, {
          value: cmd.icon || 49,
          kind: 'icon',
          title: `Button Icon (#${cmd.icon || 49})`,
          hint: 'Command bar icon graphic in AoE2',
          onChange: (newSlp) => {
            cmd.icon = newSlp;
            S.setDirty(true);
            editCommand(building, idx, element);
          }
        });
      }
    }

    // Condition Event Handlers
    const condModeSel = document.getElementById('cmd-cond-mode');
    const condActionField = document.getElementById('cmd-cond-action-field');
    const condNameField = document.getElementById('cmd-cond-name-field');
    const condActionSel = document.getElementById('cmd-cond-action');
    const condNameInput = document.getElementById('cmd-cond-name');

    if (condModeSel) {
      condModeSel.addEventListener('change', () => {
        const isCond = condModeSel.value === 'condition';
        if (condActionField) condActionField.style.display = isCond ? 'block' : 'none';
        if (condNameField) condNameField.style.display = isCond ? 'block' : 'none';
        if (isCond) {
          if (!cmd.condition) {
            const defName = cmd.text ? cmd.text.replace(/\s+/g, '') + 'Cond' : (cmd.train ? 'TrainCond' : 'ResCond');
            cmd.condition = { name: defName, action: condActionSel?.value || 'disable' };
            if (condNameInput) condNameInput.value = defName;
          }
        } else {
          delete cmd.condition;
        }
        S.setDirty(true);
      });
    }

    condActionSel?.addEventListener('change', () => {
      if (!cmd.condition) cmd.condition = { name: 'Cond', action: 'disable' };
      cmd.condition.action = condActionSel.value;
      S.setDirty(true);
    });

    condNameInput?.addEventListener('input', () => {
      if (!cmd.condition) cmd.condition = { name: 'Cond', action: 'disable' };
      cmd.condition.name = condNameInput.value.trim();
      S.setDirty(true);
    });
  }

  // ==========================================================================
  // Unit Changes Editor
  // ==========================================================================

  // Without unitId: the list of changed units of the civ; with unitId: one unit's editor.
  function renderUnitChangesList(element) {
    const civ = S.state.currentModData.civs[element.file];
    const ids = Object.keys(civ.units || {}).sort((a, b) => Number(a) - Number(b));
    const root = document.getElementById('basic-root');
    const rows = ids.map(id => {
      const u = findUnit(Number(id));
      const fields = Object.keys(civ.units[id]).map(k => k === 'graphics' ? 'look' : k.replace(/_/g, ' '));
      return `<div class="basic-list-row" data-id="${escapeHtml(id)}">
          ${unitIconHtml(u, 32)}
          <div class="basic-list-main"><strong>${escapeHtml(u ? u.name : 'Unit ' + id)}</strong> <span class="basic-muted">#${escapeHtml(id)}</span>
            <div class="basic-muted">${escapeHtml(fields.join(', ') || 'no changes yet')}</div></div>
          <button class="btn btn-wood btn-sm" data-act="edit">Edit</button>
          <button class="btn btn-wood btn-sm" data-act="remove">Remove</button>
        </div>`;
    }).join('');
    root.innerHTML = `
      <div class="basic-editor">
        <div class="basic-editor-header">
          <button class="btn btn-wood btn-sm" id="basic-back">← Back</button>
          <h2 class="basic-editor-title">${escapeHtml(civ.name || element.file)} - Unit changes</h2>
          <div class="basic-editor-actions"><button class="btn btn-save btn-sm" id="uc-add">+ Change a unit</button></div>
        </div>
        <div class="basic-editor-content"><div class="basic-form">
          <p class="basic-muted">Stats and looks this civilization changes on existing units.</p>
          <div class="basic-list">${rows || '<p class="basic-muted">No unit is changed yet.</p>'}</div>
        </div></div>
      </div>`;
    root.querySelector('#basic-back').onclick = closeElementEditor;
    root.querySelector('#uc-add').onclick = () => showUnitPicker(null, unit => {
      if (!civ.units) civ.units = {};
      if (!civ.units[unit.id]) civ.units[unit.id] = {};
      S.setDirty(true);
      openElementEditor({ ...element, unitId: String(unit.id) });
    });
    root.querySelectorAll('.basic-list-row').forEach(row => {
      const id = row.dataset.id;
      row.querySelector('[data-act="edit"]').onclick = () => openElementEditor({ ...element, unitId: id });
      row.querySelector('[data-act="remove"]').onclick = () => S.showConfirm('Remove changes', 'Remove all changes to this unit?', 'Remove', () => {
        delete civ.units[id];
        if (!Object.keys(civ.units).length) delete civ.units;
        S.setDirty(true);
        if (civ.units) renderUnitChangesList(element); else closeElementEditor();
      });
    });
  }

  function renderUnitChangesEditor(element) {
    if (element.unitId === undefined) return renderUnitChangesList(element);
    const data = S.state.currentModData;
    const civ = data.civs[element.file];
    const unitId = element.unitId;
    const changes = civ.units[unitId];
    if (!changes) return closeElementEditor();

    const unit = findUnit(Number(unitId));

    const root = document.getElementById('basic-root');
    const statCount = ['hit_points', 'speed', 'line_of_sight', 'max_range', 'reload_time', 'search_radius']
      .filter(k => changes[k] !== undefined).length;
    root.innerHTML = `
      <div class="basic-editor">
        <div class="basic-editor-header">
          <button class="btn btn-wood btn-sm" id="basic-back">← Back</button>
          <h2 class="basic-editor-title">${escapeHtml(civ.name || element.file)} - ${unit ? unit.name : 'Unit ' + unitId} Changes</h2>
          <div class="basic-editor-actions">
            <button class="btn btn-ghost danger btn-sm" id="basic-delete-element">Delete</button>
          </div>
        </div>
        <div class="basic-editor-content">
          <div class="basic-form">
            <details class="sc-section" data-pref-key="uc-stats" open>
              <summary class="sc-section-summary">
                ${CHEV_SVG}
                <span class="sc-section-title">Stats</span>
                <span class="sc-section-meta">${statCount ? `${statCount} modified` : 'Using base stats'}</span>
              </summary>
              <div class="sc-section-body">
                <div class="basic-form-row">
                  <div class="basic-form-field">
                    <label>Hit Points</label>
                    <input type="number" id="uc-hp" value="${changes.hit_points ?? ''}" class="form-input">
                  </div>
                  <div class="basic-form-field">
                    <label>Speed</label>
                    <input type="number" step="0.01" id="uc-speed" value="${changes.speed ?? ''}" class="form-input">
                  </div>
                </div>
                <details class="sc-advanced" data-pref-key="uc-stats-adv">
                  <summary>${CHEV_SVG}<span>Advanced stats (Sight, Range, Reload, Search)</span></summary>
                  <div class="sc-advanced-body">
                    <div class="basic-form-row">
                      <div class="basic-form-field">
                        <label>Line of Sight</label>
                        <input type="number" id="uc-los" value="${changes.line_of_sight ?? ''}" class="form-input">
                      </div>
                      <div class="basic-form-field">
                        <label>Max Range</label>
                        <input type="number" step="0.1" id="uc-range" value="${changes.max_range ?? ''}" class="form-input">
                      </div>
                    </div>
                    <div class="basic-form-row">
                      <div class="basic-form-field">
                        <label>Reload Time</label>
                        <input type="number" step="0.1" id="uc-reload" value="${changes.reload_time ?? ''}" class="form-input">
                      </div>
                      <div class="basic-form-field">
                        <label>Search Radius</label>
                        <input type="number" step="0.1" id="uc-search" value="${changes.search_radius ?? ''}" class="form-input">
                      </div>
                    </div>
                  </div>
                </details>
              </div>
            </details>

            <details class="sc-section" data-pref-key="uc-look">
              <summary class="sc-section-summary">
                ${CHEV_SVG}
                <span class="sc-section-title">Look &amp; Animations</span>
                <span class="sc-section-meta">${changes.graphics && Object.keys(changes.graphics).length ? `${Object.keys(changes.graphics).length} custom` : 'Default'}</span>
              </summary>
              <div class="sc-section-body">
                <div id="uc-look-container"></div>
              </div>
            </details>
          </div>
        </div>
      </div>
    `;

    document.getElementById('basic-back')?.addEventListener('click', closeElementEditor);
    document.getElementById('basic-delete-element')?.addEventListener('click', () => deleteElement(element));

    setupUnitChangesEvents(changes);
    renderLookForm('uc-look-container', changes, 'graphics', element);
    wireDisclosures(root);
  }

  function setupUnitChangesEvents(changes) {
    const fields = [
      { id: 'uc-hp', key: 'hit_points' },
      { id: 'uc-speed', key: 'speed' },
      { id: 'uc-los', key: 'line_of_sight' },
      { id: 'uc-range', key: 'max_range' },
      { id: 'uc-reload', key: 'reload_time' },
      { id: 'uc-search', key: 'search_radius' }
    ];

    fields.forEach(({ id, key }) => {
      const el = document.getElementById(id);
      if (el) {
        el.addEventListener('input', () => {
          if (el.value) {
            changes[key] = Number(el.value);
          } else {
            delete changes[key];
          }
          S.setDirty(true);
        });
      }
    });
  }

  // ==========================================================================
  // Bonuses Editor
  // ==========================================================================

  function renderBonusesEditor(element) {
    const data = S.state.currentModData;
    const civ = data.civs[element.file];
    if (!civ) return closeElementEditor();

    if (!civ.effects) civ.effects = [];
    if (!civ.team_bonus_effects) civ.team_bonus_effects = [];

    const root = document.getElementById('basic-root');
    root.innerHTML = `
      <div class="basic-editor">
        <div class="basic-editor-header">
          <button class="btn btn-wood btn-sm" id="basic-back">← Back</button>
          <h2 class="basic-editor-title">${escapeHtml(civ.name || element.file)} - Bonuses</h2>
          <div class="basic-editor-actions">
            <button class="btn btn-ghost danger btn-sm" id="basic-delete-element">Delete</button>
          </div>
        </div>
        <div class="basic-editor-content">
          <div class="basic-form">
            <details class="sc-section" data-pref-key="bonuses-civ" open>
              <summary class="sc-section-summary">
                ${CHEV_SVG}
                <span class="sc-section-title">Civilization Bonuses</span>
                <span class="sc-section-meta">${civ.effects.length} bonus${civ.effects.length === 1 ? '' : 'es'}</span>
              </summary>
              <div class="sc-section-body">
                <div id="bonuses-civ-list"></div>
                <div><button class="btn btn-save btn-sm" id="bonuses-add-civ">+ Add Bonus</button></div>
              </div>
            </details>

            <details class="sc-section" data-pref-key="bonuses-team">
              <summary class="sc-section-summary">
                ${CHEV_SVG}
                <span class="sc-section-title">Team Bonuses</span>
                <span class="sc-section-meta">${civ.team_bonus_effects.length} bonus${civ.team_bonus_effects.length === 1 ? '' : 'es'}</span>
              </summary>
              <div class="sc-section-body">
                <div id="bonuses-team-list"></div>
                <div><button class="btn btn-wood btn-sm" id="bonuses-add-team">+ Add Team Bonus</button></div>
              </div>
            </details>
          </div>
        </div>
      </div>
    `;

    document.getElementById('basic-back')?.addEventListener('click', closeElementEditor);
    document.getElementById('basic-delete-element')?.addEventListener('click', () => deleteElement(element));

    document.getElementById('bonuses-add-civ')?.addEventListener('click', () => {
      showBonusDialog(civ.effects, false, () => renderBonusesList(civ));
    });

    document.getElementById('bonuses-add-team')?.addEventListener('click', () => {
      showBonusDialog(civ.team_bonus_effects, true, () => renderBonusesList(civ));
    });

    renderBonusesList(civ);
    wireDisclosures(root);
  }

  function renderBonusesList(civ) {
    const civContainer = document.getElementById('bonuses-civ-list');
    const teamContainer = document.getElementById('bonuses-team-list');

    if (civContainer) {
      civContainer.innerHTML = civ.effects.length === 0
        ? '<p style="color: var(--text-dark-muted); font-size: 12px;">No civ bonuses yet.</p>'
        : civ.effects.map((effect, idx) => renderBonusRow(effect, idx, false, civ)).join('');

      civContainer.querySelectorAll('[data-action="delete"]').forEach(btn => {
        btn.addEventListener('click', () => {
          const idx = Number(btn.dataset.idx);
          civ.effects.splice(idx, 1);
          S.setDirty(true);
          renderBonusesList(civ);
        });
      });
    }

    if (teamContainer) {
      teamContainer.innerHTML = civ.team_bonus_effects.length === 0
        ? '<p style="color: var(--text-dark-muted); font-size: 12px;">No team bonuses yet.</p>'
        : civ.team_bonus_effects.map((effect, idx) => renderBonusRow(effect, idx, true, civ)).join('');

      teamContainer.querySelectorAll('[data-action="delete"]').forEach(btn => {
        btn.addEventListener('click', () => {
          const idx = Number(btn.dataset.idx);
          civ.team_bonus_effects.splice(idx, 1);
          S.setDirty(true);
          renderBonusesList(civ);
        });
      });
    }
  }

  function renderBonusRow(effect, idx, isTeam) {
    let summary = 'Unknown effect';

    // Starting resources
    if (effect.type === 1 && effect.b === 1) {
      const resNames = { 0: 'Food', 1: 'Wood', 2: 'Stone', 3: 'Gold' };
      summary = `Start with +${effect.d} ${resNames[effect.a] || 'Resource'}`;
    }
    // Unit class HP
    else if (effect.type === 5 && effect.c === 0) {
      const classNames = { 4: 'Villager', 6: 'Infantry', 0: 'Archer', 12: 'Cavalry', 18: 'Monk' };
      const pct = Math.round(((effect.d || 1) - 1.0) * 100);
      summary = `${classNames[effect.b] || 'Unit'} +${pct}% HP`;
    }
    // Work rate
    else if (effect.type === 5 && effect.c === 13) {
      const classNames = { 4: 'Villager', 18: 'Monk' };
      const pct = Math.round(((effect.d || 1) - 1.0) * 100);
      if (effect.a === 104 || effect.a === 30) {
        summary = `Monasteries work ${pct}% faster`;
      } else {
        summary = `${classNames[effect.b] || 'Unit'} work ${pct}% faster`;
      }
    }
    else {
      summary = `Custom effect (type=${effect.type})`;
    }

    return `
      <div class="basic-list-row" style="margin-bottom: 6px;">
        <div class="basic-list-main" style="font-size: 13px;">${escapeHtml(summary)}</div>
        <button class="btn btn-ghost danger btn-sm" data-idx="${idx}" data-action="delete">Delete</button>
      </div>
    `;
  }

  function showBonusDialog(effectsArray, isTeam, onUpdate) {
    const modal = document.getElementById('modal-container');
    if (!modal) return;

    const presets = [
      { id: 'food', label: 'Start with +N Food', def: 100 },
      { id: 'wood', label: 'Start with +N Wood', def: 100 },
      { id: 'stone', label: 'Start with +N Stone', def: 100 },
      { id: 'gold', label: 'Start with +N Gold', def: 100 },
      { id: 'vill_hp', label: 'Villager +N% HP', def: 20 },
      { id: 'inf_hp', label: 'Infantry +N% HP', def: 20 },
      { id: 'arch_hp', label: 'Archer +N% HP', def: 15 },
      { id: 'cav_hp', label: 'Cavalry +N% HP', def: 20 },
      { id: 'monk_hp', label: 'Monk +N% HP', def: 50 },
      { id: 'vill_work', label: 'Villagers work N% faster', def: 15 }
    ];

    if (isTeam) {
      presets.push({ id: 'monastery_work', label: 'Monasteries work N% faster', def: 20 });
    }

    modal.style.display = 'flex';
    modal.innerHTML = `
      <div class="modal-dialog">
        <div class="modal-header">
          <h3 class="modal-title">Add ${isTeam ? 'Team ' : ''}Bonus</h3>
          <button class="btn-modal-close">&times;</button>
        </div>
        <div class="modal-body">
          <div class="basic-form-field">
            <label>Bonus Type</label>
            <select id="bonus-type" class="form-select">
              <option value="">Choose...</option>
              ${presets.map(p => `<option value="${p.id}" data-default="${p.def}">${p.label}</option>`).join('')}
            </select>
          </div>
          <div class="basic-form-field" id="bonus-value-field" style="display: none;">
            <label id="bonus-value-label">Value</label>
            <input type="number" id="bonus-value" value="100" class="form-input">
          </div>
        </div>
        <div class="modal-footer">
          <button class="btn btn-wood" id="bonus-add">Add</button>
        </div>
      </div>
    `;

    const closeModal = () => {
      modal.style.display = 'none';
      modal.innerHTML = '';
    };

    modal.querySelector('.btn-modal-close')?.addEventListener('click', closeModal);

    const typeSel = document.getElementById('bonus-type');
    const valueField = document.getElementById('bonus-value-field');
    const valueInput = document.getElementById('bonus-value');

    typeSel?.addEventListener('change', () => {
      if (typeSel.value) {
        const opt = typeSel.selectedOptions[0];
        const def = Number(opt.dataset.default);
        valueInput.value = def;
        valueField.style.display = 'block';
      } else {
        valueField.style.display = 'none';
      }
    });

    document.getElementById('bonus-add')?.addEventListener('click', () => {
      const type = typeSel.value;
      const val = Number(valueInput.value);

      if (!type) return;

      let effect = null;
      if (type === 'food') effect = { type: 1, a: 0, b: 1, c: -1, d: val };
      else if (type === 'wood') effect = { type: 1, a: 1, b: 1, c: -1, d: val };
      else if (type === 'stone') effect = { type: 1, a: 2, b: 1, c: -1, d: val };
      else if (type === 'gold') effect = { type: 1, a: 3, b: 1, c: -1, d: val };
      else if (type === 'vill_hp') effect = { type: 5, a: -1, b: 4, c: 0, d: Number((1.0 + val / 100).toFixed(4)) };
      else if (type === 'inf_hp') effect = { type: 5, a: -1, b: 6, c: 0, d: Number((1.0 + val / 100).toFixed(4)) };
      else if (type === 'arch_hp') effect = { type: 5, a: -1, b: 0, c: 0, d: Number((1.0 + val / 100).toFixed(4)) };
      else if (type === 'cav_hp') effect = { type: 5, a: -1, b: 12, c: 0, d: Number((1.0 + val / 100).toFixed(4)) };
      else if (type === 'monk_hp') effect = { type: 5, a: -1, b: 18, c: 0, d: Number((1.0 + val / 100).toFixed(4)) };
      else if (type === 'vill_work') effect = { type: 5, a: -1, b: 4, c: 13, d: Number((1.0 + val / 100).toFixed(4)) };
      else if (type === 'monastery_work') {
        effectsArray.push({ type: 5, a: 104, b: -1, c: 13, d: Number((1.0 + val / 100).toFixed(4)) });
        effectsArray.push({ type: 5, a: 30, b: -1, c: 13, d: Number((1.0 + val / 100).toFixed(4)) });
        S.setDirty(true);
        closeModal();
        onUpdate();
        return;
      }

      if (effect) {
        effectsArray.push(effect);
        S.setDirty(true);
      }

      closeModal();
      onUpdate();
    });
  }

  function renderScriptEditor(element) {
    const root = document.getElementById('basic-root');

    if (!window.ScGraph) {
      root.innerHTML = `
        <div class="basic-editor">
          <div class="basic-editor-header">
            <button class="btn btn-wood btn-sm" id="basic-back">← Back</button>
            <h2 class="basic-editor-title">${escapeHtml(element.title)}</h2>
          </div>
          <div class="basic-editor-content">
            <p style="color: var(--text-dark); padding: 20px;">Script editor not available. The graph.js module is not loaded.</p>
          </div>
        </div>
      `;
      document.getElementById('basic-back')?.addEventListener('click', closeElementEditor);
      return;
    }

    root.innerHTML = `
      <div class="basic-editor basic-editor-fullscreen">
        <div class="basic-editor-header">
          <button class="btn btn-wood btn-sm" id="basic-back">← Back</button>
          <h2 class="basic-editor-title">${escapeHtml(element.title)}</h2>
        </div>
        <div class="basic-editor-content" style="flex: 1; display: flex; flex-direction: column;">
          <div id="script-graph-container" style="flex: 1; min-height: 400px;"></div>
        </div>
      </div>
    `;

    document.getElementById('basic-back')?.addEventListener('click', closeElementEditor);

    const slug = element.file.replace(/\.json$/, '');
    let graphData = loadedGraphs.get(slug);

    if (!graphData) {
      loadScriptGraph(element.file).then(graph => {
        if (graph) {
          graphData = graphEntry(slug, graph, false);
          loadedGraphs.set(slug, graphData);
          mountScriptEditor(graphData);
        }
      });
    } else {
      mountScriptEditor(graphData);
    }
  }

  // GET /api/file returns raw bytes (application/octet-stream): S.api gives the Response.
  async function readModFile(path) {
    const data = S.state.currentModData;
    const res = await S.api(`/api/file?folder=${encodeURIComponent(data.folder)}&path=${encodeURIComponent(path)}`);
    return typeof res === 'string' ? res : await res.text();
  }

  async function loadScriptGraph(file) {
    try {
      return JSON.parse(await readModFile(`scripts/graphs/${file}`));
    } catch (e) {
      S.showToast('Could not load script', 'error');
      return null;
    }
  }

  function mountScriptEditor(graphData) {
    const container = document.getElementById('script-graph-container');
    if (!container || !window.ScGraph) return;

    try {
      const editor = window.ScGraph.mount(container, graphData.graph, {
        catalog: S.state.catalog,
        onChange: (newGraph) => {
          graphData.graph = newGraph;
          graphData.dirty = true;
          S.setExtraDirty('graph:' + graphData.slug, true);
        }
      });
      graphData.editor = editor;
    } catch (e) {
      console.error('[Basic] Failed to mount script editor:', e);
      S.showToast('Failed to load script editor', 'error');
    }
  }

  // ==========================================================================
  // Look (Graphics) Sub-Form
  // ==========================================================================

  async function getUsedSlpNumbers() {
    const data = S.state.currentModData;
    if (!data) return [];

    const used = new Set();

    // Check existing files
    try {
      const files = await S.api(`/api/files?folder=${encodeURIComponent(data.folder)}&dir=resources/_common/drs/graphics`);
      files.forEach(f => {
        const match = f.match(/^(\d+)\.slp$/);
        if (match) used.add(Number(match[1]));
      });
    } catch (e) {
      // Directory may not exist yet
    }

    // Check all civ objects for slp/graphics references
    Object.values(data.civs || {}).forEach(civ => {
      const scanGraphics = (g) => {
        if (!g) return;
        ['standing', 'standing2', 'dying', 'decay', 'walking', 'running', 'attack'].forEach(k => {
          if (typeof g[k] === 'number') used.add(g[k]);
          else if (g[k]?.slp) used.add(g[k].slp);
        });
      };

      if (civ.unique_unit?.graphics) scanGraphics(civ.unique_unit.graphics);
      if (civ.unique_unit?.elite_graphics) scanGraphics(civ.unique_unit.elite_graphics);
      if (civ.unique_building?.slp) used.add(civ.unique_building.slp);
      (civ.custom_units || []).forEach(cu => {
        if (cu.graphics) scanGraphics(cu.graphics);
        if (typeof cu.icon === 'number' && cu.icon >= 1000) used.add(cu.icon);
      });

      Object.values(civ.units || {}).forEach(u => {
        if (u.graphics) scanGraphics(u.graphics);
      });

      (civ.unique_building?.commands || []).forEach(cmd => {
        if (cmd.train?.graphics) scanGraphics(cmd.train.graphics);
      });
    });

    return Array.from(used).sort((a, b) => a - b);
  }

  const slpThumbCache = new Map();

  async function loadSlpThumbnail(slpNum) {
    if (!slpNum || typeof slpNum !== 'number') return null;
    if (slpThumbCache.has(slpNum)) return slpThumbCache.get(slpNum);

    const p = (async () => {
      const data = S.state.currentModData;
      const modRel = `resources/_common/drs/graphics/${slpNum}.slp`;
      const gameRel = `_common/drs/graphics/${slpNum}.slp`;

      // 1. Try mod file first (graphics, unit icons, building icons)
      if (data && data.folder) {
        const candidates = [modRel, `icons/units/${slpNum}.slp`, `icons/buildings/${slpNum}.slp`];
        for (const relPath of candidates) {
          try {
            const res = await S.api(`/api/file?folder=${encodeURIComponent(data.folder)}&path=${encodeURIComponent(relPath)}`);
            if (res.ok) {
              const ab = await res.arrayBuffer();
              const decoded = window.SlpDecode.decode(ab);
              const pal = await window.GameData.palette();
              const frame = decoded.frames[0];
              if (frame) {
                const rgba = window.SlpDecode.render(frame, pal, { player: 0, shadow: true });
                const canvas = document.createElement('canvas');
                canvas.width = frame.width;
                canvas.height = frame.height;
                canvas.getContext('2d').putImageData(new ImageData(rgba, frame.width, frame.height), 0, 0);
                return canvas.toDataURL('image/png');
              }
            }
          } catch (_) {}
        }
      }

      // 2. Try game graphics
      try {
        if (window.GameData) {
          const decoded = await window.GameData.slp(gameRel);
          return await window.GameData.frameUrl(gameRel, decoded, 0, 0);
        }
      } catch (_) {}

      return null;
    })();

    slpThumbCache.set(slpNum, p);
    return p;
  }

  async function allocateSlpNumber() {
    const used = await getUsedSlpNumbers();
    let n = 15000;
    while (used.includes(n)) n++;
    return n;
  }

  // ==========================================================================
  // Universal Sprite Field & Modal Component
  // ==========================================================================

  const SpritePickerModal = {
    open(options = {}) {
      const title = options.title || 'Select or Create Sprite';
      const kind = options.kind || 'building';
      const currentSlp = options.currentSlp || null;
      let activeTab = 'image';
      let loadedImage = null;
      let loadedFile = options.initialFile || null;
      let selectedPreset = kind === 'icon' ? 'icon' : (kind === 'building' ? 'building' : 'unit');
      let loadedSlpBuffer = null;
      let loadedSlpName = '';
      let gallerySource = (kind === 'icon') ? 'interface' : 'graphics';
      let selectedGalleryItem = null;
      let selectedGallerySlp = currentSlp || null;
      let galleryItems = [];

      let backdrop = document.getElementById('sprite-picker-backdrop');
      if (!backdrop) {
        backdrop = document.createElement('div');
        backdrop.id = 'sprite-picker-backdrop';
        backdrop.className = 'sprite-picker-modal-backdrop';
        document.body.appendChild(backdrop);
      }
      backdrop.style.display = 'flex';

      const renderModal = () => {
        backdrop.innerHTML = `
          <div class="sprite-picker-modal">
            <div class="sprite-picker-header">
              <h3>${escapeHtml(title)}</h3>
              <button type="button" class="btn-modal-close" id="sp-close">&times;</button>
            </div>
            <div class="sprite-picker-tabs">
              <button type="button" class="sprite-picker-tab ${activeTab === 'image' ? 'active' : ''}" data-tab="image">Make from Image</button>
              <button type="button" class="sprite-picker-tab ${activeTab === 'slp' ? 'active' : ''}" data-tab="slp">Upload SLP</button>
              <button type="button" class="sprite-picker-tab ${activeTab === 'gallery' ? 'active' : ''}" data-tab="gallery">Game &amp; Mod Sprites</button>
            </div>
            <div class="sprite-picker-body">
              <!-- Tab 1: Image -->
              <div class="sprite-tab-pane" id="sp-tab-image" style="${activeTab === 'image' ? '' : 'display:none;'}">
                <div class="sprite-dropzone-box" id="sp-drop-image">
                  <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" style="margin: 0 auto; color: #c9a35e;">
                    <rect x="3" y="3" width="18" height="18" rx="2" ry="2"/>
                    <circle cx="8.5" cy="8.5" r="1.5"/>
                    <polyline points="21 15 16 10 5 21"/>
                  </svg>
                  <p>Drop PNG, JPG, or WebP here, or click to choose</p>
                  <span style="font-size: 11px; color: #7a634e; display: block; margin-top: 4px;">Automatically quantized to AoE2 256-color palette</span>
                  <input type="file" id="sp-file-image" accept=".png,.jpg,.jpeg,.webp,image/*" style="display:none;">
                </div>

                <div id="sp-image-preview-area" style="${loadedImage ? '' : 'display:none;'} margin-top: 14px;">
                  <div style="display: flex; gap: 16px; align-items: flex-start; background: #120c07; padding: 12px; border-radius: 4px; border: 1px solid #3d2c1c;">
                    <div style="width: 140px; height: 140px; background: #21160d; border: 1px solid #483421; border-radius: 4px; display: flex; align-items: center; justify-content: center; overflow: hidden; position: relative;">
                      <canvas id="sp-preview-canvas" style="max-width: 136px; max-height: 136px; image-rendering: pixelated;"></canvas>
                    </div>
                    <div style="flex: 1; display: flex; flex-direction: column; gap: 8px;">
                      <div id="sp-image-info" style="font-size: 12px; color: #dfc79b; font-weight: 600;"></div>
                      <div style="display: flex; gap: 6px; align-items: center;">
                        <label style="font-size: 11px; color: #a8947f;">Preset:</label>
                        <div class="sprite-preset-group" style="display: flex; gap: 4px;">
                          <button type="button" class="btn btn-wood btn-xs sp-preset-btn ${selectedPreset === 'building' ? 'active' : ''}" data-preset="building">Building</button>
                          <button type="button" class="btn btn-wood btn-xs sp-preset-btn ${selectedPreset === 'unit' ? 'active' : ''}" data-preset="unit">Unit</button>
                          <button type="button" class="btn btn-wood btn-xs sp-preset-btn ${selectedPreset === 'icon' ? 'active' : ''}" data-preset="icon">Icon (Center)</button>
                        </div>
                      </div>
                      <div style="margin-top: 6px; display: flex; gap: 8px; flex-wrap: wrap;">
                        <button type="button" class="btn btn-save btn-sm" id="sp-image-convert-btn">
                          Convert &amp; Use as Sprite
                        </button>
                        <button type="button" class="btn btn-ghost btn-sm" id="sp-image-adv-btn" title="Open full SLP Creator with frames and directions">
                          Advanced Editor...
                        </button>
                      </div>
                    </div>
                  </div>
                </div>
              </div>

              <!-- Tab 2: SLP Upload -->
              <div class="sprite-tab-pane" id="sp-tab-slp" style="${activeTab === 'slp' ? '' : 'display:none;'}">
                <div class="sprite-dropzone-box" id="sp-drop-slp">
                  <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" style="margin: 0 auto; color: #c9a35e;">
                    <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
                    <polyline points="14 2 14 8 20 8"/>
                    <line x1="16" y1="13" x2="8" y2="13"/>
                    <line x1="16" y1="17" x2="8" y2="17"/>
                    <polyline points="10 9 9 9 8 9"/>
                  </svg>
                  <p>Drop an existing AoE2 .slp file here, or click to browse</p>
                  <input type="file" id="sp-file-slp" accept=".slp" style="display:none;">
                </div>
                <div id="sp-slp-info-area" style="${loadedSlpBuffer ? '' : 'display:none;'} margin-top: 14px; background: #120c07; padding: 12px; border-radius: 4px; border: 1px solid #3d2c1c;">
                  <div id="sp-slp-info" style="font-size: 12px; color: #dfc79b; margin-bottom: 8px;"></div>
                  <button type="button" class="btn btn-wood btn-sm" id="sp-slp-use-btn" style="color: #f7e7c4; font-weight: 600;">
                    Upload &amp; Use SLP
                  </button>
                </div>
              </div>

              <!-- Tab 3: Gallery / Browser -->
              <div class="sprite-tab-pane" id="sp-tab-gallery" style="${activeTab === 'gallery' ? '' : 'display:none;'}">
                <div class="sprite-browser-container">
                  <div class="sprite-browser-sources">
                    <div class="sprite-source-tabs">
                      <button type="button" class="sprite-src-btn ${gallerySource === 'mod' ? 'active' : ''}" data-src="mod">This Mod</button>
                      <button type="button" class="sprite-src-btn ${gallerySource === 'graphics' ? 'active' : ''}" data-src="graphics">Game Graphics (Buildings/Units)</button>
                      <button type="button" class="sprite-src-btn ${gallerySource === 'interface' ? 'active' : ''}" data-src="interface">Game Icons (Interface)</button>
                    </div>
                    <div class="sprite-browser-search-wrap">
                      <input type="text" id="sp-gallery-search" class="form-input form-input-sm" placeholder="Filter by # or name..." style="width: 170px;">
                      <button type="button" class="btn btn-wood btn-xs" id="sp-gallery-full-btn" title="Open full sprite viewer with animation tools">Full Viewer...</button>
                    </div>
                  </div>

                  <div class="sprite-browser-split">
                    <!-- Left Column: Item List -->
                    <div class="sprite-browser-list-pane">
                      <div class="sprite-browser-list-header">
                        <span id="sp-browser-count">0 sprites</span>
                        <span id="sp-browser-hint">Click to inspect</span>
                      </div>
                      <div class="sprite-browser-list" id="sp-gallery-list">
                        <div style="color: #8c7863; font-size: 11px; text-align: center; padding: 24px;">Loading sprites...</div>
                      </div>
                    </div>

                    <!-- Right Column: Live Preview & Action -->
                    <div class="sprite-browser-preview-pane">
                      <div class="sprite-preview-canvas-box">
                        <canvas id="sp-gallery-canvas" width="64" height="64"></canvas>
                        <div id="sp-gallery-loading" style="display: none; position: absolute; font-size: 11px; color: #dfc79b; background: rgba(0,0,0,0.6); padding: 4px 8px; border-radius: 3px;">Loading...</div>
                      </div>
                      <div class="sprite-preview-details" id="sp-gallery-details">
                        <div class="detail-row"><span>Sprite ID:</span><span class="detail-val" id="sp-det-id">—</span></div>
                        <div class="detail-row"><span>Source:</span><span class="detail-val" id="sp-det-src">—</span></div>
                        <div class="detail-row"><span>Dimensions:</span><span class="detail-val" id="sp-det-size">—</span></div>
                        <div class="detail-row"><span>Frames:</span><span class="detail-val" id="sp-det-frames">—</span></div>
                      </div>
                      <div style="width: 100%; display: flex; flex-direction: column; gap: 6px;">
                        <button type="button" class="btn btn-wood btn-sm" id="sp-gallery-use-btn" style="width: 100%; color: #f7e7c4; font-weight: 600;" ${selectedGallerySlp ? '' : 'disabled'}>
                          ${selectedGallerySlp ? `Select Sprite #${selectedGallerySlp}` : 'Select Sprite'}
                        </button>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        `;

        setupModalEvents();
      };

      const closeModal = () => {
        backdrop.style.display = 'none';
        backdrop.innerHTML = '';
        if (options.onCancel) options.onCancel();
      };

      const setupModalEvents = () => {
        backdrop.querySelector('#sp-close')?.addEventListener('click', closeModal);

        // Tab switching
        backdrop.querySelectorAll('.sprite-picker-tab').forEach(btn => {
          btn.addEventListener('click', () => {
            activeTab = btn.dataset.tab;
            renderModal();
            if (activeTab === 'gallery') loadGallerySource(gallerySource);
            if (activeTab === 'image' && loadedImage) updateImagePreview();
          });
        });

        // Tab 1: Image
        const dropImage = backdrop.querySelector('#sp-drop-image');
        const fileImage = backdrop.querySelector('#sp-file-image');
        dropImage?.addEventListener('click', () => fileImage?.click());
        fileImage?.addEventListener('change', (e) => {
          if (e.target.files && e.target.files[0]) processImageFile(e.target.files[0]);
          fileImage.value = '';
        });

        dropImage?.addEventListener('dragover', (e) => { e.preventDefault(); dropImage.classList.add('dragover'); });
        dropImage?.addEventListener('dragleave', () => dropImage.classList.remove('dragover'));
        dropImage?.addEventListener('drop', (e) => {
          e.preventDefault();
          dropImage.classList.remove('dragover');
          if (e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0]) {
            processImageFile(e.dataTransfer.files[0]);
          }
        });

        backdrop.querySelectorAll('.sp-preset-btn').forEach(btn => {
          btn.addEventListener('click', () => {
            selectedPreset = btn.dataset.preset;
            backdrop.querySelectorAll('.sp-preset-btn').forEach(b => b.classList.toggle('active', b === btn));
          });
        });

        backdrop.querySelector('#sp-image-convert-btn')?.addEventListener('click', async () => {
          if (!loadedFile) return;
          try {
            S.showToast('Converting image to AoE2 sprite...', 'info');
            const slpNum = await allocateSlpNumber();
            const converted = await window.SlpCreator.convertImageToSlp(loadedFile, {
              kind: selectedPreset,
              comment: 'Wololoader'
            });
            const data = S.state.currentModData;
            const path = `resources/_common/drs/graphics/${slpNum}.slp`;
            await S.api(`/api/file?folder=${encodeURIComponent(data.folder)}&path=${encodeURIComponent(path)}`, {
              method: 'PUT',
              body: converted.slpBytes
            });
            if (options.kind === 'icon' || selectedPreset === 'icon') {
              const iconFolder = options.iconFolder || 'buildings';
              try {
                await S.api(`/api/file?folder=${encodeURIComponent(data.folder)}&path=${encodeURIComponent(`icons/${iconFolder}/${slpNum}.slp`)}`, {
                  method: 'PUT',
                  body: converted.slpBytes
                });
              } catch (_) {}
            }
            slpThumbCache.delete(slpNum);
            S.setDirty(true);
            S.showToast(`Converted ${loadedFile.name} to SLP #${slpNum} (${converted.width}×${converted.height})`, 'success');
            closeModal();
            if (options.onSelect) options.onSelect(slpNum);
          } catch (err) {
            console.error('Image to SLP conversion failed:', err);
            S.showToast('Conversion failed: ' + (err.message || 'Unknown error'), 'error');
          }
        });

        backdrop.querySelector('#sp-image-adv-btn')?.addEventListener('click', async () => {
          const fileToUse = loadedFile;
          closeModal();
          const made = await window.SlpViewer.createFromPng({
            title: `Create ${selectedPreset} sprite`,
            kind: selectedPreset === 'icon' ? 'icon' : (selectedPreset === 'unit' ? 'animation' : 'building'),
            file: fileToUse
          });
          if (made && options.onSelect) {
            options.onSelect(made.num);
          }
        });

        // Tab 2: SLP Upload
        const dropSlp = backdrop.querySelector('#sp-drop-slp');
        const fileSlp = backdrop.querySelector('#sp-file-slp');
        dropSlp?.addEventListener('click', () => fileSlp?.click());
        fileSlp?.addEventListener('change', (e) => {
          if (e.target.files && e.target.files[0]) processSlpFile(e.target.files[0]);
          fileSlp.value = '';
        });

        dropSlp?.addEventListener('dragover', (e) => { e.preventDefault(); dropSlp.classList.add('dragover'); });
        dropSlp?.addEventListener('dragleave', () => dropSlp.classList.remove('dragover'));
        dropSlp?.addEventListener('drop', (e) => {
          e.preventDefault();
          dropSlp.classList.remove('dragover');
          if (e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0]) {
            processSlpFile(e.dataTransfer.files[0]);
          }
        });

        backdrop.querySelector('#sp-slp-use-btn')?.addEventListener('click', async () => {
          if (!loadedSlpBuffer) return;
          try {
            const slpNum = await allocateSlpNumber();
            const data = S.state.currentModData;
            const path = `resources/_common/drs/graphics/${slpNum}.slp`;
            await S.api(`/api/file?folder=${encodeURIComponent(data.folder)}&path=${encodeURIComponent(path)}`, {
              method: 'PUT',
              body: loadedSlpBuffer
            });
            if (options.kind === 'icon') {
              const iconFolder = options.iconFolder || 'buildings';
              try {
                await S.api(`/api/file?folder=${encodeURIComponent(data.folder)}&path=${encodeURIComponent(`icons/${iconFolder}/${slpNum}.slp`)}`, {
                  method: 'PUT',
                  body: loadedSlpBuffer
                });
              } catch (_) {}
            }
            slpThumbCache.delete(slpNum);
            S.setDirty(true);
            S.showToast(`Uploaded ${loadedSlpName} as SLP #${slpNum}`, 'success');
            closeModal();
            if (options.onSelect) options.onSelect(slpNum);
          } catch (err) {
            console.error('SLP upload failed:', err);
            S.showToast('Upload failed: ' + (err.message || 'Unknown error'), 'error');
          }
        });

        // Tab 3: Gallery
        backdrop.querySelectorAll('.sprite-src-btn').forEach(btn => {
          btn.addEventListener('click', () => {
            const src = btn.dataset.src;
            backdrop.querySelectorAll('.sprite-src-btn').forEach(b => b.classList.toggle('active', b === btn));
            loadGallerySource(src);
          });
        });

        backdrop.querySelector('#sp-gallery-full-btn')?.addEventListener('click', () => {
          closeModal();
          window.SlpViewer?.open({
            source: currentSlp ? 'mod' : (gallerySource === 'interface' ? 'interface' : 'graphics'),
            name: currentSlp ? `${currentSlp}.slp` : undefined
          });
        });

        backdrop.querySelector('#sp-gallery-use-btn')?.addEventListener('click', () => {
          if (selectedGallerySlp !== null) {
            closeModal();
            if (options.onSelect) options.onSelect(selectedGallerySlp);
          }
        });

        const searchInput = backdrop.querySelector('#sp-gallery-search');
        searchInput?.addEventListener('input', () => {
          filterGallery(searchInput.value.trim().toLowerCase());
        });
      };

      const processImageFile = (file) => {
        if (!file) return;
        const isImage = file.type.startsWith('image/') || /\.(png|jpe?g|webp)$/i.test(file.name);
        if (!isImage) {
          S.showToast('Please select a PNG, JPG, or WebP image.', 'error');
          return;
        }
        loadedFile = file;
        const reader = new FileReader();
        reader.onload = (e) => {
          const img = new Image();
          img.onload = () => {
            loadedImage = img;
            updateImagePreview();
          };
          img.src = e.target.result;
        };
        reader.readAsDataURL(file);
      };

      const updateImagePreview = () => {
        if (!loadedImage) return;
        const previewArea = backdrop.querySelector('#sp-image-preview-area');
        const canvas = backdrop.querySelector('#sp-preview-canvas');
        const info = backdrop.querySelector('#sp-image-info');
        if (previewArea) previewArea.style.display = 'block';
        if (canvas) {
          canvas.width = loadedImage.width;
          canvas.height = loadedImage.height;
          const ctx = canvas.getContext('2d');
          ctx.drawImage(loadedImage, 0, 0);
        }
        if (info) {
          info.textContent = `${loadedFile ? loadedFile.name : 'Image'} (${loadedImage.width} × ${loadedImage.height} px)`;
        }
      };

      const processSlpFile = async (file) => {
        if (!file || !file.name.toLowerCase().endsWith('.slp')) {
          S.showToast('Please select an AoE2 .slp file.', 'error');
          return;
        }
        try {
          const ab = await file.arrayBuffer();
          const decoded = window.SlpDecode.decode(ab);
          loadedSlpBuffer = ab;
          loadedSlpName = file.name;
          const infoArea = backdrop.querySelector('#sp-slp-info-area');
          const info = backdrop.querySelector('#sp-slp-info');
          if (infoArea) infoArea.style.display = 'block';
          if (info) {
            const first = decoded.frames[0];
            const sizeStr = first ? `${first.width}×${first.height}` : 'unknown';
            info.innerHTML = `<strong>${escapeHtml(file.name)}</strong>: Valid SLP 2.0N (${decoded.frames.length} frame(s), ${sizeStr} px)`;
          }
        } catch (err) {
          S.showToast('Invalid SLP file: ' + err.message, 'error');
        }
      };

      const loadGallerySource = async (src) => {
        gallerySource = src;
        const list = backdrop.querySelector('#sp-gallery-list');
        const countEl = backdrop.querySelector('#sp-browser-count');
        if (list) list.innerHTML = `<div style="color: #8c7863; font-size: 11px; text-align: center; padding: 24px;">Loading sprites...</div>`;
        if (countEl) countEl.textContent = 'Loading...';

        galleryItems = [];
        try {
          if (src === 'mod') {
            const folder = S.state.currentModFolder || (S.state.currentModData && S.state.currentModData.folder);
            if (folder) {
              for (const dir of ['resources/_common/drs/graphics', 'resources/_common/drs/interface']) {
                try {
                  const names = await S.api(`/api/files?folder=${encodeURIComponent(folder)}&dir=${encodeURIComponent(dir)}`);
                  for (const name of (names || [])) {
                    const m = name.match(/^(\d+)\.slp$/i);
                    if (m) {
                      galleryItems.push({
                        slp: Number(m[1]),
                        name: name,
                        path: `${dir}/${name}`,
                        isMod: true,
                        folder: folder,
                        label: `Mod #${m[1]}`
                      });
                    }
                  }
                } catch (_) {}
              }
            }
          } else {
            const dir = (src === 'interface') ? '_common/drs/interface' : '_common/drs/graphics';
            const res = await window.GameData.list(dir);
            for (const f of (res.files || [])) {
              const m = f.name.match(/^(\d+)\.slp$/i);
              if (m) {
                galleryItems.push({
                  slp: Number(m[1]),
                  name: f.name,
                  path: `${dir}/${f.name}`,
                  size: f.size,
                  isMod: false,
                  label: `#${m[1]}`
                });
              }
            }
          }
        } catch (e) {
          console.warn('Failed to load gallery items:', e);
        }

        galleryItems.sort((a, b) => a.slp - b.slp);
        const searchInput = backdrop.querySelector('#sp-gallery-search');
        filterGallery(searchInput ? searchInput.value.trim().toLowerCase() : '');
      };

      const selectGalleryItem = async (item, updateClasses = true) => {
        if (!item) return;
        selectedGalleryItem = item;
        selectedGallerySlp = item.slp;

        if (updateClasses) {
          backdrop.querySelectorAll('.sprite-browser-item').forEach(el => {
            el.classList.toggle('selected', Number(el.dataset.slp) === item.slp);
          });
        }

        const idEl = backdrop.querySelector('#sp-det-id');
        const srcEl = backdrop.querySelector('#sp-det-src');
        const sizeEl = backdrop.querySelector('#sp-det-size');
        const framesEl = backdrop.querySelector('#sp-det-frames');
        const canvas = backdrop.querySelector('#sp-gallery-canvas');
        const loadingEl = backdrop.querySelector('#sp-gallery-loading');
        const useBtn = backdrop.querySelector('#sp-gallery-use-btn');

        if (idEl) idEl.textContent = `#${item.slp}`;
        if (srcEl) srcEl.textContent = item.isMod ? 'This Mod' : (gallerySource === 'interface' ? 'Game Icons' : 'Game Graphics');
        if (sizeEl) sizeEl.textContent = 'Loading...';
        if (framesEl) framesEl.textContent = '...';
        if (loadingEl) loadingEl.style.display = 'block';
        if (useBtn) {
          useBtn.disabled = false;
          useBtn.textContent = `Select Sprite #${item.slp}`;
        }

        try {
          let decoded = null;
          if (item.isMod) {
            const res = await S.api(`/api/file?folder=${encodeURIComponent(item.folder)}&path=${encodeURIComponent(item.path)}`);
            decoded = window.SlpDecode.decode(await res.arrayBuffer());
          } else {
            const path = item.path || (gallerySource === 'interface' ? `_common/drs/interface/${item.slp}.slp` : `_common/drs/graphics/${item.slp}.slp`);
            decoded = await window.GameData.slp(path);
          }

          if (decoded && decoded.frames && decoded.frames.length > 0) {
            const pal = await window.GameData.palette();
            const f0 = decoded.frames[0];
            if (sizeEl) sizeEl.textContent = `${f0.width} × ${f0.height} px`;
            if (framesEl) framesEl.textContent = `${decoded.frames.length} frame(s)`;

            if (canvas) {
              const rgba = window.SlpDecode.render(f0, pal, { player: 0, shadow: true });
              canvas.width = f0.width;
              canvas.height = f0.height;
              canvas.getContext('2d').putImageData(new ImageData(rgba, f0.width, f0.height), 0, 0);
            }
          } else {
            if (sizeEl) sizeEl.textContent = 'No frames';
            if (framesEl) framesEl.textContent = '0 frames';
            if (canvas) { canvas.width = 1; canvas.height = 1; }
          }
        } catch (err) {
          console.warn(`Could not decode SLP #${item.slp}:`, err);
          if (sizeEl) sizeEl.textContent = 'Unable to decode';
          if (framesEl) framesEl.textContent = '—';
          if (canvas) { canvas.width = 1; canvas.height = 1; }
        } finally {
          if (loadingEl) loadingEl.style.display = 'none';
        }
      };

      const filterGallery = (q) => {
        const list = backdrop.querySelector('#sp-gallery-list');
        const countEl = backdrop.querySelector('#sp-browser-count');
        if (!list) return;

        let filtered = galleryItems;
        if (q) {
          filtered = galleryItems.filter(i => String(i.slp).includes(q) || (i.name && i.name.toLowerCase().includes(q)));
          const qNum = parseInt(q, 10);
          if (qNum > 0 && !filtered.some(i => i.slp === qNum)) {
            filtered = [{
              slp: qNum,
              name: `${qNum}.slp`,
              path: (gallerySource === 'interface' ? `_common/drs/interface/${qNum}.slp` : `_common/drs/graphics/${qNum}.slp`),
              isMod: false,
              isCustomNumber: true,
              label: `#${qNum} (Custom Number)`
            }, ...filtered];
          }
        }

        if (countEl) countEl.textContent = `${filtered.length} sprite(s)`;

        if (filtered.length === 0) {
          list.innerHTML = `<div style="color: #8c7863; font-size: 11px; text-align: center; padding: 24px;">No matching sprites. Type a number in search to select it directly.</div>`;
          return;
        }

        const maxDisplay = 250;
        const toShow = filtered.slice(0, maxDisplay);
        list.innerHTML = toShow.map(item => {
          const isSelected = selectedGallerySlp === item.slp;
          const sizeStr = item.size ? (item.size >= 1024 ? `${Math.round(item.size / 1024)} KB` : `${item.size} B`) : (item.isMod ? 'Mod File' : '');
          return `
            <div class="sprite-browser-item ${isSelected ? 'selected' : ''}" data-slp="${item.slp}">
              <span>${item.isCustomNumber ? `Use #${item.slp}` : `SLP #${item.slp}`}</span>
              <span class="sprite-browser-item-badge">${sizeStr}</span>
            </div>
          `;
        }).join('');

        if (filtered.length > maxDisplay) {
          const extra = document.createElement('div');
          extra.style.cssText = 'color: #7d6852; font-size: 10px; text-align: center; padding: 4px;';
          extra.textContent = `+ ${filtered.length - maxDisplay} more (type to filter)`;
          list.appendChild(extra);
        }

        list.querySelectorAll('.sprite-browser-item').forEach(el => {
          el.addEventListener('click', () => {
            const slp = Number(el.dataset.slp);
            const found = filtered.find(i => i.slp === slp);
            selectGalleryItem(found || { slp: slp, name: `${slp}.slp`, isMod: false, path: `_common/drs/graphics/${slp}.slp` });
          });
        });

        if (selectedGallerySlp) {
          const hit = filtered.find(i => i.slp === selectedGallerySlp);
          if (hit) selectGalleryItem(hit, false);
        } else if (filtered.length > 0) {
          selectGalleryItem(filtered[0], false);
        }
      };

      renderModal();
      if (activeTab === 'gallery') {
        loadGallerySource(gallerySource);
      }
      if (options.initialFile) {
        if (options.initialFile.name.toLowerCase().endsWith('.slp')) {
          activeTab = 'slp';
          renderModal();
          processSlpFile(options.initialFile);
        } else {
          processImageFile(options.initialFile);
        }
      }
    }
  };

  function renderSpriteField(container, options = {}) {
    if (typeof container === 'string') container = document.getElementById(container);
    if (!container) return;

    const value = options.value;
    const kind = options.kind || 'building';
    const title = options.title || (value ? `SLP #${value}` : 'Default Art');
    const hint = options.hint || (value ? `Custom graphic file (${value}.slp)` : 'Using default visual appearance');
    const fallbackUnit = options.fallbackUnit;

    container.innerHTML = `
      <div class="aoe2-sprite-field" style="display: flex; align-items: center; gap: 12px; background: #140e08; border: 1px solid #3d2c1c; border-radius: 4px; padding: 8px 10px;">
        <div class="aoe2-sprite-thumb-box sprite-field-thumb" style="cursor: pointer;" title="Click to change sprite">
          ${value ? `
            <div class="sprite-thumb-loading"><span class="sprite-spinner"></span></div>
          ` : (fallbackUnit ? `
            <div class="sprite-thumb-fallback">${unitIconHtml(fallbackUnit, 44)}</div>
          ` : `
            <div class="empty-thumb" style="display: flex; align-items: center; justify-content: center; width: 100%; height: 100%;">
              <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#7a634e" stroke-width="1.6">
                <rect x="3" y="3" width="18" height="18" rx="2" ry="2"/>
                <circle cx="8.5" cy="8.5" r="1.5"/>
                <polyline points="21 15 16 10 5 21"/>
              </svg>
            </div>
          `)}
        </div>
        <div class="aoe2-sprite-meta" style="flex: 1; min-width: 0;">
          <span class="aoe2-sprite-title" style="display: block; font-size: 12px; font-weight: 600; color: #dfc79b;">${escapeHtml(title)}</span>
          <span class="aoe2-sprite-desc" style="display: block; font-size: 11px; color: #8c7863;">${escapeHtml(hint)}</span>
        </div>
        <div class="aoe2-sprite-actions" style="display: flex; gap: 6px;">
          <button type="button" class="btn btn-wood btn-xs btn-sprite-pick">
            ${value ? 'Change Sprite...' : 'Select / Create Sprite...'}
          </button>
          ${value && options.onRemove ? `
            <button type="button" class="btn btn-wood btn-xs btn-danger-soft btn-sprite-remove" title="Remove custom sprite">Remove</button>
          ` : ''}
        </div>
      </div>
    `;

    const root = container.firstElementChild;
    const thumbBox = root.querySelector('.sprite-field-thumb');
    if (value !== undefined && value !== null && thumbBox) {
      if (typeof value === 'number' && value < 1000 && options.sheet && window.GameIcons) {
        thumbBox.innerHTML = GameIcons.markup(options.sheet, value, fallbackUnit?.name || 'U', 44);
        GameIcons.hydrate(thumbBox);
      } else if (typeof value === 'number') {
        loadSlpThumbnail(value).then(url => {
          if (url && thumbBox) {
            thumbBox.innerHTML = `<img src="${url}" style="max-width: 48px; max-height: 48px; object-fit: contain; image-rendering: pixelated;" alt="SLP #${value}">`;
          }
        });
      }
    }

    const openPicker = (initialFile = null) => {
      SpritePickerModal.open({
        title: `Choose or Create ${kind.charAt(0).toUpperCase() + kind.slice(1)} Sprite`,
        kind: kind,
        iconFolder: options.iconFolder,
        currentSlp: value,
        initialFile: initialFile,
        onSelect: (newSlp) => {
          if (options.onChange) options.onChange(newSlp);
        }
      });
    };

    root.querySelector('.btn-sprite-pick')?.addEventListener('click', () => openPicker());
    thumbBox?.addEventListener('click', () => openPicker());

    root.querySelector('.btn-sprite-remove')?.addEventListener('click', () => {
      if (options.onRemove) options.onRemove();
    });

    root.addEventListener('dragover', (e) => {
      e.preventDefault();
      root.style.borderColor = '#c9a35e';
    });
    root.addEventListener('dragleave', (e) => {
      if (!root.contains(e.relatedTarget)) root.style.borderColor = '#3d2c1c';
    });
    root.addEventListener('drop', (e) => {
      e.preventDefault();
      root.style.borderColor = '#3d2c1c';
      if (e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0]) {
        openPicker(e.dataTransfer.files[0]);
      }
    });
  }

  window.SpritePickerModal = SpritePickerModal;
  window.renderSpriteField = renderSpriteField;

  function renderLookForm(containerId, targetObject, graphicsKey, element) {
    const container = document.getElementById(containerId);
    if (!container) return;

    const graphics = targetObject[graphicsKey] || {};
    const primary = [{ id: 'standing', label: 'Standing / Idle' }];
    const extra = [
      { id: 'walking', label: 'Walking' },
      { id: 'attack', label: 'Attack' },
      { id: 'dying', label: 'Dying' }
    ];
    const extraCount = extra.filter(a => graphics[a.id] !== undefined).length;

    container.innerHTML = `
      <div class="look-section-wrapper">
        <div class="look-cards-grid" style="display: flex; flex-direction: column; gap: 8px;">
          ${primary.map(a => `<div id="look-field-${containerId}-${a.id}"></div>`).join('')}
        </div>
        <details class="sc-advanced" data-pref-key="look-more-anims">
          <summary>${CHEV_SVG}<span>More animations (Walking, Attack, Dying${extraCount ? ` · ${extraCount} set` : ''})</span></summary>
          <div class="sc-advanced-body">
            ${extra.map(a => `<div id="look-field-${containerId}-${a.id}"></div>`).join('')}
          </div>
        </details>
      </div>
    `;

    [...primary, ...extra].forEach(a => {
      const fieldContainer = document.getElementById(`look-field-${containerId}-${a.id}`);
      if (!fieldContainer) return;
      const val = graphics[a.id];
      const slpNum = typeof val === 'number' ? val : val?.slp;

      renderSpriteField(fieldContainer, {
        value: slpNum,
        kind: 'animation',
        title: `${a.label} Animation`,
        hint: slpNum ? `Graphic SLP #${slpNum}` : 'Using base unit animation',
        onChange: (newSlp) => {
          if (!targetObject[graphicsKey]) targetObject[graphicsKey] = {};
          targetObject[graphicsKey][a.id] = newSlp;
          slpThumbCache.delete(newSlp);
          S.setDirty(true);
          renderLookForm(containerId, targetObject, graphicsKey, element);
        },
        onRemove: slpNum ? () => {
          if (targetObject[graphicsKey]) {
            delete targetObject[graphicsKey][a.id];
            if (Object.keys(targetObject[graphicsKey]).length === 0) {
              delete targetObject[graphicsKey];
            }
            S.setDirty(true);
            renderLookForm(containerId, targetObject, graphicsKey, element);
          }
        } : null
      });
    });
    wireDisclosures(container);
  }

  // ==========================================================================
  // Unit Picker Dialog
  // ==========================================================================

  function showUnitPicker(typeFilter, onPick) {
    const modal = document.getElementById('modal-container');
    if (!modal) return;

    const units = S.state.catalog.units.filter(u => !typeFilter || u.type === typeFilter);

    modal.style.display = 'flex';
    modal.innerHTML = `
      <div class="modal-dialog" style="max-height: 80vh;">
        <div class="modal-header">
          <h3 class="modal-title">Pick Unit</h3>
          <button class="btn-modal-close">&times;</button>
        </div>
        <div class="modal-body">
          <input type="text" id="unit-search" placeholder="Search units..." class="form-input" style="margin-bottom: 12px;">
          <div style="max-height: 400px; overflow-y: auto; display: flex; flex-direction: column; gap: 4px;">
            ${units.map(u =>
              `<button class="btn btn-wood btn-sm btn-block basic-pick-unit" data-unit-id="${u.id}" data-search="${escapeHtml((u.name + ' ' + u.id).toLowerCase())}" style="text-align: left;">` +
              `${unitIconHtml(u, 24)}<span>${escapeHtml(u.name)} (${u.id})</span></button>`
            ).join('')}
          </div>
        </div>
      </div>
    `;

    const closeModal = () => {
      modal.style.display = 'none';
      modal.innerHTML = '';
    };

    modal.querySelector('.btn-modal-close')?.addEventListener('click', closeModal);

    const searchInput = document.getElementById('unit-search');
    const buttons = modal.querySelectorAll('button[data-unit-id]');

    searchInput?.addEventListener('input', () => {
      const query = searchInput.value.toLowerCase();
      buttons.forEach(btn => {
        btn.style.display = btn.dataset.search.includes(query) ? 'block' : 'none';
      });
    });

    buttons.forEach(btn => {
      btn.addEventListener('click', () => {
        const unitId = Number(btn.dataset.unitId);
        const unit = units.find(u => u.id === unitId);
        if (unit) {
          closeModal();
          onPick(unit);
        }
      });
    });
  }

  // Save hook (app.js saveAll, order 20): dirty visual scripts, then the generated part of main.lua.
  async function saveGraphs() {
    const data = S.state.currentModData;
    if (!data) return;
    for (const [slug, graphData] of loadedGraphs.entries()) {
      if (!graphData.dirty) continue;
      const path = `scripts/graphs/${slug}.json`;
      await S.api(`/api/file?folder=${encodeURIComponent(data.folder)}&path=${encodeURIComponent(path)}`, {
        method: 'PUT',
        body: JSON.stringify(graphData.graph)
      });
      graphData.dirty = false;
      S.setExtraDirty('graph:' + slug, false);
    }
    await rebuildMainLua();
  }

  // Revert: unsaved visual scripts are dropped (the saved file is read again when opened).
  function discardUnsaved() {
    for (const [slug, graphData] of [...loadedGraphs.entries()]) {
      if (!graphData.dirty) continue;
      loadedGraphs.delete(slug);
      S.setExtraDirty('graph:' + slug, false);
    }
    modifiedHistoryTexts.clear();
    currentElement = null;
  }

  async function saveHistoryTexts() {
    const data = S.state.currentModData;
    if (!data) return;
    const folder = data.folder;

    if (currentElement && currentElement.type === 'civilization') {
      const textInput = document.getElementById('history-text-input');
      const civ = data.civs[currentElement.file];
      if (textInput && civ) {
        const hasHistory = civ.history && (civ.history.title || civ.history.image || civ.history.side_image || civ.history.text_file);
        const hasText = textInput.value && textInput.value.trim().length > 0;
        if (hasHistory || hasText || modifiedHistoryTexts.has(currentElement.file)) {
          civ.history = civ.history || {};
          if (!civ.history.text_file) {
            civ.history.text_file = 'history/' + getCivSlug(civ, currentElement.file) + '.txt';
          }
          delete civ.history.text;
          modifiedHistoryTexts.set(currentElement.file, {
            path: civ.history.text_file,
            text: textInput.value
          });
        }
      }
    }

    for (const [file, item] of modifiedHistoryTexts.entries()) {
      await S.api(`/api/file?folder=${encodeURIComponent(folder)}&path=${encodeURIComponent(item.path)}`, {
        method: 'PUT',
        headers: { 'Content-Type': 'text/plain; charset=utf-8' },
        body: item.text
      });
    }
    modifiedHistoryTexts.clear();
  }

  // The compiled graphs go to scripts/visual.lua (generated, do not edit). main.lua only has
  // require("visual"). A block that an older version wrote into main.lua is moved out of it.
  async function rebuildMainLua() {
    const data = S.state.currentModData;
    if (!data || !window.ScGraph) return;

    try {
      // main.lua is read only when the folder has it (no 404 on a mod without one)
      let scriptFiles = [];
      try { scriptFiles = await S.api(`/api/files?folder=${encodeURIComponent(data.folder)}&dir=scripts`); } catch (e) { scriptFiles = []; }
      const hasMain = scriptFiles.includes('main.lua');
      const hasVisual = scriptFiles.includes(ScGraph.VISUAL_MODULE + '.lua');
      let mainText = '';
      if (hasMain) mainText = await readModFile('scripts/main.lua');

      // every graph of the mod goes into visual.lua, not only the ones opened in this session
      let files = [];
      try { files = await S.api(`/api/files?folder=${encodeURIComponent(data.folder)}&dir=scripts/graphs`); } catch (e) { files = []; }
      for (const f of files) {
        const slug = f.replace(/\.json$/, '');
        if (!f.endsWith('.json') || loadedGraphs.has(slug)) continue;
        const g = await loadScriptGraph(f);
        if (g) loadedGraphs.set(slug, graphEntry(slug, g, false));
      }

      const graphLuas = [], skipped = [];
      for (const [slug, graphData] of [...loadedGraphs.entries()].sort((a, b) => a[0].localeCompare(b[0]))) {
        const result = window.ScGraph.toLua(graphData.graph);
        if (result.errors && result.errors.length > 0) {
          console.warn(`[Basic] Graph ${slug} has errors, skipping:`, result.errors);
          skipped.push(`${graphData.graph.name || slug}: ${result.errors[0].message}`);
          continue;
        }
        if (result.lua) {
          graphLuas.push({ name: slug, lua: result.lua });
        }
      }

      if (skipped.length) S.showToast('Not in the game until fixed: ' + skipped.join('; '), 'error', 8000);
      const oldBlock = window.ScGraph.hasOldBlock(mainText);
      if (!graphLuas.length && !oldBlock && !hasVisual) return;

      // visual.lua first: main.lua only needs it to exist when the game loads the mod
      await S.api(`/api/file?folder=${encodeURIComponent(data.folder)}&path=scripts/${ScGraph.VISUAL_MODULE}.lua`, {
        method: 'PUT',
        body: window.ScGraph.visualLuaFile(graphLuas)
      });
      const base = hasMain ? window.ScGraph.removeOldBlock(mainText) : '';
      const mergedText = window.ScGraph.addRequire(base, ScGraph.VISUAL_MODULE);
      if (hasMain && mergedText === mainText) return;

      await S.api(`/api/file?folder=${encodeURIComponent(data.folder)}&path=scripts/main.lua`, {
        method: 'PUT',
        body: mergedText
      });
    } catch (e) {
      console.error('[Basic] Failed to rebuild the visual scripts:', e);
      throw new Error('Failed to rebuild Lua scripts');
    }
  }

  // ==========================================================================
  // Initialization
  // ==========================================================================

  // used by SlpViewer.createFromPng (slpviewer.js) as well
  S.allocateSlpNumber = allocateSlpNumber;

  function graphEntry(slug, graph, dirty) {
    return { slug, graph, dirty, editor: null };
  }

  function init() {
    S.onModLoaded(() => {
      loadedGraphs.clear();
      modifiedHistoryTexts.clear();
      currentElement = null;
    });
    S.onSave(saveGraphs, 20);
    S.onSave(saveHistoryTexts, 25);
    S.onRevert(discardUnsaved);
    window.BasicWorkspace = {
      renderCivCards, emptyCard, escapeHtml, getElementTypeName, civGroupHtml,
      newElement: showNewElementDialog,
      createNewScript,
      openScript(file) {
        const title = file.replace(/\.json$/, '').replace(/[-_]+/g, ' ');
        openElementEditor({ type: 'script', id: `script:${file}`, title, file });
      },
      leaveEditor() { currentElement = null; renderToken++; },
      editorOpen() { return !!currentElement; },
      visualScripts() {
        return [...loadedGraphs.values()].map(g => ({ slug: g.slug, file: g.slug + '.json', name: g.graph && g.graph.name, dirty: g.dirty }));
      },
      discardUnsaved,
    };
  }

  init();


})();

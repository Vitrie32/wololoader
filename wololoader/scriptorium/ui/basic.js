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
  let loadedGraphs = new Map();
  let currentElement = null;
  let renderToken = 0;   // bumped when an editor opens: a pending draw of the page must not overwrite it

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
  function hydrateIcons() {
    if (!window.GameIcons) return;
    // the whole page: the mod nav (sections.js) and the cards draw the same placeholders
    GameIcons.hydrate(document.body);
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
    hydrateIcons();
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
    const headIcon = civ.unique_unit
      ? unitIconHtml(findUnit(civ.unique_unit.copy_from), 40)
      : unitIconHtml(findUnit(CIV_TOWN_CENTER), 40);
    return `
      <section class="civ-group">
        <button class="civ-group-head" data-element-id="${escapeHtml(civEl.id)}">
          <span class="civ-group-icon">${headIcon}</span>
          <span class="civ-group-text">
            <span class="civ-group-name">${escapeHtml(name)}</span>
            <span class="civ-group-sub">${escapeHtml(sub)}</span>
          </span>
          <span class="civ-group-edit">Edit civilization <span aria-hidden="true">&rsaquo;</span></span>
        </button>
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
    if (el.type === 'unique_unit') return unitIconHtml(findUnit(civ.unique_unit.copy_from), 30);
    if (el.type === 'unique_building') return unitIconHtml(findUnit(civ.unique_building.copy_from), 30);
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
          <p style="margin-bottom: 12px; font-size: 13px; color: var(--text-dark);">Choose the type of element to create:</p>
          <div class="new-element-types">
            <div class="new-element-type-card" data-type="civilization">
              <strong>Civilization</strong>
              <p>A complete custom civilization with bonuses, units, and tech tree</p>
            </div>
            <div class="new-element-type-card" data-type="unique_unit">
              <strong>Unique Unit</strong>
              <p>Castle unique unit trained for a civilization (requires existing civ)</p>
            </div>
            <div class="new-element-type-card" data-type="unique_building">
              <strong>Mod Building</strong>
              <p>Custom building with train/research buttons (requires existing civ)</p>
            </div>
            <div class="new-element-type-card" data-type="game_civ">
              <strong>Game Civilization</strong>
              <p>Add buildings, unit changes and bonuses to a civilization of the game (Britons, Franks, ...)</p>
            </div>
            <div class="new-element-type-card" data-type="unit_changes">
              <strong>Unit Changes</strong>
              <p>Change stats and looks of existing units for a civilization</p>
            </div>
            <div class="new-element-type-card" data-type="bonuses">
              <strong>Bonuses</strong>
              <p>Starting resources, stronger units, faster work, team bonus</p>
            </div>
            <div class="new-element-type-card" data-type="script">
              <strong>Script</strong>
              <p>A visual script: events in the game run your actions (node graph)</p>
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
          <div class="basic-editor-actions"><button class="btn btn-wood btn-sm" id="basic-delete-element">Delete</button></div>
        </div>
        <div class="basic-editor-content"><div class="basic-form">
          <div class="basic-form-section">
            <h3 class="basic-form-section-title">Civilization of the game</h3>
            <div class="basic-form-field"><label>Civilization</label><select id="gc-civ" class="form-input">${opts}</select></div>
            <p class="basic-muted">This mod changes this civilization of the game instead of making a new one.
              Add a Mod Building, Unit Changes or Bonuses for it from "+ New Element". Its name, AI names and
              unique unit stay the game's own.</p>
          </div>
        </div></div>
      </div>`;
    root.querySelector('#basic-back').onclick = closeElementEditor;
    root.querySelector('#gc-civ').onchange = e => { civ.extend = Number(e.target.value); S.setDirty(true); };
    root.querySelector('#basic-delete-element').onclick = () => S.showConfirm('Delete', `Remove all changes to ${getCivName(civ.extend)}?`, 'Delete', () => {
      delete S.state.currentModData.civs[element.file];
      if (!S.state.currentModData.deleted_civs) S.state.currentModData.deleted_civs = [];
      S.state.currentModData.deleted_civs.push(element.file);
      S.setDirty(true);
      closeElementEditor();
    });
  }

  function renderCivilizationEditor(element) {
    const data = S.state.currentModData;
    const civ = data.civs[element.file];
    if (!civ) return closeElementEditor();
    if (civ.extend) return renderGameCivEditor(element, civ);

    const root = document.getElementById('basic-root');
    root.innerHTML = `
      <div class="basic-editor">
        <div class="basic-editor-header">
          <button class="btn btn-wood btn-sm" id="basic-back">← Back</button>
          <h2 class="basic-editor-title">${escapeHtml(element.title)}</h2>
          <div class="basic-editor-actions">
            <button class="btn btn-wood btn-sm" id="basic-delete-element">Delete</button>
          </div>
        </div>
        <div class="basic-editor-content">
          <div class="basic-form">
            <div class="basic-form-section">
              <h3 class="basic-form-section-title">Identity</h3>
              <div class="basic-form-row">
                <div class="basic-form-field">
                  <label>Name <span class="required">*</span></label>
                  <input type="text" id="civ-name" value="${escapeHtml(civ.name || '')}" class="form-input">
                </div>
                <div class="basic-form-field">
                  <label>Internal Name (ASCII, max 19) <span class="required">*</span></label>
                  <input type="text" id="civ-dat-name" value="${escapeHtml(civ.dat_name || '')}" maxlength="19" class="form-input font-mono">
                </div>
              </div>
              <div class="basic-form-row">
                <div class="basic-form-field">
                  <label>Base Civilization <span class="required">*</span></label>
                  <select id="civ-base" class="form-select">
                    ${S.state.catalog.civs.filter(c => c.id >= 1 && c.id <= 31).map(c =>
                      `<option value="${c.id}" ${c.id === civ.base ? 'selected' : ''}>${escapeHtml(c.name)}</option>`
                    ).join('')}
                  </select>
                </div>
              </div>
            </div>

            <div class="basic-form-section">
              <h3 class="basic-form-section-title">Description</h3>
              <div class="basic-form-field">
                <label>In-game description (supports \\n line breaks and &lt;b&gt;bold&lt;b&gt;)</label>
                <textarea id="civ-description" class="form-textarea" rows="6">${escapeHtml(civ.description || '')}</textarea>
              </div>
            </div>

            <div class="basic-form-section">
              <h3 class="basic-form-section-title">AI Player Names</h3>
              <div class="basic-form-field">
                <label>One name per line</label>
                <textarea id="civ-ai-names" class="form-textarea" rows="4">${escapeHtml((civ.ai_names || []).join('\n'))}</textarea>
              </div>
            </div>
          </div>
        </div>
      </div>
    `;

    setupCivEditorEvents(element, civ);
  }

  function setupCivEditorEvents(element, civ) {
    document.getElementById('basic-back')?.addEventListener('click', closeElementEditor);
    document.getElementById('basic-delete-element')?.addEventListener('click', () => {
      deleteElement(element);
    });

    const inputs = ['civ-name', 'civ-dat-name', 'civ-base', 'civ-description', 'civ-ai-names'];
    inputs.forEach(id => {
      const el = document.getElementById(id);
      if (el) {
        el.addEventListener('input', () => updateCivFromForm(civ));
        el.addEventListener('change', () => updateCivFromForm(civ));
      }
    });
  }

  function updateCivFromForm(civ) {
    const nameEl = document.getElementById('civ-name');
    const datNameEl = document.getElementById('civ-dat-name');
    const baseEl = document.getElementById('civ-base');
    const descEl = document.getElementById('civ-description');
    const aiNamesEl = document.getElementById('civ-ai-names');

    if (nameEl) civ.name = nameEl.value.trim();
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
    root.innerHTML = `
      <div class="basic-editor">
        <div class="basic-editor-header">
          <button class="btn btn-wood btn-sm" id="basic-back">← Back</button>
          <h2 class="basic-editor-title">${escapeHtml(civ.name || element.file)} - Unique Unit</h2>
          <div class="basic-editor-actions">
            <button class="btn btn-wood btn-sm" id="basic-delete-element">Delete</button>
          </div>
        </div>
        <div class="basic-editor-content">
          <div class="basic-form">
            <div class="basic-form-section">
              <h3 class="basic-form-section-title">Identity</h3>
              <div class="basic-form-row">
                <div class="basic-form-field">
                  <label>Name <span class="required">*</span></label>
                  <input type="text" id="uu-name" value="${escapeHtml(uniqueUnit.name || '')}" class="form-input">
                </div>
                <div class="basic-form-field">
                  <label>Elite Name</label>
                  <input type="text" id="uu-elite-name" value="${escapeHtml(uniqueUnit.elite_name || '')}" class="form-input">
                </div>
              </div>
              <div class="basic-form-row">
                <div class="basic-form-field" style="grid-column: 1 / -1;">
                  <label>Help Text</label>
                  <textarea id="uu-help" class="form-textarea" rows="2">${escapeHtml(uniqueUnit.help || '')}</textarea>
                </div>
              </div>
              <div class="basic-form-row">
                <div class="basic-form-field">
                  <label>Based On</label>
                  <button class="btn btn-wood btn-sm btn-block" id="uu-pick-base">${baseUnit ? unitIconHtml(baseUnit, 20) + escapeHtml(baseUnit.name) + ' (' + baseUnit.id + ')' : 'Pick unit'}</button>
                </div>
              </div>
            </div>

            <div class="basic-form-section">
              <h3 class="basic-form-section-title">Stats (Base: ${baseUnit ? escapeHtml(baseUnit.name) : 'Unknown'})</h3>
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

            <div class="basic-form-section" id="uu-look-section">
              <h3 class="basic-form-section-title">Look (Unit Graphics)</h3>
              <div id="uu-look-container"></div>
            </div>
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

  function renderUniqueBuildingEditor(element) {
    const data = S.state.currentModData;
    const civ = data.civs[element.file];
    const building = civ.unique_building;
    if (!building) return closeElementEditor();

    const baseUnit = findUnit(building.copy_from || 104);
    const ages = ['Dark Age', 'Feudal Age', 'Castle Age', 'Imperial Age'];
    const pages = [{ id: 'mods', label: 'Mod Buildings' }, { id: 'military', label: 'Military' }];

    const root = document.getElementById('basic-root');
    root.innerHTML = `
      <div class="basic-editor">
        <div class="basic-editor-header">
          <button class="btn btn-wood btn-sm" id="basic-back">← Back</button>
          <h2 class="basic-editor-title">${escapeHtml(civ.name || element.file)} - Unique Building</h2>
          <div class="basic-editor-actions">
            <button class="btn btn-wood btn-sm" id="basic-delete-element">Delete</button>
          </div>
        </div>
        <div class="basic-editor-content">
          <div class="basic-form">
            <div class="basic-form-section">
              <h3 class="basic-form-section-title">Identity</h3>
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
              <div class="basic-form-row">
                <div class="basic-form-field" style="grid-column: 1 / -1;">
                  <label>Help Text</label>
                  <textarea id="ub-help" class="form-textarea" rows="2">${escapeHtml(building.help || '')}</textarea>
                </div>
              </div>
              <div class="basic-form-row">
                <div class="basic-form-field">
                  <label>Age <span class="required">*</span></label>
                  <select id="ub-age" class="form-select">
                    ${ages.map((age, i) => `<option value="${i}" ${building.age === i ? 'selected' : ''}>${age}</option>`).join('')}
                  </select>
                </div>
                <div class="basic-form-field">
                  <label>Page</label>
                  <select id="ub-page" class="form-select">
                    ${pages.map(p => `<option value="${p.id}" ${building.page === p.id ? 'selected' : ''}>${p.label}</option>`).join('')}
                  </select>
                </div>
              </div>
              <div class="basic-form-row">
                <div class="basic-form-field">
                  <label>Button Slot (1-15) <span class="required">*</span></label>
                  <input type="number" id="ub-button" min="1" max="15" value="${building.button || 1}" class="form-input">
                </div>
                <div class="basic-form-field">
                  <label>Hit Points</label>
                  <input type="number" id="ub-hp" value="${building.units?.hit_points ?? ''}" placeholder="2100" class="form-input">
                </div>
              </div>
              <div class="basic-form-row">
                <div class="basic-form-field">
                  <label>Picture (SLP)</label>
                  <div style="display: flex; gap: 8px; align-items: center;">
                    <input type="file" accept=".slp" id="ub-slp-upload" style="flex: 1;">
                    <button class="btn btn-wood btn-sm" id="ub-slp-create" style="white-space: nowrap;">Create from image...</button>
                    <button class="btn btn-wood btn-sm" id="ub-slp-view" style="white-space: nowrap;" title="Look at the game's sprites (read only)">View sprites...</button>
                    ${building.slp ? `<span class="font-mono" style="font-size: 11px; color: var(--text-dark-muted);">#${building.slp}</span>` : ''}
                  </div>
                </div>
              </div>
            </div>

            <div class="basic-form-section">
              <h3 class="basic-form-section-title">Buttons</h3>
              <div id="ub-commands-list"></div>
              <button class="btn btn-wood btn-sm" id="ub-add-command" style="margin-top: 12px;">+ Add Button</button>
            </div>
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

    setupUniqueBuildingEvents(building, element);
    renderCommandsList(building, element);
  }

  function setupUniqueBuildingEvents(building, element) {
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

    const slpUpload = document.getElementById('ub-slp-upload');
    if (slpUpload) {
      slpUpload.addEventListener('change', async (e) => {
        const file = e.target.files[0];
        if (!file) return;

        const slpNum = await allocateSlpNumber();
        const path = `resources/_common/drs/graphics/${slpNum}.slp`;

        try {
          const arrayBuffer = await file.arrayBuffer();
          const data = S.state.currentModData;
          await S.api(`/api/file?folder=${encodeURIComponent(data.folder)}&path=${encodeURIComponent(path)}`, {
            method: 'PUT',
            body: arrayBuffer
          });

          building.slp = slpNum;
          S.setDirty(true);
          S.showToast(`Uploaded ${file.name} as SLP #${slpNum}`, 'success');
          renderUniqueBuildingEditor(element);
        } catch (err) {
          console.error('Upload failed:', err);
          S.showToast('Upload failed: ' + (err.message || 'Unknown error'), 'error');
        }
      });
    }

    document.getElementById('ub-slp-view')?.addEventListener('click', () => window.SlpViewer?.open());
    const slpCreate = document.getElementById('ub-slp-create');
    if (slpCreate) {
      slpCreate.addEventListener('click', async () => {
        // the SLP creator is the Sprites pane's (slpviewer.js): it saves the sprite into the mod
        const made = await window.SlpViewer.createFromPng({ title: 'Create Building Sprite', kind: 'building' });
        if (!made) return;
        building.slp = made.num;
        S.setDirty(true);
        renderUniqueBuildingEditor(element);
      });
    }

    document.getElementById('ub-add-command')?.addEventListener('click', () => {
      showAddCommandDialog(building, element);
    });
  }

  function renderCommandsList(building, element) {
    const container = document.getElementById('ub-commands-list');
    if (!container) return;

    if (!building.commands) building.commands = [];

    container.innerHTML = building.commands.length === 0
      ? '<p style="color: var(--text-dark-muted); font-size: 12px;">No buttons yet. Click "+ Add Button" to add one.</p>'
      : building.commands.map((cmd, idx) => {
          const duplicateSlot = building.commands.filter(c => c.button === cmd.button).length > 1;
          let summary = '';
          if (cmd.train !== undefined) {
            const unitId = typeof cmd.train === 'number' ? cmd.train : cmd.train.copy_from;
            const unit = findUnit(unitId);
            summary = `Train: ${unit ? unit.name : 'Unit ' + unitId}`;
          } else if (cmd.research) {
            summary = `Research: ${cmd.research.name || 'Unnamed'}`;
          } else if (cmd.lua) {
            summary = `Script: ${cmd.text || cmd.lua}`;
          }

          return `
            <div class="basic-form-row" style="align-items: center; background: var(--parch-card-bg); padding: 8px; border: 1px solid var(--parch-border); margin-bottom: 8px;">
              <div style="flex: 1;">
                <div style="font-weight: bold; font-size: 13px; color: var(--wood-900); margin-bottom: 2px;">
                  Slot ${cmd.button} ${duplicateSlot ? '<span style="color: var(--state-red);">(duplicate!)</span>' : ''}
                </div>
                <div style="font-size: 12px; color: var(--text-dark-muted);">${escapeHtml(summary)}</div>
              </div>
              <div style="display: flex; gap: 8px;">
                <button class="btn btn-wood btn-sm" data-cmd-idx="${idx}" data-action="edit">Edit</button>
                <button class="btn btn-wood btn-sm" data-cmd-idx="${idx}" data-action="delete">Delete</button>
              </div>
            </div>
          `;
        }).join('');

    container.querySelectorAll('[data-action="edit"]').forEach(btn => {
      btn.addEventListener('click', () => {
        const idx = Number(btn.dataset.cmdIdx);
        editCommand(building, idx, element);
      });
    });

    container.querySelectorAll('[data-action="delete"]').forEach(btn => {
      btn.addEventListener('click', () => {
        const idx = Number(btn.dataset.cmdIdx);
        building.commands.splice(idx, 1);
        S.setDirty(true);
        renderCommandsList(building, element);
      });
    });
  }

  function showAddCommandDialog(building, element) {
    const modal = document.getElementById('modal-container');
    if (!modal) return;

    modal.style.display = 'flex';
    modal.innerHTML = `
      <div class="modal-dialog">
        <div class="modal-header">
          <h3 class="modal-title">Add Button</h3>
          <button class="btn-modal-close">&times;</button>
        </div>
        <div class="modal-body">
          <p style="margin-bottom: 12px; font-size: 13px; color: var(--text-dark);">Choose button type:</p>
          <div style="display: flex; flex-direction: column; gap: 8px;">
            <button class="btn btn-wood btn-block" data-type="train">Train Unit</button>
            <button class="btn btn-wood btn-block" data-type="research">Research Technology</button>
            <button class="btn btn-wood btn-block" data-type="lua">Script Button</button>
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

        const newCmd = { button: getNextFreeSlot(building) };
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
        renderCommandsList(building, element);
        editCommand(building, building.commands.length - 1, element);
      });
    });
  }

  function getNextFreeSlot(building) {
    const used = new Set((building.commands || []).map(c => c.button));
    for (let i = 1; i <= 15; i++) {
      if (!used.has(i)) return i;
    }
    return 15;
  }

  function editCommand(building, idx, element) {
    const cmd = building.commands[idx];
    const modal = document.getElementById('modal-container');
    if (!modal) return;

    let content = '';
    if (cmd.train !== undefined) {
      const isComplex = typeof cmd.train === 'object';
      const unitId = isComplex ? cmd.train.copy_from : cmd.train;
      const unit = findUnit(unitId);

      content = `
        <div class="basic-form-field">
          <label>Button Slot (1-15)</label>
          <input type="number" id="cmd-button" min="1" max="15" value="${cmd.button}" class="form-input">
        </div>
        <div class="basic-form-field">
          <label>Unit to Train</label>
          <button class="btn btn-wood btn-sm btn-block" id="cmd-pick-unit">${unit ? unitIconHtml(unit, 20) + escapeHtml(unit.name) + ' (' + unit.id + ')' : 'Pick unit'}</button>
        </div>
        ${isComplex ? `
          <div class="basic-form-field">
            <label>Custom Name</label>
            <input type="text" id="cmd-train-name" value="${escapeHtml(cmd.train.name || '')}" class="form-input">
          </div>
          <div class="basic-form-field">
            <label>Custom Help</label>
            <textarea id="cmd-train-help" class="form-textarea" rows="2">${escapeHtml(cmd.train.help || '')}</textarea>
          </div>
          <div class="basic-form-field">
            <label>Look (Graphics)</label>
            <div id="cmd-train-look"></div>
          </div>
        ` : `
          <div class="basic-form-field">
            <button class="btn btn-wood btn-sm" id="cmd-make-complex">Customize Unit</button>
          </div>
        `}
      `;
    } else if (cmd.research) {
      content = `
        <div class="basic-form-field">
          <label>Button Slot (1-15)</label>
          <input type="number" id="cmd-button" min="1" max="15" value="${cmd.button}" class="form-input">
        </div>
        <div class="basic-form-field">
          <label>Name</label>
          <input type="text" id="cmd-res-name" value="${escapeHtml(cmd.research.name || '')}" class="form-input">
        </div>
        <div class="basic-form-field">
          <label>Help Text</label>
          <textarea id="cmd-res-help" class="form-textarea" rows="2">${escapeHtml(cmd.research.help || '')}</textarea>
        </div>
        <div class="basic-form-row">
          <div class="basic-form-field">
            <label>Food Cost</label>
            <input type="number" id="cmd-res-food" value="${cmd.research.cost?.food ?? ''}" placeholder="0" class="form-input">
          </div>
          <div class="basic-form-field">
            <label>Wood Cost</label>
            <input type="number" id="cmd-res-wood" value="${cmd.research.cost?.wood ?? ''}" placeholder="0" class="form-input">
          </div>
        </div>
        <div class="basic-form-row">
          <div class="basic-form-field">
            <label>Stone Cost</label>
            <input type="number" id="cmd-res-stone" value="${cmd.research.cost?.stone ?? ''}" placeholder="0" class="form-input">
          </div>
          <div class="basic-form-field">
            <label>Gold Cost</label>
            <input type="number" id="cmd-res-gold" value="${cmd.research.cost?.gold ?? ''}" placeholder="0" class="form-input">
          </div>
        </div>
        <div class="basic-form-field">
          <label>Research Time (seconds)</label>
          <input type="number" id="cmd-res-time" value="${cmd.research.time || 30}" class="form-input">
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
    } else if (cmd.lua) {
      content = `
        <div class="basic-form-field">
          <label>Button Slot (1-15)</label>
          <input type="number" id="cmd-button" min="1" max="15" value="${cmd.button}" class="form-input">
        </div>
        <div class="basic-form-field">
          <label>Button Text</label>
          <input type="text" id="cmd-lua-text" value="${escapeHtml(cmd.text || '')}" class="form-input">
        </div>
        <div class="basic-form-field">
          <label>Help Text</label>
          <textarea id="cmd-lua-help" class="form-textarea" rows="2">${escapeHtml(cmd.help || '')}</textarea>
        </div>
        <div class="basic-form-field">
          <label>Icon Number</label>
          <input type="number" id="cmd-lua-icon" value="${cmd.icon || 49}" class="form-input">
        </div>
        <div class="basic-form-field">
          <label>Handler Name (Lua)</label>
          <input type="text" id="cmd-lua-handler" value="${escapeHtml(cmd.lua || '')}" class="form-input font-mono">
        </div>
      `;
    }

    modal.style.display = 'flex';
    modal.innerHTML = `
      <div class="modal-dialog" style="max-width: 600px; max-height: 80vh; overflow-y: auto;">
        <div class="modal-header">
          <h3 class="modal-title">Edit Button</h3>
          <button class="btn-modal-close">&times;</button>
        </div>
        <div class="modal-body">
          <div style="display: flex; flex-direction: column; gap: 12px;">
            ${content}
          </div>
        </div>
        <div class="modal-footer">
          <button class="btn btn-wood" id="cmd-save">Done</button>
        </div>
      </div>
    `;

    const closeModal = () => {
      modal.style.display = 'none';
      modal.innerHTML = '';
      renderCommandsList(building, element);
    };

    modal.querySelector('.btn-modal-close')?.addEventListener('click', closeModal);
    modal.querySelector('#cmd-save')?.addEventListener('click', closeModal);

    document.getElementById('cmd-button')?.addEventListener('input', (e) => {
      cmd.button = Number(e.target.value);
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
      ['text', 'help', 'icon', 'lua'].forEach(k => {
        const el = document.getElementById(`cmd-lua-${k === 'lua' ? 'handler' : k}`);
        if (el) {
          el.addEventListener('input', () => {
            if (k === 'lua') {
              cmd.lua = el.value;
            } else {
              cmd[k] = k === 'icon' ? Number(el.value) : el.value;
            }
            S.setDirty(true);
          });
        }
      });
    }
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
          <div class="basic-editor-actions"><button class="btn btn-wood btn-sm" id="uc-add">+ Change a unit</button></div>
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
    root.innerHTML = `
      <div class="basic-editor">
        <div class="basic-editor-header">
          <button class="btn btn-wood btn-sm" id="basic-back">← Back</button>
          <h2 class="basic-editor-title">${escapeHtml(civ.name || element.file)} - ${unit ? unit.name : 'Unit ' + unitId} Changes</h2>
          <div class="basic-editor-actions">
            <button class="btn btn-wood btn-sm" id="basic-delete-element">Delete</button>
          </div>
        </div>
        <div class="basic-editor-content">
          <div class="basic-form">
            <div class="basic-form-section">
              <h3 class="basic-form-section-title">Stats</h3>
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

            <div class="basic-form-section">
              <h3 class="basic-form-section-title">Look (Unit Graphics)</h3>
              <div id="uc-look-container"></div>
            </div>
          </div>
        </div>
      </div>
    `;

    document.getElementById('basic-back')?.addEventListener('click', closeElementEditor);
    document.getElementById('basic-delete-element')?.addEventListener('click', () => deleteElement(element));

    setupUnitChangesEvents(changes);
    renderLookForm('uc-look-container', changes, 'graphics', element);
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
            <button class="btn btn-wood btn-sm" id="basic-delete-element">Delete</button>
          </div>
        </div>
        <div class="basic-editor-content">
          <div class="basic-form">
            <div class="basic-form-section">
              <h3 class="basic-form-section-title">Civ Bonuses</h3>
              <div id="bonuses-civ-list"></div>
              <button class="btn btn-wood btn-sm" id="bonuses-add-civ" style="margin-top: 12px;">+ Add Bonus</button>
            </div>

            <div class="basic-form-section">
              <h3 class="basic-form-section-title">Team Bonuses</h3>
              <div id="bonuses-team-list"></div>
              <button class="btn btn-wood btn-sm" id="bonuses-add-team" style="margin-top: 12px;">+ Add Team Bonus</button>
            </div>
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
      <div style="background: var(--parch-card-bg); padding: 8px; border: 1px solid var(--parch-border); margin-bottom: 8px; display: flex; align-items: center;">
        <div style="flex: 1; font-size: 13px; color: var(--wood-900);">${escapeHtml(summary)}</div>
        <button class="btn btn-wood btn-sm" data-idx="${idx}" data-action="delete">Delete</button>
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

      Object.values(civ.units || {}).forEach(u => {
        if (u.graphics) scanGraphics(u.graphics);
      });

      (civ.unique_building?.commands || []).forEach(cmd => {
        if (cmd.train?.graphics) scanGraphics(cmd.train.graphics);
      });
    });

    return Array.from(used).sort((a, b) => a - b);
  }

  async function allocateSlpNumber() {
    const used = await getUsedSlpNumbers();
    let n = 15000;
    while (used.includes(n)) n++;
    return n;
  }

  function renderLookForm(containerId, targetObject, graphicsKey, element) {
    const container = document.getElementById(containerId);
    if (!container) return;

    const graphics = targetObject[graphicsKey] || {};
    const animations = ['standing', 'walking', 'attack', 'dying'];

    container.innerHTML = `
      <p style="font-size: 12px; color: var(--text-dark-muted); margin-bottom: 12px;">
        Upload SLP files with the same frames and angles as the base unit's sprite.
      </p>
      <div style="display: flex; flex-direction: column; gap: 12px;">
        ${animations.map(anim => {
          const val = graphics[anim];
          const slpNum = typeof val === 'number' ? val : val?.slp;
          return `
            <div class="basic-form-row" style="align-items: center;">
              <div class="basic-form-field">
                <label style="text-transform: capitalize;">${anim}</label>
                <div style="display: flex; gap: 8px; align-items: center;">
                  <input type="file" accept=".slp" data-anim="${anim}" class="look-upload" style="flex: 1;">
                  <button class="btn btn-wood btn-sm look-create" data-anim="${anim}" style="white-space: nowrap;">Create...</button>
                  ${slpNum ? `<span class="font-mono" style="font-size: 11px; color: var(--text-dark-muted);">#${slpNum}</span>` : ''}
                  ${slpNum ? `<button class="btn btn-wood btn-sm look-remove" data-anim="${anim}">Remove</button>` : ''}
                </div>
              </div>
            </div>
          `;
        }).join('')}
      </div>
    `;

    container.querySelectorAll('.look-upload').forEach(input => {
      input.addEventListener('change', async (e) => {
        const file = e.target.files[0];
        if (!file) return;

        const anim = input.dataset.anim;
        const slpNum = await allocateSlpNumber();
        const path = `resources/_common/drs/graphics/${slpNum}.slp`;

        try {
          const arrayBuffer = await file.arrayBuffer();
          const data = S.state.currentModData;
          await S.api(`/api/file?folder=${encodeURIComponent(data.folder)}&path=${encodeURIComponent(path)}`, {
            method: 'PUT',
            body: arrayBuffer
          });

          if (!targetObject[graphicsKey]) targetObject[graphicsKey] = {};
          targetObject[graphicsKey][anim] = slpNum;
          S.setDirty(true);
          S.showToast(`Uploaded ${file.name} as SLP #${slpNum}`, 'success');

          renderLookForm(containerId, targetObject, graphicsKey, element);
        } catch (err) {
          console.error('Upload failed:', err);
          S.showToast('Upload failed: ' + (err.message || 'Unknown error'), 'error');
        }
      });
    });

    container.querySelectorAll('.look-create').forEach(btn => {
      btn.addEventListener('click', async () => {
        const anim = btn.dataset.anim;
        const made = await window.SlpViewer.createFromPng({
          title: `Create ${anim} sprite`, kind: 'animation', frames: 10, directions: 5
        });
        if (!made) return;
        if (!targetObject[graphicsKey]) targetObject[graphicsKey] = {};
        targetObject[graphicsKey][anim] = made.num;
        S.setDirty(true);
        renderLookForm(containerId, targetObject, graphicsKey, element);
      });
    });

    container.querySelectorAll('.look-remove').forEach(btn => {
      btn.addEventListener('click', () => {
        const anim = btn.dataset.anim;
        if (targetObject[graphicsKey]) {
          delete targetObject[graphicsKey][anim];
          if (Object.keys(targetObject[graphicsKey]).length === 0) {
            delete targetObject[graphicsKey];
          }
        }
        S.setDirty(true);
        renderLookForm(containerId, targetObject, graphicsKey, element);
      });
    });
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
    currentElement = null;
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
      currentElement = null;
    });
    S.onSave(saveGraphs, 20);
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

/**
 * Scriptorium v2 — Mod Hub Front End
 * Browse, search, install, update and uninstall community mods from a public index.
 */
(function () {
  'use strict';

  const S = window.Scriptorium;
  if (!S) return;

  const esc = S.escapeHtml;
  const hubIconCache = new Map(); // id -> Promise<blobUrl | null>

  const hubState = {
    active: false,       // true when home screen is showing Mod Hub tab
    loading: false,
    busy: false,
    busyText: '',
    error: '',
    actionError: '',
    index: null,         // response from GET /api/hub/index
    selectedId: null,    // mod id open in detail view, or null for grid
    search: '',
    filter: 'all',       // 'all' | 'installed' | 'updates' | 'available'
    tag: ''
  };

  function formatBytes(n) {
    if (!n || n <= 0) return 'Unknown';
    if (n < 1024) return n + ' B';
    if (n < 1024 * 1024) return (n / 1024).toFixed(n < 10240 ? 1 : 0) + ' KB';
    return (n / (1024 * 1024)).toFixed(2) + ' MB';
  }

  function loadHubIconUrl(id) {
    if (!hubIconCache.has(id)) {
      const p = S.api(`/api/hub/icon?id=${encodeURIComponent(id)}`)
        .then(res => res.blob())
        .then(blob => URL.createObjectURL(blob))
        .catch(() => null);
      hubIconCache.set(id, p);
    }
    return hubIconCache.get(id);
  }

  function clearHubIconCache() {
    for (const p of hubIconCache.values()) {
      p.then(url => { if (url) URL.revokeObjectURL(url); });
    }
    hubIconCache.clear();
  }

  function hubIconHtml(m) {
    if (m.icon) {
      return `<img data-hub-icon="${esc(m.id)}" data-hub-name="${esc(m.name || m.id)}" alt="">`;
    }
    return S.generatedIconSvg(m.id, m.name || m.id);
  }

  function hydrateHubIcons(rootEl) {
    if (!rootEl) return;
    rootEl.querySelectorAll('img[data-hub-icon]').forEach(img => {
      const id = img.dataset.hubIcon;
      const name = img.dataset.hubName || id;
      loadHubIconUrl(id).then(url => {
        if (url) {
          img.src = url;
          return;
        }
        const tmp = document.createElement('div');
        tmp.innerHTML = S.generatedIconSvg(id, name);
        if (img.parentNode) {
          const parent = img.parentNode;
          parent.classList.add('is-generated');
          img.replaceWith(tmp.firstElementChild);
        }
      });
    });
  }

  function modBadgeHtml(m) {
    if (m.update_available) {
      return '<span class="hub-badge update">Update available</span>';
    }
    if (m.installed && m.hub_managed) {
      return '<span class="hub-badge installed">Installed</span>';
    }
    if (m.installed && !m.hub_managed) {
      return '<span class="hub-badge local" title="Present in mods folder without .hub.json">Installed (local)</span>';
    }
    if (m.compatible === false) {
      return `<span class="hub-badge warn">Needs v${esc(m.requires_wololoader)}</span>`;
    }
    return '';
  }

  function updateTabBadge() {
    const badge = document.getElementById('hub-tab-badge');
    if (!badge) return;
    const mods = (hubState.index && hubState.index.mods) || [];
    const updates = mods.filter(m => m.update_available).length;
    if (updates > 0) {
      badge.textContent = String(updates);
      badge.hidden = false;
      badge.title = `${updates} update${updates === 1 ? '' : 's'} available`;
    } else {
      badge.hidden = true;
    }
  }

  function setHomeTab(isHub) {
    hubState.active = !!isHub;
    const tabMods = document.getElementById('tab-my-mods');
    const tabHub = document.getElementById('tab-mod-hub');
    const panelMods = document.getElementById('mods-home-panel');
    const panelHub = document.getElementById('hub-home-panel');

    if (tabMods) tabMods.classList.toggle('active', !hubState.active);
    if (tabHub) tabHub.classList.toggle('active', hubState.active);
    if (panelMods) panelMods.hidden = hubState.active;
    if (panelHub) panelHub.hidden = !hubState.active;

    if (hubState.active) {
      if (!hubState.index && !hubState.loading) {
        fetchIndex(false);
      } else {
        renderHub();
      }
    }
  }

  async function fetchIndex(forceRefresh) {
    hubState.loading = true;
    hubState.error = '';
    if (forceRefresh) clearHubIconCache();
    renderHub();
    try {
      const qs = forceRefresh ? '?refresh=1' : '';
      hubState.index = await S.api('/api/hub/index' + qs);
      hubState.error = '';
    } catch (err) {
      hubState.error = err.message || 'Could not reach Mod Hub index.';
    } finally {
      hubState.loading = false;
      updateTabBadge();
      renderHub();
    }
  }

  function populateTagFilter() {
    const sel = document.getElementById('hub-tag-filter');
    if (!sel) return;
    const mods = (hubState.index && hubState.index.mods) || [];
    const tags = new Set();
    for (const m of mods) {
      for (const t of (m.tags || [])) tags.add(t);
    }
    const sorted = [...tags].sort((a, b) => a.localeCompare(b));
    const current = hubState.tag;
    sel.innerHTML = '<option value="">All tags</option>' +
      sorted.map(t => `<option value="${esc(t)}">${esc(t)}</option>`).join('');
    sel.value = sorted.includes(current) ? current : '';
    hubState.tag = sel.value;
  }

  function renderHub() {
    const body = document.getElementById('hub-body');
    const countEl = document.getElementById('hub-count');
    if (!body) return;

    populateTagFilter();

    if (hubState.loading && !hubState.index) {
      if (countEl) countEl.textContent = 'Loading...';
      body.innerHTML = `
        <div class="hub-state-box">
          <div class="hub-state-title">Loading Mod Hub...</div>
          <div class="hub-progress-track"><div class="hub-progress-bar"></div></div>
        </div>`;
      return;
    }

    if (hubState.error && !hubState.index) {
      if (countEl) countEl.textContent = 'Offline';
      body.innerHTML = `
        <div class="hub-state-box">
          <div class="hub-state-title">Could not load Mod Hub</div>
          <p class="hub-state-text">${esc(hubState.error)}</p>
          <p class="hub-state-text">Check your internet connection or configure a custom Hub index URL.</p>
          <div class="hub-state-actions">
            <button type="button" class="btn btn-save btn-sm" id="btn-hub-retry">Retry</button>
            <button type="button" class="btn btn-wood btn-sm" id="btn-hub-err-settings">Hub URL...</button>
          </div>
        </div>`;
      body.querySelector('#btn-hub-retry')?.addEventListener('click', () => fetchIndex(true));
      body.querySelector('#btn-hub-err-settings')?.addEventListener('click', openHubSettingsModal);
      return;
    }

    const mods = (hubState.index && hubState.index.mods) || [];
    const updates = mods.filter(m => m.update_available).length;
    if (countEl) {
      const parts = [`${mods.length} available`];
      if (updates > 0) parts.push(`${updates} update${updates === 1 ? '' : 's'}`);
      countEl.textContent = parts.join(' · ');
    }

    if (hubState.selectedId) {
      const mod = mods.find(m => m.id === hubState.selectedId);
      if (mod) {
        renderDetail(body, mod, mods);
        return;
      }
      hubState.selectedId = null;
    }

    const q = hubState.search.toLowerCase().trim();
    const filtered = mods.filter(m => {
      if (hubState.filter === 'installed' && !m.installed) return false;
      if (hubState.filter === 'updates' && !m.update_available) return false;
      if (hubState.filter === 'available' && m.installed) return false;
      if (hubState.tag && !(m.tags || []).includes(hubState.tag)) return false;
      if (q) {
        const hay = [m.id, m.name, m.author, m.description, ...(m.tags || [])]
          .join(' ').toLowerCase();
        if (!hay.includes(q)) return false;
      }
      return true;
    });

    if (mods.length === 0) {
      body.innerHTML = `
        <div class="hub-state-box">
          <div class="hub-state-title">No mods in this index</div>
          <p class="hub-state-text">The Mod Hub index is currently empty.</p>
        </div>`;
      return;
    }

    if (filtered.length === 0) {
      body.innerHTML = '<p class="mods-grid-note">No hub mods match your filter.</p>';
      return;
    }

    const errBanner = hubState.error
      ? `<div class="hub-error-banner" style="margin-bottom:14px;">Refresh failed (showing cached index): ${esc(hubState.error)}</div>`
      : '';

    body.innerHTML = errBanner + `<div class="hub-grid">${filtered.map(m => `
      <div class="hub-card" role="button" tabindex="0" data-hub-id="${esc(m.id)}">
        <div class="hub-card-top">
          <div class="mod-tile-icon${m.icon ? '' : ' is-generated'}">${hubIconHtml(m)}</div>
          ${modBadgeHtml(m)}
        </div>
        <div class="mod-tile-name">${esc(m.name || m.id)}</div>
        <div class="hub-card-author">v${esc(m.version || '0.1')}${m.author ? ` &middot; by ${esc(m.author)}` : ''}</div>
        <div class="hub-card-desc">${esc(m.description || 'No description provided.')}</div>
        <div class="hub-card-tags">
          ${(m.tags || []).slice(0, 3).map(t => `<span class="hub-tag">${esc(t)}</span>`).join('')}
        </div>
      </div>`).join('')}</div>`;

    hydrateHubIcons(body);
    body.querySelectorAll('.hub-card[data-hub-id]').forEach(card => {
      const open = () => {
        hubState.selectedId = card.dataset.hubId;
        hubState.actionError = '';
        renderHub();
      };
      card.addEventListener('click', open);
      card.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); open(); }
      });
    });
  }

  function renderDetail(body, m, allMods) {
    const busy = hubState.busy;
    const deps = m.depends || [];
    const compatOk = m.compatible !== false;

    const actions = [];
    if (!m.installed) {
      actions.push(`<button type="button" class="btn btn-save btn-sm" id="btn-hub-install" ${busy || !compatOk ? 'disabled' : ''}>Install</button>`);
    } else if (!m.hub_managed) {
      actions.push(`<button type="button" class="btn btn-save btn-sm" id="btn-hub-overwrite" ${busy || !compatOk ? 'disabled' : ''}>Replace with Hub version</button>`);
      actions.push(`<button type="button" class="btn btn-wood btn-sm" id="btn-hub-open-editor" ${busy ? 'disabled' : ''}>Open in Editor</button>`);
    } else {
      if (m.update_available) {
        actions.push(`<button type="button" class="btn btn-save btn-sm" id="btn-hub-update" ${busy || !compatOk ? 'disabled' : ''}>Update to v${esc(m.version)}</button>`);
      }
      actions.push(`<button type="button" class="btn btn-wood btn-sm" id="btn-hub-open-editor" ${busy ? 'disabled' : ''}>Open in Editor</button>`);
      actions.push(`<button type="button" class="btn btn-danger btn-sm" id="btn-hub-uninstall" ${busy ? 'disabled' : ''}>Uninstall</button>`);
    }

    const depHtml = deps.length === 0 ? '' : `
      <div class="hub-detail-section">
        <div class="hub-section-heading">Dependencies (${deps.length})</div>
        <div class="hub-dep-list">
          ${deps.map(depId => {
            const depMod = allMods.find(x => x.id === depId);
            const localMatch = S.state.mods.some(x => x.folder === depId);
            let pill = '<span class="hub-badge warn">Missing from index</span>';
            if ((depMod && depMod.installed) || localMatch) {
              pill = '<span class="hub-badge installed">Installed</span>';
            } else if (depMod) {
              pill = '<span class="hub-badge local">Will install automatically</span>';
            }
            return `<div class="hub-dep-row${depMod ? ' clickable' : ''}" data-dep-id="${esc(depId)}">
              <div>
                <strong>${esc(depMod ? (depMod.name || depId) : depId)}</strong>
                <span class="hub-card-author"> (${esc(depId)}${depMod ? ' · v' + esc(depMod.version) : ''})</span>
              </div>
              ${pill}
            </div>`;
          }).join('')}
        </div>
      </div>`;

    const progressHtml = busy ? `
      <div class="hub-progress-box">
        <div class="hub-progress-label">${esc(hubState.busyText || 'Working...')}</div>
        <div class="hub-progress-track"><div class="hub-progress-bar"></div></div>
      </div>` : '';

    const errorHtml = hubState.actionError ? `
      <div class="hub-error-banner">${esc(hubState.actionError)}</div>` : '';

    const verLabel = m.installed && m.installed_version && m.installed_version !== m.version
      ? `v${esc(m.version)} (installed: v${esc(m.installed_version)})`
      : `v${esc(m.version || '0.1')}`;

    body.innerHTML = `
      <div class="hub-detail">
        <div class="hub-detail-nav">
          <button type="button" class="btn btn-ghost btn-sm" id="btn-hub-back" ${busy ? 'disabled' : ''}>&lsaquo; Back to Mod Hub</button>
          <span class="hub-card-author">Folder: <code>${esc(m.id)}</code></span>
        </div>

        <div class="hub-detail-hero">
          <div class="hub-detail-left">
            <div class="hub-detail-icon${m.icon ? '' : ' is-generated'}">${hubIconHtml(m)}</div>
            <div class="hub-detail-meta">
              <div class="hub-detail-title-row">
                <h3 class="hub-detail-title">${esc(m.name || m.id)}</h3>
                ${modBadgeHtml(m)}
              </div>
              <div class="hub-detail-sub">
                <span>${verLabel}</span>
                ${m.author ? `<span>&middot;</span><span>by ${esc(m.author)}</span>` : ''}
                ${m.updated ? `<span>&middot;</span><span>Updated ${esc(m.updated)}</span>` : ''}
                ${m.requires_wololoader ? `<span>&middot;</span><span>Requires Wololoader &ge; ${esc(m.requires_wololoader)}</span>` : ''}
              </div>
              ${(m.tags && m.tags.length) ? `<div class="hub-card-tags">${m.tags.map(t => `<span class="hub-tag">${esc(t)}</span>`).join('')}</div>` : ''}
            </div>
          </div>
          <div class="hub-detail-actions">${actions.join('')}</div>
        </div>

        ${progressHtml}
        ${errorHtml}

        ${!compatOk ? `<div class="hub-error-banner">This mod requires Wololoader ${esc(m.requires_wololoader)} or newer (current: ${esc(hubState.index?.wololoader_version || 'unknown')}).</div>` : ''}

        ${depHtml}

        <div class="hub-detail-section">
          <div class="hub-section-heading">Description</div>
          <div class="hub-full-desc">${esc(m.description || 'No description provided.')}</div>
        </div>

        <div class="hub-detail-section">
          <div class="hub-section-heading">Package Details</div>
          <div class="hub-kv-grid">
            <div class="hub-kv-item">
              <span class="hub-kv-label">Archive Size</span>
              <span class="hub-kv-val">${esc(formatBytes(m.size))}</span>
            </div>
            <div class="hub-kv-item">
              <span class="hub-kv-label">SHA-256</span>
              <span class="hub-kv-val font-mono">${esc(m.sha256 || '—')}</span>
            </div>
          </div>
        </div>
      </div>`;

    hydrateHubIcons(body);

    body.querySelector('#btn-hub-back')?.addEventListener('click', () => {
      if (hubState.busy) return;
      hubState.selectedId = null;
      hubState.actionError = '';
      renderHub();
    });

    body.querySelector('#btn-hub-install')?.addEventListener('click', () => runInstall(m, false));
    body.querySelector('#btn-hub-update')?.addEventListener('click', () => runInstall(m, false));
    body.querySelector('#btn-hub-overwrite')?.addEventListener('click', () => {
      S.showConfirm(
        'Overwrite Local Mod',
        `"${m.name || m.id}" already exists in your mods folder and was not installed from Mod Hub. Replace it with the Hub version? (Your enabled/disabled setting will be kept.)`,
        'Replace Mod',
        () => runInstall(m, true)
      );
    });
    body.querySelector('#btn-hub-uninstall')?.addEventListener('click', () => {
      S.showConfirm(
        'Uninstall Mod',
        `Move "${m.name || m.id}" to wololoader/trash? You can restore it from there if needed.`,
        'Uninstall',
        () => runUninstall(m)
      );
    });
    body.querySelector('#btn-hub-open-editor')?.addEventListener('click', () => {
      S.loadMod(m.id);
    });

    body.querySelectorAll('.hub-dep-row.clickable[data-dep-id]').forEach(el => {
      el.addEventListener('click', () => {
        if (hubState.busy) return;
        hubState.selectedId = el.dataset.depId;
        hubState.actionError = '';
        renderHub();
      });
    });
  }

  async function runInstall(m, overwrite) {
    hubState.busy = true;
    hubState.actionError = '';
    const depsNote = (m.depends && m.depends.length) ? ' and dependencies' : '';
    hubState.busyText = `${m.installed ? 'Updating' : 'Installing'} ${m.name || m.id}${depsNote} (downloading & verifying SHA-256)...`;
    renderHub();

    try {
      const res = await S.api('/api/hub/install', {
        method: 'POST',
        body: { id: m.id, overwrite: !!overwrite }
      });
      await S.refreshModsList();
      hubState.index = await S.api('/api/hub/index');
      updateTabBadge();
      const depMsg = (res.installed_depends && res.installed_depends.length)
        ? ` (also installed: ${res.installed_depends.join(', ')})`
        : '';
      S.showToast(`${res.updated ? 'Updated' : 'Installed'} "${m.name || m.id}" v${res.version}${depMsg}`, 'success', 4500);
    } catch (err) {
      hubState.actionError = err.message || 'Installation failed.';
      S.showToast('Install failed: ' + hubState.actionError, 'error', 6000);
    } finally {
      hubState.busy = false;
      hubState.busyText = '';
      renderHub();
    }
  }

  async function runUninstall(m) {
    hubState.busy = true;
    hubState.actionError = '';
    hubState.busyText = `Moving ${m.name || m.id} to wololoader/trash...`;
    renderHub();

    try {
      await S.api('/api/hub/uninstall', {
        method: 'POST',
        body: { id: m.id }
      });
      await S.refreshModsList();
      hubState.index = await S.api('/api/hub/index');
      updateTabBadge();
      S.showToast(`Uninstalled "${m.name || m.id}" (moved to trash).`, 'success');
    } catch (err) {
      hubState.actionError = err.message || 'Uninstall failed.';
      S.showToast('Uninstall failed: ' + hubState.actionError, 'error', 6000);
    } finally {
      hubState.busy = false;
      hubState.busyText = '';
      renderHub();
    }
  }

  async function openHubSettingsModal() {
    const modal = document.getElementById('modal-container');
    if (!modal) return;

    let settings = {};
    try {
      settings = await S.api('/api/settings');
    } catch (e) {
      settings = {};
    }

    const defaultUrl = hubState.index?.default_url || 'https://raw.githubusercontent.com/Vitrie32/wololoader-mods/main/index.json';
    const currentCfg = settings.hub_url || '';
    const envOverride = !!hubState.index?.env_override;
    const effectiveUrl = hubState.index?.source_url || currentCfg || defaultUrl;

    modal.innerHTML = `
      <div class="modal-dialog">
        <div class="modal-header">
          <h4 class="modal-title">Mod Hub Settings</h4>
          <button class="btn-modal-close" id="modal-btn-close">&times;</button>
        </div>
        <div class="modal-body">
          ${envOverride ? `<div class="hub-error-banner" style="background:var(--accent-soft);border-color:var(--gold-border);color:var(--gold-highlight);">Environment variable <code>WL_HUB_URL</code> is currently overriding the configured URL:<br><span class="font-mono">${esc(effectiveUrl)}</span></div>` : ''}
          <div class="form-group">
            <label for="hub-url-input">Hub Index URL</label>
            <input type="text" id="hub-url-input" class="form-input font-mono" placeholder="${esc(defaultUrl)}" value="${esc(currentCfg)}">
            <span class="hub-kv-label">Leave empty to use the default public index. Supports https://, http://, file://, or a local path.</span>
          </div>
        </div>
        <div class="modal-footer">
          <button type="button" class="btn btn-secondary btn-sm" id="modal-btn-reset">Use Default</button>
          <button type="button" class="btn btn-secondary btn-sm" id="modal-btn-cancel">Cancel</button>
          <button type="button" class="btn btn-gold btn-sm" id="modal-btn-save">Save & Refresh</button>
        </div>
      </div>`;
    modal.style.display = 'flex';

    const close = () => { modal.style.display = 'none'; modal.innerHTML = ''; };
    modal.querySelector('#modal-btn-close').onclick = close;
    modal.querySelector('#modal-btn-cancel').onclick = close;
    modal.querySelector('#modal-btn-reset').onclick = () => {
      modal.querySelector('#hub-url-input').value = '';
    };
    modal.querySelector('#modal-btn-save').onclick = async () => {
      const nextUrl = modal.querySelector('#hub-url-input').value.trim();
      const updated = { ...settings };
      if (nextUrl) updated.hub_url = nextUrl;
      else delete updated.hub_url;
      try {
        await S.api('/api/settings', { method: 'PUT', body: updated });
        close();
        hubState.selectedId = null;
        hubState.index = null;
        await fetchIndex(true);
        S.showToast('Mod Hub URL updated.', 'success');
      } catch (err) {
        S.showToast('Could not save settings: ' + err.message, 'error');
      }
    };
  }

  function setup() {
    document.getElementById('tab-my-mods')?.addEventListener('click', () => setHomeTab(false));
    document.getElementById('tab-mod-hub')?.addEventListener('click', () => setHomeTab(true));

    const search = document.getElementById('hub-search');
    const clear = document.getElementById('hub-search-clear');
    search?.addEventListener('input', () => {
      hubState.search = search.value;
      if (clear) clear.style.display = search.value ? 'block' : 'none';
      renderHub();
    });
    clear?.addEventListener('click', () => {
      hubState.search = '';
      if (search) search.value = '';
      clear.style.display = 'none';
      renderHub();
    });

    document.getElementById('hub-filter')?.addEventListener('change', (e) => {
      hubState.filter = e.target.value;
      renderHub();
    });
    document.getElementById('hub-tag-filter')?.addEventListener('change', (e) => {
      hubState.tag = e.target.value;
      renderHub();
    });
    document.getElementById('btn-hub-refresh')?.addEventListener('click', () => {
      if (!hubState.busy) fetchIndex(true);
    });
    document.getElementById('btn-hub-settings')?.addEventListener('click', openHubSettingsModal);
  }

  window.ModHub = {
    open() { setHomeTab(true); },
    close() { setHomeTab(false); },
    refresh() { return fetchIndex(true); }
  };

  document.addEventListener('DOMContentLoaded', setup);
})();

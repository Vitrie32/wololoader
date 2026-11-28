// Icon library picker: a searchable grid of the icons that ship with Scriptorium
// (scriptorium/icon_library, served by GET /api/icon-library and /api/icon-library/<category>/<id>.png).
//
//   ScIconLib.pick({
//     title: 'Pick a command icon',
//     dest: entry => `icons/commands/${entry.id}.png`,   // optional: copy the PNG into the mod (PUT /api/file)
//     onPicked: (entry, info) => {}                       // info = { path, blob }; path is null without dest
//   });
//
// It opens its own overlay (not #modal-container), so it can sit on top of another dialog.
(function () {
  'use strict';
  const S = window.Scriptorium;
  if (!S) return;

  const CATEGORY_LABELS = { units: 'Units', buildings: 'Buildings', general: 'General' };
  let catalogPromise = null;
  const blobUrls = new Map();   // png path -> Promise<blob URL>
  const blobs = new Map();      // png path -> Promise<Blob>

  const esc = s => S.escapeHtml(String(s === undefined || s === null ? '' : s));

  function loadCatalog() {
    if (!catalogPromise) {
      catalogPromise = S.api('/api/icon-library').then(list => {
        if (!Array.isArray(list)) throw new Error('Bad icon library catalog');
        return list;
      }).catch(err => { catalogPromise = null; throw err; });
    }
    return catalogPromise;
  }

  // The PNG as a Blob (the API needs the token header, so <img src> cannot load it directly).
  function loadBlob(entry) {
    if (!blobs.has(entry.png)) {
      blobs.set(entry.png, S.api('/api/icon-library/' + entry.png).then(r => r.blob())
        .catch(err => { blobs.delete(entry.png); throw err; }));
    }
    return blobs.get(entry.png);
  }

  function loadUrl(entry) {
    if (!blobUrls.has(entry.png)) {
      blobUrls.set(entry.png, loadBlob(entry).then(b => URL.createObjectURL(b)).catch(() => { blobUrls.delete(entry.png); return null; }));
    }
    return blobUrls.get(entry.png);
  }

  async function copyToMod(entry, destPath) {
    const folder = S.state.currentModFolder;
    if (!folder) throw new Error('No mod is open');
    const blob = await loadBlob(entry);
    await S.api(`/api/file?folder=${encodeURIComponent(folder)}&path=${encodeURIComponent(destPath)}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'image/png' },
      body: blob
    });
    return blob;
  }

  function pick(opts) {
    opts = opts || {};
    document.querySelectorAll('.iconlib-overlay').forEach(el => el.remove());
    const overlay = document.createElement('div');
    overlay.className = 'iconlib-overlay';
    overlay.innerHTML = `
      <div class="iconlib-dialog" role="dialog" aria-modal="true" aria-label="${esc(opts.title || 'Icon library')}">
        <div class="iconlib-head">
          <h3 class="iconlib-title">${esc(opts.title || 'Icon library')}</h3>
          <button type="button" class="btn-modal-close" aria-label="Close">&times;</button>
        </div>
        <div class="iconlib-bar">
          <input type="text" class="form-input iconlib-search" placeholder="Search by name or tag..." autocomplete="off" spellcheck="false" aria-label="Search icons">
          <div class="iconlib-cats" role="group" aria-label="Category"></div>
        </div>
        <div class="iconlib-grid" tabindex="0"><p class="iconlib-empty">Loading...</p></div>
        <div class="iconlib-foot"><span class="iconlib-count"></span><span class="iconlib-err"></span></div>
      </div>`;
    document.body.appendChild(overlay);

    const grid = overlay.querySelector('.iconlib-grid');
    const search = overlay.querySelector('.iconlib-search');
    const catsEl = overlay.querySelector('.iconlib-cats');
    const countEl = overlay.querySelector('.iconlib-count');
    const errEl = overlay.querySelector('.iconlib-err');
    let category = '';
    let list = [];
    let io = null;

    const close = () => {
      document.removeEventListener('keydown', onKey, true);
      if (io) io.disconnect();
      overlay.remove();
    };
    // Capture phase + stopPropagation: Escape must close only this picker, not the dialog under it.
    function onKey(e) {
      if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); close(); }
    }
    document.addEventListener('keydown', onKey, true);
    overlay.addEventListener('mousedown', e => { if (e.target === overlay) close(); });
    overlay.querySelector('.btn-modal-close').onclick = close;

    function render() {
      const q = search.value.trim().toLowerCase();
      const words = q.split(/\s+/).filter(Boolean);
      const shown = list.filter(e => {
        if (category && e.category !== category) return false;
        const hay = (e.name + ' ' + e.id + ' ' + (e.tags || []).join(' ')).toLowerCase();
        return words.every(w => hay.includes(w));
      });
      countEl.textContent = `${shown.length} of ${list.length} icons`;
      if (io) io.disconnect();
      if (!shown.length) { grid.innerHTML = '<p class="iconlib-empty">No icons match.</p>'; return; }
      grid.innerHTML = shown.map((e, i) => `
        <button type="button" class="iconlib-item" data-i="${i}" title="${esc(e.name)}${e.tags && e.tags.length ? ' - ' + esc(e.tags.join(', ')) : ''}">
          <span class="iconlib-img"><img alt="" width="48" height="48"></span>
          <span class="iconlib-name">${esc(e.name)}</span>
        </button>`).join('');
      const items = grid.querySelectorAll('.iconlib-item');
      const fill = btn => {
        const img = btn.querySelector('img');
        if (img.dataset.done) return;
        img.dataset.done = '1';
        loadUrl(shown[Number(btn.dataset.i)]).then(url => { if (url && img.isConnected) img.src = url; });
      };
      if ('IntersectionObserver' in window) {
        io = new IntersectionObserver(ents => ents.forEach(en => { if (en.isIntersecting) { fill(en.target); io.unobserve(en.target); } }), { root: grid, rootMargin: '120px' });
        items.forEach(b => io.observe(b));
      } else {
        items.forEach(fill);
      }
      items.forEach(btn => {
        btn.onclick = () => choose(shown[Number(btn.dataset.i)], btn);
      });
    }

    let busy = false;
    async function choose(entry, btn) {
      if (busy) return;
      busy = true;
      errEl.textContent = '';
      btn.classList.add('busy');
      try {
        let path = null;
        let blob;
        if (typeof opts.dest === 'function') {
          path = opts.dest(entry);
          blob = await copyToMod(entry, path);
        } else {
          blob = await loadBlob(entry);
        }
        close();
        if (opts.onPicked) await opts.onPicked(entry, { path, blob });
      } catch (err) {
        busy = false;
        btn.classList.remove('busy');
        errEl.textContent = 'Could not use this icon: ' + err.message;
      }
    }

    search.addEventListener('input', render);
    setTimeout(() => search.focus(), 30);

    loadCatalog().then(l => {
      if (!overlay.isConnected) return;
      list = l;
      const cats = [''].concat(Array.from(new Set(l.map(e => e.category))));
      catsEl.innerHTML = cats.map(c => `<button type="button" class="iconlib-cat${c === category ? ' active' : ''}" data-cat="${esc(c)}">${esc(c ? (CATEGORY_LABELS[c] || c) : 'All')}</button>`).join('');
      catsEl.querySelectorAll('.iconlib-cat').forEach(b => {
        b.onclick = () => {
          category = b.dataset.cat;
          catsEl.querySelectorAll('.iconlib-cat').forEach(x => x.classList.toggle('active', x === b));
          render();
        };
      });
      render();
    }).catch(err => {
      grid.innerHTML = `<p class="iconlib-empty">Could not load the icon library: ${esc(err.message)}</p>`;
    });
    return close;
  }

  window.ScIconLib = { pick };
})();

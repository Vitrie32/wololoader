/**
 * SlpViewer - the Sprites pane of a mod. Lists the mod's own SLP files (resources/_common/drs/
 * graphics and interface) and, behind the "Game graphics" toggle, the game's files for reference
 * (read only). "+ New sprite from PNG" runs the SLP creator (slpcreator.js) and saves the result
 * into the mod. Export PNG saves a download; nothing else is written.
 *
 * The view is mounted into #sprites-view (index.html). app.js showPane('sprites') shows it,
 * showPane('main') hides it. Depends on: SlpDecode (slpdecode.js), GameData (gamedata.js),
 * SlpCreator (slpcreator.js), Scriptorium (app.js, plus allocateSlpNumber from basic.js).
 */
(function () {
  'use strict';

  const SOURCES = [
    { id: 'graphics', label: 'Graphics', dir: '_common/drs/graphics' },
    { id: 'interface', label: 'Interface', dir: '_common/drs/interface' },
    { id: 'mod', label: 'This mod' },
  ];
  // the mod's own sprite folders (relative to the mod folder, as /api/files takes them)
  const MOD_DIRS = ['resources/_common/drs/graphics', 'resources/_common/drs/interface'];
  const MAX_LIST_ROWS = 400;           // the list is filtered, not paged: keep the DOM small
  const ZOOM_MIN = 1, ZOOM_MAX = 4;
  const CHECK_A = '#6b8e3a', CHECK_B = '#5a7a2f';   // the creator's preview background

  const S = () => window.Scriptorium;

  const st = {
    root: null,            // the Sprites view element (#sprites-view)
    shown: false,          // the Sprites pane is visible
    gameOn: false,         // "Game graphics" toggle: also browse the game's sprites (off by default)
    paletteAsked: false,
    palette: null,
    source: 'mod',
    filter: '',
    items: [],             // [{ name, path, size, mod }]
    truncated: false,
    listNote: '',
    selected: null,        // item
    decoded: null,         // SlpDecode.decode() result of the selected file
    frame: 0,              // absolute frame index
    perDir: 0,             // frames per direction (0 = all frames are one animation)
    zoom: 2,
    player: 0,
    shadow: true,
    outline: false,
    fps: 10,
    timer: null,
    pendingName: null,     // name to select once the list is loaded
  };

  // ==========================================================================
  // Helpers
  // ==========================================================================

  function esc(s) {
    return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function toast(msg, type) {
    if (S() && S().showToast) S().showToast(msg, type || 'info');
  }

  // numbers first in numeric order (123.slp before 1000.slp), then names
  function cmpNames(a, b) {
    const na = parseInt(a.name, 10), nb = parseInt(b.name, 10);
    if (!isNaN(na) && !isNaN(nb) && na !== nb) return na - nb;
    return a.name.localeCompare(b.name, 'en', { numeric: true });
  }

  function currentFrame() {
    return st.decoded ? st.decoded.frames[st.frame] : null;
  }

  // [first, last] frame index of the direction that holds the current frame
  function animRange() {
    const n = st.decoded ? st.decoded.frames.length : 0;
    if (!st.perDir || st.perDir >= n) return [0, n - 1];
    const dir = Math.floor(st.frame / st.perDir);
    return [dir * st.perDir, Math.min(n, (dir + 1) * st.perDir) - 1];
  }

  // ==========================================================================
  // Data
  // ==========================================================================

  async function loadList() {
    const src = SOURCES.find(s => s.id === st.source);
    st.items = [];
    st.truncated = false;
    st.listNote = '';
    renderList('Loading...');
    try {
      if (src.id === 'mod') {
        const folder = S() && S().state.currentModFolder;
        if (!folder) {
          st.listNote = 'Open a mod to see its sprites.';
        } else {
          for (const dir of MOD_DIRS) {
            const names = await S().api(`/api/files?folder=${encodeURIComponent(folder)}&dir=${encodeURIComponent(dir)}`);
            for (const name of names) {
              if (!/\.slp$/i.test(name)) continue;
              st.items.push({ name, mod: folder, path: dir + '/' + name, size: null });
            }
          }
        }
      } else {
        const res = await GameData.list(src.dir);
        for (const f of res.files) {
          if (!/\.slp$/i.test(f.name)) continue;
          st.items.push({ name: f.name, path: src.dir + '/' + f.name, size: f.size });
        }
        st.truncated = !!res.truncated;
      }
    } catch (e) {
      st.listNote = 'Could not list the folder: ' + e.message;
    }
    st.items.sort(cmpNames);
    renderList();
    if (st.pendingName) {
      const want = st.pendingName;
      st.pendingName = null;
      const hit = st.items.find(i => i.name === want);
      if (hit) select(hit);
    }
  }

  // Decodes an item: the game's files are cached by GameData, the mod's are read every time
  // (the user may have edited them).
  async function loadItem(item) {
    if (item.mod) {
      const res = await S().api(`/api/file?folder=${encodeURIComponent(item.mod)}&path=${encodeURIComponent(item.path)}`);
      return SlpDecode.decode(await res.arrayBuffer());
    }
    return GameData.slp(item.path);
  }

  // ==========================================================================
  // Rendering: list
  // ==========================================================================

  function renderList(loadingText) {
    const box = st.root && st.root.querySelector('.slpv-items');
    if (!box) return;
    if (loadingText) { box.innerHTML = `<p class="slpv-note">${esc(loadingText)}</p>`; return; }
    const q = st.filter.trim().toLowerCase();
    const matches = q ? st.items.filter(i => i.name.toLowerCase().includes(q)) : st.items;
    const shown = matches.slice(0, MAX_LIST_ROWS);
    let html = shown.map(i => {
      const sel = st.selected && st.selected.path === i.path && !!st.selected.mod === !!i.mod ? ' selected' : '';
      const size = i.size == null ? '' : (i.size >= 1024 ? Math.round(i.size / 1024) + ' KB' : i.size + ' B');
      return `<button class="slpv-item${sel}" data-path="${esc(i.path)}" data-mod="${esc(i.mod || '')}">` +
        `<span class="slpv-item-name">${esc(i.name)}</span><span class="slpv-item-size">${esc(size)}</span></button>`;
    }).join('');
    if (!html) {
      html = `<p class="slpv-note">${esc(st.listNote || (q ? 'Nothing matches the filter.' : 'No SLP files here.'))}</p>`;
    } else if (st.listNote) {
      html = `<p class="slpv-note">${esc(st.listNote)}</p>` + html;
    }
    if (matches.length > MAX_LIST_ROWS) {
      html += `<p class="slpv-note">Showing ${MAX_LIST_ROWS} of ${matches.length}. Type more of the name or number.</p>`;
    }
    if (st.truncated) html += '<p class="slpv-note">The folder has more files than the list can show.</p>';
    box.innerHTML = html;
    const count = st.root.querySelector('.slpv-count');
    if (count) count.textContent = `${matches.length} of ${st.items.length}`;
  }

  // ==========================================================================
  // Selecting and drawing
  // ==========================================================================

  async function select(item) {
    stopPlay();
    st.selected = item;
    st.decoded = null;
    st.frame = 0;
    st.perDir = 0;
    renderList();
    setTitle(item.name);
    setInfo('Decoding ' + item.name + '...');
    try {
      st.decoded = await loadItem(item);
      st.frame = 0;
      fitZoom();
      setControlsFor();
      redraw();
    } catch (e) {
      st.decoded = null;
      setInfo('Cannot show this file: ' + e.message);
      clearCanvas();
      toast('Cannot show ' + item.name + ': ' + e.message, 'error');
    }
  }

  // Largest zoom (1x..4x) at which the biggest frame fits the stage: small icons open large.
  function fitZoom() {
    const stage = st.root.querySelector('.slpv-stage');
    let w = 1, h = 1;
    for (const f of st.decoded.frames) { w = Math.max(w, f.width); h = Math.max(h, f.height); }
    const z = Math.floor(Math.min((stage.clientWidth - 40) / w, (stage.clientHeight - 40) / h));
    st.zoom = Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, z));
    st.root.querySelector('.slpv-zoom-label').textContent = st.zoom + 'x';
  }

  function setTitle(name) {
    const t = st.root.querySelector('.slpv-title');
    if (t) t.textContent = name || 'Sprites';
  }

  function setInfo(text) {
    const el = st.root.querySelector('.slpv-info');
    if (el) el.textContent = text;
  }

  // scrubber, direction and play controls for the decoded file
  function setControlsFor() {
    const n = st.decoded ? st.decoded.frames.length : 0;
    const scrub = st.root.querySelector('.slpv-scrub');
    const dirSel = st.root.querySelector('.slpv-dir');
    const perDirInput = st.root.querySelector('.slpv-perdir');
    const play = st.root.querySelector('.slpv-play');
    scrub.max = Math.max(0, n - 1);
    scrub.disabled = n <= 1;
    play.disabled = n <= 1;
    if (perDirInput.value !== String(st.perDir)) perDirInput.value = String(st.perDir);
    const dirs = st.perDir && st.perDir < n ? Math.ceil(n / st.perDir) : 0;
    dirSel.innerHTML = Array.from({ length: dirs }, (_, d) => `<option value="${d}">Direction ${d + 1}</option>`).join('');
    dirSel.hidden = dirs === 0;
    dirSel.value = String(dirs ? Math.floor(st.frame / st.perDir) : 0);
    syncScrub();
  }

  // the scrubber covers the current direction (or the whole file when there are no directions)
  function syncScrub() {
    const scrub = st.root.querySelector('.slpv-scrub');
    const [a, b] = animRange();
    scrub.min = String(a);
    scrub.max = String(Math.max(a, b));
    scrub.value = String(st.frame);
    const label = st.root.querySelector('.slpv-frame-label');
    const n = st.decoded ? st.decoded.frames.length : 0;
    if (label) {
      label.textContent = n ? `Frame ${st.frame + 1} of ${n}` +
        (st.perDir && st.perDir < n ? ` (direction ${Math.floor(st.frame / st.perDir) + 1}, frame ${st.frame % st.perDir + 1})` : '') : '';
    }
  }

  function clearCanvas() {
    const c = st.root.querySelector('.slpv-canvas');
    c.width = 1; c.height = 1;
  }

  // the frame as an unscaled canvas with its own alpha (used for drawing and export)
  function frameCanvas(frame) {
    const rgba = SlpDecode.render(frame, st.palette, { player: st.player, shadow: st.shadow });
    const off = document.createElement('canvas');
    off.width = frame.width;
    off.height = frame.height;
    off.getContext('2d').putImageData(new ImageData(rgba, frame.width, frame.height), 0, 0);
    return off;
  }

  function redraw() {
    const frame = currentFrame();
    if (!frame || !st.palette) { clearCanvas(); return; }
    const z = st.zoom;
    const W = frame.width * z, H = frame.height * z;
    const canvas = st.root.querySelector('.slpv-canvas');
    canvas.width = W;
    canvas.height = H;
    canvas.style.width = W + 'px';
    canvas.style.height = H + 'px';
    const ctx = canvas.getContext('2d');
    ctx.imageSmoothingEnabled = false;

    // grass checker behind the sprite (display only; export has no background)
    const tile = 8;
    for (let y = 0; y < H; y += tile) {
      for (let x = 0; x < W; x += tile) {
        ctx.fillStyle = ((x + y) / tile) % 2 === 0 ? CHECK_A : CHECK_B;
        ctx.fillRect(x, y, tile, tile);
      }
    }
    ctx.drawImage(frameCanvas(frame), 0, 0, W, H);

    if (st.outline) drawOutline(ctx, frame, z);
    drawHotspot(ctx, frame, z, W, H);
    updateInfo(frame);
  }

  // dashed box around the opaque pixels of the frame
  function drawOutline(ctx, frame, z) {
    let x0 = frame.width, y0 = frame.height, x1 = -1, y1 = -1;
    for (let y = 0; y < frame.height; y++) {
      for (let x = 0; x < frame.width; x++) {
        if (frame.kind[y * frame.width + x] === SlpDecode.TRANSPARENT) continue;
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
      }
    }
    if (x1 < 0) return;
    ctx.save();
    ctx.setLineDash([4, 3]);
    ctx.strokeStyle = '#e2b354';
    ctx.lineWidth = 1;
    ctx.strokeRect(x0 * z + 0.5, y0 * z + 0.5, (x1 - x0 + 1) * z - 1, (y1 - y0 + 1) * z - 1);
    ctx.restore();
  }

  // red cross at the hotspot, the point the game draws the sprite from
  function drawHotspot(ctx, frame, z, W, H) {
    const cx = frame.hotspotX * z + z / 2, cy = frame.hotspotY * z + z / 2;
    ctx.save();
    ctx.strokeStyle = '#ff4d4d';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(cx + 0.5, 0); ctx.lineTo(cx + 0.5, H);
    ctx.moveTo(0, cy + 0.5); ctx.lineTo(W, cy + 0.5);
    ctx.stroke();
    ctx.restore();
  }

  function updateInfo(frame) {
    const n = st.decoded.frames.length;
    const parts = [
      `${frame.width} × ${frame.height} px`,
      `hotspot ${frame.hotspotX}, ${frame.hotspotY}`,
      `${n} frame${n === 1 ? '' : 's'}`,
    ];
    if (st.decoded.comment) parts.push('"' + st.decoded.comment + '"');
    let text = parts.join('  ·  ');
    if (frame.unsupportedRows) {
      text += `\n${frame.unsupportedRows} row(s) of this frame use extended commands (0x4E / 0x5E); they are left transparent.`;
    }
    setInfo(text);
  }

  // ==========================================================================
  // Playback
  // ==========================================================================

  function stopPlay() {
    if (st.timer) clearInterval(st.timer);
    st.timer = null;
    const btn = st.root && st.root.querySelector('.slpv-play');
    if (btn) btn.textContent = 'Play';
  }

  function startPlay() {
    if (!st.decoded || st.decoded.frames.length < 2) return;
    stopPlay();
    st.timer = setInterval(() => {
      const [a, b] = animRange();
      st.frame = st.frame >= b || st.frame < a ? a : st.frame + 1;
      syncScrub();
      redraw();
    }, Math.round(1000 / st.fps));
    st.root.querySelector('.slpv-play').textContent = 'Pause';
  }

  function togglePlay() {
    if (st.timer) stopPlay(); else startPlay();
  }

  // ==========================================================================
  // Export
  // ==========================================================================

  function exportPng() {
    const frame = currentFrame();
    if (!frame || !st.palette) return;
    frameCanvas(frame).toBlob((blob) => {
      if (!blob) { toast('Could not make the PNG', 'error'); return; }
      const base = (st.selected ? st.selected.name : 'sprite').replace(/\.slp$/i, '');
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = `${base}-frame${st.frame + 1}.png`;
      document.body.appendChild(a);
      a.click();
      a.remove();
      setTimeout(() => URL.revokeObjectURL(a.href), 2000);
    }, 'image/png');
  }

  // ==========================================================================
  // Window
  // ==========================================================================

  function buildSwatches() {
    const box = st.root.querySelector('.slpv-swatches');
    box.innerHTML = Array.from({ length: 8 }, (_, p) => {
      // the ramp's middle shade shows the player's colour
      const c = st.palette[16 + 16 * p + 4];
      return `<button class="slpv-swatch${p === st.player ? ' active' : ''}" data-player="${p}" ` +
        `title="Player ${p + 1}" style="background:rgb(${c[0]},${c[1]},${c[2]})"></button>`;
    }).join('');
  }

  function build(root) {
    root.classList.add('slpv-view');
    root.innerHTML = `
      <div class="slpv-body">
        <aside class="slpv-side">
          <div class="slpv-side-top">
            <button class="btn btn-gold btn-sm slpv-new" title="Make a sprite from a PNG or JPG with the SLP creator. It is saved in this mod.">+ New sprite from PNG</button>
          </div>
          <label class="slpv-check slpv-game-toggle" title="Also browse the game's own sprites, for reference. Read only.">
            <input type="checkbox" class="slpv-game"> Game graphics
          </label>
          <div class="slpv-sources" hidden>
            ${SOURCES.filter(s => s.id !== 'mod').map(s => `<button class="slpv-source" data-source="${s.id}">${esc(s.label)}</button>`).join('')}
          </div>
          <div class="slpv-filter-row">
            <input type="text" class="slpv-filter form-input" placeholder="Filter by number or name" autocomplete="off">
            <span class="slpv-count"></span>
          </div>
          <div class="slpv-items"></div>
        </aside>
        <section class="slpv-main">
          <header class="slpv-head">
            <h3 class="slpv-title">Sprites</h3>
            <span class="slpv-hint">Game files are never changed.</span>
          </header>
          <div class="slpv-toolbar">
            <div class="slpv-group">
              <button class="btn btn-wood btn-sm slpv-zoom-out" title="Zoom out">−</button>
              <span class="slpv-zoom-label">${st.zoom}x</span>
              <button class="btn btn-wood btn-sm slpv-zoom-in" title="Zoom in">+</button>
            </div>
            <div class="slpv-group">
              <label class="slpv-check"><input type="checkbox" class="slpv-shadow" checked> Shadow</label>
              <label class="slpv-check"><input type="checkbox" class="slpv-outline"> Outline</label>
            </div>
            <div class="slpv-group">
              <span class="slpv-label">Player colour</span>
              <span class="slpv-swatches"></span>
            </div>
            <div class="slpv-group">
              <button class="btn btn-wood btn-sm slpv-export">Export PNG</button>
            </div>
          </div>
          <div class="slpv-stage"><canvas class="slpv-canvas"></canvas></div>
          <div class="slpv-controls">
            <button class="btn btn-wood btn-sm slpv-play" disabled>Play</button>
            <label class="slpv-label">FPS <input type="range" class="slpv-fps" min="1" max="30" value="${st.fps}"><span class="slpv-fps-label">${st.fps}</span></label>
            <label class="slpv-label">Frames per direction <input type="number" class="slpv-perdir form-input" min="0" max="999" value="0" title="0 = one animation of all frames"></label>
            <select class="slpv-dir form-input" hidden></select>
            <input type="range" class="slpv-scrub" min="0" max="0" value="0" step="1">
            <span class="slpv-frame-label"></span>
          </div>
          <p class="slpv-info">Pick a sprite on the left to look at it.</p>
        </section>
      </div>`;
    st.root = root;

    const q = (sel) => root.querySelector(sel);
    q('.slpv-new').addEventListener('click', () => createFromPng());
    q('.slpv-game').addEventListener('change', (e) => setGameShown(e.target.checked));
    root.querySelectorAll('.slpv-source').forEach(btn => btn.addEventListener('click', () => setSource(btn.dataset.source)));
    q('.slpv-filter').addEventListener('input', (e) => { st.filter = e.target.value; renderList(); });
    q('.slpv-items').addEventListener('click', (e) => {
      const btn = e.target.closest('.slpv-item');
      if (!btn) return;
      const item = st.items.find(i => i.path === btn.dataset.path && (i.mod || '') === btn.dataset.mod);
      if (item) select(item);
    });

    q('.slpv-zoom-in').addEventListener('click', () => setZoom(st.zoom + 1));
    q('.slpv-zoom-out').addEventListener('click', () => setZoom(st.zoom - 1));
    q('.slpv-shadow').addEventListener('change', (e) => { st.shadow = e.target.checked; redraw(); });
    q('.slpv-outline').addEventListener('change', (e) => { st.outline = e.target.checked; redraw(); });
    q('.slpv-swatches').addEventListener('click', (e) => {
      const btn = e.target.closest('.slpv-swatch');
      if (!btn) return;
      st.player = Number(btn.dataset.player);
      root.querySelectorAll('.slpv-swatch').forEach(b => b.classList.toggle('active', b === btn));
      redraw();
    });
    q('.slpv-export').addEventListener('click', exportPng);

    q('.slpv-play').addEventListener('click', togglePlay);
    q('.slpv-fps').addEventListener('input', (e) => {
      st.fps = Number(e.target.value);
      q('.slpv-fps-label').textContent = String(st.fps);
      if (st.timer) startPlay();
    });
    q('.slpv-perdir').addEventListener('change', (e) => {
      stopPlay();
      const n = st.decoded ? st.decoded.frames.length : 0;
      st.perDir = Math.max(0, Math.min(n, parseInt(e.target.value, 10) || 0));
      st.frame = st.perDir ? Math.floor(st.frame / st.perDir) * st.perDir : st.frame;
      setControlsFor();
      redraw();
    });
    q('.slpv-dir').addEventListener('change', (e) => {
      stopPlay();
      st.frame = Number(e.target.value) * st.perDir;
      syncScrub();
      redraw();
    });
    q('.slpv-scrub').addEventListener('input', (e) => {
      st.frame = Number(e.target.value);
      syncScrub();
      redraw();
    });
  }

  function markSource() {
    st.root.querySelectorAll('.slpv-source').forEach(b => b.classList.toggle('active', b.dataset.source === st.source));
    st.root.querySelector('.slpv-game').checked = st.gameOn;
    st.root.querySelector('.slpv-sources').hidden = !st.gameOn;
  }

  function setSource(id) {
    st.source = id;
    st.filter = '';
    st.root.querySelector('.slpv-filter').value = '';
    markSource();
    loadList();
  }

  // "Game graphics" toggle: off shows only the mod's sprites (the default)
  function setGameShown(on) {
    st.gameOn = on;
    if (!on) st.source = 'mod';
    else if (st.source === 'mod') st.source = 'graphics';
    setSource(st.source);
  }

  function setZoom(z) {
    st.zoom = Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, z));
    st.root.querySelector('.slpv-zoom-label').textContent = st.zoom + 'x';
    redraw();
  }

  // ==========================================================================
  // Creating a sprite (the SLP creator, slpcreator.js, is the only creator)
  // ==========================================================================

  /**
   * Makes an SLP from a PNG or JPG with SlpCreator and saves it as a new file in this mod:
   * resources/_common/drs/graphics/<n>.slp, n from Scriptorium.allocateSlpNumber (basic.js).
   * Resolves to { num, path, info }, or null when the creator is cancelled or the save fails.
   * The Sprites view lists and selects the new file. the Basic workspace's element editors call this too.
   * opts: { title, kind ('building' | 'animation' | 'sprite'), frames, directions }
   */
  function createFromPng(opts = {}) {
    return new Promise((resolve) => {
      const app = S();
      if (typeof SlpCreator === 'undefined') { toast('SLP Creator not loaded', 'error'); resolve(null); return; }
      const data = app && app.state.currentModData;
      if (!data) { toast('Open a mod first', 'error'); resolve(null); return; }
      SlpCreator.open({
        title: opts.title || 'New sprite from PNG',
        kind: opts.kind || 'sprite',
        frames: opts.frames,
        directions: opts.directions,
        onCancel: () => resolve(null),
        onDone: async (slpBytes, info) => {
          try {
            const num = await app.allocateSlpNumber();
            const path = `resources/_common/drs/graphics/${num}.slp`;
            await app.api(`/api/file?folder=${encodeURIComponent(data.folder)}&path=${encodeURIComponent(path)}`, {
              method: 'PUT',
              body: slpBytes
            });
            toast(`Created SLP #${num} (${info.width}×${info.height})`, 'success');
            if (st.root && st.shown) {
              st.gameOn = false;
              st.pendingName = `${num}.slp`;
              setSource('mod');
            }
            resolve({ num, path, info });
          } catch (err) {
            toast('Upload failed: ' + (err.message || 'Unknown error'), 'error');
            resolve(null);
          }
        }
      });
    });
  }

  // ==========================================================================
  // Showing and hiding the view (app.js showPane)
  // ==========================================================================

  function show() {
    if (!st.root) return;
    st.shown = true;
    markSource();
    loadList();   // the mod's files may have changed while another pane was open
    if (!st.palette && !st.paletteAsked) {
      st.paletteAsked = true;
      GameData.palette().then((pal) => {
        st.palette = pal;
        buildSwatches();
        if (st.decoded) redraw();
      }).catch((e) => { st.paletteAsked = false; toast('Could not read the game palette: ' + e.message, 'error'); });
    }
  }

  function hide() {
    stopPlay();
    st.shown = false;
  }

  // another mod was opened (or the home list): forget the selection of the previous one
  function resetForMod() {
    stopPlay();
    st.selected = null;
    st.decoded = null;
    st.pendingName = null;
    st.frame = 0;
    if (!st.root) return;
    clearCanvas();
    setTitle('Sprites');
    setInfo('Pick a sprite on the left to look at it.');
    if (st.shown) loadList();
  }

  /**
   * Opens the Sprites pane. Optional: { source: 'mod' | 'graphics' | 'interface', name: '123.slp' }
   * picks the list and selects that file once the list is loaded. Used by the Basic workspace's "View sprites".
   */
  function open(opts = {}) {
    if (opts.source && SOURCES.some(s => s.id === opts.source)) {
      st.gameOn = opts.source !== 'mod';
      st.source = opts.source;
    }
    if (opts.name) st.pendingName = opts.name;
    if (S() && S().showPane) S().showPane('sprites');
    else show();
  }

  // back to the editor's main pane
  function close() {
    if (S() && S().showPane) S().showPane('main');
    else hide();
  }

  const SlpViewer = { open, close, show, hide, createFromPng };

  if (typeof window !== 'undefined') {
    window.SlpViewer = SlpViewer;
    document.addEventListener('DOMContentLoaded', () => {
      const root = document.getElementById('sprites-view');
      if (root) build(root);
      if (S()) S().onModLoaded(resetForMod);
    });
  }
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = SlpViewer;
  }
})();

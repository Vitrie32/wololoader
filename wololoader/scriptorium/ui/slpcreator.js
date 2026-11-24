/**
 * SlpCreator — PNG/JPG to Age of Empires II SLP 2.0N converter
 * Vanilla ES2020, no dependencies, offline-first, byte-identical to slpwrite.py
 */

(function() {
  'use strict';

  const SlpCreator = {};

  // ==========================================================================
  // SLP 2.0N Encoder (port of slpwrite.py)
  // ==========================================================================

  function _runs(row) {
    const out = [];
    for (const px of row) {
      const kind = px === null ? 't' : px[0];
      if (out.length > 0 && out[out.length - 1][0] === kind && out[out.length - 1][1].length < 4095) {
        out[out.length - 1][1].push(px);
      } else {
        out.push([kind, [px]]);
      }
    }
    return out;
  }

  function _encodeRow(row) {
    const w = row.length;
    let left = 0;
    while (left < w && row[left] === null) left++;
    if (left === w) {
      return { left: 0x8000, right: 0x8000, data: new Uint8Array([0x0F]) };
    }
    let right = 0;
    while (row[w - 1 - right] === null) right++;

    const b = [];
    for (const [kind, pxs] of _runs(row.slice(left, w - right))) {
      let n = pxs.length;
      if (kind === 't') {
        while (n > 0) {
          const k = Math.min(n, 4095);
          if (k < 64) {
            b.push((k << 2) | 1);
          } else {
            b.push(((k >> 8) << 4) | 3, k & 0xFF);
          }
          n -= k;
        }
      } else if (kind === 'c') {
        let i = 0;
        while (i < n) {
          const k = Math.min(n - i, 4095);
          if (k < 64) {
            b.push(k << 2);
          } else {
            b.push(((k >> 8) << 4) | 2, k & 0xFF);
          }
          for (let j = 0; j < k; j++) {
            b.push(pxs[i + j][1]);
          }
          i += k;
        }
      } else if (kind === 'p' || kind === 's') {
        const op = kind === 'p' ? 0x06 : 0x0B;
        let i = 0;
        while (i < n) {
          const k = Math.min(n - i, 255);
          if (k < 16) {
            b.push((k << 4) | op);
          } else {
            b.push(op, k);
          }
          if (kind === 'p') {
            for (let j = 0; j < k; j++) {
              b.push(pxs[i + j][1]);
            }
          }
          i += k;
        }
      }
    }
    b.push(0x0F);
    return { left, right, data: new Uint8Array(b) };
  }

  SlpCreator.encode = function(frames, comment = 'Wololoader') {
    // frames: [{ rows: [[px...]], hx, hy }, ...]
    const commentBytes = new TextEncoder().encode(comment.substring(0, 24));
    const commentPadded = new Uint8Array(24);
    commentPadded.set(commentBytes);

    const headBuf = new ArrayBuffer(32);
    const headView = new DataView(headBuf);
    const headBytes = new Uint8Array(headBuf);
    headBytes.set(new TextEncoder().encode('2.0N'), 0);
    headView.setInt32(4, frames.length, true);
    headBytes.set(commentPadded, 8);

    const infoSize = 32 * frames.length;
    const allBodyParts = [];
    const infos = [];
    let currentBodyOffset = headBytes.length + infoSize;

    for (const { rows, hx, hy, properties } of frames) {
      const h = rows.length;
      const w = rows[0].length;
      const encoded = rows.map(r => _encodeRow(r));

      // Outline table
      const outlineOff = currentBodyOffset;
      const outlineBuf = new ArrayBuffer(4 * h);
      const outlineView = new DataView(outlineBuf);
      for (let i = 0; i < h; i++) {
        outlineView.setUint16(i * 4, encoded[i].left, true);
        outlineView.setUint16(i * 4 + 2, encoded[i].right, true);
      }
      const outlineBytes = new Uint8Array(outlineBuf);

      // Command table
      const cmdTabOff = currentBodyOffset + outlineBytes.length;
      const cmdTabBuf = new ArrayBuffer(4 * h);
      const cmdTabView = new DataView(cmdTabBuf);
      const cmdTabBytes = new Uint8Array(cmdTabBuf);

      // Command data - calculate offsets and collect
      const cmdDataParts = [];
      let cmdDataOffset = cmdTabOff + cmdTabBytes.length;
      for (let i = 0; i < h; i++) {
        cmdTabView.setUint32(i * 4, cmdDataOffset, true);
        cmdDataParts.push(encoded[i].data);
        cmdDataOffset += encoded[i].data.length;
      }

      // Add this frame's body parts to the global list
      allBodyParts.push(outlineBytes);
      allBodyParts.push(cmdTabBytes);
      allBodyParts.push(...cmdDataParts);

      // Advance offset for next frame
      currentBodyOffset = cmdDataOffset;

      // Frame info
      const infoBuf = new ArrayBuffer(32);
      const infoView = new DataView(infoBuf);
      infoView.setUint32(0, cmdTabOff, true);
      infoView.setUint32(4, outlineOff, true);
      infoView.setUint32(8, 0, true);
      infoView.setUint32(12, properties || 0, true);
      infoView.setInt32(16, w, true);
      infoView.setInt32(20, h, true);
      infoView.setInt32(24, hx, true);
      infoView.setInt32(28, hy, true);
      infos.push(new Uint8Array(infoBuf));
    }

    const totalLength = headBytes.length + infoSize + allBodyParts.reduce((sum, p) => sum + p.length, 0);
    const result = new Uint8Array(totalLength);
    let offset = 0;
    result.set(headBytes, offset);
    offset += headBytes.length;
    for (const info of infos) {
      result.set(info, offset);
      offset += info.length;
    }
    for (const part of allBodyParts) {
      result.set(part, offset);
      offset += part.length;
    }

    return result;
  };

  // ==========================================================================
  // Color Quantization (port of to_palette.py)
  // ==========================================================================

  const USABLE_INDICES = (() => {
    const arr = [];
    for (let i = 1; i < 256; i++) {
      if (i < 16 || i > 23) arr.push(i);
    }
    return arr;
  })();

  // UI icon palette indices: exclude index 0 (transparent), all 8 player-colour ramps
  // (16..23, 32..39, 48..55, 64..71, 80..87, 96..103, 112..119, 128..135), and 240..255
  // (special UI/minimap indices that render with wrong colours on HD building icons).
  const ICON_USABLE_INDICES = (() => {
    const arr = [];
    for (let i = 1; i < 240; i++) {
      if (i >= 16 && i < 136 && (i % 16) < 8) continue;
      arr.push(i);
    }
    return arr;
  })();

  function rgbToLab(r, g, b) {
    const lin = v => {
      v /= 255.0;
      return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
    };
    const rLin = lin(r), gLin = lin(g), bLin = lin(b);
    const X = (0.4124 * rLin + 0.3576 * gLin + 0.1805 * bLin) / 0.95047;
    const Y = 0.2126 * rLin + 0.7152 * gLin + 0.0722 * bLin;
    const Z = (0.0193 * rLin + 0.1192 * gLin + 0.9505 * bLin) / 1.08883;
    const f = t => t > 0.008856 ? Math.pow(t, 1/3) : 7.787 * t + 16/116;
    return [116 * f(Y) - 16, 500 * (f(X) - f(Y)), 200 * (f(Y) - f(Z))];
  }

  SlpCreator.quantize = function(rgba, w, h, palette, opts = {}) {
    const labCache = new Map();
    const getLabForPalette = i => {
      if (!labCache.has(i)) {
        labCache.set(i, rgbToLab(palette[i][0], palette[i][1], palette[i][2]));
      }
      return labCache.get(i);
    };

    const usable = opts.icon ? ICON_USABLE_INDICES : USABLE_INDICES;
    const nearestCache = new Map();
    const nearest = (r, g, b) => {
      const key = (r << 16) | (g << 8) | b;
      if (nearestCache.has(key)) return nearestCache.get(key);
      const L = rgbToLab(r, g, b);
      let best = usable[0], bestDist = 1e30;
      for (const i of usable) {
        const pL = getLabForPalette(i);
        const d = (pL[0] - L[0]) ** 2 + (pL[1] - L[1]) ** 2 + (pL[2] - L[2]) ** 2;
        if (d < bestDist) {
          bestDist = d;
          best = i;
        }
      }
      nearestCache.set(key, best);
      return best;
    };

    const rows = [];
    for (let y = 0; y < h; y++) {
      const row = [];
      for (let x = 0; x < w; x++) {
        const idx = (y * w + x) * 4;
        const r = rgba[idx], g = rgba[idx + 1], b = rgba[idx + 2], a = rgba[idx + 3];

        // Shadow first: a half-transparent dark pixel (50% black = alpha 128) is shadow, not
        // a black colour pixel and not transparent.
        if (opts.shadow && r < 60 && g < 60 && b < 60 && a >= 40 && a <= 200) {
          row.push(['s', 0]);
        } else if (a < 128) {
          row.push(null);
        } else {
          // Player color detection
          if (opts.playerColour && r > 150 && b > 150 && g < 0.6 * Math.min(r, b)) {
            const brightness = (r + g + b) / 3 / 255;
            const shade = Math.round(brightness * 7);
            row.push(['p', Math.max(0, Math.min(7, shade))]);
          } else {
            // Quantize to nearest palette color
            const qR = (r >> 2) << 2;
            const qG = (g >> 2) << 2;
            const qB = (b >> 2) << 2;
            row.push(['c', nearest(qR, qG, qB)]);
          }
        }
      }
      rows.push(row);
    }

    return rows;
  };

  // ==========================================================================
  // Crop (port of to_slp.py)
  // ==========================================================================

  SlpCreator.crop = function(rows, hx, hy) {
    const h = rows.length;
    if (h === 0) return { rows: [[null]], hx: 0, hy: 0 };
    const w = rows[0].length;

    const ys = [];
    for (let y = 0; y < h; y++) {
      if (rows[y].some(px => px !== null)) ys.push(y);
    }
    if (ys.length === 0) return { rows: [[null]], hx: 0, hy: 0 };

    const xs = [];
    for (let x = 0; x < w; x++) {
      if (ys.some(y => rows[y][x] !== null)) xs.push(x);
    }
    if (xs.length === 0) return { rows: [[null]], hx: 0, hy: 0 };

    const x0 = Math.min(...xs), x1 = Math.max(...xs) + 1;
    const y0 = Math.min(...ys), y1 = Math.max(...ys) + 1;

    const cropped = rows.slice(y0, y1).map(r => r.slice(x0, x1));
    return { rows: cropped, hx: hx - x0, hy: hy - y0 };
  };

  // Convert an image (File, Blob, or HTMLImageElement) directly to SLP 2.0N bytes
  SlpCreator.convertImageToSlp = async function(fileOrImg, opts = {}) {
    let img = fileOrImg;
    if (typeof File !== 'undefined' && (fileOrImg instanceof File || fileOrImg instanceof Blob)) {
      img = await new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = (e) => {
          const image = new Image();
          image.onload = () => resolve(image);
          image.onerror = () => reject(new Error('Failed to decode image'));
          image.src = e.target.result;
        };
        reader.onerror = () => reject(new Error('Failed to read image file'));
        reader.readAsDataURL(fileOrImg);
      });
    }

    let pal = opts.palette;
    if (!pal) {
      if (typeof window !== 'undefined' && window.GameData && window.GameData.palette) {
        pal = await window.GameData.palette();
      } else if (typeof window !== 'undefined' && window.Scriptorium && window.Scriptorium.api) {
        pal = await window.Scriptorium.api('/api/palette');
      }
    }
    if (!pal || !Array.isArray(pal) || pal.length !== 256) {
      throw new Error('Game palette not available');
    }

    const isIcon = opts.kind === 'icon';
    const scale = opts.scale || 1.0;
    const w = isIcon ? 36 : Math.round(img.width * scale);
    const h = isIcon ? 36 : Math.round(img.height * scale);

    const tempCanvas = document.createElement('canvas');
    tempCanvas.width = w;
    tempCanvas.height = h;
    const tempCtx = tempCanvas.getContext('2d', { willReadFrequently: true });
    tempCtx.imageSmoothingEnabled = isIcon || opts.scaleMode === 'smooth';
    tempCtx.drawImage(img, 0, 0, w, h);

    const imageData = tempCtx.getImageData(0, 0, w, h);
    const rows = SlpCreator.quantize(imageData.data, w, h, pal, {
      icon: isIcon,
      playerColour: isIcon ? false : (opts.playerColour !== false),
      shadow: isIcon ? false : (opts.shadow !== false)
    });

    let finalFrame;
    if (isIcon) {
      // Vanilla gamedata_x2/50706.slp icon frames use properties = 0x10.
      finalFrame = { rows, hx: 0, hy: 0, properties: 0x10 };
    } else {
      const isBuilding = opts.kind === 'building';
      const hx = Math.round((opts.hotspotX !== undefined ? opts.hotspotX : (w / 2)) * scale);
      const hy = Math.round((opts.hotspotY !== undefined ? opts.hotspotY : (isBuilding ? h * 0.75 : h * 0.85)) * scale);
      finalFrame = SlpCreator.crop(rows, hx, hy);
    }
    const slpBytes = SlpCreator.encode([finalFrame], opts.comment || 'Wololoader');

    return {
      slpBytes,
      width: finalFrame.rows[0].length,
      height: finalFrame.rows.length,
      hx: finalFrame.hx,
      hy: finalFrame.hy
    };
  };

  // ==========================================================================
  // UI Dialog
  // ==========================================================================

  SlpCreator.open = function(config) {
    // onDone(slpBytes, info) when the user creates the SLP; onCancel() when the dialog is closed without it
    const { title, kind, frames = 10, directions = 5, onDone, onCancel } = config;

    const modal = document.getElementById('modal-container');
    if (!modal) return;

    let currentImage = null;
    let currentFrames = kind === 'animation' ? frames : 1;
    let currentDirections = kind === 'animation' ? directions : 1;
    let currentMode = 'single';
    let hotspotX = 0, hotspotY = 0;
    let scale = 1.0;
    let scaleMode = 'nearest';
    let palette = null;
    let playerColour = true;
    let shadow = true;
    let zoom = 2;
    let animFrame = 0;
    let isDraggingHotspot = false;

    modal.style.display = 'flex';
    modal.innerHTML = `
      <div class="modal-dialog slpcreator-dialog">
        <div class="modal-header">
          <h3 class="modal-title">${escapeHtml(title || 'Create SLP from Image')}</h3>
          <button class="btn-modal-close">&times;</button>
        </div>
        <div class="modal-body slpcreator-body">
          <div class="slpcreator-left">
            <div class="slpcreator-section">
              <h4>Image Source</h4>
              <div class="slpcreator-dropzone" id="slp-dropzone">
                <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                  <path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4M17 8l-5-5-5 5M12 3v12"/>
                </svg>
                <p>Drop image here or click to browse</p>
                <input type="file" id="slp-file-input" accept="image/png,image/jpeg,image/jpg,image/webp" style="display:none;">
              </div>
            </div>

            ${kind === 'animation' ? `
              <div class="slpcreator-section">
                <h4>Animation Mode</h4>
                <div class="slpcreator-mode-btns">
                  <button class="slpcreator-mode-btn active" data-mode="single">One picture per frame</button>
                  <button class="slpcreator-mode-btn" data-mode="sheet">Sprite sheet</button>
                </div>
                <div id="slp-sheet-options" style="display:none; margin-top:12px;">
                  <label>Frames (columns): <input type="number" id="slp-frames" min="1" max="100" value="${frames}" style="width:60px;"></label>
                  <label style="margin-top:8px;">Directions (rows): <input type="number" id="slp-directions" min="1" max="8" value="${directions}" style="width:60px;"></label>
                  <p class="slpcreator-note">Frame count should match the game's animation or the unit graphics "frames" will be set to it.</p>
                </div>
              </div>
            ` : ''}

            <details class="sc-advanced" data-pref-key="slpc-options">
              <summary>Conversion options (Player colour, Shadow, Scale)</summary>
              <div class="sc-advanced-body">
                <label class="slpcreator-checkbox">
                  <input type="checkbox" id="slp-opt-pc" ${playerColour ? 'checked' : ''}>
                  <span>Player colour (magenta = team colour)</span>
                </label>
                <label class="slpcreator-checkbox">
                  <input type="checkbox" id="slp-opt-shadow" ${shadow ? 'checked' : ''}>
                  <span>Shadow</span>
                </label>
                <div class="slpcreator-scale-row">
                  <label>Scale: <input type="range" id="slp-scale" min="50" max="200" value="100" step="10" style="width:120px;"></label>
                  <span id="slp-scale-val">100%</span>
                </div>
                <div class="slpcreator-mode-btns" style="margin-top:4px;">
                  <button class="slpcreator-mode-btn slpcreator-scale-mode active" data-scale-mode="nearest">Nearest</button>
                  <button class="slpcreator-mode-btn slpcreator-scale-mode" data-scale-mode="smooth">Smooth</button>
                </div>
              </div>
            </details>
          </div>

          <div class="slpcreator-right">
            <div class="slpcreator-preview-container">
              <canvas id="slp-preview-canvas" class="slpcreator-canvas"></canvas>
              <div id="slp-hotspot-marker" class="slpcreator-hotspot" style="display:none;"></div>
            </div>
            <div class="slpcreator-preview-controls">
              <button class="btn btn-wood btn-sm" id="slp-zoom-out">Zoom -</button>
              <span id="slp-zoom-label">2x</span>
              <button class="btn btn-wood btn-sm" id="slp-zoom-in">Zoom +</button>
              ${kind === 'animation' ? `<button class="btn btn-wood btn-sm" id="slp-play-anim">Play</button>` : ''}
            </div>
          </div>
        </div>
        <div class="modal-footer">
          <button class="btn btn-wood" id="slp-cancel">Cancel</button>
          <button class="btn btn-save" id="slp-create" disabled>Create SLP</button>
        </div>
      </div>
    `;

    function escapeHtml(str) {
      return String(str || '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    }

    const dropzone = document.getElementById('slp-dropzone');
    const fileInput = document.getElementById('slp-file-input');
    const createBtn = document.getElementById('slp-create');
    const canvas = document.getElementById('slp-preview-canvas');
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    const hotspotMarker = document.getElementById('slp-hotspot-marker');

    async function loadPalette() {
      if (palette) return;
      try {
        // through Scriptorium.api: it adds the session token (a bare fetch gets 403)
        palette = await window.Scriptorium.api('/api/palette');
        if (!Array.isArray(palette) || palette.length !== 256) throw new Error('bad palette');
      } catch (e) {
        console.error('Failed to load palette:', e);
        palette = null;
        if (window.Scriptorium) window.Scriptorium.showToast('Could not read the game palette: ' + e.message, 'error');
      }
    }

    function handleFile(file) {
      if (!file || !file.type.startsWith('image/')) return;
      const reader = new FileReader();
      reader.onload = (e) => {
        const img = new Image();
        img.onload = () => {
          currentImage = img;
          if (kind === 'building') {
            hotspotX = Math.floor(img.width / 2);
            hotspotY = Math.floor(img.height * 0.75);
          } else {
            hotspotX = Math.floor(img.width / 2);
            hotspotY = Math.floor(img.height * 0.85);
          }
          updatePreview();
          createBtn.disabled = false;
        };
        img.src = e.target.result;
      };
      reader.readAsDataURL(file);
    }

    dropzone.addEventListener('click', () => fileInput.click());
    dropzone.addEventListener('dragover', (e) => { e.preventDefault(); dropzone.classList.add('dragover'); });
    dropzone.addEventListener('dragleave', () => dropzone.classList.remove('dragover'));
    dropzone.addEventListener('drop', (e) => {
      e.preventDefault();
      dropzone.classList.remove('dragover');
      if (e.dataTransfer.files[0]) handleFile(e.dataTransfer.files[0]);
    });
    fileInput.addEventListener('change', (e) => { if (e.target.files[0]) handleFile(e.target.files[0]); });

    modal.querySelectorAll('.slpcreator-mode-btn[data-mode]').forEach(btn => {
      btn.addEventListener('click', () => {
        modal.querySelectorAll('.slpcreator-mode-btn[data-mode]').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        currentMode = btn.dataset.mode;
        const sheetOpts = document.getElementById('slp-sheet-options');
        if (sheetOpts) sheetOpts.style.display = currentMode === 'sheet' ? 'block' : 'none';
      });
    });

    modal.querySelectorAll('.slpcreator-scale-mode').forEach(btn => {
      btn.addEventListener('click', () => {
        modal.querySelectorAll('.slpcreator-scale-mode').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        scaleMode = btn.dataset.scaleMode;
        updatePreview();
      });
    });

    const scaleSlider = document.getElementById('slp-scale');
    const scaleVal = document.getElementById('slp-scale-val');
    scaleSlider?.addEventListener('input', () => {
      scale = scaleSlider.value / 100;
      scaleVal.textContent = scaleSlider.value + '%';
      updatePreview();
    });

    document.getElementById('slp-opt-pc')?.addEventListener('change', (e) => { playerColour = e.target.checked; updatePreview(); });
    document.getElementById('slp-opt-shadow')?.addEventListener('change', (e) => { shadow = e.target.checked; updatePreview(); });
    document.getElementById('slp-frames')?.addEventListener('input', (e) => { currentFrames = parseInt(e.target.value) || 10; });
    document.getElementById('slp-directions')?.addEventListener('input', (e) => { currentDirections = parseInt(e.target.value) || 5; });

    document.getElementById('slp-zoom-in')?.addEventListener('click', () => { zoom = Math.min(4, zoom + 1); updatePreview(); });
    document.getElementById('slp-zoom-out')?.addEventListener('click', () => { zoom = Math.max(1, zoom - 1); updatePreview(); });

    let animInterval = null;
    document.getElementById('slp-play-anim')?.addEventListener('click', (e) => {
      if (animInterval) {
        clearInterval(animInterval);
        animInterval = null;
        e.target.textContent = 'Play';
      } else {
        animInterval = setInterval(() => {
          animFrame = (animFrame + 1) % currentFrames;
          updatePreview();
        }, 100);
        e.target.textContent = 'Pause';
      }
    });

    function updatePreview() {
      if (!currentImage || !palette) return;

      const w = Math.round(currentImage.width * scale);
      const h = Math.round(currentImage.height * scale);

      const tempCanvas = document.createElement('canvas');
      tempCanvas.width = w;
      tempCanvas.height = h;
      const tempCtx = tempCanvas.getContext('2d', { willReadFrequently: true });
      tempCtx.imageSmoothingEnabled = scaleMode === 'smooth';
      tempCtx.drawImage(currentImage, 0, 0, w, h);

      const imageData = tempCtx.getImageData(0, 0, w, h);
      const rows = SlpCreator.quantize(imageData.data, w, h, palette, { icon: kind === 'icon', playerColour, shadow });

      // Render preview
      const displayW = w * zoom;
      const displayH = h * zoom;
      canvas.width = displayW;
      canvas.height = displayH;

      // Grass checker background
      for (let y = 0; y < displayH; y += 16) {
        for (let x = 0; x < displayW; x += 16) {
          ctx.fillStyle = ((x + y) / 16) % 2 === 0 ? '#6b8e3a' : '#5a7a2f';
          ctx.fillRect(x, y, 16, 16);
        }
      }

      // Draw pixels
      for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
          const px = rows[y][x];
          if (px === null) continue;
          let r, g, b;
          if (px[0] === 'c') {
            [r, g, b] = palette[px[1]];
          } else if (px[0] === 'p') {
            [r, g, b] = palette[16 + px[1]];
          } else if (px[0] === 's') {
            ctx.fillStyle = 'rgba(0,0,0,0.45)';   // the game darkens the ground under shadows
            ctx.fillRect(x * zoom, y * zoom, zoom, zoom);
            continue;
          }
          ctx.fillStyle = `rgb(${r},${g},${b})`;
          ctx.fillRect(x * zoom, y * zoom, zoom, zoom);
        }
      }

      // Hotspot marker
      hotspotMarker.style.display = 'block';
      // the marker sits in the preview container, the canvas is centred in it
      hotspotMarker.style.left = (canvas.offsetLeft + hotspotX * scale * zoom) + 'px';
      hotspotMarker.style.top = (canvas.offsetTop + hotspotY * scale * zoom) + 'px';
      document.getElementById('slp-zoom-label').textContent = zoom + 'x';
    }

    // Hotspot dragging
    hotspotMarker.addEventListener('mousedown', (e) => {
      isDraggingHotspot = true;
      e.preventDefault();
    });
    // click anywhere on the picture to put the hotspot there; drag to move it
    const moveHotspot = (e) => {
      if (!currentImage) return;
      const rect = canvas.getBoundingClientRect();
      const x = (e.clientX - rect.left) / zoom / scale;
      const y = (e.clientY - rect.top) / zoom / scale;
      hotspotX = Math.round(Math.max(0, Math.min(currentImage.width - 1, x)));
      hotspotY = Math.round(Math.max(0, Math.min(currentImage.height - 1, y)));
      updatePreview();
    };
    canvas.addEventListener('mousedown', (e) => { isDraggingHotspot = true; moveHotspot(e); e.preventDefault(); });
    document.addEventListener('mousemove', (e) => { if (isDraggingHotspot) moveHotspot(e); });
    canvas.style.cursor = 'crosshair';
    document.addEventListener('mouseup', () => { isDraggingHotspot = false; });

    const cancel = () => {
      if (animInterval) clearInterval(animInterval);
      modal.style.display = 'none';
      modal.innerHTML = '';
      if (onCancel) onCancel();
    };
    document.querySelector('.btn-modal-close')?.addEventListener('click', cancel);
    document.getElementById('slp-cancel')?.addEventListener('click', cancel);

    document.getElementById('slp-create')?.addEventListener('click', async () => {
      if (!currentImage || !palette) return;

      await loadPalette();

      const w = Math.round(currentImage.width * scale);
      const h = Math.round(currentImage.height * scale);

      const tempCanvas = document.createElement('canvas');
      tempCanvas.width = w;
      tempCanvas.height = h;
      const tempCtx = tempCanvas.getContext('2d', { willReadFrequently: true });
      tempCtx.imageSmoothingEnabled = scaleMode === 'smooth';
      tempCtx.drawImage(currentImage, 0, 0, w, h);

      const imageData = tempCtx.getImageData(0, 0, w, h);
      let rows = SlpCreator.quantize(imageData.data, w, h, palette, { icon: kind === 'icon', playerColour, shadow });

      const hx = Math.round(hotspotX * scale);
      const hy = Math.round(hotspotY * scale);
      const cropped = SlpCreator.crop(rows, hx, hy);

      const framesList = [];
      if (kind === 'animation' && currentMode === 'sheet') {
        const frameW = Math.floor(w / currentFrames);
        const frameH = Math.floor(h / currentDirections);
        for (let dir = 0; dir < currentDirections; dir++) {
          for (let fr = 0; fr < currentFrames; fr++) {
            const frameRows = [];
            for (let y = 0; y < frameH; y++) {
              const srcY = dir * frameH + y;
              if (srcY >= h) break;
              const frameRow = [];
              for (let x = 0; x < frameW; x++) {
                const srcX = fr * frameW + x;
                if (srcX >= w) break;
                frameRow.push(rows[srcY][srcX]);
              }
              frameRows.push(frameRow);
            }
            const fhx = Math.floor(frameW / 2);
            const fhy = Math.floor(frameH * 0.85);
            const fc = SlpCreator.crop(frameRows, fhx, fhy);
            framesList.push({ rows: fc.rows, hx: fc.hx, hy: fc.hy });
          }
        }
      } else {
        framesList.push({ rows: cropped.rows, hx: cropped.hx, hy: cropped.hy, properties: kind === 'icon' ? 0x10 : 0 });
      }

      const slpBytes = SlpCreator.encode(framesList);

      const info = {
        frames: kind === 'animation' && currentMode === 'sheet' ? currentFrames : framesList.length,
        directions: kind === 'animation' && currentMode === 'sheet' ? currentDirections : 1,
        width: cropped.rows[0].length,
        height: cropped.rows.length
      };

      if (animInterval) clearInterval(animInterval);
      modal.style.display = 'none';
      modal.innerHTML = '';

      onDone(slpBytes, info);
    });

    if (window.Scriptorium && window.Scriptorium.wireDisclosures) {
      window.Scriptorium.wireDisclosures(modal);
    }

    loadPalette().then(() => {
      if (config.file) handleFile(config.file);
    });
  };

  // Export for both browser and Node
  if (typeof window !== 'undefined') {
    window.SlpCreator = SlpCreator;
  }
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = SlpCreator;
  }
})();

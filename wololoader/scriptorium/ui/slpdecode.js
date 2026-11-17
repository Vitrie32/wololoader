/**
 * SlpDecode - Age of Empires II SLP 2.0N decoder (the exact inverse of SlpCreator.encode)
 *
 * Pure functions, no DOM: used by the SLP viewer, the game icons and tools/test_slpview.js.
 * Vanilla ES2020, no dependencies.
 *
 * File layout (all little-endian):
 *   header 32 bytes: "2.0N", frame count (i32 at 4), comment (24 bytes at 8)
 *   frame info 32 bytes each: command table offset (u32), outline table offset (u32),
 *     palette offset (u32, unused by the game), properties (u32), width, height,
 *     hotspot x, hotspot y (i32 each)
 *   outline table per frame: left and right transparent pixel counts per row (u16 each);
 *     0x8000 marks an empty row
 *   command table per frame: one absolute u32 offset per row into the command data
 *   command data per row: commands until 0x0F (end of row)
 *
 * Commands (low nibble first; checked in this order because some low nibbles overlap):
 *   0x06 player colour   n = high nibble or next byte; n pixels, each a shade 0..7
 *   0x07 fill            n = high nibble or next byte; one colour pixel repeated n times
 *   0x0A player fill    n = high nibble or next byte; one shade repeated n times
 *   0x0B shadow          n = high nibble or next byte; n shadow pixels (no data)
 *   x0   lesser copy     n = cmd >> 2 (1..63); n colour pixels follow
 *   x1   lesser skip     n = cmd >> 2; n transparent pixels
 *   x2   greater copy    n = (cmd & 0xF0) << 4 | next byte; n colour pixels follow
 *   x3   greater skip    n = (cmd & 0xF0) << 4 | next byte; n transparent pixels
 *   0x0E extended        NOT decoded: the rest of that row stays transparent and the
 *                        frame counts it in unsupportedRows. The game's interface sheets
 *                        use 0x4E and 0x5E (about 900 rows in the whole folder); their
 *                        meaning is not verified yet, so guessing would draw wrong pixels.
 *   0x0F end of row
 *
 * Player colours: the stored shade s of player p (0..7) is palette entry 16 + 16 * p + s.
 * Verified on the game palette 50500.bina: the eight 8-entry ramps start at 16, 32, ... 128.
 */
(function () {
  'use strict';

  // Cell kinds. A frame is two arrays of width * height: kind[] and value[].
  const TRANSPARENT = 0;   // value unused
  const COLOUR = 1;        // value = palette index
  const PLAYER = 2;        // value = shade 0..7 (palette entry 16 + 16 * player + shade)
  const SHADOW = 3;        // value unused: the ground under a shadow

  const MAX_CELLS = 1 << 24;   // 16M pixels per frame: far above any game sprite

  function u16(u8, p) { return u8[p] | (u8[p + 1] << 8); }
  function u32(u8, p) { return (u8[p] | (u8[p + 1] << 8) | (u8[p + 2] << 16) | (u8[p + 3] << 24)) >>> 0; }
  function i32(u8, p) { return u8[p] | (u8[p + 1] << 8) | (u8[p + 2] << 16) | (u8[p + 3] << 24); }

  /**
   * Decodes one row's commands starting at byte p, writing cells from x = startX.
   * Returns true at the end-of-row command, false when an extended command was met.
   * Throws when the data runs past the buffer or past the row width (corrupt file).
   */
  function decodeRow(u8, p, startX, w, kind, value, rowBase) {
    const len = u8.length;
    let x = startX;
    const need = (n) => { if (p + n > len) throw new Error('SLP data is truncated'); };
    const put = (k, v) => {
      if (x >= w) throw new Error('SLP row is wider than its frame');
      kind[rowBase + x] = k;
      value[rowBase + x] = v;
      x++;
    };
    for (;;) {
      need(1);
      const c = u8[p++];
      const lo = c & 0x0F;
      if (c === 0x0F) return true;
      if (lo === 0x0E) return false;

      if (lo === 0x06 || lo === 0x07 || lo === 0x0A || lo === 0x0B) {
        let n = c >> 4;
        if (n === 0) { need(1); n = u8[p++]; }
        if (lo === 0x06) {                       // player colour list
          need(n);
          for (let i = 0; i < n; i++) put(PLAYER, u8[p++]);
        } else if (lo === 0x0B) {                // shadow run
          for (let i = 0; i < n; i++) put(SHADOW, 0);
        } else {                                 // 0x07 colour fill, 0x0A player fill
          need(1);
          const v = u8[p++];
          for (let i = 0; i < n; i++) put(lo === 0x07 ? COLOUR : PLAYER, v);
        }
      } else if ((c & 0x03) === 0) {             // lesser copy
        const n = c >> 2;
        need(n);
        for (let i = 0; i < n; i++) put(COLOUR, u8[p++]);
      } else if ((c & 0x03) === 1) {             // lesser skip
        const n = c >> 2;
        for (let i = 0; i < n; i++) put(TRANSPARENT, 0);
      } else if (lo === 0x02) {                  // greater copy
        need(1);
        const n = ((c & 0xF0) << 4) | u8[p++];
        need(n);
        for (let i = 0; i < n; i++) put(COLOUR, u8[p++]);
      } else {                                   // lo === 0x03: greater skip
        need(1);
        const n = ((c & 0xF0) << 4) | u8[p++];
        for (let i = 0; i < n; i++) put(TRANSPARENT, 0);
      }
    }
  }

  /**
   * Decodes an SLP 2.0N file.
   * @param {ArrayBuffer|Uint8Array} input
   * @returns {{comment: string, frames: Array<{width, height, hotspotX, hotspotY,
   *            kind: Uint8Array, value: Uint8Array, unsupportedRows: number}>}}
   */
  function decode(input) {
    const u8 = input instanceof Uint8Array ? input : new Uint8Array(input);
    if (u8.length < 32) throw new Error('Not an SLP file (too short)');
    const sig = String.fromCharCode(u8[0], u8[1], u8[2], u8[3]);
    if (sig !== '2.0N') throw new Error('Not an SLP 2.0N file (header "' + sig + '")');

    let comment = '';
    for (let i = 8; i < 32 && u8[i] !== 0; i++) comment += String.fromCharCode(u8[i]);

    const frameCount = i32(u8, 4);
    if (frameCount < 0 || 32 + 32 * frameCount > u8.length) throw new Error('SLP frame table is truncated');

    const frames = [];
    for (let f = 0; f < frameCount; f++) {
      const info = 32 + 32 * f;
      const cmdTab = u32(u8, info);
      const outline = u32(u8, info + 4);
      const width = i32(u8, info + 16);
      const height = i32(u8, info + 20);
      const hotspotX = i32(u8, info + 24);
      const hotspotY = i32(u8, info + 28);
      if (width <= 0 || height <= 0 || width * height > MAX_CELLS) throw new Error('SLP frame ' + f + ' has a bad size');
      if (outline + 4 * height > u8.length || cmdTab + 4 * height > u8.length) {
        throw new Error('SLP frame ' + f + ' tables are outside the file');
      }

      const kind = new Uint8Array(width * height);      // all TRANSPARENT (0)
      const value = new Uint8Array(width * height);
      let unsupportedRows = 0;
      for (let y = 0; y < height; y++) {
        const left = u16(u8, outline + 4 * y);
        const right = u16(u8, outline + 4 * y + 2);
        if ((left & 0x8000) || (right & 0x8000)) continue;   // empty row
        const p = u32(u8, cmdTab + 4 * y);
        if (p >= u8.length) throw new Error('SLP frame ' + f + ' row ' + y + ' points outside the file');
        if (!decodeRow(u8, p, left, width, kind, value, y * width)) unsupportedRows++;
      }
      frames.push({ width, height, hotspotX, hotspotY, kind, value, unsupportedRows });
    }
    return { comment, frames };
  }

  /**
   * Renders one decoded frame to RGBA bytes (length width * height * 4) with the game palette.
   * @param frame   one entry of decode().frames
   * @param palette 256 entries of [r, g, b] (GET /api/palette)
   * @param opts    { player: 0..7 (player colour), shadow: bool (draw shadows, default true) }
   */
  function render(frame, palette, opts = {}) {
    const player = Math.max(0, Math.min(7, opts.player | 0));
    const showShadow = opts.shadow !== false;
    const out = new Uint8ClampedArray(frame.width * frame.height * 4);
    const playerBase = 16 + 16 * player;
    for (let i = 0; i < frame.kind.length; i++) {
      const k = frame.kind[i];
      const o = i * 4;
      if (k === TRANSPARENT) continue;   // alpha 0
      if (k === SHADOW) {
        // the game darkens the ground under a shadow; drawn as 45% black like the creator preview
        if (showShadow) out[o + 3] = 115;
        continue;
      }
      const idx = k === PLAYER ? playerBase + Math.min(7, frame.value[i]) : frame.value[i];
      const c = palette[idx];
      out[o] = c[0];
      out[o + 1] = c[1];
      out[o + 2] = c[2];
      out[o + 3] = 255;
    }
    return out;
  }

  const SlpDecode = { decode, render, TRANSPARENT, COLOUR, PLAYER, SHADOW };

  if (typeof window !== 'undefined') {
    window.SlpDecode = SlpDecode;
  }
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = SlpDecode;
  }
})();

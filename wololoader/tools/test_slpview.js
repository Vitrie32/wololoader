#!/usr/bin/env node
/**
 * test_slpview.js - round trip: SlpCreator.encode (the creator's encoder) then SlpDecode.decode.
 *
 * Every pixel must come back with the same kind (transparent / palette colour / player shade /
 * shadow) and value, frame sizes and hotspots must match, and render() must map the player and
 * palette entries the way the game does. The encoder never emits extended commands, so this
 * test covers every command the viewer decodes. Optional: with WL_GAME_RESOURCES=<game>/resources
 * the test also decodes a few real game sprites (read only) and reports their unsupported rows.
 *
 * Run: node tools/test_slpview.js
 */

const fs = require('fs');
const path = require('path');
const SlpCreator = require('../scriptorium/ui/slpcreator.js');
const SlpDecode = require('../scriptorium/ui/slpdecode.js');

let failures = 0;
function check(cond, what) {
  if (!cond) {
    failures++;
    console.error('✗ ' + what);
  }
}

// Deterministic pseudo random numbers (xorshift32), so a failure can be reproduced.
function rng(seed) {
  let s = seed >>> 0 || 1;
  return () => {
    s ^= s << 13; s >>>= 0;
    s ^= s >>> 17;
    s ^= s << 5; s >>>= 0;
    return s / 4294967296;
  };
}

// Expected cell for an encoder pixel: null | ['c', idx] | ['p', shade] | ['s', 0]
function expectedCell(px) {
  if (px === null) return { kind: SlpDecode.TRANSPARENT, value: 0 };
  if (px[0] === 'c') return { kind: SlpDecode.COLOUR, value: px[1] };
  if (px[0] === 'p') return { kind: SlpDecode.PLAYER, value: px[1] };
  return { kind: SlpDecode.SHADOW, value: 0 };
}

function compareFrames(name, encodedFrames, decoded) {
  check(decoded.frames.length === encodedFrames.length,
    `${name}: frame count ${decoded.frames.length} != ${encodedFrames.length}`);
  for (let f = 0; f < encodedFrames.length && f < decoded.frames.length; f++) {
    const src = encodedFrames[f];
    const got = decoded.frames[f];
    const h = src.rows.length, w = src.rows[0].length;
    check(got.width === w && got.height === h, `${name} frame ${f}: size ${got.width}x${got.height} != ${w}x${h}`);
    check(got.hotspotX === src.hx && got.hotspotY === src.hy,
      `${name} frame ${f}: hotspot ${got.hotspotX},${got.hotspotY} != ${src.hx},${src.hy}`);
    check(got.unsupportedRows === 0, `${name} frame ${f}: ${got.unsupportedRows} unsupported rows`);
    let bad = 0, first = null;
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const e = expectedCell(src.rows[y][x]);
        const i = y * w + x;
        if (got.kind[i] !== e.kind || (e.kind !== SlpDecode.TRANSPARENT && e.kind !== SlpDecode.SHADOW && got.value[i] !== e.value)) {
          bad++;
          if (!first) first = { x, y, want: e, got: { kind: got.kind[i], value: got.value[i] } };
        }
      }
    }
    check(bad === 0, `${name} frame ${f}: ${bad} pixel(s) differ, first ${JSON.stringify(first)}`);
  }
}

function roundTrip(name, frames, comment) {
  const bytes = SlpCreator.encode(frames, comment);
  let decoded;
  try {
    decoded = SlpDecode.decode(bytes);
  } catch (e) {
    check(false, `${name}: decode threw ${e.message}`);
    return;
  }
  compareFrames(name, frames, decoded);
  if (comment !== undefined) {
    check(decoded.comment === comment.substring(0, 24), `${name}: comment "${decoded.comment}"`);
  }
}

// ---- hand-made cases (the same shapes test_slp.js uses for the encoder) --------------------

roundTrip('single transparent pixel', [{ rows: [[null]], hx: 0, hy: 0 }]);
roundTrip('single colour pixel', [{ rows: [[['c', 42]]], hx: 0, hy: 0 }]);
roundTrip('mixed pixel types', [{
  rows: [[null, null, ['c', 10], ['c', 11], null, ['p', 3], ['p', 4], ['s', 0], null]],
  hx: 4, hy: 0
}]);
roundTrip('multiple rows', [{
  rows: [
    [null, ['c', 50], ['c', 51], null],
    [['c', 60], ['c', 61], ['c', 62], ['c', 63]],
    [null, null, null, null]
  ],
  hx: 2, hy: 1
}]);
roundTrip('long colour run (greater copy)', [{ rows: [Array.from({ length: 100 }, () => ['c', 128])], hx: 50, hy: 0 }]);
roundTrip('long transparent run (greater skip)', [{ rows: [Array.from({ length: 100 }, () => null)], hx: 50, hy: 0 }]);
roundTrip('long player run (count byte)', [{
  rows: [Array.from({ length: 30 }, (_, i) => ['p', i % 8])], hx: 15, hy: 0
}]);
roundTrip('very long run (over 255 and 4095)', [{
  rows: [[...Array.from({ length: 5000 }, (_, i) => ['c', i % 256]), ...Array.from({ length: 300 }, () => null)]],
  hx: 0, hy: 0
}]);
roundTrip('shadow run and empty row', [{
  rows: [
    Array.from({ length: 40 }, () => ['s', 0]),
    Array.from({ length: 40 }, () => null),
    [['p', 7], ...Array.from({ length: 20 }, () => ['s', 0]), ['c', 1], ...Array.from({ length: 18 }, () => null)]
  ],
  hx: 3, hy: 2
}], 'Wololoader test');
roundTrip('multiple frames with comment', [
  { rows: [[['c', 10]]], hx: 0, hy: 0 },
  { rows: [[['c', 20]]], hx: 0, hy: 0 },
  { rows: [[['c', 30]]], hx: 0, hy: 0 }
], 'TestComment');

// building-like sprite and an animation (5 directions x 10 frames), as in test_slp.js
const buildingRows = [];
for (let y = 0; y < 50; y++) {
  const row = [];
  for (let x = 0; x < 40; x++) {
    row.push(y < 5 || y > 45 || x < 5 || x > 35 ? null : ['c', 100 + ((x + y) % 50)]);
  }
  buildingRows.push(row);
}
roundTrip('building sprite', [{ rows: buildingRows, hx: 20, hy: 38 }]);

const anim = [];
for (let dir = 0; dir < 5; dir++) {
  for (let fr = 0; fr < 10; fr++) {
    const rows = [];
    for (let y = 0; y < 30; y++) {
      const row = [];
      for (let x = 0; x < 25; x++) {
        if (x < 5 || x > 20 || y < 5 || y > 25) row.push(null);
        else if (x === 10 && y === 10) row.push(['p', (dir + fr) % 8]);
        else row.push(['c', 50 + dir * 10 + fr]);
      }
      rows.push(row);
    }
    anim.push({ rows, hx: 12, hy: 25 });
  }
}
roundTrip('unit animation (50 frames)', anim);

// ---- random sprites: every kind at random run lengths --------------------------------------

const rand = rng(12345);
for (let t = 0; t < 200; t++) {
  const frames = [];
  const nFrames = 1 + Math.floor(rand() * 3);
  for (let f = 0; f < nFrames; f++) {
    const w = 1 + Math.floor(rand() * 180);
    const h = 1 + Math.floor(rand() * 40);
    const rows = [];
    for (let y = 0; y < h; y++) {
      const row = [];
      let mode = 0, left = 0;
      while (row.length < w) {
        if (left === 0) {
          mode = Math.floor(rand() * 4);
          left = rand() < 0.2 ? 1 + Math.floor(rand() * 300) : 1 + Math.floor(rand() * 12);
        }
        left--;
        if (mode === 0) row.push(null);
        else if (mode === 1) row.push(['c', Math.floor(rand() * 256)]);
        else if (mode === 2) row.push(['p', Math.floor(rand() * 8)]);
        else row.push(['s', 0]);
      }
      rows.push(row);
    }
    frames.push({ rows, hx: Math.floor(rand() * 60) - 10, hy: Math.floor(rand() * 60) - 10 });
  }
  roundTrip(`random sprite ${t}`, frames);
}

// ---- render(): palette lookup for colour, player and shadow cells --------------------------

{
  const palette = Array.from({ length: 256 }, (_, i) => [i, 255 - i, (i * 7) & 255]);
  const frame = {
    width: 4, height: 1,
    kind: Uint8Array.from([SlpDecode.COLOUR, SlpDecode.PLAYER, SlpDecode.SHADOW, SlpDecode.TRANSPARENT]),
    value: Uint8Array.from([9, 3, 0, 0]),
    unsupportedRows: 0
  };
  const px = SlpDecode.render(frame, palette, { player: 2 });
  check(px[0] === 9 && px[1] === 246 && px[3] === 255, 'render: colour cell uses palette[9]');
  // player 2 (third player) shade 3 = palette 16 + 16*2 + 3 = 51
  check(px[4] === 51 && px[5] === 255 - 51 && px[7] === 255, 'render: player shade uses palette[16 + 16*player + shade]');
  check(px[8 + 3] === 115 && px[8] === 0, 'render: shadow is 45% black');
  check(px[12 + 3] === 0, 'render: transparent cell has alpha 0');
  const noShadow = SlpDecode.render(frame, palette, { player: 2, shadow: false });
  check(noShadow[8 + 3] === 0, 'render: shadow can be hidden');
}

// ---- optional: real game sprites (read only) -----------------------------------------------

const gameRes = process.env.WL_GAME_RESOURCES;
if (gameRes) {
  const folder = path.join(gameRes, '_common', 'drs', 'interface');
  for (const name of ['50729.slp', '50730.slp', '50791.slp', '50705.slp']) {
    const file = path.join(folder, name);
    if (!fs.existsSync(file)) { console.log(`- ${name} not in the game folder, skipped`); continue; }
    try {
      const d = SlpDecode.decode(fs.readFileSync(file));
      const unsupported = d.frames.reduce((s, f) => s + f.unsupportedRows, 0);
      console.log(`- ${name}: ${d.frames.length} frames, ${unsupported} rows with extended commands (not decoded)`);
    } catch (e) {
      check(false, `${name}: ${e.message}`);
    }
  }
}

console.log('');
if (failures) {
  console.log(`✗ ${failures} failure(s)`);
  process.exit(1);
}
console.log('✓ SLP round trip: encoder and decoder agree on every pixel');

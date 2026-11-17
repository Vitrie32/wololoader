#!/usr/bin/env node
/**
 * test_slp.js — Verify that slpcreator.js encodes byte-identical to slpwrite.py
 */

const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

// Load the JS encoder
const SlpCreator = require('../scriptorium/ui/slpcreator.js');

function pythonEncode(frames, comment = 'Wololoader') {
  // Convert frames to Python format and invoke slpwrite.py
  const framesJson = JSON.stringify(frames);
  const commentJson = JSON.stringify(comment);

  const pythonScript = `
import sys
import json
sys.path.insert(0, '${path.join(__dirname)}')
from slpwrite import encode

frames_data = json.loads('''${framesJson}''')
comment = json.loads('''${commentJson}''')

# Convert from JS format to Python format
py_frames = []
for frame in frames_data:
    rows = []
    for row in frame['rows']:
        py_row = []
        for px in row:
            if px is None:
                py_row.append(None)
            elif px[0] == 'c':
                py_row.append(('c', px[1]))
            elif px[0] == 'p':
                py_row.append(('p', px[1]))
            elif px[0] == 's':
                py_row.append(('s', 0))
        rows.append(py_row)
    py_frames.append((rows, frame['hx'], frame['hy']))

result = encode(py_frames, comment.encode('utf-8'))
sys.stdout.buffer.write(result)
`;

  const tmpScript = path.join('/tmp', `test_slp_${Date.now()}.py`);
  fs.writeFileSync(tmpScript, pythonScript);

  try {
    const result = execSync(`python3 "${tmpScript}"`, { encoding: 'buffer' });
    fs.unlinkSync(tmpScript);
    return new Uint8Array(result);
  } catch (err) {
    fs.unlinkSync(tmpScript);
    throw err;
  }
}

function compareBytes(a, b, name) {
  if (a.length !== b.length) {
    console.error(`✗ ${name}: Length mismatch (JS: ${a.length}, Python: ${b.length})`);
    return false;
  }

  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) {
      console.error(`✗ ${name}: First difference at offset ${i} (0x${i.toString(16)}): JS=${a[i]} (0x${a[i].toString(16)}), Python=${b[i]} (0x${b[i].toString(16)})`);
      // Show context
      const start = Math.max(0, i - 8);
      const end = Math.min(a.length, i + 9);
      console.error(`  Context JS: ${Array.from(a.slice(start, end)).map(x => x.toString(16).padStart(2, '0')).join(' ')}`);
      console.error(`  Context Py: ${Array.from(b.slice(start, end)).map(x => x.toString(16).padStart(2, '0')).join(' ')}`);
      return false;
    }
  }

  console.log(`✓ ${name}: ${a.length} bytes identical`);
  return true;
}

function runTest(name, frames, comment) {
  try {
    const jsResult = SlpCreator.encode(frames, comment);
    const pyResult = pythonEncode(frames, comment);
    return compareBytes(jsResult, pyResult, name);
  } catch (err) {
    console.error(`✗ ${name}: Exception: ${err.message}`);
    return false;
  }
}

// Test cases
console.log('Testing SLP encoder byte-identical output...\n');

let allPassed = true;

// Test 1: Single transparent pixel
allPassed &= runTest('Single transparent pixel', [
  { rows: [[null]], hx: 0, hy: 0 }
]);

// Test 2: Single color pixel
allPassed &= runTest('Single color pixel', [
  { rows: [[['c', 42]]], hx: 0, hy: 0 }
]);

// Test 3: Row with transparency, color, player, shadow
allPassed &= runTest('Mixed pixel types', [
  {
    rows: [
      [null, null, ['c', 10], ['c', 11], null, ['p', 3], ['p', 4], ['s', 0], null]
    ],
    hx: 4,
    hy: 0
  }
]);

// Test 4: Multiple rows
allPassed &= runTest('Multiple rows', [
  {
    rows: [
      [null, ['c', 50], ['c', 51], null],
      [['c', 60], ['c', 61], ['c', 62], ['c', 63]],
      [null, null, null, null]
    ],
    hx: 2,
    hy: 1
  }
]);

// Test 5: Long runs (>63 pixels to test extended commands)
const longRow = [];
for (let i = 0; i < 100; i++) longRow.push(['c', 128]);
allPassed &= runTest('Long color run', [
  { rows: [longRow], hx: 50, hy: 0 }
]);

const longTransRow = [];
for (let i = 0; i < 100; i++) longTransRow.push(null);
allPassed &= runTest('Long transparent run', [
  { rows: [longTransRow], hx: 50, hy: 0 }
]);

// Test 6: Large player color run
const longPlayerRow = [];
for (let i = 0; i < 30; i++) longPlayerRow.push(['p', i % 8]);
allPassed &= runTest('Long player color run', [
  { rows: [longPlayerRow], hx: 15, hy: 0 }
]);

// Test 7: Multiple frames
allPassed &= runTest('Multiple frames', [
  { rows: [[['c', 10]]], hx: 0, hy: 0 },
  { rows: [[['c', 20]]], hx: 0, hy: 0 },
  { rows: [[['c', 30]]], hx: 0, hy: 0 }
]);

// Test 8: Different comment
allPassed &= runTest('Custom comment', [
  { rows: [[['c', 100]]], hx: 0, hy: 0 }
], 'TestComment');

// Test 9: Building-like frame (single large frame)
const buildingRows = [];
for (let y = 0; y < 50; y++) {
  const row = [];
  for (let x = 0; x < 40; x++) {
    if (y < 5 || y > 45 || x < 5 || x > 35) {
      row.push(null);
    } else {
      row.push(['c', 100 + ((x + y) % 50)]);
    }
  }
  buildingRows.push(row);
}
allPassed &= runTest('Building sprite', [
  { rows: buildingRows, hx: 20, hy: 38 }
]);

// Test 10: Unit animation (5 directions × 10 frames = 50 frames)
const animFrames = [];
for (let dir = 0; dir < 5; dir++) {
  for (let frame = 0; frame < 10; frame++) {
    const rows = [];
    for (let y = 0; y < 30; y++) {
      const row = [];
      for (let x = 0; x < 25; x++) {
        if (x < 5 || x > 20 || y < 5 || y > 25) {
          row.push(null);
        } else if (x === 10 && y === 10) {
          row.push(['p', (dir + frame) % 8]);
        } else {
          row.push(['c', 50 + dir * 10 + frame]);
        }
      }
      rows.push(row);
    }
    animFrames.push({ rows, hx: 12, hy: 25 });
  }
}
allPassed &= runTest('Unit animation (50 frames)', animFrames);

console.log('\n' + '='.repeat(60));
if (allPassed) {
  console.log('✓ All tests passed — JS encoder is byte-identical to Python');
  process.exit(0);
} else {
  console.log('✗ Some tests failed');
  process.exit(1);
}

#!/usr/bin/env node
// Test suite for graph.js (Scriptorium visual scripting editor)
// Run: node test_graph.js

const ScGraph = require('../scriptorium/ui/graph.js');
const { spawnSync } = require('child_process');

let testCount = 0;
let passCount = 0;

function test(name, fn) {
  testCount++;
  try {
    fn();
    passCount++;
    console.log(`✓ Test ${testCount}: ${name}`);
  } catch (err) {
    console.error(`✗ Test ${testCount}: ${name}`);
    console.error(`  ${err.message}`);
    if (err.stack) {
      console.error(err.stack.split('\n').slice(1, 4).join('\n'));
    }
  }
}

function assertEqual(actual, expected, msg) {
  if (actual !== expected) {
    throw new Error(`${msg}\n  Expected: ${expected}\n  Actual: ${actual}`);
  }
}

function assertContains(text, substring, msg) {
  if (!text.includes(substring)) {
    throw new Error(`${msg}\n  Expected to contain: ${substring}\n  Actual: ${text.substring(0, 100)}...`);
  }
}

function assertLuaSyntax(lua) {
  const result = spawnSync('luac', ['-p', '-'], { input: lua, encoding: 'utf8' });
  if (result.status !== 0) {
    throw new Error(`Lua syntax error:\n${result.stderr || result.stdout}`);
  }
}

// Test 1: OnGameStart -> ShowMessage("Hello")
test('Simple event with action', () => {
  const g = ScGraph.newGraph('test1');

  g.nodes.push({
    id: 'n1',
    type: 'OnGameStart',
    x: 100,
    y: 100,
    values: {}
  });

  g.nodes.push({
    id: 'n2',
    type: 'ShowMessage',
    x: 300,
    y: 100,
    values: { text: 'Hello', color: 36 }
  });

  g.links.push({
    from: ['n1', 'exec'],
    to: ['n2', 'exec']
  });

  const result = ScGraph.toLua(g);

  assertEqual(result.errors.length, 0, 'Should have no errors');
  assertContains(result.lua, 'Game.OnStart', 'Should contain OnStart');
  assertContains(result.lua, 'UI.Message("Hello"', 'Should contain message');
  assertContains(result.lua, 'Color=36', 'Should contain color parameter');
  assertLuaSyntax(result.lua);
});

// Test 2: OnBuildingButton -> AddResource -> CreateUnit
test('Event with chained actions', () => {
  const g = ScGraph.newGraph('test2');

  g.nodes.push({
    id: 'n1',
    type: 'OnBuildingButton',
    x: 100,
    y: 100,
    values: { button: 'spawn_unit' }
  });

  g.nodes.push({
    id: 'n2',
    type: 'AddResource',
    x: 300,
    y: 100,
    values: { resource: 'Gold' }
  });

  g.nodes.push({
    id: 'n3',
    type: 'LocalPlayer',
    x: 200,
    y: 50,
    values: {}
  });

  g.nodes.push({
    id: 'n4',
    type: 'CreateUnit',
    x: 500,
    y: 100,
    values: { unit: 125 }
  });

  // Links
  g.links.push({ from: ['n1', 'exec'], to: ['n2', 'exec'] });
  g.links.push({ from: ['n3', 'player'], to: ['n2', 'player'] });
  g.links.push({ from: ['n3', 'player'], to: ['n4', 'player'] });
  g.links.push({ from: ['n2', 'exec'], to: ['n4', 'exec'] });

  // Inline values for AddResource amount and CreateUnit x,y
  g.nodes[1].values.amount = 100;
  g.nodes[3].values.x = 50;
  g.nodes[3].values.y = 50;

  const result = ScGraph.toLua(g);

  assertEqual(result.errors.length, 0, 'Should have no errors');
  assertContains(result.lua, 'Game.OnCommand("spawn_unit"', 'Should contain button command');
  assertContains(result.lua, 'AddGold(100)', 'Should contain add gold');
  assertContains(result.lua, 'CreateUnit(125, 50, 50)', 'Should contain create unit');
  assertLuaSyntax(result.lua);
});

// Test 3: EverySecond -> Branch -> ShowMessage with JoinText
test('Control flow with data expressions', () => {
  const g = ScGraph.newGraph('test3');

  g.nodes.push({
    id: 'n1',
    type: 'EverySecond',
    x: 100,
    y: 100,
    values: {}
  });

  g.nodes.push({
    id: 'n2',
    type: 'Branch',
    x: 300,
    y: 100,
    values: {}
  });

  g.nodes.push({
    id: 'n3',
    type: 'Compare',
    x: 200,
    y: 50,
    values: { a: 10, op: '>', b: 5 }
  });

  g.nodes.push({
    id: 'n4',
    type: 'ShowMessage',
    x: 500,
    y: 80,
    values: {}
  });

  g.nodes.push({
    id: 'n5',
    type: 'JoinText',
    x: 400,
    y: 50,
    values: { a: 'Count: ', b: '10' }
  });

  g.nodes.push({
    id: 'n6',
    type: 'ShowMessage',
    x: 500,
    y: 150,
    values: { text: 'Too small' }
  });

  // Links
  g.links.push({ from: ['n1', 'exec'], to: ['n2', 'exec'] });
  g.links.push({ from: ['n3', 'result'], to: ['n2', 'condition'] });
  g.links.push({ from: ['n2', 'true'], to: ['n4', 'exec'] });
  g.links.push({ from: ['n2', 'false'], to: ['n6', 'exec'] });
  g.links.push({ from: ['n5', 'result'], to: ['n4', 'text'] });

  const result = ScGraph.toLua(g);

  assertEqual(result.errors.length, 0, 'Should have no errors');
  assertContains(result.lua, 'Game.OnSecond', 'Should contain OnSecond');
  assertContains(result.lua, 'if (10 > 5) then', 'Should contain condition');
  assertContains(result.lua, 'tostring', 'Should contain tostring for join');
  assertContains(result.lua, 'else', 'Should contain else branch');
  assertLuaSyntax(result.lua);
});

// Test 4: ForEachUnit -> ShowMessage
test('Loop with iteration', () => {
  const g = ScGraph.newGraph('test4');

  g.nodes.push({
    id: 'n1',
    type: 'OnGameStart',
    x: 100,
    y: 100,
    values: {}
  });

  g.nodes.push({
    id: 'n2',
    type: 'ForEachUnit',
    x: 300,
    y: 100,
    values: {}
  });

  g.nodes.push({
    id: 'n3',
    type: 'LocalPlayer',
    x: 150,
    y: 50,
    values: {}
  });

  g.nodes.push({
    id: 'n4',
    type: 'PlayerUnits',
    x: 200,
    y: 80,
    values: { class: 'All' }
  });

  g.nodes.push({
    id: 'n5',
    type: 'ShowMessage',
    x: 500,
    y: 100,
    values: {}
  });

  g.nodes.push({
    id: 'n6',
    type: 'UnitProperty',
    x: 400,
    y: 80,
    values: { property: 'Type' }
  });

  // Links
  g.links.push({ from: ['n1', 'exec'], to: ['n2', 'exec'] });
  g.links.push({ from: ['n3', 'player'], to: ['n4', 'player'] });
  g.links.push({ from: ['n4', 'units'], to: ['n2', 'units'] });
  g.links.push({ from: ['n2', 'body'], to: ['n5', 'exec'] });
  g.links.push({ from: ['n2', 'unit'], to: ['n6', 'unit'] });
  g.links.push({ from: ['n6', 'value'], to: ['n5', 'text'] });

  const result = ScGraph.toLua(g);

  assertEqual(result.errors.length, 0, 'Should have no errors');
  assertContains(result.lua, 'Game.OnStart', 'Should contain OnStart');
  assertContains(result.lua, ':Units()', 'Should contain Units call');
  assertContains(result.lua, 'for _, unit_', 'Should contain for loop');
  assertContains(result.lua, 'ipairs', 'Should use ipairs');
  assertContains(result.lua, '.Type', 'Should access Type property');
  assertLuaSyntax(result.lua);
});

// Test 5: Error cases (unlinked required input)
test('Error detection for missing required inputs', () => {
  const g = ScGraph.newGraph('test5');

  g.nodes.push({
    id: 'n1',
    type: 'OnGameStart',
    x: 100,
    y: 100,
    values: {}
  });

  g.nodes.push({
    id: 'n2',
    type: 'CreateUnit',
    x: 300,
    y: 100,
    values: { unit: 125 }
    // Missing player, x, y
  });

  g.links.push({ from: ['n1', 'exec'], to: ['n2', 'exec'] });

  const result = ScGraph.toLua(g);

  if (result.errors.length === 0) {
    throw new Error('Should have detected missing required inputs');
  }

  const hasPlayerError = result.errors.some(e =>
    e.nodeId === 'n2' && e.message.includes('player')
  );

  if (!hasPlayerError) {
    throw new Error('Should report missing player input');
  }
});

// Test 6: graphs go to scripts/visual.lua; main.lua only requires it and loses the old block
test('visual.lua and main.lua require (migration from the marked block)', () => {
  const blocks = [
    { name: 'test_graph', lua: 'Game.OnStart(function()\n  UI.Message("Test")\nend)' }
  ];

  const visual = ScGraph.visualLuaFile(blocks);
  assertContains(visual, 'generated by Scriptorium, do not edit', 'visual.lua header');
  assertContains(visual, 'do -- graph: test_graph', 'Should wrap in do block');
  assertContains(visual, 'UI.Message("Test")', 'Should include generated code');
  assertEqual(ScGraph.visualLuaFile([]).includes('do -- graph'), false, 'No graphs: header only');
  assertEqual(ScGraph.VISUAL_MODULE, 'visual', 'module name');

  // an old main.lua: the author's code around the old marked block
  const oldMain = `-- My custom code
print("Hello from main.lua")

function myHelper()
  return 42
end

-- >>> Scriptorium graphs (generated: edit them in Scriptorium) >>>
do -- graph: old
  UI.Message("old")
end
-- <<< Scriptorium graphs <<<

-- More code here
print("End of file")
`;
  assertEqual(ScGraph.hasOldBlock(oldMain), true, 'old block found');
  const cleaned = ScGraph.removeOldBlock(oldMain);
  assertEqual(ScGraph.hasOldBlock(cleaned), false, 'old block removed');
  assertEqual(cleaned.includes('UI.Message("old")'), false, 'old graph code removed');
  assertContains(cleaned, '-- My custom code', 'Should preserve existing code');
  assertContains(cleaned, 'function myHelper()', 'Should preserve functions');
  assertContains(cleaned, 'print("End of file")', 'Should preserve end');
  assertEqual(cleaned.includes('\n\n\n'), false, 'no stray blank lines left behind');

  // require("visual") is added once
  const withReq = ScGraph.addRequire(cleaned, 'visual');
  assertContains(withReq, 'require("visual")', 'require added');
  assertEqual(ScGraph.addRequire(withReq, 'visual'), withReq, 'second add changes nothing');
  assertEqual(ScGraph.requiresModule('require "visual"', 'visual'), true, 'spaces and no parens');
  assertEqual(ScGraph.requiresModule("require('visual')", 'visual'), true, 'single quotes');
  assertEqual(ScGraph.requiresModule('-- require("visual_old")', 'visual'), false, 'other module names do not match');
  assertEqual(ScGraph.requiresModule('require("ai.economy")', 'ai.economy'), true, 'dotted names');
  assertEqual(ScGraph.requiresModule('require("ai_economy")', 'ai.economy'), false, 'dot is literal');

  // only a complete block is removed
  const halfMain = 'x = 1\n-- >>> Scriptorium graphs (generated: edit them in Scriptorium) >>>\nx = 2\n';
  assertEqual(ScGraph.removeOldBlock(halfMain), halfMain, 'start marker without end is left alone');
});

// Test 7: Text escaping
test('Lua string escaping', () => {
  const g = ScGraph.newGraph('test7');

  g.nodes.push({
    id: 'n1',
    type: 'OnGameStart',
    x: 100,
    y: 100,
    values: {}
  });

  g.nodes.push({
    id: 'n2',
    type: 'ShowMessage',
    x: 300,
    y: 100,
    values: {
      text: 'He said "Hello"\nNew line\tTab\\Backslash',
      color: 36
    }
  });

  g.links.push({ from: ['n1', 'exec'], to: ['n2', 'exec'] });

  const result = ScGraph.toLua(g);

  assertEqual(result.errors.length, 0, 'Should have no errors');
  assertContains(result.lua, '\\"Hello\\"', 'Should escape quotes');
  assertContains(result.lua, '\\n', 'Should escape newlines');
  assertContains(result.lua, '\\t', 'Should escape tabs');
  assertContains(result.lua, '\\\\', 'Should escape backslashes');
  assertLuaSyntax(result.lua);
});

// Test 8: ButtonCondition and SetButtonAvailable
test('ButtonCondition and SetButtonAvailable', () => {
  const g = ScGraph.newGraph('test8');

  g.nodes.push({
    id: 'n1',
    type: 'ButtonCondition',
    x: 100,
    y: 100,
    values: { button: 'CallCrusaders' }
  });

  g.nodes.push({
    id: 'n2',
    type: 'SetButtonAvailable',
    x: 300,
    y: 100,
    values: { available: true }
  });

  g.links.push({ from: ['n1', 'exec'], to: ['n2', 'exec'] });

  const result = ScGraph.toLua(g);
  assertEqual(result.errors.length, 0, 'Should have no errors');
  assertContains(result.lua, 'Game.CommandCondition("CallCrusaders"', 'Registers CommandCondition');
  assertContains(result.lua, 'return true', 'Returns button availability');
  assertLuaSyntax(result.lua);
});

// Summary
console.log(`\n${passCount}/${testCount} tests passed`);

if (passCount === testCount) {
  console.log('\n✓ All tests passed!');
  process.exit(0);
} else {
  console.log(`\n✗ ${testCount - passCount} test(s) failed`);
  process.exit(1);
}

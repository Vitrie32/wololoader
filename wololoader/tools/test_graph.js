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

// Test 9: ForEachPlayer + GaiaPlayer + ResourceDeposit + SpawnNearUnit
test('ForEachPlayer, GaiaPlayer, ResourceDeposit, SpawnNearUnit', () => {
  const g = ScGraph.newGraph('deposit_spawn');
  g.nodes.push(
    { id: 'n1', type: 'OnGameStart', x: 0, y: 0, values: {} },
    { id: 'n2', type: 'ForEachPlayer', x: 200, y: 0, values: {} },
    { id: 'n3', type: 'UnitsOfType', x: 200, y: 200, values: { unitType: 109 } },
    { id: 'n4', type: 'ForEachUnit', x: 400, y: 0, values: {} },
    { id: 'n5', type: 'GaiaPlayer', x: 400, y: 200, values: {} },
    { id: 'n6', type: 'ResourceDeposit', x: 400, y: 300, values: { resource: 'crystal' } },
    { id: 'n7', type: 'SpawnNearUnit', x: 600, y: 0, values: { dx: 5, dy: -2 } }
  );
  g.links.push(
    { from: ['n1', 'exec'], to: ['n2', 'exec'] },
    { from: ['n2', 'body'], to: ['n4', 'exec'] },
    { from: ['n2', 'player'], to: ['n3', 'player'] },
    { from: ['n3', 'units'], to: ['n4', 'units'] },
    { from: ['n4', 'body'], to: ['n7', 'exec'] },
    { from: ['n5', 'player'], to: ['n7', 'player'] },
    { from: ['n6', 'unit'], to: ['n7', 'unit'] },
    { from: ['n4', 'unit'], to: ['n7', 'near'] }
  );
  const result = ScGraph.toLua(g);
  assertEqual(result.errors.length, 0, 'Should have no errors');
  assertContains(result.lua, 'for _, player_2 in ipairs(Game.Players()) do', 'Loops over all players');
  assertContains(result.lua, 'Game.Player(0)', 'Uses Gaia player');
  assertContains(result.lua, 'Game.ResourceDeposit("crystal")', 'Looks up deposit ID');
  assertContains(result.lua, ':CreateUnit(Game.ResourceDeposit("crystal"), unit_4.X + (5), unit_4.Y + (-2))', 'Spawns near unit');
  assertLuaSyntax(result.lua);
});

// Test 10: SetCounter, HideCounter, PlayerIsEnemy, SelectedUnits, UnitPosition
test('SetCounter, HideCounter, PlayerIsEnemy, SelectedUnits, UnitPosition', () => {
  const g = ScGraph.newGraph('hud_and_helpers');
  g.nodes.push(
    { id: 'n1', type: 'EverySecond', x: 0, y: 0, values: {} },
    { id: 'n2', type: 'LocalPlayer', x: 0, y: 150, values: {} },
    { id: 'n3', type: 'ModeEnemyPlayer', x: 0, y: 250, values: {} },
    { id: 'n4', type: 'PlayerIsEnemy', x: 200, y: 200, values: {} },
    { id: 'n5', type: 'Branch', x: 400, y: 0, values: {} },
    { id: 'n6', type: 'SelectedUnits', x: 400, y: 200, values: {} },
    { id: 'n7', type: 'ForEachUnit', x: 600, y: 0, values: {} },
    { id: 'n8', type: 'UnitPosition', x: 600, y: 200, values: {} },
    { id: 'n9', type: 'ToText', x: 800, y: 200, values: {} },
    { id: 'n10', type: 'SetCounter', x: 1000, y: 0, values: { name: 'pos' } },
    { id: 'n11', type: 'HideCounter', x: 600, y: 350, values: { name: 'pos' } }
  );
  g.links.push(
    { from: ['n1', 'exec'], to: ['n5', 'exec'] },
    { from: ['n2', 'player'], to: ['n4', 'a'] },
    { from: ['n3', 'player'], to: ['n4', 'b'] },
    { from: ['n4', 'result'], to: ['n5', 'condition'] },
    { from: ['n5', 'true'], to: ['n7', 'exec'] },
    { from: ['n5', 'false'], to: ['n11', 'exec'] },
    { from: ['n6', 'units'], to: ['n7', 'units'] },
    { from: ['n7', 'body'], to: ['n10', 'exec'] },
    { from: ['n7', 'unit'], to: ['n8', 'unit'] },
    { from: ['n8', 'x'], to: ['n9', 'value'] },
    { from: ['n9', 'text'], to: ['n10', 'text'] }
  );
  const result = ScGraph.toLua(g);
  assertEqual(result.errors.length, 0, 'Should have no errors');
  assertContains(result.lua, 'local __wl_counters = {}', 'Declares counter table');
  assertContains(result.lua, ':IsEnemy(', 'Checks diplomacy');
  assertContains(result.lua, 'Game.Selected()', 'Reads selected units');
  assertContains(result.lua, 'unit_7.X or 0', 'Reads UnitPosition X');
  assertContains(result.lua, 'UI:CreateCounter(', 'Creates HUD counter');
  assertContains(result.lua, '__wl_counters["pos"]:Hide()', 'Hides HUD counter');
  assertLuaSyntax(result.lua);
});

// Test 11: OnMapClick, SetMapClickCapture, SetUnitControlDisabled, PlaySound ("Villager"), PlaySoundFile
test('OnMapClick, SetMapClickCapture, SetUnitControlDisabled, PlaySound Villager, PlaySoundFile', () => {
  const g = ScGraph.newGraph('map_click_and_sound');
  g.nodes.push(
    { id: 'n1', type: 'OnGameStart', x: 0, y: 0, values: {} },
    { id: 'n2', type: 'SetMapClickCapture', x: 200, y: 0, values: { enabled: true } },
    { id: 'n3', type: 'SetUnitControlDisabled', x: 400, y: 0, values: { disabled: true } },
    { id: 'n4', type: 'OnMapClick', x: 0, y: 200, values: {} },
    { id: 'n5', type: 'Compare', x: 200, y: 300, values: { op: '==', b: 'left' } },
    { id: 'n6', type: 'Branch', x: 400, y: 200, values: {} },
    { id: 'n7', type: 'LocalPlayer', x: 400, y: 350, values: {} },
    { id: 'n8', type: 'CreateUnit', x: 600, y: 200, values: { unit: 4, capturable: true } },
    { id: 'n9', type: 'PlaySound', x: 800, y: 200, values: { sound: 'Villager' } },
    { id: 'n10', type: 'PlaySoundFile', x: 1000, y: 200, values: { path: 'resources/_common/drs/sounds/5353.wav' } },
    { id: 'n11', type: 'SetMapClickCapture', x: 600, y: 400, values: { enabled: false } },
    { id: 'n12', type: 'SetUnitControlDisabled', x: 800, y: 400, values: { disabled: false } }
  );
  g.links.push(
    { from: ['n1', 'exec'], to: ['n2', 'exec'] },
    { from: ['n2', 'exec'], to: ['n3', 'exec'] },
    { from: ['n4', 'exec'], to: ['n6', 'exec'] },
    { from: ['n4', 'button'], to: ['n5', 'a'] },
    { from: ['n5', 'result'], to: ['n6', 'condition'] },
    { from: ['n6', 'true'], to: ['n8', 'exec'] },
    { from: ['n7', 'player'], to: ['n8', 'player'] },
    { from: ['n4', 'x'], to: ['n8', 'x'] },
    { from: ['n4', 'y'], to: ['n8', 'y'] },
    { from: ['n8', 'exec'], to: ['n9', 'exec'] },
    { from: ['n9', 'exec'], to: ['n10', 'exec'] },
    { from: ['n6', 'false'], to: ['n11', 'exec'] },
    { from: ['n11', 'exec'], to: ['n12', 'exec'] }
  );
  const result = ScGraph.toLua(g);
  assertEqual(result.errors.length, 0, 'Should have no errors');
  assertContains(result.lua, 'Game.SetMapClickCapture(true)', 'Enables map click capture');
  assertContains(result.lua, 'Game.SetUnitControlDisabled(true)', 'Disables unit control');
  assertContains(result.lua, 'Game.OnMapClick(function(x_4, y_4, button_4)', 'Registers OnMapClick handler');
  assertContains(result.lua, 'if (button_4 == "left") then', 'Branches on click button');
  assertContains(result.lua, 'Game.LocalPlayer:CreateUnit(4, x_4, y_4)', 'Spawns unit at clicked tile');
  assertContains(result.lua, 'Sound.Play("Villager")', 'Plays Villager sound');
  assertContains(result.lua, 'Sound.PlayFile("resources/_common/drs/sounds/5353.wav")', 'Plays sound file');
  assertContains(result.lua, 'Game.SetMapClickCapture(false)', 'Disables map click capture');
  assertContains(result.lua, 'Game.SetUnitControlDisabled(false)', 'Re-enables unit control');
  assertLuaSyntax(result.lua);
});

// Test 12: SetSession, GetSession, ClearSession, MapSize, RestartMatch
test('SetSession, GetSession, ClearSession, MapSize, RestartMatch', () => {
  const g = ScGraph.newGraph('session_map_restart');
  g.nodes.push(
    { id: 'n1', type: 'OnGameStart', x: 0, y: 0, values: {} },
    { id: 'n2', type: 'GetSession', x: 0, y: 150, values: { key: 'level' } },
    { id: 'n3', type: 'Math', x: 200, y: 150, values: { op: '+', b: 1 } },
    { id: 'n4', type: 'SetSession', x: 400, y: 0, values: { key: 'level' } },
    { id: 'n5', type: 'SetSession', x: 600, y: 0, values: { key: 'army', value: '50' } },
    { id: 'n6', type: 'SetSession', x: 800, y: 0, values: { key: 'tag', value: 'boss' } },
    { id: 'n7', type: 'MapSize', x: 800, y: 150, values: {} },
    { id: 'n8', type: 'ToText', x: 1000, y: 150, values: {} },
    { id: 'n9', type: 'ShowMessage', x: 1000, y: 0, values: {} },
    { id: 'n10', type: 'RestartMatch', x: 1200, y: 0, values: {} },
    { id: 'n11', type: 'ClearSession', x: 1400, y: 0, values: {} }
  );
  g.links.push(
    { from: ['n1', 'exec'], to: ['n4', 'exec'] },
    { from: ['n2', 'value'], to: ['n3', 'a'] },
    { from: ['n3', 'result'], to: ['n4', 'value'] },
    { from: ['n4', 'exec'], to: ['n5', 'exec'] },
    { from: ['n5', 'exec'], to: ['n6', 'exec'] },
    { from: ['n6', 'exec'], to: ['n9', 'exec'] },
    { from: ['n7', 'tiles'], to: ['n8', 'value'] },
    { from: ['n8', 'text'], to: ['n9', 'text'] },
    { from: ['n9', 'exec'], to: ['n10', 'exec'] },
    { from: ['n10', 'exec'], to: ['n11', 'exec'] }
  );
  const result = ScGraph.toLua(g);
  assertEqual(result.errors.length, 0, 'Should have no errors');
  assertContains(result.lua, 'Game.SetSession("level", (Game.GetSession("level") + 1))', 'Reads and updates numeric session value');
  assertContains(result.lua, 'Game.SetSession("army", 50)', 'Converts numeric literal string in SetSession to Lua number');
  assertContains(result.lua, 'Game.SetSession("tag", "boss")', 'Preserves string value in SetSession');
  assertContains(result.lua, 'UI.Message(tostring(Game.MapSize()))', 'Reads Game.MapSize()');
  assertContains(result.lua, 'Game.Restart()', 'Emits Game.Restart()');
  assertContains(result.lua, 'Game.ClearSession()', 'Emits Game.ClearSession()');
  assertLuaSyntax(result.lua);
});

// Test 13: All built-in templates (including click-to-spawn) compile cleanly
test('All built-in templates compile without errors', () => {
  const clickTpl = ScGraph.TEMPLATES.find(t => t.id === 'click-to-spawn');
  if (!clickTpl) throw new Error('Missing click-to-spawn template');

  for (const tpl of ScGraph.TEMPLATES) {
    const g = ScGraph.newGraph(tpl.id);
    const refToId = {};
    tpl.nodes.forEach((spec, idx) => {
      const id = 'n' + (idx + 1);
      refToId[spec.ref] = id;
      const def = ScGraph.NODES[spec.type];
      const values = def && def.values ? JSON.parse(JSON.stringify(def.values)) : {};
      if (spec.values) Object.assign(values, JSON.parse(JSON.stringify(spec.values)));
      g.nodes.push({ id, type: spec.type, x: spec.x, y: spec.y, values });
    });
    for (const [fRef, fPort, tRef, tPort] of tpl.links) {
      g.links.push({ from: [refToId[fRef], fPort], to: [refToId[tRef], tPort] });
    }
    const result = ScGraph.toLua(g);
    assertEqual(result.errors.length, 0, `Template ${tpl.id} should have no errors`);
    assertLuaSyntax(result.lua);
    if (tpl.id === 'click-to-spawn') {
      assertContains(result.lua, 'Game.SetMapClickCapture(true)', 'Template enables map capture');
      assertContains(result.lua, 'Game.OnMapClick(', 'Template uses OnMapClick');
      assertContains(result.lua, ':CreateUnit(4,', 'Template spawns unit');
      assertContains(result.lua, 'Sound.Play("Villager")', 'Template plays Villager sound');
    }
  }
});

// Test 14: Validation errors for new required inputs
test('Validation errors for missing PlaySoundFile, SetSession, GetSession inputs', () => {
  const g = ScGraph.newGraph('new_nodes_validation');
  g.nodes.push(
    { id: 'n1', type: 'OnGameStart', x: 0, y: 0, values: {} },
    { id: 'n2', type: 'PlaySoundFile', x: 200, y: 0, values: { path: '' } },
    { id: 'n3', type: 'SetSession', x: 400, y: 0, values: { key: '', value: '' } },
    { id: 'n4', type: 'GetSession', x: 400, y: 150, values: { key: '' } },
    { id: 'n5', type: 'Log', x: 600, y: 0, values: {} }
  );
  g.links.push(
    { from: ['n1', 'exec'], to: ['n2', 'exec'] },
    { from: ['n2', 'exec'], to: ['n3', 'exec'] },
    { from: ['n3', 'exec'], to: ['n5', 'exec'] },
    { from: ['n4', 'value'], to: ['n5', 'text'] }
  );
  const result = ScGraph.toLua(g);
  if (!result.errors.some(e => e.nodeId === 'n2' && e.message.includes('path'))) {
    throw new Error('Expected PlaySoundFile path error');
  }
  if (!result.errors.some(e => e.nodeId === 'n3' && e.message.includes('key'))) {
    throw new Error('Expected SetSession key error');
  }
  if (!result.errors.some(e => e.nodeId === 'n4' && e.message.includes('key'))) {
    throw new Error('Expected GetSession key error');
  }
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

// Script graph editor: window.ScGraph (see graph spec).
// Unity Bolt-style visual scripting for Lua event scripts.
// Plain ES2020, no modules, works offline.

(function() {
  'use strict';

  // ==========================================================================
  // Node Library
  // ==========================================================================

  const NODES = {
    // Events (red, flow out only)
    OnGameStart: {
      category: 'event',
      title: 'On Game Start',
      tooltip: 'Runs when the game starts',
      inputs: [],
      outputs: [{ name: 'exec', kind: 'flow' }]
    },
    OnGameModeStart: {
      category: 'event',
      title: 'On Game Mode Start',
      tooltip: 'Runs at game start only when the given custom game mode is active',
      inputs: [],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { mode: '' }
    },
    EverySecond: {
      category: 'event',
      title: 'Every Second',
      tooltip: 'Runs once per game second',
      inputs: [],
      outputs: [{ name: 'exec', kind: 'flow' }]
    },
    OnAgeUp: {
      category: 'event',
      title: 'On Age Up',
      tooltip: 'Runs when any player advances age',
      inputs: [],
      outputs: [{ name: 'exec', kind: 'flow' }, { name: 'player', type: 'player' }, { name: 'age', type: 'text' }]
    },
    OnUnitCreated: {
      category: 'event',
      title: 'On Unit Created',
      tooltip: 'Runs when a unit is created',
      inputs: [],
      outputs: [{ name: 'exec', kind: 'flow' }, { name: 'unit', type: 'unit' }]
    },
    OnUnitDied: {
      category: 'event',
      title: 'On Unit Died',
      tooltip: 'Runs when a unit dies, converts, or is deleted',
      inputs: [],
      outputs: [{ name: 'exec', kind: 'flow' }, { name: 'id', type: 'number' }, { name: 'player', type: 'player' }]
    },
    OnResearch: {
      category: 'event',
      title: 'On Research',
      tooltip: 'Runs when a technology is researched',
      inputs: [],
      outputs: [{ name: 'exec', kind: 'flow' }, { name: 'player', type: 'player' }, { name: 'tech', type: 'number' }]
    },
    OnBuildingDone: {
      category: 'event',
      title: 'On Building Done',
      tooltip: 'Runs when a building is finished',
      inputs: [],
      outputs: [{ name: 'exec', kind: 'flow' }, { name: 'unit', type: 'unit' }]
    },
    OnBuildingButton: {
      category: 'event',
      title: 'On Building Button',
      tooltip: 'Runs when a custom building command button is pressed',
      inputs: [],
      outputs: [{ name: 'exec', kind: 'flow' }, { name: 'building', type: 'unit' }],
      values: { button: '' }
    },
    ButtonCondition: {
      category: 'event',
      title: 'Button Condition',
      tooltip: 'Defines whether a building button is enabled/active or disabled/hidden',
      inputs: [],
      outputs: [{ name: 'exec', kind: 'flow' }, { name: 'building', type: 'unit' }],
      values: { button: '' }
    },
    EveryNSeconds: {
      category: 'event',
      title: 'Every N Seconds',
      tooltip: 'Runs repeatedly every N seconds',
      inputs: [],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { seconds: 5 }
    },
    OnMapClick: {
      category: 'event',
      title: 'On Map Click',
      tooltip: 'Runs on map click while map click capture is enabled (button is "left" or "right")',
      inputs: [],
      outputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'x', type: 'number' },
        { name: 'y', type: 'number' },
        { name: 'button', type: 'text' }
      ]
    },

    // Actions (blue, flow in+out)
    AddResource: {
      category: 'action',
      title: 'Add Resource',
      tooltip: 'Give or take resources from a player',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'player', type: 'player', required: true },
        { name: 'resource', type: 'text', required: true },
        { name: 'amount', type: 'number', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { resource: 'Gold' }
    },
    CreateUnit: {
      category: 'action',
      title: 'Create Unit',
      tooltip: 'Spawn a unit at coordinates',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'player', type: 'player', required: true },
        { name: 'unit', type: 'number', required: true },
        { name: 'x', type: 'number', required: true },
        { name: 'y', type: 'number', required: true },
        { name: 'capturable', label: 'Can be captured', type: 'bool' }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { unit: 125, capturable: true }
    },
    ResearchTech: {
      category: 'action',
      title: 'Research Tech',
      tooltip: 'Instantly research a technology',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'player', type: 'player', required: true },
        { name: 'tech', type: 'number', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { tech: 0 }
    },
    SetAge: {
      category: 'action',
      title: 'Set Age',
      tooltip: 'Change player age',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'player', type: 'player', required: true },
        { name: 'age', type: 'text', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { age: 'Feudal' }
    },
    DeleteUnit: {
      category: 'action',
      title: 'Delete Unit',
      tooltip: 'Remove a unit from the game',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'unit', type: 'unit', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }]
    },
    ShowMessage: {
      category: 'action',
      title: 'Show Message',
      tooltip: 'Display a message on screen',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'text', type: 'text', required: true },
        { name: 'color', type: 'number' }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { text: 'Hello', color: 36 }
    },
    Chat: {
      category: 'action',
      title: 'Chat',
      tooltip: 'Send a chat message',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'text', type: 'text', required: true },
        { name: 'from', type: 'text' }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { text: 'Message', from: '' }
    },
    PlaySound: {
      category: 'action',
      title: 'Play Sound',
      tooltip: 'Play a UI sound effect',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'sound', type: 'text', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { sound: 'Chat' }
    },
    PlaySoundFile: {
      category: 'action',
      title: 'Play Sound File',
      tooltip: 'Play a WAV or MP3 sound file relative to the game folder (Sound.PlayFile)',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'path', type: 'text', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { path: '' }
    },
    Wait: {
      category: 'action',
      title: 'Wait',
      tooltip: 'Pause execution for N seconds (only in events)',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'seconds', type: 'number', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { seconds: 1 }
    },
    Log: {
      category: 'action',
      title: 'Log',
      tooltip: 'Print to the Wololoader log',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'text', type: 'text', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { text: 'Debug' }
    },
    SetVariable: {
      category: 'action',
      title: 'Set Variable',
      tooltip: 'Assign a value to a graph variable',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'var', type: 'text', required: true },
        { name: 'value', type: 'any', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { var: '' }
    },
    SetButtonAvailable: {
      category: 'action',
      title: 'Set Button Available',
      tooltip: 'Set whether the button is available (true) or disabled (false)',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'available', type: 'bool', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { available: true }
    },
    AddBuildingButton: {
      category: 'action',
      title: 'Add Building Button',
      tooltip: 'Register a custom command button on a building (run once at game start)',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'building', type: 'number', required: true },
        { name: 'page', type: 'number' },
        { name: 'button', type: 'number', required: true },
        { name: 'name', type: 'text', required: true },
        { name: 'text', type: 'text' },
        { name: 'help', type: 'text' },
        { name: 'icon', type: 'text' },
        { name: 'shared', label: 'Allies can use it', type: 'bool' }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { building: 84, page: 1, button: 10, name: '', text: '', help: '', icon: '32', shared: false }
    },
    WinGame: {
      category: 'action',
      title: 'Win Game',
      tooltip: 'Declare a player (and optionally their allies) the winner',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'player', type: 'player', required: true },
        { name: 'withAllies', label: 'with allies', type: 'bool' }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { withAllies: false }
    },
    DefeatPlayer: {
      category: 'action',
      title: 'Defeat Player',
      tooltip: 'Immediately defeat a player',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'player', type: 'player', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }]
    },
    MoveUnit: {
      category: 'action',
      title: 'Move Unit',
      tooltip: 'Order a unit to move to map coordinates',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'unit', type: 'unit', required: true },
        { name: 'x', type: 'number', required: true },
        { name: 'y', type: 'number', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { x: 0, y: 0 }
    },
    AttackUnit: {
      category: 'action',
      title: 'Attack',
      tooltip: 'Order a unit to attack a target unit or building',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'unit', type: 'unit', required: true },
        { name: 'target', type: 'unit', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }]
    },
    AddCustomResource: {
      category: 'action',
      title: 'Add Custom Resource',
      tooltip: 'Add or subtract a custom or numeric resource (by key, name, or ID)',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'player', type: 'player', required: true },
        { name: 'resource', type: 'text', required: true },
        { name: 'amount', type: 'number', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { resource: '', amount: 0 }
    },
    SetResource: {
      category: 'action',
      title: 'Set Resource',
      tooltip: 'Set a vanilla, custom, or numeric resource to an exact amount',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'player', type: 'player', required: true },
        { name: 'resource', type: 'text', required: true },
        { name: 'amount', type: 'number', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { resource: 'Gold', amount: 0 }
    },
    SpawnNearUnit: {
      category: 'action',
      title: 'Spawn Near Unit',
      tooltip: 'Spawn a unit or deposit at an (dx, dy) tile offset from an existing unit',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'player', type: 'player', required: true },
        { name: 'unit', label: 'unit ID', type: 'number', required: true },
        { name: 'near', label: 'near unit', type: 'unit', required: true },
        { name: 'dx', type: 'number' },
        { name: 'dy', type: 'number' },
        { name: 'capturable', label: 'Can be captured', type: 'bool' }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { unit: 4, dx: 3, dy: 0, capturable: false }
    },
    SetCounter: {
      category: 'action',
      title: 'Set HUD Counter',
      tooltip: 'Create or update a named HUD counter in the top-right countdown area',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'name', type: 'text', required: true },
        { name: 'text', type: 'text', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { name: 'status', text: 'Score: 0' }
    },
    HideCounter: {
      category: 'action',
      title: 'Hide HUD Counter',
      tooltip: 'Hide a named HUD counter created with Set HUD Counter',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'name', type: 'text', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { name: 'status' }
    },
    SetMapClickCapture: {
      category: 'action',
      title: 'Set Map Click Capture',
      tooltip: 'Enable or disable capturing map clicks for On Map Click instead of normal game clicks',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'enabled', type: 'bool', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { enabled: true }
    },
    SetUnitControlDisabled: {
      category: 'action',
      title: 'Set Unit Control Disabled',
      tooltip: 'Disable or re-enable manual right-click unit move and attack orders',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'disabled', type: 'bool', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { disabled: true }
    },
    RestartMatch: {
      category: 'action',
      title: 'Restart Match',
      tooltip: 'Restart the current single-player match with a new random map (keeps session data)',
      inputs: [{ name: 'exec', kind: 'flow' }],
      outputs: [{ name: 'exec', kind: 'flow' }]
    },
    SetSession: {
      category: 'action',
      title: 'Set Session Value',
      tooltip: 'Save a number or text value across Match Restart (cleared on new lobby matches)',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'key', type: 'text', required: true },
        { name: 'value', type: 'any', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { key: '', value: '' }
    },
    ClearSession: {
      category: 'action',
      title: 'Clear Session',
      tooltip: 'Clear all saved session values (Game.ClearSession)',
      inputs: [{ name: 'exec', kind: 'flow' }],
      outputs: [{ name: 'exec', kind: 'flow' }]
    },

    // Flow control (yellow)
    Branch: {
      category: 'flow',
      title: 'Branch',
      tooltip: 'If/else conditional execution',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'condition', type: 'bool', required: true }
      ],
      outputs: [
        { name: 'true', kind: 'flow' },
        { name: 'false', kind: 'flow' }
      ]
    },
    ForLoop: {
      category: 'flow',
      title: 'For Loop',
      tooltip: 'Loop from start to end',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'from', type: 'number', required: true },
        { name: 'to', type: 'number', required: true }
      ],
      outputs: [
        { name: 'body', kind: 'flow' },
        { name: 'index', type: 'number' },
        { name: 'done', kind: 'flow' }
      ],
      values: { from: 1, to: 10 }
    },
    ForEachPlayer: {
      category: 'flow',
      title: 'For Each Player',
      tooltip: 'Loop over every player in the match (Game.Players())',
      inputs: [
        { name: 'exec', kind: 'flow' }
      ],
      outputs: [
        { name: 'body', kind: 'flow' },
        { name: 'player', type: 'player' },
        { name: 'done', kind: 'flow' }
      ]
    },
    ForEachUnit: {
      category: 'flow',
      title: 'For Each Unit',
      tooltip: 'Loop over a list of units',
      inputs: [
        { name: 'exec', kind: 'flow' },
        { name: 'units', type: 'units', required: true }
      ],
      outputs: [
        { name: 'body', kind: 'flow' },
        { name: 'unit', type: 'unit' },
        { name: 'done', kind: 'flow' }
      ]
    },
    Sequence: {
      category: 'flow',
      title: 'Sequence',
      tooltip: 'Execute outputs in order',
      inputs: [{ name: 'exec', kind: 'flow' }],
      outputs: [
        { name: 'then1', kind: 'flow' },
        { name: 'then2', kind: 'flow' },
        { name: 'then3', kind: 'flow' }
      ]
    },

    // Data (green, pure expressions)
    LocalPlayer: {
      category: 'data',
      title: 'Local Player',
      tooltip: 'The player on this machine',
      inputs: [],
      outputs: [{ name: 'player', type: 'player' }]
    },
    PlayerById: {
      category: 'data',
      title: 'Player By ID',
      tooltip: 'Get player by ID (1-8)',
      inputs: [{ name: 'id', type: 'number', required: true }],
      outputs: [{ name: 'player', type: 'player' }],
      values: { id: 1 }
    },
    PlayerResource: {
      category: 'data',
      title: 'Player Resource',
      tooltip: 'Get player resource amount',
      inputs: [
        { name: 'player', type: 'player', required: true },
        { name: 'resource', type: 'text', required: true }
      ],
      outputs: [{ name: 'amount', type: 'number' }],
      values: { resource: 'Gold' }
    },
    PlayerAge: {
      category: 'data',
      title: 'Player Age',
      tooltip: 'Get player age name',
      inputs: [{ name: 'player', type: 'player', required: true }],
      outputs: [{ name: 'age', type: 'text' }]
    },
    PlayerUnits: {
      category: 'data',
      title: 'Player Units',
      tooltip: 'Get units owned by player',
      inputs: [
        { name: 'player', type: 'player', required: true },
        { name: 'class', type: 'text' }
      ],
      outputs: [{ name: 'units', type: 'units' }],
      values: { class: 'All' }
    },
    Count: {
      category: 'data',
      title: 'Count',
      tooltip: 'Count items in a list',
      inputs: [{ name: 'list', type: 'units', required: true }],
      outputs: [{ name: 'count', type: 'number' }]
    },
    UnitProperty: {
      category: 'data',
      title: 'Unit Property',
      tooltip: 'Get unit property',
      inputs: [
        { name: 'unit', type: 'unit', required: true },
        { name: 'property', type: 'text', required: true }
      ],
      outputs: [{ name: 'value', type: 'any' }],
      values: { property: 'X' }
    },
    UnitOwner: {
      category: 'data',
      title: 'Unit Owner',
      tooltip: 'Get the player who owns this unit',
      inputs: [{ name: 'unit', type: 'unit', required: true }],
      outputs: [{ name: 'player', type: 'player' }]
    },
    IsBuilding: {
      category: 'data',
      title: 'Is Building',
      tooltip: 'Check if unit is a building',
      inputs: [{ name: 'unit', type: 'unit', required: true }],
      outputs: [{ name: 'result', type: 'bool' }]
    },
    GameTime: {
      category: 'data',
      title: 'Game Time',
      tooltip: 'Current game time in seconds',
      inputs: [],
      outputs: [{ name: 'time', type: 'number' }]
    },
    Math: {
      category: 'data',
      title: 'Math',
      tooltip: 'Math operation',
      inputs: [
        { name: 'a', type: 'number', required: true },
        { name: 'op', type: 'text', required: true },
        { name: 'b', type: 'number', required: true }
      ],
      outputs: [{ name: 'result', type: 'number' }],
      values: { a: 0, op: '+', b: 0 }
    },
    Compare: {
      category: 'data',
      title: 'Compare',
      tooltip: 'Compare two values',
      inputs: [
        { name: 'a', type: 'any', required: true },
        { name: 'op', type: 'text', required: true },
        { name: 'b', type: 'any', required: true }
      ],
      outputs: [{ name: 'result', type: 'bool' }],
      values: { a: 0, op: '==', b: 0 }
    },
    And: {
      category: 'data',
      title: 'And',
      tooltip: 'Logical AND',
      inputs: [
        { name: 'a', type: 'bool', required: true },
        { name: 'b', type: 'bool', required: true }
      ],
      outputs: [{ name: 'result', type: 'bool' }]
    },
    Or: {
      category: 'data',
      title: 'Or',
      tooltip: 'Logical OR',
      inputs: [
        { name: 'a', type: 'bool', required: true },
        { name: 'b', type: 'bool', required: true }
      ],
      outputs: [{ name: 'result', type: 'bool' }]
    },
    Not: {
      category: 'data',
      title: 'Not',
      tooltip: 'Logical NOT',
      inputs: [{ name: 'value', type: 'bool', required: true }],
      outputs: [{ name: 'result', type: 'bool' }]
    },
    JoinText: {
      category: 'data',
      title: 'Join Text',
      tooltip: 'Concatenate two strings',
      inputs: [
        { name: 'a', type: 'any', required: true },
        { name: 'b', type: 'any', required: true }
      ],
      outputs: [{ name: 'result', type: 'text' }],
      values: { a: '', b: '' }
    },
    ToText: {
      category: 'data',
      title: 'To Text',
      tooltip: 'Convert any value to text',
      inputs: [{ name: 'value', type: 'any', required: true }],
      outputs: [{ name: 'text', type: 'text' }]
    },
    RandomNumber: {
      category: 'data',
      title: 'Random Number',
      tooltip: 'Generate random number (differs per player in MP)',
      inputs: [
        { name: 'min', type: 'number', required: true },
        { name: 'max', type: 'number', required: true }
      ],
      outputs: [{ name: 'result', type: 'number' }],
      values: { min: 1, max: 100 }
    },
    GetVariable: {
      category: 'data',
      title: 'Get Variable',
      tooltip: 'Read a graph variable',
      inputs: [{ name: 'var', type: 'text', required: true }],
      outputs: [{ name: 'value', type: 'any' }],
      values: { var: '' }
    },
    GameMode: {
      category: 'data',
      title: 'Game Mode',
      tooltip: 'Key of the active custom game mode (or nil for vanilla modes)',
      inputs: [],
      outputs: [{ name: 'mode', type: 'text' }]
    },
    ModeEnemyPlayer: {
      category: 'data',
      title: 'Mode Enemy Player',
      tooltip: 'The custom game mode enemy player (or nil if none)',
      inputs: [],
      outputs: [{ name: 'player', type: 'player' }]
    },
    UnitsOfType: {
      category: 'data',
      title: 'Units Of Type',
      tooltip: 'Get all units of a specific unit type ID owned by a player',
      inputs: [
        { name: 'player', type: 'player', required: true },
        { name: 'unitType', label: 'unit type', type: 'number', required: true }
      ],
      outputs: [{ name: 'units', type: 'units' }],
      values: { unitType: 125 }
    },
    ModSetting: {
      category: 'data',
      title: 'Mod Setting',
      tooltip: 'Read a mod setting value declared in mod.json (MOD.settings)',
      inputs: [{ name: 'setting', type: 'text', required: true }],
      outputs: [{ name: 'value', type: 'any' }],
      values: { setting: '' }
    },
    PlayerCiv: {
      category: 'data',
      title: 'Player Civ',
      tooltip: 'Get player civilization ID (1-31 vanilla, 35+ custom)',
      inputs: [{ name: 'player', type: 'player', required: true }],
      outputs: [{ name: 'civ', type: 'number' }]
    },
    GetResource: {
      category: 'data',
      title: 'Get Resource',
      tooltip: 'Get a vanilla, custom, or numeric resource amount (by key, name, or ID)',
      inputs: [
        { name: 'player', type: 'player', required: true },
        { name: 'resource', type: 'text', required: true }
      ],
      outputs: [{ name: 'amount', type: 'number' }],
      values: { resource: '' }
    },
    GaiaPlayer: {
      category: 'data',
      title: 'Gaia Player',
      tooltip: 'Neutral Gaia player (Game.Player(0)) — use for spawning deposits, relics, and animals',
      inputs: [],
      outputs: [{ name: 'player', type: 'player' }]
    },
    ResourceDeposit: {
      category: 'data',
      title: 'Resource Deposit ID',
      tooltip: 'Get the Gaia deposit unit ID for a gatherable custom resource (Game.ResourceDeposit)',
      inputs: [{ name: 'resource', type: 'text', required: true }],
      outputs: [{ name: 'unit', label: 'unit ID', type: 'number' }],
      values: { resource: 'crystal' }
    },
    PlayerIsEnemy: {
      category: 'data',
      title: 'Is Enemy',
      tooltip: 'True when player A considers player B an enemy',
      inputs: [
        { name: 'a', label: 'player A', type: 'player', required: true },
        { name: 'b', label: 'player B', type: 'player', required: true }
      ],
      outputs: [{ name: 'result', type: 'bool' }]
    },
    SelectedUnits: {
      category: 'data',
      title: 'Selected Units',
      tooltip: 'Units currently selected by the local player (Game.Selected())',
      inputs: [],
      outputs: [{ name: 'units', type: 'units' }]
    },
    UnitPosition: {
      category: 'data',
      title: 'Unit Position',
      tooltip: 'Get both X and Y map coordinates of a unit',
      inputs: [{ name: 'unit', type: 'unit', required: true }],
      outputs: [
        { name: 'x', type: 'number' },
        { name: 'y', type: 'number' }
      ]
    },
    MapSize: {
      category: 'data',
      title: 'Map Size',
      tooltip: 'Side length of the map in tiles from the lobby size (Game.MapSize())',
      inputs: [],
      outputs: [{ name: 'tiles', type: 'number' }]
    },
    GetSession: {
      category: 'data',
      title: 'Get Session Value',
      tooltip: 'Read a value saved with Set Session Value across Match Restart (nil if unset)',
      inputs: [{ name: 'key', type: 'text', required: true }],
      outputs: [{ name: 'value', type: 'any' }],
      values: { key: '' }
    }
  };

  // ==========================================================================
  // Categories, Templates & Graph Creation
  // ==========================================================================

  const CATEGORY_ORDER = ['event', 'action', 'flow', 'data'];
  const CATEGORY_LABELS = {
    event: 'Events',
    action: 'Actions',
    flow: 'Flow',
    data: 'Values'
  };

  const TEMPLATES = [
    {
      id: 'unit-created-gold',
      title: 'Give gold when a unit is created',
      desc: 'On Unit Created -> Unit Owner -> Add Resource (Gold)',
      nodes: [
        { ref: 'ev', type: 'OnUnitCreated', x: 60, y: 120 },
        { ref: 'own', type: 'UnitOwner', x: 340, y: 205 },
        { ref: 'add', type: 'AddResource', x: 620, y: 120, values: { resource: 'Gold', amount: 25 } }
      ],
      links: [
        ['ev', 'exec', 'add', 'exec'],
        ['ev', 'unit', 'own', 'unit'],
        ['own', 'player', 'add', 'player']
      ]
    },
    {
      id: 'market-button',
      title: 'Button on the Market',
      desc: 'Register a command button on Market (84) and handle clicks',
      nodes: [
        { ref: 'start', type: 'OnGameStart', x: 60, y: 80 },
        { ref: 'btn', type: 'AddBuildingButton', x: 350, y: 80, values: { building: 84, page: 1, button: 10, name: 'market_gift', text: 'Trade Gift', help: 'Gain 50 Gold', icon: '32' } },
        { ref: 'press', type: 'OnBuildingButton', x: 60, y: 420, values: { button: 'market_gift' } },
        { ref: 'own', type: 'UnitOwner', x: 350, y: 505 },
        { ref: 'gold', type: 'AddResource', x: 630, y: 420, values: { resource: 'Gold', amount: 50 } }
      ],
      links: [
        ['start', 'exec', 'btn', 'exec'],
        ['press', 'exec', 'gold', 'exec'],
        ['press', 'building', 'own', 'unit'],
        ['own', 'player', 'gold', 'player']
      ]
    },
    {
      id: 'game-mode-rules',
      title: 'Game mode rules at start',
      desc: 'On Game Mode Start -> give starting gold and show a message',
      nodes: [
        { ref: 'mode', type: 'OnGameModeStart', x: 60, y: 110, values: { mode: 'my_mode' } },
        { ref: 'lp', type: 'LocalPlayer', x: 60, y: 270 },
        { ref: 'gold', type: 'AddResource', x: 360, y: 110, values: { resource: 'Gold', amount: 500 } },
        { ref: 'msg', type: 'ShowMessage', x: 660, y: 110, values: { text: 'Mode started!', color: 36 } }
      ],
      links: [
        ['mode', 'exec', 'gold', 'exec'],
        ['lp', 'player', 'gold', 'player'],
        ['gold', 'exec', 'msg', 'exec']
      ]
    },
    {
      id: 'every-10s-custom-res',
      title: 'Every 10 seconds add a custom resource',
      desc: 'Every N Seconds (10) -> Local Player -> Add Custom Resource',
      nodes: [
        { ref: 'ev', type: 'EveryNSeconds', x: 60, y: 120, values: { seconds: 10 } },
        { ref: 'lp', type: 'LocalPlayer', x: 60, y: 270 },
        { ref: 'res', type: 'AddCustomResource', x: 360, y: 120, values: { resource: 'faith', amount: 5 } }
      ],
      links: [
        ['ev', 'exec', 'res', 'exec'],
        ['lp', 'player', 'res', 'player']
      ]
    },
    {
      id: 'spawn-deposits-tc',
      title: 'Spawn custom deposits near every Town Center',
      desc: 'On Game Start -> For Each Player -> Town Centers (109) -> Spawn Near Unit (Gaia)',
      nodes: [
        { ref: 'start', type: 'OnGameStart', x: 40, y: 100 },
        { ref: 'fep', type: 'ForEachPlayer', x: 280, y: 100 },
        { ref: 'tcs', type: 'UnitsOfType', x: 280, y: 280, values: { unitType: 109 } },
        { ref: 'feu', type: 'ForEachUnit', x: 560, y: 100 },
        { ref: 'gaia', type: 'GaiaPlayer', x: 560, y: 290 },
        { ref: 'dep', type: 'ResourceDeposit', x: 560, y: 390, values: { resource: 'crystal' } },
        { ref: 'sp1', type: 'SpawnNearUnit', x: 860, y: 100, values: { dx: 5, dy: 0 } },
        { ref: 'sp2', type: 'SpawnNearUnit', x: 1140, y: 100, values: { dx: -5, dy: 0 } }
      ],
      links: [
        ['start', 'exec', 'fep', 'exec'],
        ['fep', 'body', 'feu', 'exec'],
        ['fep', 'player', 'tcs', 'player'],
        ['tcs', 'units', 'feu', 'units'],
        ['feu', 'body', 'sp1', 'exec'],
        ['gaia', 'player', 'sp1', 'player'],
        ['dep', 'unit', 'sp1', 'unit'],
        ['feu', 'unit', 'sp1', 'near'],
        ['sp1', 'exec', 'sp2', 'exec'],
        ['gaia', 'player', 'sp2', 'player'],
        ['dep', 'unit', 'sp2', 'unit'],
        ['feu', 'unit', 'sp2', 'near']
      ]
    },
    {
      id: 'passive-building-income',
      title: 'Passive custom resource per building',
      desc: 'Every 10s -> For Each Player -> count Monasteries (104) -> Add Custom Resource',
      nodes: [
        { ref: 'ev', type: 'EveryNSeconds', x: 40, y: 110, values: { seconds: 10 } },
        { ref: 'fep', type: 'ForEachPlayer', x: 300, y: 110 },
        { ref: 'bld', type: 'UnitsOfType', x: 300, y: 290, values: { unitType: 104 } },
        { ref: 'cnt', type: 'Count', x: 580, y: 290 },
        { ref: 'add', type: 'AddCustomResource', x: 840, y: 110, values: { resource: 'faith', amount: 1 } }
      ],
      links: [
        ['ev', 'exec', 'fep', 'exec'],
        ['fep', 'body', 'add', 'exec'],
        ['fep', 'player', 'bld', 'player'],
        ['bld', 'units', 'cnt', 'list'],
        ['fep', 'player', 'add', 'player'],
        ['cnt', 'count', 'add', 'amount']
      ]
    },
    {
      id: 'hud-counter-update',
      title: 'Live HUD counter in top-right bar',
      desc: 'Every Second -> read custom resource -> Set HUD Counter',
      nodes: [
        { ref: 'ev', type: 'EverySecond', x: 40, y: 110 },
        { ref: 'lp', type: 'LocalPlayer', x: 40, y: 260 },
        { ref: 'res', type: 'GetResource', x: 280, y: 260, values: { resource: 'crystal' } },
        { ref: 'txt', type: 'ToText', x: 540, y: 260 },
        { ref: 'join', type: 'JoinText', x: 760, y: 260, values: { a: 'Crystal: ' } },
        { ref: 'ctr', type: 'SetCounter', x: 1020, y: 110, values: { name: 'crystal_hud' } }
      ],
      links: [
        ['ev', 'exec', 'ctr', 'exec'],
        ['lp', 'player', 'res', 'player'],
        ['res', 'amount', 'txt', 'value'],
        ['txt', 'text', 'join', 'b'],
        ['join', 'result', 'ctr', 'text']
      ]
    },
    {
      id: 'castle-spend-resource',
      title: 'Spend custom resource at Castle to summon a unit',
      desc: 'Castle (82) button with cost check (50 crystal) -> deduct resource & spawn Champion',
      nodes: [
        { ref: 'start', type: 'OnGameStart', x: 40, y: 60 },
        { ref: 'btn', type: 'AddBuildingButton', x: 320, y: 60, values: { building: 82, page: 1, button: 10, name: 'summon_champ', text: 'Summon Champion', help: 'Costs 50 Crystal', icon: '32' } },
        { ref: 'cond', type: 'ButtonCondition', x: 40, y: 380, values: { button: 'summon_champ' } },
        { ref: 'cown', type: 'UnitOwner', x: 320, y: 470 },
        { ref: 'cres', type: 'GetResource', x: 560, y: 470, values: { resource: 'crystal' } },
        { ref: 'cmp', type: 'Compare', x: 820, y: 470, values: { op: '>=', b: 50 } },
        { ref: 'avail', type: 'SetButtonAvailable', x: 1060, y: 380 },
        { ref: 'press', type: 'OnBuildingButton', x: 40, y: 660, values: { button: 'summon_champ' } },
        { ref: 'pown', type: 'UnitOwner', x: 320, y: 760 },
        { ref: 'pay', type: 'AddCustomResource', x: 580, y: 660, values: { resource: 'crystal', amount: -50 } },
        { ref: 'sp', type: 'SpawnNearUnit', x: 880, y: 660, values: { unit: 567, dx: 3, dy: 3 } }
      ],
      links: [
        ['start', 'exec', 'btn', 'exec'],
        ['cond', 'exec', 'avail', 'exec'],
        ['cond', 'building', 'cown', 'unit'],
        ['cown', 'player', 'cres', 'player'],
        ['cres', 'amount', 'cmp', 'a'],
        ['cmp', 'result', 'avail', 'available'],
        ['press', 'exec', 'pay', 'exec'],
        ['press', 'building', 'pown', 'unit'],
        ['pown', 'player', 'pay', 'player'],
        ['pay', 'exec', 'sp', 'exec'],
        ['pown', 'player', 'sp', 'player'],
        ['press', 'building', 'sp', 'near']
      ]
    },
    {
      id: 'click-to-spawn',
      title: 'Click on the map to spawn units',
      desc: 'Enable map click capture at start -> On Map Click -> left-click spawns Archer + plays Villager sound, right-click exits',
      nodes: [
        { ref: 'start', type: 'OnGameStart', x: 40, y: 60 },
        { ref: 'cap_on', type: 'SetMapClickCapture', x: 320, y: 60, values: { enabled: true } },
        { ref: 'click', type: 'OnMapClick', x: 40, y: 260 },
        { ref: 'cmp', type: 'Compare', x: 320, y: 380, values: { op: '==', b: 'left' } },
        { ref: 'br', type: 'Branch', x: 600, y: 260 },
        { ref: 'lp', type: 'LocalPlayer', x: 600, y: 420 },
        { ref: 'spawn', type: 'CreateUnit', x: 880, y: 220, values: { unit: 4, capturable: true } },
        { ref: 'sfx', type: 'PlaySound', x: 1180, y: 220, values: { sound: 'Villager' } },
        { ref: 'cap_off', type: 'SetMapClickCapture', x: 880, y: 440, values: { enabled: false } }
      ],
      links: [
        ['start', 'exec', 'cap_on', 'exec'],
        ['click', 'exec', 'br', 'exec'],
        ['click', 'button', 'cmp', 'a'],
        ['cmp', 'result', 'br', 'condition'],
        ['br', 'true', 'spawn', 'exec'],
        ['lp', 'player', 'spawn', 'player'],
        ['click', 'x', 'spawn', 'x'],
        ['click', 'y', 'spawn', 'y'],
        ['spawn', 'exec', 'sfx', 'exec'],
        ['br', 'false', 'cap_off', 'exec']
      ]
    }
  ];

  function escHtml(str) {
    if (str === null || str === undefined) return '';
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }

  function newGraph(name) {
    return {
      version: 1,
      name: name || 'New Graph',
      nodes: [],
      links: [],
      vars: []
    };
  }

  // ==========================================================================
  // Editor Mount
  // ==========================================================================

  function mount(container, graph, opts) {
    opts = opts || {};
    const catalog = opts.catalog || {};
    const onChange = opts.onChange || function() {};

    const state = {
      graph: JSON.parse(JSON.stringify(graph)),
      container,
      catalog,
      onChange,
      pan: { x: 0, y: 0 },
      zoom: 1,
      selectedNodes: new Set(),
      selectedWire: null,
      draggingNode: null,
      draggingWire: null,
      dragOffset: null,
      panning: false,
      panStart: null,
      selecting: false,
      selectStart: null,
      selectBox: null,
      showVars: false,
      showLua: false,
      finder: null,
      finderCallback: null,
      history: [],
      historyIndex: -1,
      nextId: 1
    };

    // Assign IDs to existing nodes
    graph.nodes.forEach(n => {
      if (!n.id) n.id = 'n' + (state.nextId++);
      else {
        const num = parseInt(n.id.substring(1));
        if (num >= state.nextId) state.nextId = num + 1;
      }
    });

    state.graph = JSON.parse(JSON.stringify(graph));
    saveHistory(state);

    // Build DOM
    container.innerHTML = '';
    container.className = 'scgraph-container';

    const toolbar = document.createElement('div');
    toolbar.className = 'scgraph-toolbar';
    toolbar.innerHTML = `
      <button type="button" class="btn btn-save btn-sm" id="scg-add-node" title="Add Node (Space or double-click canvas)">+ Add node</button>
      <div class="sc-more-wrap">
        <button type="button" class="btn btn-wood btn-sm" id="scg-templates-btn" data-more-toggle aria-haspopup="true" aria-expanded="false" title="Insert a ready-made starter graph">Templates &#9662;</button>
        <div class="sc-more-menu scgraph-more-menu scgraph-templates-menu" hidden>
          ${TEMPLATES.map(t => `
            <button type="button" class="sc-more-item scgraph-tpl-item" data-template="${t.id}">
              <span class="scgraph-tpl-title">${escHtml(t.title)}</span>
              <span class="scgraph-tpl-desc">${escHtml(t.desc)}</span>
            </button>
          `).join('')}
        </div>
      </div>
      <div class="scgraph-toolbar-sep" aria-hidden="true"></div>
      <div class="scgraph-zoom-group" role="group" aria-label="Zoom controls">
        <button type="button" class="btn btn-wood btn-sm" id="scg-zoom-out" title="Zoom out">&minus;</button>
        <span class="scgraph-zoom-pct" id="scg-zoom-pct">100%</span>
        <button type="button" class="btn btn-wood btn-sm" id="scg-zoom-in" title="Zoom in">+</button>
        <button type="button" class="btn btn-wood btn-sm" id="scg-fit" title="Fit all nodes in view">Fit</button>
      </div>
      <button type="button" class="btn btn-wood btn-sm scg-delete-btn" id="scg-delete" title="Delete selected node or wire (Delete)" disabled>Delete</button>
      <div class="sc-more-wrap">
        <button type="button" class="btn btn-wood btn-sm" data-more-toggle aria-haspopup="true" aria-expanded="false">More &#9662;</button>
        <div class="sc-more-menu scgraph-more-menu" hidden>
          <button type="button" class="sc-more-item" id="scg-vars" title="Variables Panel">Variables</button>
          <button type="button" class="sc-more-item" id="scg-lua" title="Show Generated Lua">Generated Lua</button>
        </div>
      </div>
      <span class="scgraph-help-line">Drag from a pin to connect; Space to add a node</span>
    `;
    container.appendChild(toolbar);

    const emptyHint = document.createElement('div');
    emptyHint.className = 'scgraph-empty-hint';
    emptyHint.id = 'scg-empty-hint';
    emptyHint.hidden = graph.nodes.length > 0;
    emptyHint.innerHTML = 'Empty script &mdash; click <strong>+ Add node</strong> (or press <strong>Space</strong> / double-click) to add a node, or pick a starter from <strong>Templates</strong>.';
    container.appendChild(emptyHint);

    const canvas = document.createElement('div');
    canvas.className = 'scgraph-canvas';
    container.appendChild(canvas);

    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('class', 'scgraph-svg');
    canvas.appendChild(svg);

    const nodesDiv = document.createElement('div');
    nodesDiv.className = 'scgraph-nodes';
    canvas.appendChild(nodesDiv);

    // Event handlers
    toolbar.querySelector('#scg-add-node').addEventListener('click', () => openFinder(state, null));
    toolbar.querySelectorAll('[data-template]').forEach(btn => {
      btn.addEventListener('click', () => insertTemplate(state, btn.dataset.template));
    });
    toolbar.querySelector('#scg-zoom-out').addEventListener('click', () => zoomBy(state, 1 / 1.2));
    toolbar.querySelector('#scg-zoom-in').addEventListener('click', () => zoomBy(state, 1.2));
    toolbar.querySelector('#scg-fit').addEventListener('click', () => fitView(state));
    toolbar.querySelector('#scg-delete').addEventListener('click', () => deleteSelected(state));
    toolbar.querySelector('#scg-vars').addEventListener('click', () => toggleVars(state));
    toolbar.querySelector('#scg-lua').addEventListener('click', () => toggleLua(state));

    canvas.addEventListener('mousedown', e => onCanvasMouseDown(state, e));
    canvas.addEventListener('mousemove', e => onCanvasMouseMove(state, e));
    canvas.addEventListener('mouseup', e => onCanvasMouseUp(state, e));
    canvas.addEventListener('wheel', e => onCanvasWheel(state, e), { passive: false });
    canvas.addEventListener('dblclick', e => {
      if (e.target !== state.canvas && !e.target.classList.contains('scgraph-nodes')) return;
      openFinder(state, { x: e.clientX, y: e.clientY });
    });
    canvas.addEventListener('contextmenu', e => { e.preventDefault(); openFinder(state, { x: e.clientX, y: e.clientY }); });

    const onKeyDownBound = e => onKeyDown(state, e);
    const onKeyUpBound = e => onKeyUp(state, e);
    document.addEventListener('keydown', onKeyDownBound);
    document.addEventListener('keyup', onKeyUpBound);

    // Resize observer
    const resizeObserver = new ResizeObserver(() => {
      updateCanvasSize(state);
      render(state);
    });
    resizeObserver.observe(container);

    state.toolbar = toolbar;
    state.canvas = canvas;
    state.svg = svg;
    state.nodesDiv = nodesDiv;
    state.resizeObserver = resizeObserver;

    updateCanvasSize(state);
    render(state);

    return {
      getGraph: () => JSON.parse(JSON.stringify(state.graph)),
      destroy: () => {
        document.removeEventListener('keydown', onKeyDownBound);
        document.removeEventListener('keyup', onKeyUpBound);
        resizeObserver.disconnect();
        container.innerHTML = '';
      },
      fit: () => fitView(state)
    };
  }

  // ==========================================================================
  // Rendering
  // ==========================================================================

  function render(state) {
    renderNodes(state);
    renderWires(state);
    updateToolbarState(state);
  }

  function updateToolbarState(state) {
    if (!state.toolbar) return;
    const delBtn = state.toolbar.querySelector('#scg-delete');
    if (delBtn) delBtn.disabled = state.selectedNodes.size === 0 && state.selectedWire === null;
    const zoomEl = state.toolbar.querySelector('#scg-zoom-pct');
    if (zoomEl) zoomEl.textContent = `${Math.round(state.zoom * 100)}%`;
  }

  function renderNodes(state) {
    const { nodesDiv, graph, selectedNodes } = state;
    const hint = state.container && state.container.querySelector('#scg-empty-hint');
    if (hint) hint.hidden = graph.nodes.length > 0;
    nodesDiv.innerHTML = '';

    graph.nodes.forEach(node => {
      const nodeEl = document.createElement('div');
      nodeEl.className = 'scgraph-node' + (selectedNodes.has(node.id) ? ' selected' : '');
      nodeEl.dataset.nodeId = node.id;
      nodeEl.style.left = node.x + 'px';
      nodeEl.style.top = node.y + 'px';

      const def = NODES[node.type];
      if (!def) return;

      const header = document.createElement('div');
      header.className = 'scgraph-node-header category-' + def.category;
      header.textContent = def.title;
      header.title = def.tooltip || '';
      nodeEl.appendChild(header);

      const body = document.createElement('div');
      body.className = 'scgraph-node-body';
      nodeEl.appendChild(body);

      // Render ports
      const inputs = def.inputs || [];
      const outputs = def.outputs || [];
      const values = node.values || {};

      inputs.forEach((inp, idx) => {
        const row = document.createElement('div');
        row.className = 'scgraph-port-row input';

        const port = document.createElement('div');
        port.className = 'scgraph-port ' + (inp.kind || 'data') + (inp.type ? ' type-' + inp.type : '');
        port.dataset.port = inp.name;
        port.dataset.dir = 'input';
        row.appendChild(port);

        const label = document.createElement('span');
        label.className = 'scgraph-port-label';
        label.textContent = inp.label || inp.name;
        row.appendChild(label);

        // Inline input if no link
        if (!inp.kind && !hasLinkTo(graph, node.id, inp.name)) {
          const val = values[inp.name] !== undefined ? values[inp.name] : (def.values ? def.values[inp.name] : undefined);
          const input = createInlineInput(state, node, inp, val);
          if (input) row.appendChild(input);
        }

        body.appendChild(row);
      });

      // Settings that are not ports (the button name of On Building Button, the seconds of
      // Every N Seconds, ...): an inline editor each, above the outputs.
      const portNames = new Set(inputs.map(i => i.name));
      Object.keys(def.values || {}).filter(k => !portNames.has(k)).forEach(key => {
        const row = document.createElement('div');
        row.className = 'scgraph-port-row setting';
        const label = document.createElement('span');
        label.className = 'scgraph-port-label';
        label.textContent = (def.labels && def.labels[key]) || key;
        row.appendChild(label);
        const defVal = def.values[key];
        const kind = typeof defVal === 'number' ? 'number' : (typeof defVal === 'boolean' ? 'bool' : 'text');
        const val = values[key] !== undefined ? values[key] : defVal;
        const input = createInlineInput(state, node, { name: key, type: kind }, val);
        if (input) row.appendChild(input);
        body.appendChild(row);
      });

      outputs.forEach((out, idx) => {
        const row = document.createElement('div');
        row.className = 'scgraph-port-row output';

        const label = document.createElement('span');
        label.className = 'scgraph-port-label';
        label.textContent = out.name;
        row.appendChild(label);

        const port = document.createElement('div');
        port.className = 'scgraph-port ' + (out.kind || 'data') + (out.type ? ' type-' + out.type : '');
        port.dataset.port = out.name;
        port.dataset.dir = 'output';
        row.appendChild(port);

        body.appendChild(row);
      });

      // Port mouse events
      nodeEl.querySelectorAll('.scgraph-port').forEach(portEl => {
        portEl.addEventListener('mousedown', e => onPortMouseDown(state, e, node, portEl));
      });

      // Header drag
      header.addEventListener('mousedown', e => onNodeMouseDown(state, e, node));

      nodesDiv.appendChild(nodeEl);
    });
  }

  function renderWires(state) {
    const { svg, graph } = state;
    svg.innerHTML = '';

    graph.links.forEach((link, idx) => {
      const [fromId, fromPort] = link.from;
      const [toId, toPort] = link.to;

      const fromPos = getPortPosition(state, fromId, fromPort, 'output');
      const toPos = getPortPosition(state, toId, toPort, 'input');

      if (!fromPos || !toPos) return;

      const fromNode = graph.nodes.find(n => n.id === fromId);
      const toNode = graph.nodes.find(n => n.id === toId);
      if (!fromNode || !toNode) return;

      const fromDef = NODES[fromNode.type];
      const toDef = NODES[toNode.type];
      if (!fromDef || !toDef) return;

      const outPort = (fromDef.outputs || []).find(p => p.name === fromPort);
      const inPort = (toDef.inputs || []).find(p => p.name === toPort);
      if (!outPort || !inPort) return;

      const kind = outPort.kind || 'data';
      const type = outPort.type || 'any';

      const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
      path.setAttribute('class', 'scgraph-wire ' + kind + (type ? ' type-' + type : '') + (idx === state.selectedWire ? ' selected' : ''));
      path.setAttribute('d', bezierPath(fromPos.x, fromPos.y, toPos.x, toPos.y));
      path.dataset.linkIdx = idx;
      path.addEventListener('click', e => onWireClick(state, e, idx));
      svg.appendChild(path);
    });

    // Draw dragging wire
    if (state.draggingWire) {
      const { startPos, currentPos, portKind, portType } = state.draggingWire;
      const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
      path.setAttribute('class', 'scgraph-wire ' + portKind + (portType ? ' type-' + portType : ''));
      path.setAttribute('d', bezierPath(startPos.x, startPos.y, currentPos.x, currentPos.y));
      path.style.opacity = '0.6';
      svg.appendChild(path);
    }
  }

  function bezierPath(x1, y1, x2, y2) {
    const dist = Math.abs(x2 - x1);
    const offset = Math.min(dist * 0.5, 100);
    return `M ${x1} ${y1} C ${x1 + offset} ${y1}, ${x2 - offset} ${y2}, ${x2} ${y2}`;
  }

  // Screen (client) point -> graph space. The wires' SVG and the nodes share one
  // `translate(pan) scale(zoom)` transform with origin 0 0, so everything drawn in them is in
  // graph space; mixing in canvas pixels drew the wires away from the ports and the mouse.
  function toGraph(state, clientX, clientY) {
    const r = state.canvas.getBoundingClientRect();
    return { x: (clientX - r.left - state.pan.x) / state.zoom, y: (clientY - r.top - state.pan.y) / state.zoom };
  }

  function getPortPosition(state, nodeId, portName, dir) {
    const nodeEl = state.nodesDiv.querySelector(`[data-node-id="${nodeId}"]`);
    if (!nodeEl) return null;

    const portEl = Array.from(nodeEl.querySelectorAll('.scgraph-port')).find(p =>
      p.dataset.port === portName && p.dataset.dir === dir
    );
    if (!portEl) return null;

    const rect = portEl.getBoundingClientRect();
    return toGraph(state, rect.left + rect.width / 2, rect.top + rect.height / 2);
  }

  function createInlineInput(state, node, inputDef, value) {
    if (!node.values) node.values = {};
    if (value === undefined) value = '';

    const type = inputDef.type;

    if ((inputDef.name === 'resource' && (node.type === 'AddResource' || node.type === 'PlayerResource')) || inputDef.name === 'sound') {
      const sel = document.createElement('select');
      sel.className = 'scgraph-inline-input';
      const opts = inputDef.name === 'resource'
        ? (node.type === 'AddResource' ? ['Gold', 'Food', 'Wood', 'Stone'] : ['Gold', 'Food', 'Wood', 'Stone', 'Population'])
        : ['Chat', 'CantDo', 'Tribute', 'Attack', 'TownBell', 'TownRecall', 'Objective', 'Relic', 'Villager'];
      opts.forEach(o => {
        const opt = document.createElement('option');
        opt.value = o;
        opt.textContent = o;
        opt.selected = o === value;
        sel.appendChild(opt);
      });
      sel.addEventListener('change', () => {
        node.values[inputDef.name] = sel.value;
        notifyChange(state);
      });
      return sel;
    }

    if (inputDef.name === 'age') {
      const sel = document.createElement('select');
      sel.className = 'scgraph-inline-input';
      ['Dark', 'Feudal', 'Castle', 'Imperial'].forEach(o => {
        const opt = document.createElement('option');
        opt.value = o;
        opt.textContent = o;
        opt.selected = o === value;
        sel.appendChild(opt);
      });
      sel.addEventListener('change', () => {
        node.values[inputDef.name] = sel.value;
        notifyChange(state);
      });
      return sel;
    }

    if (inputDef.name === 'class') {
      const sel = document.createElement('select');
      sel.className = 'scgraph-inline-input';
      const classes = ['All', 'Villager', 'Infantry', 'Cavalry', 'Archer', 'Monk', 'Siege', 'Building', 'Trade', 'Warship'];
      classes.forEach(o => {
        const opt = document.createElement('option');
        opt.value = o;
        opt.textContent = o;
        opt.selected = o === value;
        sel.appendChild(opt);
      });
      sel.addEventListener('change', () => {
        node.values[inputDef.name] = sel.value;
        notifyChange(state);
      });
      return sel;
    }

    if (inputDef.name === 'property') {
      const sel = document.createElement('select');
      sel.className = 'scgraph-inline-input';
      ['X', 'Y', 'Type', 'Id'].forEach(o => {
        const opt = document.createElement('option');
        opt.value = o;
        opt.textContent = o;
        opt.selected = o === value;
        sel.appendChild(opt);
      });
      sel.addEventListener('change', () => {
        node.values[inputDef.name] = sel.value;
        notifyChange(state);
      });
      return sel;
    }

    if (inputDef.name === 'op' && node.type === 'Math') {
      const sel = document.createElement('select');
      sel.className = 'scgraph-inline-input';
      ['+', '-', '*', '/', '%', 'min', 'max'].forEach(o => {
        const opt = document.createElement('option');
        opt.value = o;
        opt.textContent = o;
        opt.selected = o === value;
        sel.appendChild(opt);
      });
      sel.addEventListener('change', () => {
        node.values[inputDef.name] = sel.value;
        notifyChange(state);
      });
      return sel;
    }

    if (inputDef.name === 'op' && node.type === 'Compare') {
      const sel = document.createElement('select');
      sel.className = 'scgraph-inline-input';
      ['==', '~=', '<', '<=', '>', '>='].forEach(o => {
        const opt = document.createElement('option');
        opt.value = o;
        opt.textContent = o;
        opt.selected = o === value;
        sel.appendChild(opt);
      });
      sel.addEventListener('change', () => {
        node.values[inputDef.name] = sel.value;
        notifyChange(state);
      });
      return sel;
    }

    if (inputDef.name === 'var') {
      const sel = document.createElement('select');
      sel.className = 'scgraph-inline-input';
      const opt = document.createElement('option');
      opt.value = '';
      opt.textContent = '(select)';
      sel.appendChild(opt);
      state.graph.vars.forEach(v => {
        const opt = document.createElement('option');
        opt.value = v.name;
        opt.textContent = v.name;
        opt.selected = v.name === value;
        sel.appendChild(opt);
      });
      sel.addEventListener('change', () => {
        node.values[inputDef.name] = sel.value;
        notifyChange(state);
      });
      return sel;
    }

    if (inputDef.name === 'unit' && type === 'number' && state.catalog.units) {
      const sel = document.createElement('select');
      sel.className = 'scgraph-inline-input';
      state.catalog.units.slice(0, 50).forEach(u => {
        const opt = document.createElement('option');
        opt.value = u.id;
        opt.textContent = `${u.id}: ${u.name}`;
        opt.selected = u.id === value;
        sel.appendChild(opt);
      });
      sel.addEventListener('change', () => {
        node.values[inputDef.name] = parseInt(sel.value);
        notifyChange(state);
      });
      return sel;
    }

    if (inputDef.name === 'tech' && state.catalog.techs) {
      const sel = document.createElement('select');
      sel.className = 'scgraph-inline-input';
      state.catalog.techs.slice(0, 50).forEach(t => {
        const opt = document.createElement('option');
        opt.value = t.id;
        opt.textContent = `${t.id}: ${t.name}`;
        opt.selected = t.id === value;
        sel.appendChild(opt);
      });
      sel.addEventListener('change', () => {
        node.values[inputDef.name] = parseInt(sel.value);
        notifyChange(state);
      });
      return sel;
    }

    if (type === 'bool') {
      const chk = document.createElement('input');
      chk.type = 'checkbox';
      chk.checked = !!value;
      chk.addEventListener('change', () => {
        node.values[inputDef.name] = chk.checked;
        notifyChange(state);
      });
      return chk;
    }

    const input = document.createElement('input');
    input.type = type === 'number' ? 'number' : 'text';
    input.className = 'scgraph-inline-input';
    input.value = value;
    input.addEventListener('input', () => {
      node.values[inputDef.name] = type === 'number' ? parseFloat(input.value) || 0 : input.value;
      notifyChange(state);
    });

    // Command button icon: a PNG name under icons/commands/ (or a frame number), or one from the icon library.
    if (node.type === 'AddBuildingButton' && inputDef.name === 'icon' && window.ScIconLib) {
      const wrap = document.createElement('span');
      wrap.style.cssText = 'display:inline-flex;gap:3px;flex:1;min-width:0;';
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'scgraph-inline-input';
      btn.style.cssText = 'flex:none;cursor:pointer;padding:2px 5px;';
      btn.textContent = 'Library';
      btn.title = 'Pick from library: copies the PNG to icons/commands/ in this mod and uses its name';
      btn.addEventListener('click', () => {
        window.ScIconLib.pick({
          title: 'Pick a command button icon',
          dest: entry => `icons/commands/${entry.id}.png`,
          onPicked: entry => {
            input.value = entry.id;
            node.values[inputDef.name] = entry.id;
            notifyChange(state);
          }
        });
      });
      wrap.appendChild(input);
      wrap.appendChild(btn);
      return wrap;
    }
    return input;
  }

  function hasLinkTo(graph, nodeId, portName) {
    return graph.links.some(l => l.to[0] === nodeId && l.to[1] === portName);
  }

  // ==========================================================================
  // Mouse Events
  // ==========================================================================

  function onCanvasMouseDown(state, e) {
    if (e.target !== state.canvas && !e.target.classList.contains('scgraph-nodes')) return;

    if (e.button === 1 || e.button === 2 || (e.button === 0 && e.shiftKey)) {
      state.panning = true;
      state.panStart = { x: e.clientX - state.pan.x, y: e.clientY - state.pan.y };
      state.canvas.classList.add('panning');
      e.preventDefault();
    } else if (e.button === 0) {
      state.selecting = true;
      const rect = state.canvas.getBoundingClientRect();
      state.selectStart = { x: e.clientX - rect.left, y: e.clientY - rect.top };
      state.selectedNodes.clear();
      state.selectedWire = null;
      render(state);
    }
  }

  function onCanvasMouseMove(state, e) {
    if (state.panning) {
      state.pan.x = e.clientX - state.panStart.x;
      state.pan.y = e.clientY - state.panStart.y;
      updateTransform(state);
    } else if (state.draggingNode) {
      const dx = e.clientX - state.draggingNode.lastX;
      const dy = e.clientY - state.draggingNode.lastY;
      state.draggingNode.lastX = e.clientX;
      state.draggingNode.lastY = e.clientY;

      state.selectedNodes.forEach(id => {
        const node = state.graph.nodes.find(n => n.id === id);
        if (node) {
          node.x += dx / state.zoom;
          node.y += dy / state.zoom;
        }
      });
      render(state);
    } else if (state.draggingWire) {
      state.draggingWire.currentPos = toGraph(state, e.clientX, e.clientY);
      renderWires(state);
    } else if (state.selecting) {
      const rect = state.canvas.getBoundingClientRect();
      const x1 = state.selectStart.x;
      const y1 = state.selectStart.y;
      const x2 = e.clientX - rect.left;
      const y2 = e.clientY - rect.top;

      const left = Math.min(x1, x2);
      const top = Math.min(y1, y2);
      const width = Math.abs(x2 - x1);
      const height = Math.abs(y2 - y1);

      if (!state.selectBox) {
        state.selectBox = document.createElement('div');
        state.selectBox.className = 'scgraph-selection-box';
        state.canvas.appendChild(state.selectBox);
      }

      state.selectBox.style.left = left + 'px';
      state.selectBox.style.top = top + 'px';
      state.selectBox.style.width = width + 'px';
      state.selectBox.style.height = height + 'px';

      // Select nodes in box
      state.selectedNodes.clear();
      state.graph.nodes.forEach(node => {
        const nx = node.x * state.zoom + state.pan.x;
        const ny = node.y * state.zoom + state.pan.y;
        if (nx >= left && nx <= left + width && ny >= top && ny <= top + height) {
          state.selectedNodes.add(node.id);
        }
      });
      render(state);
    }
  }

  function onCanvasMouseUp(state, e) {
    if (state.panning) {
      state.panning = false;
      state.canvas.classList.remove('panning');
    } else if (state.draggingNode) {
      state.draggingNode = null;
      notifyChange(state);
    } else if (state.draggingWire) {
      const target = document.elementFromPoint(e.clientX, e.clientY);
      if (target && target.classList.contains('scgraph-port')) {
        const nodeEl = target.closest('.scgraph-node');
        if (nodeEl) {
          const toId = nodeEl.dataset.nodeId;
          const toPort = target.dataset.port;
          const toDir = target.dataset.dir;

          const { fromId, fromPort, dir } = state.draggingWire;

          if (dir === 'output' && toDir === 'input') {
            tryAddLink(state, fromId, fromPort, toId, toPort);
          } else if (dir === 'input' && toDir === 'output') {
            tryAddLink(state, toId, toPort, fromId, fromPort);
          }
        } else {
          // Dropped on empty space: open finder to create node
          openFinder(state, { x: e.clientX, y: e.clientY }, (type) => {
            const rect = state.canvas.getBoundingClientRect();
            const x = (e.clientX - rect.left - state.pan.x) / state.zoom;
            const y = (e.clientY - rect.top - state.pan.y) / state.zoom;
            const newNode = addNode(state, type, x, y);

            const { fromId, fromPort, dir, portType } = state.draggingWire;

            // Auto-link to compatible port
            const def = NODES[type];
            if (def) {
              if (dir === 'output') {
                const compatPort = (def.inputs || []).find(p =>
                  (p.kind || 'data') === (state.draggingWire.portKind) &&
                  (!p.type || p.type === portType || p.type === 'any' || portType === 'any')
                );
                if (compatPort) {
                  tryAddLink(state, fromId, fromPort, newNode.id, compatPort.name);
                }
              } else {
                const compatPort = (def.outputs || []).find(p =>
                  (p.kind || 'data') === (state.draggingWire.portKind) &&
                  (!p.type || p.type === portType || p.type === 'any' || portType === 'any')
                );
                if (compatPort) {
                  tryAddLink(state, newNode.id, compatPort.name, fromId, fromPort);
                }
              }
            }
          });
        }
      }
      state.draggingWire = null;
      renderWires(state);
    } else if (state.selecting) {
      state.selecting = false;
      if (state.selectBox) {
        state.selectBox.remove();
        state.selectBox = null;
      }
    }
  }

  function onCanvasWheel(state, e) {
    e.preventDefault();
    const rect = state.canvas.getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;

    const oldZoom = state.zoom;
    const delta = e.deltaY < 0 ? 1.1 : 0.9;
    state.zoom = Math.max(0.3, Math.min(2, state.zoom * delta));

    const zoomChange = state.zoom / oldZoom;
    state.pan.x = mx - (mx - state.pan.x) * zoomChange;
    state.pan.y = my - (my - state.pan.y) * zoomChange;

    updateTransform(state);
  }

  function onNodeMouseDown(state, e, node) {
    if (e.button !== 0) return;
    e.stopPropagation();

    const hadWire = state.selectedWire !== null;
    state.selectedWire = null;
    if (!state.selectedNodes.has(node.id)) {
      if (!e.shiftKey) state.selectedNodes.clear();
      state.selectedNodes.add(node.id);
      render(state);
    } else if (hadWire) {
      render(state);
    }

    state.draggingNode = {
      lastX: e.clientX,
      lastY: e.clientY
    };
  }

  function onPortMouseDown(state, e, node, portEl) {
    e.stopPropagation();
    e.preventDefault();

    const port = portEl.dataset.port;
    const dir = portEl.dataset.dir;

    const def = NODES[node.type];
    if (!def) return;

    const portDef = dir === 'output'
      ? (def.outputs || []).find(p => p.name === port)
      : (def.inputs || []).find(p => p.name === port);

    if (!portDef) return;

    // If input port has a link, detach it
    if (dir === 'input') {
      const linkIdx = state.graph.links.findIndex(l => l.to[0] === node.id && l.to[1] === port);
      if (linkIdx >= 0) {
        const link = state.graph.links[linkIdx];
        state.graph.links.splice(linkIdx, 1);

        // Start dragging from the output
        const fromPos = getPortPosition(state, link.from[0], link.from[1], 'output');
        if (fromPos) {
          const fromNode = state.graph.nodes.find(n => n.id === link.from[0]);
          const fromDef = NODES[fromNode.type];
          const fromPortDef = (fromDef.outputs || []).find(p => p.name === link.from[1]);

          state.draggingWire = {
            fromId: link.from[0],
            fromPort: link.from[1],
            dir: 'output',
            startPos: fromPos,
            currentPos: toGraph(state, e.clientX, e.clientY),
            portKind: fromPortDef.kind || 'data',
            portType: fromPortDef.type || 'any'
          };
          render(state);
        }
        return;
      }
    }

    const pos = getPortPosition(state, node.id, port, dir);
    if (!pos) return;

    state.draggingWire = {
      fromId: node.id,
      fromPort: port,
      dir,
      startPos: pos,
      currentPos: pos,
      portKind: portDef.kind || 'data',
      portType: portDef.type || 'any'
    };
    renderWires(state);
  }

  function onWireClick(state, e, linkIdx) {
    e.stopPropagation();
    state.selectedNodes.clear();
    state.selectedWire = linkIdx;
    render(state);
  }

  function deleteSelected(state) {
    if (state.selectedWire !== null) {
      state.graph.links.splice(state.selectedWire, 1);
      state.selectedWire = null;
      render(state);
      notifyChange(state);
    } else if (state.selectedNodes.size > 0) {
      state.selectedNodes.forEach(id => {
        const idx = state.graph.nodes.findIndex(n => n.id === id);
        if (idx >= 0) state.graph.nodes.splice(idx, 1);
        state.graph.links = state.graph.links.filter(l => l.from[0] !== id && l.to[0] !== id);
      });
      state.selectedNodes.clear();
      render(state);
      notifyChange(state);
    }
  }

  function onKeyDown(state, e) {
    if (!state.container || !state.container.isConnected) return;

    // Space: open finder (if not typing and not already open)
    if (e.code === 'Space' && !isTyping(e) && !state.finder && state.container.offsetParent !== null) {
      e.preventDefault();
      openFinder(state, null);
    }

    // Delete: remove selected
    if (e.code === 'Delete' && !isTyping(e)) {
      deleteSelected(state);
    }

    // Ctrl+C: copy
    if ((e.ctrlKey || e.metaKey) && e.code === 'KeyC' && state.selectedNodes.size > 0) {
      e.preventDefault();
      const nodes = Array.from(state.selectedNodes).map(id => state.graph.nodes.find(n => n.id === id)).filter(Boolean);
      state.clipboard = JSON.stringify(nodes);
    }

    // Ctrl+V: paste
    if ((e.ctrlKey || e.metaKey) && e.code === 'KeyV' && state.clipboard) {
      e.preventDefault();
      try {
        const nodes = JSON.parse(state.clipboard);
        const idMap = {};
        nodes.forEach(n => {
          const newId = 'n' + (state.nextId++);
          idMap[n.id] = newId;
          state.graph.nodes.push({
            ...n,
            id: newId,
            x: n.x + 20,
            y: n.y + 20
          });
        });
        state.selectedNodes.clear();
        Object.values(idMap).forEach(id => state.selectedNodes.add(id));
        render(state);
        notifyChange(state);
      } catch (err) {}
    }

    // Ctrl+Z: undo
    if ((e.ctrlKey || e.metaKey) && e.code === 'KeyZ' && !e.shiftKey) {
      e.preventDefault();
      undo(state);
    }

    // Ctrl+Y: redo
    if ((e.ctrlKey || e.metaKey) && (e.code === 'KeyY' || (e.code === 'KeyZ' && e.shiftKey))) {
      e.preventDefault();
      redo(state);
    }
  }

  function onKeyUp(state, e) {}

  function isTyping(e) {
    const tag = e.target.tagName.toLowerCase();
    return tag === 'input' || tag === 'textarea' || tag === 'select';
  }

  // ==========================================================================
  // Graph Operations
  // ==========================================================================

  function addNode(state, type, x, y) {
    // do not drop a node exactly on another one (finder without a position, repeated adds)
    for (let k = 0; k < 50 && state.graph.nodes.some(n => Math.abs(n.x - x) < 24 && Math.abs(n.y - y) < 24); k++) {
      x += 36;
      y += 36;
    }
    const node = {
      id: 'n' + (state.nextId++),
      type,
      x,
      y,
      values: {}
    };

    const def = NODES[type];
    if (def && def.values) {
      node.values = JSON.parse(JSON.stringify(def.values));
    }

    state.graph.nodes.push(node);
    render(state);
    notifyChange(state);
    return node;
  }

  function tryAddLink(state, fromId, fromPort, toId, toPort) {
    if (fromId === toId) return;

    const fromNode = state.graph.nodes.find(n => n.id === fromId);
    const toNode = state.graph.nodes.find(n => n.id === toId);
    if (!fromNode || !toNode) return;

    const fromDef = NODES[fromNode.type];
    const toDef = NODES[toNode.type];
    if (!fromDef || !toDef) return;

    const outPort = (fromDef.outputs || []).find(p => p.name === fromPort);
    const inPort = (toDef.inputs || []).find(p => p.name === toPort);
    if (!outPort || !inPort) return;

    // Type check
    const outKind = outPort.kind || 'data';
    const inKind = inPort.kind || 'data';
    if (outKind !== inKind) return;

    if (outKind === 'data') {
      const outType = outPort.type || 'any';
      const inType = inPort.type || 'any';
      if (outType !== 'any' && inType !== 'any' && outType !== inType) {
        // Allow number->text via tostring
        if (!(outType === 'number' && inType === 'text')) return;
      }
    }

    // Remove existing link to this input (data inputs can only have one)
    if (inKind === 'data') {
      state.graph.links = state.graph.links.filter(l => !(l.to[0] === toId && l.to[1] === toPort));
    }

    // Flow outputs can only have one link
    if (outKind === 'flow') {
      state.graph.links = state.graph.links.filter(l => !(l.from[0] === fromId && l.from[1] === fromPort));
    }

    state.graph.links.push({
      from: [fromId, fromPort],
      to: [toId, toPort]
    });

    render(state);
    notifyChange(state);
  }

  function insertTemplate(state, templateId) {
    const tpl = TEMPLATES.find(t => t.id === templateId);
    if (!tpl) return;

    let offsetY = 0;
    if (state.graph.nodes.length > 0) {
      const maxY = Math.max(...state.graph.nodes.map(n => n.y));
      offsetY = maxY + 220;
    }

    const refToId = {};
    state.selectedNodes.clear();
    state.selectedWire = null;

    tpl.nodes.forEach(spec => {
      const def = NODES[spec.type];
      if (!def) return;
      const id = 'n' + (state.nextId++);
      refToId[spec.ref] = id;
      const values = def.values ? JSON.parse(JSON.stringify(def.values)) : {};
      if (spec.values) Object.assign(values, JSON.parse(JSON.stringify(spec.values)));
      state.graph.nodes.push({
        id,
        type: spec.type,
        x: spec.x,
        y: spec.y + offsetY,
        values
      });
      state.selectedNodes.add(id);
    });

    (tpl.links || []).forEach(([fromRef, fromPort, toRef, toPort]) => {
      const fromId = refToId[fromRef];
      const toId = refToId[toRef];
      if (fromId && toId) {
        state.graph.links.push({ from: [fromId, fromPort], to: [toId, toPort] });
      }
    });

    render(state);
    notifyChange(state);
    fitView(state);
  }

  // ==========================================================================
  // Node Finder
  // ==========================================================================

  function openFinder(state, pos, callback) {
    if (state.finder) state.finder.remove();

    const finder = document.createElement('div');
    finder.className = 'scgraph-finder';

    // The finder is positioned inside the container: turn the click's client point into
    // container coordinates and keep the whole box (480 x up to 520) inside it.
    const rect = state.container.getBoundingClientRect();
    const fw = Math.min(480, Math.max(280, rect.width - 16));
    const fh = Math.min(520, Math.max(240, rect.height - 16));
    let fx, fy;
    if (pos) {
      fx = pos.x - rect.left;
      fy = pos.y - rect.top;
    } else {
      fx = rect.width / 2 - fw / 2;
      fy = rect.height / 2 - fh / 2;
    }
    fx = Math.max(8, Math.min(fx, rect.width - fw - 8));
    fy = Math.max(8, Math.min(fy, rect.height - fh - 8));
    finder.style.left = fx + 'px';
    finder.style.top = fy + 'px';
    finder.style.maxHeight = fh + 'px';

    finder.innerHTML = `
      <div class="scgraph-finder-search">
        <input type="text" placeholder="Search nodes by name or description..." autofocus>
      </div>
      <div class="scgraph-finder-results"></div>
    `;

    state.container.appendChild(finder);
    state.finder = finder;
    state.finderCallback = callback;

    const input = finder.querySelector('input');
    const results = finder.querySelector('.scgraph-finder-results');

    let selectedIdx = 0;
    let items = [];

    const closeFinder = () => {
      document.removeEventListener('mousedown', closeOnClickOutside);
      if (state.finder === finder) state.finder = null;
      finder.remove();
    };

    const renderResults = (query) => {
      results.innerHTML = '';
      items = [];

      const fuzzyMatch = (str, q) => {
        str = str.toLowerCase();
        q = q.toLowerCase();
        let idx = 0;
        for (let i = 0; i < q.length; i++) {
          idx = str.indexOf(q[i], idx);
          if (idx < 0) return false;
          idx++;
        }
        return true;
      };

      const q = (query || '').trim().toLowerCase();
      const categories = { event: [], action: [], flow: [], data: [] };

      Object.keys(NODES).forEach(type => {
        const def = NODES[type];
        const catLabel = CATEGORY_LABELS[def.category] || def.category;
        const hit = !q
          || def.title.toLowerCase().includes(q)
          || (def.tooltip || '').toLowerCase().includes(q)
          || catLabel.toLowerCase().includes(q)
          || type.toLowerCase().includes(q)
          || fuzzyMatch(def.title, q);
        if (hit && categories[def.category]) {
          categories[def.category].push({ type, def });
        }
      });

      CATEGORY_ORDER.forEach(cat => {
        if (categories[cat].length === 0) return;

        const catDiv = document.createElement('div');
        catDiv.className = 'scgraph-finder-category';
        catDiv.innerHTML = `<span class="scgraph-finder-dot category-${cat}"></span><span>${escHtml(CATEGORY_LABELS[cat])}</span>`;
        results.appendChild(catDiv);

        categories[cat].forEach(({ type, def }) => {
          const item = document.createElement('div');
          item.className = 'scgraph-finder-item';
          item.innerHTML = `
            <div class="scgraph-finder-item-title"><span class="scgraph-finder-dot category-${def.category}"></span><span>${escHtml(def.title)}</span></div>
            <div class="scgraph-finder-item-desc">${escHtml(def.tooltip || '')}</div>
          `;
          item.addEventListener('click', () => selectNode(type));
          results.appendChild(item);
          items.push({ el: item, type });
        });
      });

      if (items.length === 0) {
        results.innerHTML = '<div class="scgraph-finder-empty">No matching nodes.</div>';
      }

      selectedIdx = 0;
      updateSelection();
    };

    const updateSelection = () => {
      items.forEach((it, idx) => {
        it.el.classList.toggle('selected', idx === selectedIdx);
      });
      if (items[selectedIdx]) {
        items[selectedIdx].el.scrollIntoView({ block: 'nearest' });
      }
    };

    const selectNode = (type) => {
      closeFinder();

      if (callback) {
        callback(type);
      } else if (pos) {
        const pt = toGraph(state, pos.x, pos.y);
        addNode(state, type, pt.x, pt.y);
      } else {
        const cRect = state.canvas.getBoundingClientRect();
        const x = (cRect.width / 2 - state.pan.x) / state.zoom - 110;
        const y = (cRect.height / 2 - state.pan.y) / state.zoom - 50;
        addNode(state, type, x, y);
      }
    };

    input.addEventListener('input', () => renderResults(input.value));
    input.addEventListener('keydown', (e) => {
      if (e.code === 'ArrowDown') {
        e.preventDefault();
        selectedIdx = Math.min(items.length - 1, selectedIdx + 1);
        updateSelection();
      } else if (e.code === 'ArrowUp') {
        e.preventDefault();
        selectedIdx = Math.max(0, selectedIdx - 1);
        updateSelection();
      } else if (e.code === 'Enter') {
        e.preventDefault();
        if (items[selectedIdx]) selectNode(items[selectedIdx].type);
      } else if (e.code === 'Escape') {
        e.preventDefault();
        closeFinder();
      }
    });

    const closeOnClickOutside = (e) => {
      if (!finder.contains(e.target)) {
        closeFinder();
      }
    };
    setTimeout(() => document.addEventListener('mousedown', closeOnClickOutside), 0);

    renderResults('');
    input.focus();
  }

  // ==========================================================================
  // Panels
  // ==========================================================================

  function toggleVars(state) {
    state.showVars = !state.showVars;
    if (state.showLua) state.showLua = false;
    renderPanels(state);
  }

  function toggleLua(state) {
    state.showLua = !state.showLua;
    if (state.showVars) state.showVars = false;
    renderPanels(state);
  }

  function renderPanels(state) {
    const existing = state.container.querySelectorAll('.scgraph-vars-panel, .scgraph-lua-panel');
    existing.forEach(el => el.remove());

    if (state.showVars) {
      const panel = document.createElement('div');
      panel.className = 'scgraph-vars-panel';
      panel.innerHTML = `
        <div class="scgraph-vars-header">
          <span class="scgraph-vars-title">Variables</span>
          <button class="btn btn-wood btn-xs">+ Add</button>
        </div>
        <div class="scgraph-vars-body"></div>
      `;

      const body = panel.querySelector('.scgraph-vars-body');
      state.graph.vars.forEach((v, idx) => {
        const row = document.createElement('div');
        row.className = 'scgraph-var-row';
        row.innerHTML = `
          <input type="text" class="scgraph-var-name" value="${v.name}" placeholder="name">
          <select class="scgraph-var-type">
            <option value="number" ${v.type === 'number' ? 'selected' : ''}>number</option>
            <option value="text" ${v.type === 'text' ? 'selected' : ''}>text</option>
            <option value="bool" ${v.type === 'bool' ? 'selected' : ''}>bool</option>
          </select>
          <input type="${v.type === 'number' ? 'number' : 'text'}" class="scgraph-var-value" value="${v.value}">
          <button class="btn btn-wood btn-xs" title="Delete">&times;</button>
        `;

        row.querySelector('.scgraph-var-name').addEventListener('input', (e) => {
          v.name = e.target.value;
          notifyChange(state);
        });
        row.querySelector('.scgraph-var-type').addEventListener('change', (e) => {
          v.type = e.target.value;
          row.querySelector('.scgraph-var-value').type = v.type === 'number' ? 'number' : 'text';
          notifyChange(state);
        });
        row.querySelector('.scgraph-var-value').addEventListener('input', (e) => {
          v.value = v.type === 'number' ? parseFloat(e.target.value) || 0 : e.target.value;
          notifyChange(state);
        });
        row.querySelector('button').addEventListener('click', () => {
          state.graph.vars.splice(idx, 1);
          renderPanels(state);
          notifyChange(state);
        });

        body.appendChild(row);
      });

      panel.querySelector('.scgraph-vars-header button').addEventListener('click', () => {
        state.graph.vars.push({ name: 'var' + (state.graph.vars.length + 1), type: 'number', value: 0 });
        renderPanels(state);
        notifyChange(state);
      });

      state.container.appendChild(panel);
    }

    if (state.showLua) {
      const result = toLua(state.graph);

      const panel = document.createElement('div');
      panel.className = 'scgraph-lua-panel';
      panel.innerHTML = `
        <div class="scgraph-lua-header">
          <span class="scgraph-lua-title">Generated Lua</span>
          <button class="btn btn-wood btn-xs">&times;</button>
        </div>
        <div class="scgraph-lua-code"></div>
        ${result.errors.length > 0 ? '<div class="scgraph-lua-errors"></div>' : ''}
      `;

      panel.querySelector('.scgraph-lua-header button').addEventListener('click', () => toggleLua(state));
      panel.querySelector('.scgraph-lua-code').textContent = result.lua;

      if (result.errors.length > 0) {
        const errDiv = panel.querySelector('.scgraph-lua-errors');
        result.errors.forEach(err => {
          const errEl = document.createElement('div');
          errEl.className = 'scgraph-lua-error';
          errEl.textContent = `Node ${err.nodeId}: ${err.message}`;
          errEl.addEventListener('click', () => {
            state.selectedNodes.clear();
            state.selectedNodes.add(err.nodeId);
            render(state);
          });
          errDiv.appendChild(errEl);
        });
      }

      state.container.appendChild(panel);

      // Mark error nodes
      result.errors.forEach(err => {
        const nodeEl = state.nodesDiv.querySelector(`[data-node-id="${err.nodeId}"]`);
        if (nodeEl) nodeEl.classList.add('error');
      });
    }
  }

  // ==========================================================================
  // Utility
  // ==========================================================================

  function updateCanvasSize(state) {
    const rect = state.container.getBoundingClientRect();
    state.svg.setAttribute('width', rect.width);
    state.svg.setAttribute('height', rect.height);
  }

  function updateTransform(state) {
    const transform = `translate(${state.pan.x}px, ${state.pan.y}px) scale(${state.zoom})`;
    state.svg.style.transform = transform;
    state.nodesDiv.style.transform = transform;
    state.svg.style.transformOrigin = '0 0';
    state.nodesDiv.style.transformOrigin = '0 0';
    renderWires(state);
    updateToolbarState(state);
  }

  function zoomBy(state, factor) {
    const rect = state.canvas.getBoundingClientRect();
    const cx = rect.width / 2;
    const cy = rect.height / 2;
    const oldZoom = state.zoom;
    state.zoom = Math.max(0.3, Math.min(2, state.zoom * factor));
    const zoomChange = state.zoom / oldZoom;
    state.pan.x = cx - (cx - state.pan.x) * zoomChange;
    state.pan.y = cy - (cy - state.pan.y) * zoomChange;
    updateTransform(state);
  }

  function fitView(state) {
    if (state.graph.nodes.length === 0) {
      state.pan = { x: 0, y: 0 };
      state.zoom = 1;
      updateTransform(state);
      return;
    }

    const xs = state.graph.nodes.map(n => n.x);
    const ys = state.graph.nodes.map(n => n.y);
    const minX = Math.min(...xs);
    const maxX = Math.max(...xs) + 250;
    const minY = Math.min(...ys);
    const maxY = Math.max(...ys) + 170;

    const rect = state.container.getBoundingClientRect();
    const paddingX = 50;
    const paddingTop = 80;
    const paddingBottom = 40;

    const zoomX = (rect.width - paddingX * 2) / Math.max(1, maxX - minX);
    const zoomY = (rect.height - paddingTop - paddingBottom) / Math.max(1, maxY - minY);
    state.zoom = Math.max(0.3, Math.min(1, Math.min(zoomX, zoomY)));

    state.pan.x = rect.width / 2 - (minX + maxX) / 2 * state.zoom;
    state.pan.y = paddingTop + (rect.height - paddingTop - paddingBottom) / 2 - (minY + maxY) / 2 * state.zoom;

    updateTransform(state);
  }

  function notifyChange(state) {
    saveHistory(state);
    state.onChange(state.graph);
    if (state.showLua) renderPanels(state);
  }

  function saveHistory(state) {
    const snapshot = JSON.stringify(state.graph);
    if (state.historyIndex >= 0 && state.history[state.historyIndex] === snapshot) return;

    state.history = state.history.slice(0, state.historyIndex + 1);
    state.history.push(snapshot);
    if (state.history.length > 50) state.history.shift();
    else state.historyIndex++;
  }

  function undo(state) {
    if (state.historyIndex > 0) {
      state.historyIndex--;
      state.graph = JSON.parse(state.history[state.historyIndex]);
      render(state);
      state.onChange(state.graph);
    }
  }

  function redo(state) {
    if (state.historyIndex < state.history.length - 1) {
      state.historyIndex++;
      state.graph = JSON.parse(state.history[state.historyIndex]);
      render(state);
      state.onChange(state.graph);
    }
  }

  // ==========================================================================
  // Lua Code Generation
  // ==========================================================================

  function toLua(graph) {
    const errors = [];
    const lines = [];
    const indent = '  ';

    // Variables
    if (graph.vars && graph.vars.length > 0) {
      graph.vars.forEach(v => {
        let val = v.value;
        if (v.type === 'text') val = luaString(val);
        else if (v.type === 'bool') val = val ? 'true' : 'false';
        lines.push(`local ${v.name} = ${val}`);
      });
      lines.push('');
    }

    // Named HUD counters table (only emitted if used)
    if (graph.nodes.some(n => n.type === 'SetCounter' || n.type === 'HideCounter')) {
      lines.push('local __wl_counters = {}');
      lines.push('');
    }

    // Event nodes
    const eventNodes = graph.nodes.filter(n => {
      const def = NODES[n.type];
      return def && def.category === 'event';
    });

    eventNodes.forEach(eventNode => {
      const def = NODES[eventNode.type];
      if (!def) return;

      const flowOut = (def.outputs || []).find(p => p.kind === 'flow');
      if (!flowOut) return;

      const chain = getFlowChain(graph, eventNode.id, flowOut.name);
      if (chain.length === 0) return; // Empty event, skip

      const ctx = { graph, errors, indent, depth: 0, locals: new Map() };

      // Event parameters become locals
      (def.outputs || []).filter(p => p.kind !== 'flow').forEach(p => {
        const localName = makeLocal(ctx, eventNode.id, p.name);
        ctx.locals.set([eventNode.id, p.name].toString(), localName);
      });

      const eventCode = [];

      if (eventNode.type === 'OnGameStart') {
        eventCode.push('Game.OnStart(function()');
      } else if (eventNode.type === 'OnGameModeStart') {
        const mode = (eventNode.values && eventNode.values.mode) || '';
        if (!mode) {
          errors.push({ nodeId: eventNode.id, message: 'Game mode ID is required' });
          return;
        }
        eventCode.push('Game.OnStart(function()');
        eventCode.push(`${indent}if Game.Mode() == ${luaString(mode)} then`);
        ctx.depth++;
      } else if (eventNode.type === 'EverySecond') {
        eventCode.push('Game.OnSecond(function()');
      } else if (eventNode.type === 'OnAgeUp') {
        const pLocal = makeLocal(ctx, eventNode.id, 'player');
        const aLocal = makeLocal(ctx, eventNode.id, 'age');
        eventCode.push(`Game.OnAgeUp(function(${pLocal}, ${aLocal})`);
      } else if (eventNode.type === 'OnUnitCreated') {
        const uLocal = makeLocal(ctx, eventNode.id, 'unit');
        eventCode.push(`Game.OnUnitCreated(function(${uLocal})`);
      } else if (eventNode.type === 'OnUnitDied') {
        const iLocal = makeLocal(ctx, eventNode.id, 'id');
        const pLocal = makeLocal(ctx, eventNode.id, 'player');
        eventCode.push(`Game.OnUnitDied(function(${iLocal}, ${pLocal})`);
      } else if (eventNode.type === 'OnResearch') {
        const pLocal = makeLocal(ctx, eventNode.id, 'player');
        const tLocal = makeLocal(ctx, eventNode.id, 'tech');
        eventCode.push(`Game.OnResearch(function(${pLocal}, ${tLocal})`);
      } else if (eventNode.type === 'OnBuildingDone') {
        const uLocal = makeLocal(ctx, eventNode.id, 'unit');
        eventCode.push(`Game.OnBuildingDone(function(${uLocal})`);
      } else if (eventNode.type === 'OnBuildingButton') {
        const name = eventNode.values.button || eventNode.values.name || '';
        if (!name) {
          errors.push({ nodeId: eventNode.id, message: 'Button name is required' });
          return;
        }
        const bLocal = makeLocal(ctx, eventNode.id, 'building');
        eventCode.push(`Game.OnCommand(${luaString(name)}, function(${bLocal})`);
      } else if (eventNode.type === 'ButtonCondition') {
        const name = eventNode.values.button || eventNode.values.name || '';
        if (!name) {
          errors.push({ nodeId: eventNode.id, message: 'Button name is required' });
          return;
        }
        const bLocal = makeLocal(ctx, eventNode.id, 'building');
        eventCode.push(`Game.CommandCondition(${luaString(name)}, function(${bLocal})`);
      } else if (eventNode.type === 'EveryNSeconds') {
        const sec = eventNode.values.seconds || 5;
        eventCode.push(`Game.OnStart(function()`);
        eventCode.push(`${indent}Time.Every(${sec}, function()`);
        ctx.depth++;
      } else if (eventNode.type === 'OnMapClick') {
        const xLocal = makeLocal(ctx, eventNode.id, 'x');
        const yLocal = makeLocal(ctx, eventNode.id, 'y');
        const bLocal = makeLocal(ctx, eventNode.id, 'button');
        eventCode.push(`Game.OnMapClick(function(${xLocal}, ${yLocal}, ${bLocal})`);
      }

      ctx.depth++;
      chain.forEach(nodeId => {
        const code = generateNodeCode(ctx, nodeId);
        if (code) eventCode.push(...code.map(l => indent.repeat(ctx.depth) + l));
      });
      ctx.depth--;

      if (eventNode.type === 'EveryNSeconds') {
        eventCode.push(`${indent}end)`);
        eventCode.push('end)');
      } else if (eventNode.type === 'OnGameModeStart') {
        eventCode.push(`${indent}end`);
        eventCode.push('end)');
      } else {
        eventCode.push('end)');
      }

      lines.push(...eventCode);
      lines.push('');
    });

    return {
      lua: lines.join('\n'),
      errors
    };
  }

  function getFlowChain(graph, nodeId, portName) {
    const chain = [];
    const visited = new Set();

    let current = graph.links.find(l => l.from[0] === nodeId && l.from[1] === portName);

    while (current) {
      const nextId = current.to[0];
      if (visited.has(nextId)) break; // Cycle
      visited.add(nextId);
      chain.push(nextId);

      const nextNode = graph.nodes.find(n => n.id === nextId);
      if (!nextNode) break;

      const nextDef = NODES[nextNode.type];
      if (!nextDef) break;

      const flowOut = (nextDef.outputs || []).find(p => p.kind === 'flow' && p.name === 'exec');
      if (!flowOut) break;

      current = graph.links.find(l => l.from[0] === nextId && l.from[1] === flowOut.name);
    }

    return chain;
  }

  function generateNodeCode(ctx, nodeId) {
    const node = ctx.graph.nodes.find(n => n.id === nodeId);
    if (!node) return null;

    const def = NODES[node.type];
    if (!def) return null;

    if (node.type === 'AddResource') {
      const player = getInputExpr(ctx, node, 'player');
      const res = getInputExpr(ctx, node, 'resource');
      const amt = getInputExpr(ctx, node, 'amount');
      if (!player || !res || !amt) return null;

      const resMap = { Gold: 'AddGold', Food: 'AddFood', Wood: 'AddWood', Stone: 'AddStone', Population: 'AddResource' };
      const method = resMap[res.replace(/"/g, '')] || 'AddResource';
      if (method === 'AddResource') {
        return [`${player}:${method}(${res}, ${amt})`];
      } else {
        return [`${player}:${method}(${amt})`];
      }
    }

    if (node.type === 'CreateUnit') {
      const p = getInputExpr(ctx, node, 'player');
      const u = getInputExpr(ctx, node, 'unit');
      const x = getInputExpr(ctx, node, 'x');
      const y = getInputExpr(ctx, node, 'y');
      if (!p || !u || !x || !y) return null;
      const cap = getInputExpr(ctx, node, 'capturable');
      if (cap === 'false') {
        return [`${p}:CreateUnit(${u}, ${x}, ${y}, { Capturable = false })`];
      } else if (cap && cap !== 'true') {
        return [`${p}:CreateUnit(${u}, ${x}, ${y}, { Capturable = ${cap} })`];
      }
      return [`${p}:CreateUnit(${u}, ${x}, ${y})`];
    }

    if (node.type === 'ResearchTech') {
      const p = getInputExpr(ctx, node, 'player');
      const t = getInputExpr(ctx, node, 'tech');
      if (!p || !t) return null;
      return [`${p}:Research(${t})`];
    }

    if (node.type === 'SetAge') {
      const p = getInputExpr(ctx, node, 'player');
      const a = getInputExpr(ctx, node, 'age');
      if (!p || !a) return null;
      return [`${p}:SetAge(${a})`];
    }

    if (node.type === 'DeleteUnit') {
      const u = getInputExpr(ctx, node, 'unit');
      if (!u) return null;
      return [`${u}:Delete()`];
    }

    if (node.type === 'ShowMessage') {
      const t = getInputExpr(ctx, node, 'text');
      if (!t) return null;
      const c = getInputExpr(ctx, node, 'color');
      if (c) {
        return [`UI.Message(${t}, {Color=${c}})`];
      } else {
        return [`UI.Message(${t})`];
      }
    }

    if (node.type === 'Chat') {
      const t = getInputExpr(ctx, node, 'text');
      if (!t) return null;
      const f = getInputExpr(ctx, node, 'from');
      if (f) {
        return [`UI.Chat(${t}, {From=${f}})`];
      } else {
        return [`UI.Chat(${t})`];
      }
    }

    if (node.type === 'PlaySound') {
      const s = getInputExpr(ctx, node, 'sound');
      if (!s) return null;
      return [`Sound.Play(${s})`];
    }

    if (node.type === 'PlaySoundFile') {
      const p = getInputExpr(ctx, node, 'path');
      if (!p) return null;
      return [`Sound.PlayFile(${p})`];
    }

    if (node.type === 'Wait') {
      const s = getInputExpr(ctx, node, 'seconds');
      if (!s) return null;
      return [`Time.Wait(${s})`];
    }

    if (node.type === 'Log') {
      const t = getInputExpr(ctx, node, 'text');
      if (!t) return null;
      return [`print(${t})`];
    }

    if (node.type === 'SetButtonAvailable') {
      const avail = getInputExpr(ctx, node, 'available');
      return [`return ${avail !== null ? avail : 'true'}`];
    }

    if (node.type === 'SetVariable') {
      const v = getInputExpr(ctx, node, 'var');
      const val = getInputExpr(ctx, node, 'value');
      if (!v || !val) return null;
      return [`${v.replace(/"/g, '')} = ${val}`];
    }

    if (node.type === 'AddBuildingButton') {
      const bLink = ctx.graph.links.find(l => l.to[0] === node.id && l.to[1] === 'building');
      const pLink = ctx.graph.links.find(l => l.to[0] === node.id && l.to[1] === 'page');
      const btnLink = ctx.graph.links.find(l => l.to[0] === node.id && l.to[1] === 'button');
      const iconLink = ctx.graph.links.find(l => l.to[0] === node.id && l.to[1] === 'icon');

      const b = getInputExpr(ctx, node, 'building');
      const btn = getInputExpr(ctx, node, 'button');
      const name = getInputExpr(ctx, node, 'name');
      if (!b || !btn || !name) return null;

      if (!bLink) {
        const bv = Number(node.values && node.values.building);
        if (!Number.isFinite(bv) || bv < 0) {
          ctx.errors.push({ nodeId: node.id, message: 'Building ID must be >= 0' });
          return null;
        }
      }
      if (!btnLink) {
        const slot = Number(node.values && node.values.button);
        if (!Number.isFinite(slot) || slot < 1 || slot > 15) {
          ctx.errors.push({ nodeId: node.id, message: 'Button slot must be between 1 and 15' });
          return null;
        }
      }

      const page = getInputExpr(ctx, node, 'page');
      if (page && !pLink) {
        const pv = Number(node.values && node.values.page);
        if (!Number.isFinite(pv) || pv < 1) {
          ctx.errors.push({ nodeId: node.id, message: 'Page must be >= 1' });
          return null;
        }
      }

      const fields = [];
      fields.push(`building = ${bLink ? `math.floor(${b})` : Math.floor(Number(node.values.building))}`);
      if (page) {
        fields.push(`page = ${pLink ? `math.floor(${page})` : Math.floor(Number(node.values.page))}`);
      }
      fields.push(`button = ${btnLink ? `math.floor(${btn})` : Math.floor(Number(node.values.button))}`);
      fields.push(`name = ${name}`);

      const text = getInputExpr(ctx, node, 'text');
      if (text) fields.push(`text = ${text}`);

      const help = getInputExpr(ctx, node, 'help');
      if (help) fields.push(`help = ${help}`);

      if (iconLink) {
        const icon = generateDataExpr(ctx, iconLink.from[0], iconLink.from[1]);
        if (icon) fields.push(`icon = (math.tointeger(${icon}) or ${icon})`);
      } else {
        const rawIcon = node.values ? node.values.icon : undefined;
        if (rawIcon !== undefined && rawIcon !== null && String(rawIcon).trim() !== '') {
          const trimmed = String(rawIcon).trim();
          if (/^\d+$/.test(trimmed)) {
            fields.push(`icon = ${parseInt(trimmed, 10)}`);
          } else {
            fields.push(`icon = ${luaString(trimmed)}`);
          }
        }
      }

      // "Allies can use it": shared = true (only emitted when set, so older graphs compile the same)
      const shLink = ctx.graph.links.find(l => l.to[0] === node.id && l.to[1] === 'shared');
      if (shLink) {
        const sh = getInputExpr(ctx, node, 'shared');
        if (sh) fields.push(`shared = ${sh}`);
      } else if (node.values && node.values.shared === true) {
        fields.push('shared = true');
      }

      return [`Game.AddCommand({ ${fields.join(', ')} })`];
    }

    if (node.type === 'WinGame') {
      const p = getInputExpr(ctx, node, 'player');
      if (!p) return null;
      const allies = getInputExpr(ctx, node, 'withAllies');
      return [`if ${p} then ${p}:Win(${allies || 'false'}) end`];
    }

    if (node.type === 'DefeatPlayer') {
      const p = getInputExpr(ctx, node, 'player');
      if (!p) return null;
      return [`if ${p} then ${p}:Defeat() end`];
    }

    if (node.type === 'MoveUnit') {
      const u = getInputExpr(ctx, node, 'unit');
      const x = getInputExpr(ctx, node, 'x');
      const y = getInputExpr(ctx, node, 'y');
      if (!u || !x || !y) return null;
      return [`if ${u} then ${u}:MoveTo(${x}, ${y}) end`];
    }

    if (node.type === 'AttackUnit') {
      const u = getInputExpr(ctx, node, 'unit');
      const target = getInputExpr(ctx, node, 'target');
      if (!u || !target) return null;
      return [`if ${u} and ${target} then ${u}:Attack(${target}) end`];
    }

    if (node.type === 'AddCustomResource') {
      const p = getInputExpr(ctx, node, 'player');
      const res = getResourceExpr(ctx, node, 'resource');
      const amt = getInputExpr(ctx, node, 'amount');
      if (!p || !res || !amt) return null;
      return [`if ${p} then ${p}:AddResource(${res}, ${amt}) end`];
    }

    if (node.type === 'SetResource') {
      const p = getInputExpr(ctx, node, 'player');
      const res = getResourceExpr(ctx, node, 'resource');
      const amt = getInputExpr(ctx, node, 'amount');
      if (!p || !res || !amt) return null;
      return [`if ${p} then ${p}:SetResource(${res}, ${amt}) end`];
    }

    if (node.type === 'Branch') {
      const cond = getInputExpr(ctx, node, 'condition');
      if (!cond) return null;

      const trueLink = ctx.graph.links.find(l => l.from[0] === node.id && l.from[1] === 'true');
      const falseLink = ctx.graph.links.find(l => l.from[0] === node.id && l.from[1] === 'false');

      const lines = [];
      lines.push(`if ${cond} then`);

      if (trueLink) {
        ctx.depth++;
        const trueChain = getFlowChain(ctx.graph, node.id, 'true');
        trueChain.forEach(nid => {
          const code = generateNodeCode(ctx, nid);
          if (code) lines.push(...code.map(l => ctx.indent + l));
        });
        ctx.depth--;
      }

      if (falseLink) {
        lines.push('else');
        ctx.depth++;
        const falseChain = getFlowChain(ctx.graph, node.id, 'false');
        falseChain.forEach(nid => {
          const code = generateNodeCode(ctx, nid);
          if (code) lines.push(...code.map(l => ctx.indent + l));
        });
        ctx.depth--;
      }

      lines.push('end');
      return lines;
    }

    if (node.type === 'ForLoop') {
      const from = getInputExpr(ctx, node, 'from');
      const to = getInputExpr(ctx, node, 'to');
      if (!from || !to) return null;

      const indexLocal = makeLocal(ctx, node.id, 'index');
      ctx.locals.set([node.id, 'index'].toString(), indexLocal);

      const lines = [];
      lines.push(`for ${indexLocal} = ${from}, ${to} do`);

      ctx.depth++;
      const bodyChain = getFlowChain(ctx.graph, node.id, 'body');
      bodyChain.forEach(nid => {
        const code = generateNodeCode(ctx, nid);
        if (code) lines.push(...code.map(l => ctx.indent + l));
      });
      ctx.depth--;

      lines.push('end');

      const doneChain = getFlowChain(ctx.graph, node.id, 'done');
      doneChain.forEach(nid => {
        const code = generateNodeCode(ctx, nid);
        if (code) lines.push(...code);
      });

      return lines;
    }

    if (node.type === 'SpawnNearUnit') {
      const player = getInputExpr(ctx, node, 'player');
      const unit = getInputExpr(ctx, node, 'unit');
      const near = getInputExpr(ctx, node, 'near');
      const dx = getInputExpr(ctx, node, 'dx') || '0';
      const dy = getInputExpr(ctx, node, 'dy') || '0';
      const cap = getInputExpr(ctx, node, 'capturable');
      if (!player || !unit || !near) return null;
      const opt = (cap === 'true') ? ', { capturable = true }' : '';
      return [`if ${player} and ${near} and ${unit} then ${player}:CreateUnit(${unit}, ${near}.X + (${dx}), ${near}.Y + (${dy})${opt}) end`];
    }

    if (node.type === 'SetCounter') {
      const name = getInputExpr(ctx, node, 'name');
      const text = getInputExpr(ctx, node, 'text');
      if (!name || !text) return null;
      return [
        `if __wl_counters[${name}] then`,
        `${ctx.indent}__wl_counters[${name}]:SetText(${text})`,
        `${ctx.indent}__wl_counters[${name}]:Show()`,
        `else`,
        `${ctx.indent}__wl_counters[${name}] = UI:CreateCounter(${text})`,
        `end`
      ];
    }

    if (node.type === 'HideCounter') {
      const name = getInputExpr(ctx, node, 'name');
      if (!name) return null;
      return [`if __wl_counters[${name}] then __wl_counters[${name}]:Hide() end`];
    }

    if (node.type === 'SetMapClickCapture') {
      const on = getInputExpr(ctx, node, 'enabled');
      if (!on) return null;
      return [`Game.SetMapClickCapture(${on})`];
    }

    if (node.type === 'SetUnitControlDisabled') {
      const off = getInputExpr(ctx, node, 'disabled');
      if (!off) return null;
      return [`Game.SetUnitControlDisabled(${off})`];
    }

    if (node.type === 'RestartMatch') {
      return ['Game.Restart()'];
    }

    if (node.type === 'SetSession') {
      const k = getInputExpr(ctx, node, 'key');
      const vLink = ctx.graph.links.find(l => l.to[0] === node.id && l.to[1] === 'value');
      let v = getInputExpr(ctx, node, 'value');
      if (!k || !v) return null;
      if (!vLink && node.values && typeof node.values.value === 'string' && /^-?\d+(?:\.\d+)?$/.test(node.values.value.trim())) {
        v = String(Number(node.values.value.trim()));
      }
      return [`Game.SetSession(${k}, ${v})`];
    }

    if (node.type === 'ClearSession') {
      return ['Game.ClearSession()'];
    }

    if (node.type === 'ForEachPlayer') {
      const playerLocal = makeLocal(ctx, node.id, 'player');
      ctx.locals.set([node.id, 'player'].toString(), playerLocal);

      const lines = [];
      lines.push(`for _, ${playerLocal} in ipairs(Game.Players()) do`);

      ctx.depth++;
      const bodyChain = getFlowChain(ctx.graph, node.id, 'body');
      bodyChain.forEach(nid => {
        const code = generateNodeCode(ctx, nid);
        if (code) lines.push(...code.map(l => ctx.indent + l));
      });
      ctx.depth--;

      lines.push('end');

      const doneChain = getFlowChain(ctx.graph, node.id, 'done');
      doneChain.forEach(nid => {
        const code = generateNodeCode(ctx, nid);
        if (code) lines.push(...code);
      });

      return lines;
    }

    if (node.type === 'ForEachUnit') {
      const units = getInputExpr(ctx, node, 'units');
      if (!units) return null;

      const unitLocal = makeLocal(ctx, node.id, 'unit');
      ctx.locals.set([node.id, 'unit'].toString(), unitLocal);

      const lines = [];
      lines.push(`for _, ${unitLocal} in ipairs(${units}) do`);

      ctx.depth++;
      const bodyChain = getFlowChain(ctx.graph, node.id, 'body');
      bodyChain.forEach(nid => {
        const code = generateNodeCode(ctx, nid);
        if (code) lines.push(...code.map(l => ctx.indent + l));
      });
      ctx.depth--;

      lines.push('end');

      const doneChain = getFlowChain(ctx.graph, node.id, 'done');
      doneChain.forEach(nid => {
        const code = generateNodeCode(ctx, nid);
        if (code) lines.push(...code);
      });

      return lines;
    }

    if (node.type === 'Sequence') {
      const lines = [];
      ['then1', 'then2', 'then3'].forEach(port => {
        const chain = getFlowChain(ctx.graph, node.id, port);
        chain.forEach(nid => {
          const code = generateNodeCode(ctx, nid);
          if (code) lines.push(...code);
        });
      });
      return lines;
    }

    return null;
  }

  function getInputExpr(ctx, node, portName) {
    const link = ctx.graph.links.find(l => l.to[0] === node.id && l.to[1] === portName);

    if (link) {
      // Follow data link
      return generateDataExpr(ctx, link.from[0], link.from[1]);
    } else {
      // Use literal value
      const val = node.values ? node.values[portName] : undefined;
      if (val === undefined || val === '') {
        const def = NODES[node.type];
        const inputDef = (def.inputs || []).find(p => p.name === portName);
        if (inputDef && inputDef.required) {
          ctx.errors.push({ nodeId: node.id, message: `Input '${portName}' is required` });
          return null;
        }
        return null;
      }

      if (typeof val === 'string') return luaString(val);
      if (typeof val === 'boolean') return val ? 'true' : 'false';
      return String(val);
    }
  }

  function getResourceExpr(ctx, node, portName) {
    const link = ctx.graph.links.find(l => l.to[0] === node.id && l.to[1] === portName);
    if (link) {
      const expr = generateDataExpr(ctx, link.from[0], link.from[1]);
      if (!expr) return null;
      return `(math.tointeger(${expr}) or ${expr})`;
    }
    const raw = node.values ? node.values[portName] : undefined;
    if (raw === undefined || raw === null || String(raw).trim() === '') {
      ctx.errors.push({ nodeId: node.id, message: `Input '${portName}' is required` });
      return null;
    }
    const trimmed = String(raw).trim();
    if (/^\d+$/.test(trimmed)) return String(parseInt(trimmed, 10));
    return luaString(trimmed);
  }

  function generateDataExpr(ctx, nodeId, portName) {
    const key = [nodeId, portName].toString();

    // Check if already generated as a local
    if (ctx.locals.has(key)) {
      return ctx.locals.get(key);
    }

    const node = ctx.graph.nodes.find(n => n.id === nodeId);
    if (!node) return null;

    if (node.type === 'LocalPlayer') {
      return 'Game.LocalPlayer';
    }

    if (node.type === 'PlayerById') {
      const id = getInputExpr(ctx, node, 'id');
      if (!id) return null;
      return `Game.Player(${id})`;
    }

    if (node.type === 'PlayerResource') {
      const p = getInputExpr(ctx, node, 'player');
      const r = getInputExpr(ctx, node, 'resource');
      if (!p || !r) return null;
      const res = r.replace(/"/g, '');
      return `${p}.${res}`;
    }

    if (node.type === 'PlayerAge') {
      const p = getInputExpr(ctx, node, 'player');
      if (!p) return null;
      return `${p}.AgeName`;
    }

    if (node.type === 'PlayerUnits') {
      const p = getInputExpr(ctx, node, 'player');
      if (!p) return null;
      const cls = getInputExpr(ctx, node, 'class');
      if (cls && cls !== '"All"') {
        return `${p}:Units(${cls})`;
      } else {
        return `${p}:Units()`;
      }
    }

    if (node.type === 'Count') {
      const l = getInputExpr(ctx, node, 'list');
      if (!l) return null;
      return `#${l}`;
    }

    if (node.type === 'UnitProperty') {
      const u = getInputExpr(ctx, node, 'unit');
      const prop = getInputExpr(ctx, node, 'property');
      if (!u || !prop) return null;
      return `${u}.${prop.replace(/"/g, '')}`;
    }

    if (node.type === 'UnitOwner') {
      const u = getInputExpr(ctx, node, 'unit');
      if (!u) return null;
      return `${u}.Owner`;
    }

    if (node.type === 'IsBuilding') {
      const u = getInputExpr(ctx, node, 'unit');
      if (!u) return null;
      return `${u}.IsBuilding`;
    }

    if (node.type === 'GameTime') {
      return 'Game.Time';
    }

    if (node.type === 'Math') {
      const a = getInputExpr(ctx, node, 'a');
      const op = getInputExpr(ctx, node, 'op');
      const b = getInputExpr(ctx, node, 'b');
      if (!a || !op || !b) return null;

      const opStr = op.replace(/"/g, '');
      if (opStr === 'min') return `math.min(${a}, ${b})`;
      if (opStr === 'max') return `math.max(${a}, ${b})`;
      return `(${a} ${opStr} ${b})`;
    }

    if (node.type === 'Compare') {
      const a = getInputExpr(ctx, node, 'a');
      const op = getInputExpr(ctx, node, 'op');
      const b = getInputExpr(ctx, node, 'b');
      if (!a || !op || !b) return null;
      return `(${a} ${op.replace(/"/g, '')} ${b})`;
    }

    if (node.type === 'And') {
      const a = getInputExpr(ctx, node, 'a');
      const b = getInputExpr(ctx, node, 'b');
      if (!a || !b) return null;
      return `(${a} and ${b})`;
    }

    if (node.type === 'Or') {
      const a = getInputExpr(ctx, node, 'a');
      const b = getInputExpr(ctx, node, 'b');
      if (!a || !b) return null;
      return `(${a} or ${b})`;
    }

    if (node.type === 'Not') {
      const v = getInputExpr(ctx, node, 'value');
      if (!v) return null;
      return `(not ${v})`;
    }

    if (node.type === 'JoinText') {
      const a = getInputExpr(ctx, node, 'a');
      const b = getInputExpr(ctx, node, 'b');
      if (!a || !b) return null;
      return `(tostring(${a}) .. tostring(${b}))`;
    }

    if (node.type === 'ToText') {
      const v = getInputExpr(ctx, node, 'value');
      if (!v) return null;
      return `tostring(${v})`;
    }

    if (node.type === 'RandomNumber') {
      const min = getInputExpr(ctx, node, 'min');
      const max = getInputExpr(ctx, node, 'max');
      if (!min || !max) return null;
      return `math.random(${min}, ${max})`;
    }

    if (node.type === 'GetVariable') {
      const v = getInputExpr(ctx, node, 'var');
      if (!v) return null;
      return v.replace(/"/g, '');
    }

    if (node.type === 'GameMode') {
      return 'Game.Mode()';
    }

    if (node.type === 'ModeEnemyPlayer') {
      return 'Game.ModeEnemy()';
    }

    if (node.type === 'UnitsOfType') {
      const p = getInputExpr(ctx, node, 'player');
      const uLink = ctx.graph.links.find(l => l.to[0] === node.id && l.to[1] === 'unitType');
      const u = getInputExpr(ctx, node, 'unitType');
      if (!p || !u) return null;
      if (!uLink) {
        const uv = Number(node.values && node.values.unitType);
        if (!Number.isFinite(uv) || uv < 0) {
          ctx.errors.push({ nodeId: node.id, message: 'Unit type ID must be >= 0' });
          return null;
        }
      }
      const uExpr = uLink ? `math.floor(${u})` : String(Math.floor(Number(node.values.unitType)));
      return `(${p} and ${p}:UnitsOfType(${uExpr}) or {})`;
    }

    if (node.type === 'ModSetting') {
      const key = getInputExpr(ctx, node, 'setting');
      if (!key) return null;
      return `(MOD.settings and MOD.settings[${key}])`;
    }

    if (node.type === 'PlayerCiv') {
      const p = getInputExpr(ctx, node, 'player');
      if (!p) return null;
      return `(${p} and ${p}.Civ)`;
    }

    if (node.type === 'GetResource') {
      const p = getInputExpr(ctx, node, 'player');
      const res = getResourceExpr(ctx, node, 'resource');
      if (!p || !res) return null;
      return `(${p} and ${p}:GetResource(${res}) or 0)`;
    }

    if (node.type === 'GaiaPlayer') {
      return 'Game.Player(0)';
    }

    if (node.type === 'ResourceDeposit') {
      const res = getInputExpr(ctx, node, 'resource');
      if (!res) return null;
      return `Game.ResourceDeposit(${res})`;
    }

    if (node.type === 'PlayerIsEnemy') {
      const a = getInputExpr(ctx, node, 'a');
      const b = getInputExpr(ctx, node, 'b');
      if (!a || !b) return null;
      return `(${a} and ${b} and ${a}:IsEnemy(${b}) or false)`;
    }

    if (node.type === 'SelectedUnits') {
      return 'Game.Selected()';
    }

    if (node.type === 'UnitPosition') {
      const u = getInputExpr(ctx, node, 'unit');
      if (!u) return null;
      const prop = (portName === 'y') ? 'Y' : 'X';
      return `(${u} and ${u}.${prop} or 0)`;
    }

    if (node.type === 'MapSize') {
      return 'Game.MapSize()';
    }

    if (node.type === 'GetSession') {
      const k = getInputExpr(ctx, node, 'key');
      if (!k) return null;
      return `Game.GetSession(${k})`;
    }

    return null;
  }

  function makeLocal(ctx, nodeId, portName) {
    const base = portName.replace(/[^a-z0-9]/gi, '_');
    const suffix = nodeId.replace('n', '');
    return `${base}_${suffix}`;
  }

  function luaString(str) {
    str = String(str);
    str = str.replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n').replace(/\t/g, '\\t').replace(/\r/g, '\\r');
    return `"${str}"`;
  }

  // ==========================================================================
  // Generated scripts/visual.lua (required by main.lua)
  // ==========================================================================
  // Older Scriptorium versions wrote the graphs into a block between these markers of main.lua.
  const OLD_BLOCK_START = '-- >>> Scriptorium graphs (generated: edit them in Scriptorium) >>>';
  const OLD_BLOCK_END = '-- <<< Scriptorium graphs <<<';
  const VISUAL_MODULE = 'visual';

  // The whole scripts/visual.lua: one do-block per compiled graph ({ name, lua }[]).
  function visualLuaFile(blocks) {
    const header = '-- scripts/visual.lua: generated by Scriptorium, do not edit.\n'
      + '-- The visual scripts (node graphs) of this mod. Change them in Scriptorium.\n'
      + '-- main.lua loads this file with require("visual").\n';
    const body = blocks.map(b => `do -- graph: ${b.name}\n${b.lua}\nend`).join('\n\n');
    return header + (body ? '\n' + body + '\n' : '');
  }

  // True when the text of main.lua loads the module (plain text search, like the editor's hint).
  function requiresModule(mainText, name) {
    const pattern = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    return new RegExp('require\\s*\\(?\\s*["\']' + pattern + '["\']\\s*\\)?').test(mainText || '');
  }

  // main.lua with require("name") appended, unchanged when it is already there.
  function addRequire(mainText, name) {
    if (requiresModule(mainText, name)) return mainText;
    let t = mainText || '';
    if (t && !t.endsWith('\n')) t += '\n';
    return t + `require("${name}")\n`;
  }

  function hasOldBlock(mainText) {
    return (mainText || '').includes(OLD_BLOCK_START);
  }

  // main.lua without the block an older version wrote (its graphs now live in scripts/visual.lua).
  // Only a complete block (start and end marker) is removed; the code around it is kept.
  function removeOldBlock(mainText) {
    const s = mainText.indexOf(OLD_BLOCK_START);
    const e = mainText.indexOf(OLD_BLOCK_END);
    if (s < 0 || e < s) return mainText;
    let from = s;
    if (mainText.slice(0, from).endsWith('\n\n')) from--;      // the blank line before the block
    let to = e + OLD_BLOCK_END.length;
    if (mainText[to] === '\n') to++;
    return mainText.slice(0, from) + mainText.slice(to);
  }

  // ==========================================================================
  // Export
  // ==========================================================================

  const ScGraph = {
    NODES,
    TEMPLATES,
    newGraph,
    mount,
    toLua,
    visualLuaFile,
    VISUAL_MODULE,
    requiresModule,
    addRequire,
    hasOldBlock,
    removeOldBlock
  };

  if (typeof window !== 'undefined') {
    window.ScGraph = ScGraph;
  }

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = ScGraph;
  }
})();

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
        { name: 'y', type: 'number', required: true }
      ],
      outputs: [{ name: 'exec', kind: 'flow' }],
      values: { unit: 125 }
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
    }
  };

  // ==========================================================================
  // Graph Creation
  // ==========================================================================

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
      <button class="btn btn-save btn-sm" id="scg-add-node" title="Add Node (Space)">+ Node</button>
      <button class="btn btn-wood btn-sm" id="scg-fit" title="Fit View">Fit</button>
      <div class="sc-more-wrap">
        <button type="button" class="btn btn-wood btn-sm" data-more-toggle aria-haspopup="true" aria-expanded="false">More &#9662;</button>
        <div class="sc-more-menu scgraph-more-menu" hidden>
          <button type="button" class="sc-more-item" id="scg-vars" title="Variables Panel">Variables</button>
          <button type="button" class="sc-more-item" id="scg-lua" title="Show Generated Lua">Generated Lua</button>
        </div>
      </div>
    `;
    container.appendChild(toolbar);

    const emptyHint = document.createElement('div');
    emptyHint.className = 'scgraph-empty-hint';
    emptyHint.id = 'scg-empty-hint';
    emptyHint.hidden = graph.nodes.length > 0;
    emptyHint.innerHTML = 'Empty script &mdash; click <strong>+ Node</strong> (or press Space) to add an event such as <em>On Game Start</em>.';
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
    toolbar.querySelector('#scg-fit').addEventListener('click', () => fitView(state));
    toolbar.querySelector('#scg-vars').addEventListener('click', () => toggleVars(state));
    toolbar.querySelector('#scg-lua').addEventListener('click', () => toggleLua(state));

    canvas.addEventListener('mousedown', e => onCanvasMouseDown(state, e));
    canvas.addEventListener('mousemove', e => onCanvasMouseMove(state, e));
    canvas.addEventListener('mouseup', e => onCanvasMouseUp(state, e));
    canvas.addEventListener('wheel', e => onCanvasWheel(state, e), { passive: false });
    canvas.addEventListener('contextmenu', e => { e.preventDefault(); openFinder(state, { x: e.clientX, y: e.clientY }); });

    document.addEventListener('keydown', e => onKeyDown(state, e));
    document.addEventListener('keyup', e => onKeyUp(state, e));

    // Resize observer
    const resizeObserver = new ResizeObserver(() => {
      updateCanvasSize(state);
      render(state);
    });
    resizeObserver.observe(container);

    state.canvas = canvas;
    state.svg = svg;
    state.nodesDiv = nodesDiv;
    state.resizeObserver = resizeObserver;

    updateCanvasSize(state);
    render(state);

    return {
      getGraph: () => JSON.parse(JSON.stringify(state.graph)),
      destroy: () => {
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
        label.textContent = inp.name;
        row.appendChild(label);

        // Inline input if no link
        if (!inp.kind && !hasLinkTo(graph, node.id, inp.name)) {
          const input = createInlineInput(state, node, inp, values[inp.name]);
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
        label.textContent = key;
        row.appendChild(label);
        const kind = typeof def.values[key] === 'number' ? 'number' : 'text';
        const input = createInlineInput(state, node, { name: key, type: kind }, values[key]);
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
      path.setAttribute('class', 'scgraph-wire ' + kind + (type ? ' type-' + type : ''));
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
    if (value === undefined) value = '';

    const type = inputDef.type;

    if (inputDef.name === 'resource' || inputDef.name === 'sound') {
      const sel = document.createElement('select');
      sel.className = 'scgraph-inline-input';
      const opts = inputDef.name === 'resource'
        ? (node.type === 'AddResource' ? ['Gold', 'Food', 'Wood', 'Stone'] : ['Gold', 'Food', 'Wood', 'Stone', 'Population'])
        : ['Chat', 'CantDo', 'Tribute', 'Attack', 'TownBell', 'TownRecall', 'Objective', 'Relic'];
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

    if (inputDef.name === 'unit' && state.catalog.units) {
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

    if (!state.selectedNodes.has(node.id)) {
      if (!e.shiftKey) state.selectedNodes.clear();
      state.selectedNodes.add(node.id);
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
    state.selectedWire = linkIdx;
  }

  function onKeyDown(state, e) {
    // Space: open finder (if not typing)
    if (e.code === 'Space' && !isTyping(e)) {
      e.preventDefault();
      openFinder(state, null);
    }

    // Delete: remove selected
    if (e.code === 'Delete') {
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

  // ==========================================================================
  // Node Finder
  // ==========================================================================

  function openFinder(state, pos, callback) {
    if (state.finder) state.finder.remove();

    const finder = document.createElement('div');
    finder.className = 'scgraph-finder';

    // The finder is positioned inside the container: turn the click's client point into
    // container coordinates and keep the whole box (420 x up to 480) inside it.
    const rect = state.container.getBoundingClientRect();
    const fw = 420, fh = Math.min(480, rect.height - 16);
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
        <input type="text" placeholder="Search nodes..." autofocus>
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

      const categories = { event: [], action: [], flow: [], data: [] };

      Object.keys(NODES).forEach(type => {
        const def = NODES[type];
        if (!query || fuzzyMatch(def.title, query) || fuzzyMatch(def.category, query)) {
          categories[def.category].push({ type, def });
        }
      });

      ['event', 'action', 'flow', 'data'].forEach(cat => {
        if (categories[cat].length === 0) return;

        const catDiv = document.createElement('div');
        catDiv.className = 'scgraph-finder-category';
        catDiv.textContent = cat.charAt(0).toUpperCase() + cat.slice(1);
        results.appendChild(catDiv);

        categories[cat].forEach(({ type, def }) => {
          const item = document.createElement('div');
          item.className = 'scgraph-finder-item';
          item.innerHTML = `
            <div class="scgraph-finder-item-title">${def.title}</div>
            <div class="scgraph-finder-item-desc">${def.tooltip || ''}</div>
          `;
          item.addEventListener('click', () => selectNode(type));
          results.appendChild(item);
          items.push({ el: item, type });
        });
      });

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
      finder.remove();
      state.finder = null;

      if (callback) {
        callback(type);
      } else {
        const rect = state.canvas.getBoundingClientRect();
        const x = (rect.width / 2 - state.pan.x) / state.zoom;
        const y = (rect.height / 2 - state.pan.y) / state.zoom;
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
        finder.remove();
        state.finder = null;
      }
    });

    const closeOnClickOutside = (e) => {
      if (!finder.contains(e.target)) {
        finder.remove();
        state.finder = null;
        document.removeEventListener('mousedown', closeOnClickOutside);
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
    const maxX = Math.max(...xs) + 200;
    const minY = Math.min(...ys);
    const maxY = Math.max(...ys) + 100;

    const rect = state.container.getBoundingClientRect();
    const padding = 50;

    const zoomX = (rect.width - padding * 2) / (maxX - minX);
    const zoomY = (rect.height - padding * 2) / (maxY - minY);
    state.zoom = Math.max(0.3, Math.min(1, Math.min(zoomX, zoomY)));

    state.pan.x = rect.width / 2 - (minX + maxX) / 2 * state.zoom;
    state.pan.y = rect.height / 2 - (minY + maxY) / 2 * state.zoom;

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
      const val = node.values[portName];
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

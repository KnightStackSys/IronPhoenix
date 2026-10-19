'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');
const { startServer } = require('../server');

const binaryPath = process.env.IRONPHOENIX_NATIVE_ENGINE;
test('native IronPhoenix engine loads modern FEN4, plays a move and analyzes', {
  skip: !binaryPath
}, async () => {
  const binary = path.resolve(binaryPath);
  assert.ok(fs.existsSync(binary), 'Native engine binary must exist: ' + binary);

  const { server, bridge, address } = await startServer({ binary, port: 0 });
  const base = 'http://127.0.0.1:' + address.port;
  const post = async (endpoint, data) => {
    const response = await fetch(base + endpoint, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(data)
    });
    const body = await response.json();
    assert.equal(response.status, 200, endpoint + ': ' + JSON.stringify(body));
    return body;
  };

  try {
    const state = await fetch(base + '/api/state').then(res => res.json());
    assert.equal(state.online, true);
    assert.ok(state.fen.startsWith('R-'), 'Modern FEN4 is normalized');
    assert.ok(state.fen.split('/').length === 14);
    assert.ok(state.legal.length > 0, 'Engine returned legal opening moves');

    const firstMove = state.legal[0];
    const moved = await post('/api/position', { moves: [firstMove] });
    assert.deepEqual(moved.moves, [firstMove]);
    assert.notEqual(moved.fen, state.fen, 'Move applied in native C++ engine');

    const restored = await post('/api/position', { moves: [] });
    assert.equal(restored.fen, state.fen, 'Returning to start is reversible');

    let unsubscribe;
    const bestMoveReceived = new Promise((resolve, reject) => {
      const timeout = setTimeout(() => { unsubscribe(); reject(new Error('Native search did not return bestmove.')); }, 10000);
      unsubscribe = bridge.subscribe((type, data) => {
        if (type === 'bestmove') {
          clearTimeout(timeout);
          unsubscribe();
          resolve(data);
        }
      });
    });

    const started = await post('/api/analyze', { depth: 1, multiPV: 1 });
    assert.equal(started.searchId, 1);
    const result = await bestMoveReceived;
    assert.match(result.move, /^[a-n]\d{1,2}[a-n]\d{1,2}[nbrq]?$/i);
  } finally {
    bridge.close();
    await new Promise(resolve => server.close(resolve));
  }
});

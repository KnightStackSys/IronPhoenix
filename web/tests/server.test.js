'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const { startServer, infoLine } = require('../server');

function url(base, endpoint) { return base + endpoint; }
async function post(base, endpoint, body) {
  const res = await fetch(url(base, endpoint), {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(body)
  });
  return { status: res.status, body: await res.json() };
}

test('parses engine PV information for arrows', () => {
  const info = infoLine('info depth 8 multipv 2 score mate 5 nodes 1234 nps 5000 time 100 pv d2d4 d12d10');
  assert.equal(info.depth, 8);
  assert.equal(info.multiPV, 2);
  assert.deepEqual(info.pv, ['d2d4', 'd12d10']);
  assert.deepEqual(info.score, { type: 'mate', value: 5 });
});

test('loopback HTTP bridge serves GUI, validates 4PC positions and launches engine', async () => {
  const app = await startServer({
    port: 0, binary: process.execPath,
    args: [path.join(__dirname, 'fake-engine.js')]
  });
  const base = 'http://127.0.0.1:' + app.address.port;
  try {
    let result = await fetch(url(base, '/'));
    assert.equal(result.status, 200);
    assert.match(await result.text(), /IronPhoenix/);
    result = await fetch(url(base, '/app.js'));
    assert.equal(result.status, 200);

    const state = await fetch(url(base, '/api/state')).then(r => r.json());
    assert.equal(state.online, true);
    assert.equal(state.legal[0], 'd2d4');

    const fenResult = await post(base, '/api/position', { moves: ['d2d4'] });
    assert.equal(fenResult.status, 200);
    assert.deepEqual(fenResult.body.moves, ['d2d4']);

    const broken = await post(base, '/api/position', { fen: 'BAD', moves: [] });
    assert.equal(broken.status, 400);

    const pgn = await post(base, '/api/pgn', { text: '1. d2d4 d12d10' });
    assert.equal(pgn.status, 200);
    assert.deepEqual(pgn.body.moves, ['d2d4', 'd12d10']);

    const search = await post(base, '/api/analyze', { depth: 3, multiPV: 2 });
    assert.equal(search.status, 200);
    assert.equal(search.body.searchId, 1);
    // A call without options defaults to low-overhead single-PV mode.
    const stopped = await post(base, '/api/stop', {});
    assert.equal(stopped.status, 200);
    const quick = await post(base, '/api/analyze', {});
    assert.equal(quick.status, 200);
    assert.equal(quick.body.searchId, 2);

    const stop = await post(base, '/api/stop', {});
    assert.equal(stop.status, 200);

    const traversal = await fetch(url(base, '/../../server.js'));
    assert.equal(traversal.status, 404);

    const badOrigin = await fetch(url(base, '/api/state'), {
      headers: { Origin: 'http://evil.example' }
    });
    assert.equal(badOrigin.status, 403);
  } finally {
    app.bridge.close();
    await new Promise(resolve => app.server.close(resolve));
  }
});

'use strict';

const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { parseFen4, parsePgn, normalizeMove } = require('./notation');

const ROOT = __dirname;
const HOST = '127.0.0.1';
const CONTENT = {
  '/': ['index.html', 'text/html; charset=utf-8'],
  '/index.html': ['index.html', 'text/html; charset=utf-8'],
  '/style.css': ['style.css', 'text/css; charset=utf-8'],
  '/app.js': ['app.js', 'text/javascript; charset=utf-8']
};
const MAX_BODY = 1_000_000;

function response(res, status, value) {
  const body = JSON.stringify(value);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body),
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff'
  });
  res.end(body);
}

function readJson(req) {
  return new Promise((resolve, reject) => {
    let value = '';
    req.setEncoding('utf8');
    req.on('data', chunk => {
      value += chunk;
      if (value.length > MAX_BODY) {
        reject(Object.assign(new Error('Request exceeds 1 MB.'), { status: 413 }));
        req.destroy();
      }
    });
    req.on('end', () => {
      try { resolve(JSON.parse(value || '{}')); }
      catch { reject(Object.assign(new Error('Invalid JSON request.'), { status: 400 })); }
    });
    req.on('error', reject);
  });
}

function findEngine() {
  for (const file of process.platform === 'win32'
    ? ['ironphoenix.exe', 'IronPhoenix.exe', 'ironphoenix']
    : ['ironphoenix', 'ironphoenix.exe']) {
    const location = path.join(ROOT, file);
    if (fs.existsSync(location)) return location;
  }
  return null;
}

function infoLine(line) {
  if (!line.startsWith('info ') || line.startsWith('info string ')) return null;
  const number = key => {
    const found = line.match(new RegExp('(?:^|\\s)' + key + '\\s+(\\d+)'));
    return found ? Number(found[1]) : null;
  };
  const score = line.match(/\bscore (cp|mate) (-?\d+)/);
  const pv = line.match(/\bpv(?:\s+([a-n0-9]+(?:\s+[a-n0-9]+)*))?\s*$/i);
  return {
    depth: number('depth'), seldepth: number('seldepth'), nodes: number('nodes'),
    nps: number('nps'), time: number('time'), multiPV: number('multipv') || 1,
    score: score ? { type: score[1], value: Number(score[2]) } : null,
    pv: pv && pv[1] ? pv[1].trim().split(/\s+/) : [],
    bound: /\blowerbound\b/.test(line) ? 'lower' : /\bupperbound\b/.test(line) ? 'upper' : 'exact'
  };
}

class EngineBridge {
  constructor(options = {}) {
    this.binary = options.binary || findEngine();
    this.args = options.args || [];
    this.cwd = options.cwd || ROOT;
    this.child = null;
    this.boot = null;
    this.pending = null;
    this.stopWaiter = null;
    this.buffer = '';
    this.queue = Promise.resolve();
    this.listeners = new Set();
    this.analysing = false;
    this.searchId = 0;
    this.position = { baseFen: null, moves: [], fen: null, legal: [] };
  }

  subscribe(fn) {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  }

  emit(type, data) {
    for (const fn of this.listeners) {
      try { fn(type, data); }
      catch { /* one disconnected browser must not disrupt the engine */ }
    }
  }

  snapshot() {
    return {
      ...this.position, online: !!this.child && this.child.exitCode === null,
      analysing: this.analysing, searchId: this.searchId
    };
  }

  serialize(fn) {
    const result = this.queue.then(fn, fn);
    this.queue = result.catch(() => {});
    return result;
  }

  write(line) {
    if (!this.child || this.child.exitCode !== null || !this.child.stdin.writable)
      throw new Error('IronPhoenix is not running.');
    if (/[\r\n\0]/.test(line)) throw new Error('Multiline engine commands are not allowed.');
    this.child.stdin.write(line + '\n');
  }

  onLine(line) {
    if (!line) return;
    if (this.pending) {
      this.pending.lines.push(line);
      if (this.pending.done(line)) {
        const current = this.pending;
        this.pending = null;
        clearTimeout(current.timer);
        current.resolve(current.lines);
      }
    }
    if (line.startsWith('info string ')) {
      this.emit('message', line.slice(12));
    } else if (line.startsWith('info ')) {
      const info = infoLine(line);
      if (info && info.score) this.emit('analysis', { ...info, searchId: this.searchId });
    }
    if (line.startsWith('bestmove ')) {
      this.analysing = false;
      this.emit('bestmove', {
        move: line.match(/^bestmove\s+(\S+)/)?.[1] || '0000',
        searchId: this.searchId
      });
      if (this.stopWaiter) {
        clearTimeout(this.stopWaiter.timer);
        this.stopWaiter.resolve();
        this.stopWaiter = null;
      }
    }
  }

  collect(commands, done, timeout = 12000) {
    if (this.pending) return Promise.reject(new Error('An engine command is still active.'));
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending = null;
        reject(new Error('IronPhoenix did not respond to a command.'));
      }, timeout);
      this.pending = { lines: [], done, resolve, reject, timer };
      try { for (const line of commands) this.write(line); }
      catch (err) {
        clearTimeout(timer);
        this.pending = null;
        reject(err);
      }
    });
  }

  async ensure() {
    if (this.child && this.child.exitCode === null && this.boot) return this.boot;
    if (!this.binary) throw new Error('Engine not found. Copy ironphoenix.exe (Windows) or ironphoenix (Linux/macOS) into web/.');
    if (this.boot) return this.boot;
    this.boot = (async () => {
      const child = spawn(this.binary, this.args, {
        cwd: this.cwd, shell: false, windowsHide: true,
        stdio: ['pipe', 'pipe', 'pipe']
      });
      this.child = child;
      child.stdout.setEncoding('utf8');
      child.stdout.on('data', chunk => {
        this.buffer += chunk;
        let index;
        while ((index = this.buffer.indexOf('\n')) !== -1) {
          const line = this.buffer.slice(0, index).replace(/\r$/, '');
          this.buffer = this.buffer.slice(index + 1);
          this.onLine(line);
        }
        if (this.buffer.length > 128000) this.buffer = '';
      });
      child.stderr.setEncoding('utf8');
      child.stderr.on('data', text => this.emit('message', text.trim().slice(0, 300)));
      const broken = err => {
        const reason = err?.message || 'Engine process exited.';
        this.emit('error', reason);
        this.analysing = false;
        if (this.pending) {
          const current = this.pending;
          this.pending = null;
          clearTimeout(current.timer);
          current.reject(new Error(reason));
        }
        if (this.stopWaiter) {
          clearTimeout(this.stopWaiter.timer);
          this.stopWaiter.reject(new Error(reason));
          this.stopWaiter = null;
        }
      };
      child.on('error', broken);
      child.on('exit', () => { broken(new Error('Engine process exited.')); this.child = null; this.boot = null; });
      await this.collect(['uci'], line => line === 'uciok');
      await this.collect(['isready'], line => line === 'readyok');
      return true;
    })();
    try { return await this.boot; }
    catch (err) { this.boot = null; throw err; }
  }

  async stopInternal() {
    if (!this.analysing) return;
    const done = new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.stopWaiter = null;
        reject(new Error('Timed out stopping the engine search.'));
      }, 5000);
      this.stopWaiter = { resolve, reject, timer };
    });
    this.write('stop');
    await done;
  }

  async positionInternal({ fen = null, moves = [] }) {
    if (fen !== null) {
      try { parseFen4(fen); }
      catch (err) { err.status = 400; throw err; }
    }
    if (!Array.isArray(moves) || moves.length > 2000) throw Object.assign(new Error('Invalid move list.'), { status: 400 });
    const normalized = moves.map(move => normalizeMove(move));
    if (normalized.some(m => !m)) throw Object.assign(new Error('Moves must use 4PC coordinate notation.'), { status: 400 });
    await this.ensure();
    await this.stopInternal();
    const base = fen === null ? 'startpos' : 'fen ' + fen.trim();
    const command = 'position ' + base + (normalized.length ? ' moves ' + normalized.join(' ') : '');
    const lines = await this.collect([command, 'fen', 'moves', 'isready'], line => line === 'readyok');
    const error = lines.find(line => /^info string (?:FEN4 error|move list error|expected:|startpos is not|StartFEN failed)/.test(line));
    if (error) throw Object.assign(new Error(error.replace(/^info string /, '')), { status: 400 });
    const currentFen = lines.find(line => /^[RBYG]-[01],[01],[01],[01]-/.test(line));
    if (!currentFen) throw new Error('Engine did not provide a valid normalized FEN4.');
    parseFen4(currentFen);
    const legalLine = lines.find(line => /^legal moves \(\d+\):/.test(line));
    if (!legalLine) throw new Error('Engine did not provide legal moves.');
    const legal = legalLine.slice(legalLine.indexOf(':') + 1).trim().split(/\s+/).filter(Boolean);
    this.position = { baseFen: fen, moves: normalized, fen: currentFen, legal };
    this.emit('position', this.snapshot());
    return this.snapshot();
  }

  async state() {
    return this.serialize(async () => {
      await this.ensure();
      if (!this.position.fen) await this.positionInternal({});
      return this.snapshot();
    });
  }

  async positionFrom(request) {
    return this.serialize(() => this.positionInternal(request));
  }

  async pgn(request) {
    let parsed;
    try { parsed = parsePgn(request.text); }
    catch (err) { err.status = 400; throw err; }
    const result = await this.positionFrom({ fen: parsed.fen, moves: parsed.moves });
    return { ...result, tags: parsed.tags };
  }

  async analyze(options) {
    return this.serialize(async () => {
      await this.ensure();
      if (!this.position.fen) await this.positionInternal({});
      await this.stopInternal();
      const multiPV = Number(options.multiPV ?? 1);
      const depth = Number(options.depth ?? 6);
      const moveTime = Number(options.movetime ?? 0);
      if (!Number.isInteger(multiPV) || multiPV < 1 || multiPV > 8
        || !Number.isInteger(depth) || depth < 1 || depth > 64
        || !Number.isInteger(moveTime) || moveTime < 0 || moveTime > 120000)
        throw Object.assign(new Error('Invalid analysis depth, time or MultiPV.'), { status: 400 });
      // Starting a new search from an unchanged position does not require
      // replaying the entire game; positionInternal left the engine positioned.
      this.write('setoption name MultiPV value ' + multiPV);
      this.searchId++;
      this.analysing = true;
      this.emit('started', { searchId: this.searchId });
      this.write(moveTime ? 'go movetime ' + moveTime : 'go depth ' + depth);
      return this.snapshot();
    });
  }

  async stop() {
    return this.serialize(async () => {
      await this.stopInternal();
      return this.snapshot();
    });
  }

  close() {
    if (this.child) {
      this.child.kill();
      this.child = null;
    }
  }
}

function startServer(options = {}) {
  const bridge = options.bridge || new EngineBridge(options);
  const server = http.createServer(async (req, res) => {
    try {
      const host = new URL('http://' + (req.headers.host || '')).hostname;
      if (!['127.0.0.1', 'localhost'].includes(host)) return response(res, 403, { error: 'Localhost only.' });
      if (req.headers.origin) {
        const origin = new URL(req.headers.origin);
        if (!['127.0.0.1', 'localhost'].includes(origin.hostname)
          || String(origin.port || '80') !== String(server.address().port))
          return response(res, 403, { error: 'Cross-origin requests are blocked.' });
      }
      const url = new URL(req.url, 'http://127.0.0.1');
      if (req.method === 'GET' && url.pathname === '/events') {
        res.writeHead(200, {
          'Content-Type': 'text/event-stream; charset=utf-8',
          'Cache-Control': 'no-cache, no-transform', Connection: 'keep-alive',
          'X-Content-Type-Options': 'nosniff'
        });
        const send = (type, data) => res.write('event: ' + type + '\ndata: ' + JSON.stringify(data) + '\n\n');
        send('position', bridge.snapshot());
        const off = bridge.subscribe(send);
        const keepalive = setInterval(() => res.write(': keepalive\n\n'), 20000);
        req.on('close', () => { off(); clearInterval(keepalive); });
        return;
      }
      if (req.method === 'GET' && url.pathname === '/api/state')
        return response(res, 200, await bridge.state());
      if (req.method === 'POST') {
        const body = await readJson(req);
        if (url.pathname === '/api/position')
          return response(res, 200, await bridge.positionFrom(body));
        if (url.pathname === '/api/pgn')
          return response(res, 200, await bridge.pgn(body));
        if (url.pathname === '/api/analyze')
          return response(res, 200, await bridge.analyze(body));
        if (url.pathname === '/api/stop')
          return response(res, 200, await bridge.stop());
      }
      if (req.method === 'GET' && Object.hasOwn(CONTENT, url.pathname)) {
        const [file, type] = CONTENT[url.pathname];
        res.writeHead(200, {
          'Content-Type': type, 'Cache-Control': 'no-store',
          'X-Content-Type-Options': 'nosniff', 'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; base-uri 'none'; frame-ancestors 'none'"
        });
        return fs.createReadStream(path.join(ROOT, file)).pipe(res);
      }
      response(res, 404, { error: 'Not found.' });
    } catch (err) {
      response(res, err.status || 500, { error: err.message || 'Unknown server error.' });
    }
  });
  server.on('close', () => bridge.close());
  const port = options.port ?? Number(process.env.PORT || 8765);
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, HOST, () => resolve({ server, bridge, address: server.address() }));
  });
}

if (require.main === module) {
  startServer().then(({ address }) => {
    console.log('IronPhoenix web UI: http://127.0.0.1:' + address.port);
    console.log('Engine executable must be in ' + ROOT);
  }).catch(err => { console.error(err.message); process.exitCode = 1; });
}

module.exports = { EngineBridge, startServer, infoLine };

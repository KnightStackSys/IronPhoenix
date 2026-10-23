'use strict';

const $ = id => document.getElementById(id);
const colors = { r: 'red', b: 'blue', y: 'yellow', g: 'green' };
const names = { r: 'RED', b: 'BLUE', y: 'YELLOW', g: 'GREEN' };
const glyphs = { P: '♟', N: '♞', B: '♝', R: '♜', Q: '♛', K: '♚' };
const arrowsColors = ['#ffc56a', '#67d4da', '#b0a0ed', '#a9d58a'];

let position = { baseFen: null, moves: [], fen: null, legal: [] };
let gameBaseFen = null;
let gameMoves = [];
let cursor = 0;
let selected = null;
let busy = false;
let analysis = new Map();
let activePV = 1;
let lastSearchId = 0;
let engineOnline = false;
let analysing = false;
let toastTimer = null;
let boardState = null;
let boardTurn = 'r';
const boardCells = new Map();
let renderedMoveKey = null;
let renderedMoveButtons = [];
let renderedMoveCursor = 0;
let analysisFrame = null;

function queueAnalysisRender() {
  if (analysisFrame !== null) return;
  analysisFrame = requestAnimationFrame(() => {
    analysisFrame = null;
    renderAnalysis();
  });
}

function setConnection(online, running = false) {
  engineOnline = online;
  analysing = running;
  $('statusDot').className = 'status-dot ' + (!online ? 'offline' : running ? 'running' : 'online');
  $('statusText').textContent = !online ? 'Engine offline' : running ? 'Engine searching' : 'Engine connected';
  $('analyzeBtn').disabled = busy || !online || running;
  $('stopBtn').disabled = busy || !online || !running;
}

function message(text, error = false) {
  const toast = $('toast');
  toast.textContent = text;
  toast.style.borderColor = error ? '#d9737e' : '#5a9185';
  toast.style.background = error ? '#482533' : '#263d44';
  toast.classList.add('visible');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toast.classList.remove('visible'), 4500);
}

function showError(text) {
  const banner = $('errorBanner');
  banner.textContent = text;
  banner.hidden = false;
  setConnection(false);
  message(text, true);
}

async function api(url, body = undefined) {
  const response = await fetch(url, body === undefined ? {} : {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  let content;
  try { content = await response.json(); } catch { throw new Error('Invalid response from localhost server.'); }
  if (!response.ok) throw new Error(content.error || 'Request failed.');
  return content;
}

async function transact(operation) {
  if (busy) return;
  busy = true;
  setConnection(engineOnline, analysing);
  try {
    await operation();
    $('errorBanner').hidden = true;
  } catch (error) {
    message(error.message, true);
  } finally {
    busy = false;
    setConnection(engineOnline, analysing);
  }
}

function parseFenBoard(fen) {
  const turn = fen[0].toLowerCase();
  const fields = fen.slice(fen.lastIndexOf('-') + 1).split('/');
  if (fields.length !== 14) throw new Error('The engine returned an invalid FEN4 board.');
  const board = [];
  for (const rank of fields) {
    const row = [];
    for (const token of rank.split(',')) {
      const count = Number(token);
      if (Number.isInteger(count) && count > 0) {
        for (let i = 0; i < count; ++i) row.push(null);
      } else if (token.toLowerCase() === 'x') row.push(null);
      else row.push(token[0].toLowerCase() + token[1].toUpperCase());
    }
    if (row.length !== 14) throw new Error('The engine returned an invalid FEN4 rank.');
    board.push(row);
  }
  return { board, turn };
}

function squareName(row, col) { return String.fromCharCode(97 + col) + String(14 - row); }
function isPlayable(row, col) { return (col >= 3 && col <= 10) || (row >= 3 && row <= 10); }
function squareCoords(value) {
  const found = /^([a-n])(1[0-4]|[1-9])$/i.exec(value || '');
  if (!found) return null;
  return { col: found[1].toLowerCase().charCodeAt(0) - 97, row: 14 - Number(found[2]) };
}
function splitMove(move) {
  const match = /^([a-n](?:1[0-4]|[1-9]))([a-n](?:1[0-4]|[1-9]))([nbrq]?)$/i.exec(move || '');
  return match ? [match[1].toLowerCase(), match[2].toLowerCase(), match[3].toLowerCase()] : null;
}

function renderBoard() {
  if (!position.fen) return;
  const { board, turn } = parseFenBoard(position.fen);
  boardState = board;
  boardTurn = turn;
  $('turnText').textContent = names[turn] + ' TO MOVE';
  $('turnBadge').querySelector('.turn-light').style.backgroundColor = 'var(--' + colors[turn] + ')';

  const boardEl = $('board');
  if (boardCells.size === 0) {
    // The board geometry never changes. Build each square once instead of
    // allocating 196 buttons, handlers, and piece glyphs on every click.
    const fragment = document.createDocumentFragment();
    for (let row = 0; row < 14; ++row) {
      for (let col = 0; col < 14; ++col) {
        const sq = squareName(row, col);
        const valid = isPlayable(row, col);
        const cell = document.createElement('button');
        cell.type = 'button';
        cell.dataset.square = sq;
        cell.disabled = !valid;
        cell.className = valid ? 'square valid ' + ((row + col) % 2 ? 'dark' : 'light') : 'square invalid';
        cell.setAttribute('role', 'gridcell');
        if (valid && ((row === 10 && col >= 3 && col <= 10) ||
            (col === 3 && row >= 3 && row <= 10))) {
          const coord = document.createElement('span');
          coord.className = 'coordinates';
          coord.textContent = row === 10 ? sq[0] : sq.slice(1);
          cell.appendChild(coord);
        }
        boardCells.set(sq, cell);
        fragment.appendChild(cell);
      }
    }
    boardEl.appendChild(fragment);
    // One event listener handles all legal board squares.
    boardEl.addEventListener('click', event => {
      const cell = event.target.closest('.square.valid');
      if (!cell || !boardState) return;
      const loc = squareCoords(cell.dataset.square);
      if (loc) chooseSquare(cell.dataset.square, boardState[loc.row][loc.col], boardTurn);
    });
  }

  const legalTargets = new Set();
  if (selected) {
    for (const move of position.legal) {
      const parsed = splitMove(move);
      if (parsed && parsed[0] === selected) legalTargets.add(parsed[1]);
    }
  }
  const lastMove = position.moves.length
    ? splitMove(position.moves[position.moves.length - 1]) : null;
  for (let row = 0; row < 14; ++row) {
    for (let col = 0; col < 14; ++col) {
      if (!isPlayable(row, col)) continue;
      const sq = squareName(row, col);
      const cell = boardCells.get(sq);
      const pieceId = board[row][col] || '';
      if (cell.dataset.piece !== pieceId) {
        cell.querySelector('.piece')?.remove();
        cell.dataset.piece = pieceId;
        if (pieceId) {
          const piece = document.createElement('span');
          piece.className = 'piece ' + colors[pieceId[0]];
          piece.textContent = glyphs[pieceId[1]] || '?';
          cell.prepend(piece);
        }
        cell.setAttribute('aria-label', sq + (pieceId ? ' ' + colors[pieceId[0]] + ' ' + pieceId[1] : ''));
      }
      let className = 'square valid ' + ((row + col) % 2 ? 'dark' : 'light');
      if (selected === sq) className += ' selected';
      if (legalTargets.has(sq)) className += ' legal-target';
      if (lastMove && lastMove[0] === sq) className += ' lastfrom';
      if (lastMove && lastMove[1] === sq) className += ' lastto';
      if (cell.className !== className) cell.className = className;
    }
  }
  renderArrows();
}

function appendArrow(svg, move, color, opacity, width) {
  const parsed = splitMove(move);
  if (!parsed) return;
  const from = squareCoords(parsed[0]), to = squareCoords(parsed[1]);
  if (!from || !to) return;
  const ax = (from.col + .5) * 100, ay = (from.row + .5) * 100;
  const bx = (to.col + .5) * 100, by = (to.row + .5) * 100;
  const dx = bx - ax, dy = by - ay, norm = Math.hypot(dx, dy);
  if (norm < 1) return;
  const vx = dx / norm, vy = dy / norm;
  const startX = ax + vx * 18, startY = ay + vy * 18;
  const endX = bx - vx * 30, endY = by - vy * 30;
  const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
  for (const [k, v] of Object.entries({ x1: startX, y1: startY, x2: endX, y2: endY,
    stroke: color, 'stroke-width': width, 'stroke-linecap': 'round', opacity })) line.setAttribute(k, v);
  svg.appendChild(line);
  const wing = 22, back = 30;
  const points = [
    [bx - vx * 17, by - vy * 17],
    [bx - vx * (back + 17) - vy * wing, by - vy * (back + 17) + vx * wing],
    [bx - vx * (back + 17) + vy * wing, by - vy * (back + 17) - vx * wing]
  ];
  const arrow = document.createElementNS('http://www.w3.org/2000/svg', 'polygon');
  arrow.setAttribute('points', points.map(pair => pair.join(',')).join(' '));
  arrow.setAttribute('fill', color);
  arrow.setAttribute('opacity', opacity);
  svg.appendChild(arrow);
}

function renderArrows() {
  const svg = $('arrows');
  svg.replaceChildren();
  const lines = [...analysis.values()].sort((a, b) => b.multiPV - a.multiPV);
  for (const line of lines) {
    if (line.pv.length) {
      const highlight = line.multiPV === activePV;
      appendArrow(svg, line.pv[0], arrowsColors[(line.multiPV - 1) % arrowsColors.length],
        highlight ? .9 : .52, highlight ? 25 : 16);
    }
  }
}

function formatScore(score) {
  if (!score) return '—';
  if (score.type === 'mate') return score.value > 0 ? 'M' + score.value : '−M' + Math.abs(score.value);
  const value = (score.value / 100).toFixed(2);
  return score.value > 0 ? '+' + value : value;
}

function formatNumber(value) {
  return Number.isFinite(value) ? Number(value).toLocaleString() : '—';
}

function renderAnalysis() {
  const items = [...analysis.values()].sort((a, b) => a.multiPV - b.multiPV);
  const first = items[0] || null;
  if (first) {
    $('depthStat').textContent = first.depth ?? '—';
    $('nodesStat').textContent = formatNumber(first.nodes);
    $('npsStat').textContent = formatNumber(first.nps);
    $('timeStat').textContent = first.time === null ? '—' : (first.time / 1000).toFixed(2) + 's';
    $('scoreHint').textContent = 'TEAM SCORE ' + formatScore(first.score);
  }
  const panel = $('pvLines');
  panel.replaceChildren();
  if (!items.length) {
    const empty = document.createElement('p');
    empty.className = 'empty';
    empty.textContent = 'Start analysis to see best moves and arrows.';
    panel.appendChild(empty);
    renderArrows();
    return;
  }
  for (const line of items) {
    const button = document.createElement('button');
    button.className = 'pv-line' + (line.multiPV === activePV ? ' active' : '');
    const badge = document.createElement('span');
    badge.className = 'pv-index';
    badge.textContent = line.multiPV;
    const content = document.createElement('span');
    content.className = 'pv-content';
    const score = document.createElement('span');
    score.className = 'pv-score';
    score.textContent = formatScore(line.score) + (line.bound === 'exact' ? '' : ' (' + line.bound + ')');
    const moves = document.createElement('span');
    moves.className = 'pv-moves';
    moves.textContent = line.pv.slice(0, 10).join(' ') || 'Calculating…';
    content.append(score, moves);
    button.append(badge, content);
    button.addEventListener('click', () => { activePV = line.multiPV; renderAnalysis(); });
    panel.append(button);
  }
  renderArrows();
}

function clearAnalysis() {
  if (analysisFrame !== null) {
    cancelAnimationFrame(analysisFrame);
    analysisFrame = null;
  }
  analysis = new Map();
  activePV = 1;
  $('bestmove').textContent = '—';
  for (const id of ['depthStat', 'nodesStat', 'npsStat', 'timeStat']) $(id).textContent = '—';
  $('scoreHint').textContent = 'Waiting for analysis';
  renderAnalysis();
}

function renderMoves() {
  $('moveCount').textContent = gameMoves.length + ' PLY';
  $('moveCursor').textContent = cursor === 0 ? 'Starting position' : 'After move ' + cursor + ' / ' + gameMoves.length;
  $('firstBtn').disabled = $('prevBtn').disabled = cursor === 0;
  $('nextBtn').disabled = $('lastBtn').disabled = cursor === gameMoves.length;
  const key = gameMoves.join('\u0000');
  if (key !== renderedMoveKey) {
    renderedMoveKey = key;
    renderedMoveButtons = [];
    const container = $('moveList');
    container.replaceChildren();
    if (!gameMoves.length) {
      const empty = document.createElement('p');
      empty.className = 'empty';
      empty.textContent = 'No moves loaded yet.';
      container.appendChild(empty);
    } else {
      const fragment = document.createDocumentFragment();
      gameMoves.forEach((move, index) => {
        const button = document.createElement('button');
        button.className = 'move-chip';
        button.textContent = (Math.floor(index / 4) + 1) + (index % 4 === 0 ? '.' : '·') + move;
        button.title = 'Go to ply ' + (index + 1);
        button.addEventListener('click', () => gotoPly(index + 1));
        renderedMoveButtons.push(button);
        fragment.appendChild(button);
      });
      container.appendChild(fragment);
    }
    renderedMoveCursor = 0;
  }
  if (renderedMoveCursor) renderedMoveButtons[renderedMoveCursor - 1]?.classList.remove('active');
  if (cursor) renderedMoveButtons[cursor - 1]?.classList.add('active');
  renderedMoveCursor = cursor;
}

function applyPosition(next) {
  if (!next?.fen) return;
  const samePosition = position.fen === next.fen
    && position.moves.join(' ') === next.moves.join(' ');
  position = next;
  selected = null;
  setConnection(next.online, next.analysing);
  // Local API replies and SSE both send the same position. Do not redraw
  // the entire board twice or overwrite FEN text while a user is editing.
  if (!samePosition) {
    $('fenInput').value = next.fen;
    renderBoard();
  }
  renderMoves();
}

async function gotoPly(ply) {
  if (ply === cursor || busy) return;
  await transact(async () => {
    const snapshot = await api('/api/position', { fen: gameBaseFen, moves: gameMoves.slice(0, ply) });
    cursor = ply;
    clearAnalysis();
    applyPosition(snapshot);
  });
}

async function chooseSquare(square, piece, turn) {
  if (busy) return;
  if (selected) {
    const candidates = position.legal.filter(move => {
      const parsed = splitMove(move);
      return parsed && parsed[0] === selected && parsed[1] === square;
    });
    if (candidates.length) {
      let move = candidates.find(x => x.endsWith('q')) || candidates[0];
      if (candidates.length > 1) {
        const answer = window.prompt('Promote to Q, R, B or N:', 'Q');
        if (answer === null) return;
        move = candidates.find(x => x.endsWith(answer.trim().toLowerCase()));
        if (!move) { message('Choose Q, R, B, or N for promotion.', true); return; }
      }
      await transact(async () => {
        const nextMoves = gameMoves.slice(0, cursor).concat(move);
        const snapshot = await api('/api/position', { fen: gameBaseFen, moves: nextMoves });
        gameMoves = nextMoves;
        cursor = gameMoves.length;
        clearAnalysis();
        applyPosition(snapshot);
      });
      return;
    }
  }
  if (piece && piece[0] === turn && position.legal.some(m => splitMove(m)?.[0] === square))
    selected = selected === square ? null : square;
  else selected = null;
  renderBoard();
}

function setTab(tab) {
  const pgn = tab === 'pgn';
  $('fenPane').hidden = pgn;
  $('pgnPane').hidden = !pgn;
  $('fenTab').classList.toggle('active', !pgn);
  $('pgnTab').classList.toggle('active', pgn);
  $('fenTab').setAttribute('aria-selected', String(!pgn));
  $('pgnTab').setAttribute('aria-selected', String(pgn));
}

$('fenTab').addEventListener('click', () => setTab('fen'));
$('pgnTab').addEventListener('click', () => setTab('pgn'));
$('loadFenBtn').addEventListener('click', () => transact(async () => {
  const snapshot = await api('/api/position', { fen: $('fenInput').value.trim(), moves: [] });
  gameBaseFen = snapshot.baseFen;
  gameMoves = [];
  cursor = 0;
  clearAnalysis();
  applyPosition(snapshot);
  message('FEN4 position loaded.');
}));
$('resetBtn').addEventListener('click', () => transact(async () => {
  const snapshot = await api('/api/position', { moves: [] });
  gameBaseFen = null;
  gameMoves = [];
  cursor = 0;
  clearAnalysis();
  applyPosition(snapshot);
  message('Modern starting position loaded.');
}));
$('loadPgnBtn').addEventListener('click', () => transact(async () => {
  const snapshot = await api('/api/pgn', { text: $('pgnInput').value });
  gameBaseFen = snapshot.baseFen;
  gameMoves = snapshot.moves;
  cursor = gameMoves.length;
  clearAnalysis();
  applyPosition(snapshot);
  message('Loaded ' + gameMoves.length + ' moves.');
}));
$('pgnFile').addEventListener('change', async event => {
  const file = event.target.files[0];
  if (!file) return;
  if (file.size > 900000) { message('PGN file exceeds 900 KB.', true); return; }
  $('pgnInput').value = await file.text();
  message('File loaded into editor. Click Load PGN to validate it.');
});
$('copyFenBtn').addEventListener('click', async () => {
  if (!position.fen) return;
  try { await navigator.clipboard.writeText(position.fen); message('FEN4 copied to clipboard.'); }
  catch { $('fenInput').focus(); $('fenInput').select(); message('Select and copy the FEN4 text.'); }
});
$('analyzeBtn').addEventListener('click', () => transact(async () => {
  clearAnalysis();
  const snapshot = await api('/api/analyze', {
    depth: Number($('depthInput').value),
    multiPV: Number($('multiInput').value),
    movetime: Number($('timeInput').value)
  });
  lastSearchId = snapshot.searchId;
  setConnection(true, snapshot.analysing);
}));
$('stopBtn').addEventListener('click', () => transact(async () => {
  const snapshot = await api('/api/stop', {});
  setConnection(snapshot.online, snapshot.analysing);
}));
$('firstBtn').addEventListener('click', () => gotoPly(0));
$('prevBtn').addEventListener('click', () => gotoPly(Math.max(0, cursor - 1)));
$('nextBtn').addEventListener('click', () => gotoPly(Math.min(gameMoves.length, cursor + 1)));
$('lastBtn').addEventListener('click', () => gotoPly(gameMoves.length));

const updates = new EventSource('/events');
updates.addEventListener('position', event => {
  const next = JSON.parse(event.data);
  if (next.fen) applyPosition(next);
});
updates.addEventListener('started', event => {
  const data = JSON.parse(event.data);
  if (data.searchId < lastSearchId) return;
  lastSearchId = data.searchId;
  clearAnalysis();
  setConnection(true, true);
});
updates.addEventListener('analysis', event => {
  const data = JSON.parse(event.data);
  if (data.searchId !== lastSearchId) return;
  analysis.set(data.multiPV, data);
  // UCI can output multiple lines in one event-loop turn. Only rebuild
  // analysis rows/arrows once per browser animation frame.
  queueAnalysisRender();
});
updates.addEventListener('bestmove', event => {
  const data = JSON.parse(event.data);
  if (data.searchId !== lastSearchId) return;
  $('bestmove').textContent = data.move;
  setConnection(true, false);
});
updates.addEventListener('error', event => {
  // EventSource also fires this on temporary HTTP disconnects. Reconnect
  // happens automatically and the API error banner carries useful details.
  if (event.data) message(String(event.data), true);
});
updates.addEventListener('message', event => {
  const text = event.data ? JSON.parse(event.data) : '';
  if (/FEN4 error|move list error/i.test(text)) message(text, true);
});

(async () => {
  try {
    const snapshot = await api('/api/state');
    gameBaseFen = snapshot.baseFen;
    gameMoves = snapshot.moves;
    cursor = gameMoves.length;
    applyPosition(snapshot);
  } catch (err) {
    showError(err.message + ' Place the compiled engine beside web/server.js, then refresh.');
  }
})();

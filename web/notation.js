'use strict';

// IronPhoenix accepts FEN4, not orthodox 8x8 FEN. Validation of legal moves
// is delegated to the running C++ engine.
const SQ = '[a-n](?:1[0-4]|[1-9])';
// PGN4 long algebraic moves can include mover / captured piece letters.
const MOVE = new RegExp('^(?:[KQRBNP])?(' + SQ + ')[-x:]?(?:[KQRBNP])?(' + SQ + ')(?:=?([nbrq]))?[+#?!]*

function normalizeMove(raw) {
  const token = String(raw || '').trim().replace(/^[([{]+|[)\]}.,;]+$/g, '');
  const match = token.match(MOVE);
  return match ? (match[1] + match[2] + (match[3] || '')).toLowerCase() : null;
}

function splitFen(fen) {
  const fields = [];
  let start = 0;
  let depth = 0;
  let quote = '';
  for (let i = 0; i < fen.length; ++i) {
    const ch = fen[i];
    if (quote) {
      if (ch === '\\') { ++i; continue; }
      if (ch === quote) quote = '';
    } else if (ch === '"' || ch === "'") {
      quote = ch;
    } else if (ch === '{') ++depth;
    else if (ch === '}') --depth;
    else if (ch === '-' && depth === 0) {
      fields.push(fen.slice(start, i));
      start = i + 1;
    }
  }
  fields.push(fen.slice(start));
  return fields;
}

function parseFen4(raw) {
  const fen = String(raw || '').trim();
  if (!fen || /[\r\n\0]/.test(fen)) throw new Error('Provide a single-line FEN4 position.');
  const fields = splitFen(fen);
  if (![7, 8].includes(fields.length)) throw new Error('Expected seven or eight FEN4 fields, not orthodox chess FEN.');
  if (!/^[RBYG]$/i.test(fields[0])) throw new Error('Invalid FEN4 side to move.');
  const ranks = fields[fields.length - 1].split('/');
  if (ranks.length !== 14) throw new Error('FEN4 must have 14 ranks.');
  const board = Array.from({ length: 14 }, () => Array(14).fill(null));
  ranks.forEach((rank, index) => {
    let file = 0;
    for (const part of rank.split(',')) {
      const item = part.trim();
      if (/^\d+$/.test(item)) {
        const count = Number(item);
        if (count < 1 || count > 14) throw new Error('Invalid FEN4 empty run.');
        file += count;
      } else if (item.toLowerCase() === 'x') {
        if (file >= 14) throw new Error('FEN4 rank too wide.');
        file++;
      } else if (/^[rbyg][pnbrqk]$/i.test(item)) {
        if (file >= 14) throw new Error('FEN4 rank too wide.');
        board[index][file++] = item[0].toLowerCase() + item[1].toUpperCase();
      } else {
        throw new Error('Invalid FEN4 board token: ' + item);
      }
      if (file > 14) throw new Error('FEN4 rank too wide.');
    }
    if (file !== 14) throw new Error('FEN4 rank ' + (14 - index) + ' is not 14 files.');
  });
  return { fen, turn: fields[0].toUpperCase(), board };
}

function parsePgn(raw) {
  const source = String(raw || '');
  if (source.length > 900000) throw new Error('PGN is too large (900 KB limit).');
  const tags = {};
  const moveBody = source.replace(/^\s*\[([^\s\]]+)\s+"((?:\\.|[^"\\])*)"\s*\]\s*$/gm,
    (_line, name, value) => {
      tags[name.toLowerCase()] = value.replace(/\\"/g, '"');
      return ' ';
    });
  let body = moveBody
    .replace(/;[^\r\n]*/g, ' ')
    .replace(/\{[^}]*\}/g, ' ')
    .replace(/\([^)]*\)/g, ' ')
    .replace(/\$\d+/g, ' ')
    .replace(/\d+\.(?:\.\.)?/g, ' ')
    .replace(/\b(?:1-0|0-1|1\/2-1\/2|\*)\b/g, ' ');
  const moves = [];
  for (const token of body.split(/\s+/).filter(Boolean)) {
    if (/^(?:red|blue|yellow|green|r|b|y|g):?$/i.test(token) || /^(?:1-0|0-1|1\/2-1\/2|\*)$/.test(token)) continue;
    if (/^(?:\.{2,3}|#|R|T)$/i.test(token)) continue;
    const move = normalizeMove(token);
    if (!move) throw new Error('Unsupported PGN4 move "' + token.slice(0, 60) + '". Use 4PC long algebraic notation (e.g. d2-d4 or Qn8xQi4); ordinary SAN is not supported.');
    moves.push(move);
    if (moves.length > 4000) throw new Error('PGN contains too many moves.');
  }
  if (tags.variant && /(?:ffa|free.?for.?all)/i.test(tags.variant))
    throw new Error('This PGN4 is Free-For-All; IronPhoenix supports Teams positions.');
  const rawFen = tags.startfen4 || tags.fen4 || tags.fen || null;
  const fen = rawFen === '4PC' ? null : rawFen;
  if (fen) parseFen4(fen);
  if (!moves.length && !rawFen) throw new Error('No moves or FEN4 tag found in PGN.');
  return { fen, moves, tags };
}

module.exports = { normalizeMove, parseFen4, parsePgn };
, 'i');

function normalizeMove(raw) {
  const token = String(raw || '').trim().replace(/^[([{]+|[)\]}.,;]+$/g, '');
  const match = token.match(MOVE);
  return match ? (match[1] + match[2] + (match[3] || '')).toLowerCase() : null;
}

function splitFen(fen) {
  const fields = [];
  let start = 0;
  let depth = 0;
  let quote = '';
  for (let i = 0; i < fen.length; ++i) {
    const ch = fen[i];
    if (quote) {
      if (ch === '\\') { ++i; continue; }
      if (ch === quote) quote = '';
    } else if (ch === '"' || ch === "'") {
      quote = ch;
    } else if (ch === '{') ++depth;
    else if (ch === '}') --depth;
    else if (ch === '-' && depth === 0) {
      fields.push(fen.slice(start, i));
      start = i + 1;
    }
  }
  fields.push(fen.slice(start));
  return fields;
}

function parseFen4(raw) {
  const fen = String(raw || '').trim();
  if (!fen || /[\r\n\0]/.test(fen)) throw new Error('Provide a single-line FEN4 position.');
  const fields = splitFen(fen);
  if (![7, 8].includes(fields.length)) throw new Error('Expected seven or eight FEN4 fields, not orthodox chess FEN.');
  if (!/^[RBYG]$/i.test(fields[0])) throw new Error('Invalid FEN4 side to move.');
  const ranks = fields[fields.length - 1].split('/');
  if (ranks.length !== 14) throw new Error('FEN4 must have 14 ranks.');
  const board = Array.from({ length: 14 }, () => Array(14).fill(null));
  ranks.forEach((rank, index) => {
    let file = 0;
    for (const part of rank.split(',')) {
      const item = part.trim();
      if (/^\d+$/.test(item)) {
        const count = Number(item);
        if (count < 1 || count > 14) throw new Error('Invalid FEN4 empty run.');
        file += count;
      } else if (item.toLowerCase() === 'x') {
        if (file >= 14) throw new Error('FEN4 rank too wide.');
        file++;
      } else if (/^[rbyg][pnbrqk]$/i.test(item)) {
        if (file >= 14) throw new Error('FEN4 rank too wide.');
        board[index][file++] = item[0].toLowerCase() + item[1].toUpperCase();
      } else {
        throw new Error('Invalid FEN4 board token: ' + item);
      }
      if (file > 14) throw new Error('FEN4 rank too wide.');
    }
    if (file !== 14) throw new Error('FEN4 rank ' + (14 - index) + ' is not 14 files.');
  });
  return { fen, turn: fields[0].toUpperCase(), board };
}

function parsePgn(raw) {
  const source = String(raw || '');
  if (source.length > 900000) throw new Error('PGN is too large (900 KB limit).');
  const tags = {};
  const moveBody = source.replace(/^\s*\[([^\s\]]+)\s+"((?:\\.|[^"\\])*)"\s*\]\s*$/gm,
    (_line, name, value) => {
      tags[name.toLowerCase()] = value.replace(/\\"/g, '"');
      return ' ';
    });
  let body = moveBody
    .replace(/;[^\r\n]*/g, ' ')
    .replace(/\{[^}]*\}/g, ' ')
    .replace(/\([^)]*\)/g, ' ')
    .replace(/\$\d+/g, ' ')
    .replace(/\d+\.(?:\.\.)?/g, ' ')
    .replace(/\b(?:1-0|0-1|1\/2-1\/2|\*)\b/g, ' ');
  const moves = [];
  for (const token of body.split(/\s+/).filter(Boolean)) {
    if (/^(?:red|blue|yellow|green|r|b|y|g):?$/i.test(token) || /^(?:1-0|0-1|1\/2-1\/2|\*)$/.test(token)) continue;
    const move = normalizeMove(token);
    if (!move) throw new Error('Unsupported PGN move "' + token.slice(0, 60) + '". IronPhoenix uses 4PC coordinate moves (e.g. d2d4), not standard SAN.');
    moves.push(move);
    if (moves.length > 4000) throw new Error('PGN contains too many moves.');
  }
  const fen = tags.fen4 || tags.fen || null;
  if (fen) parseFen4(fen);
  if (!moves.length && !fen) throw new Error('No coordinate moves or FEN4 tag found in PGN.');
  return { fen, moves, tags };
}

module.exports = { normalizeMove, parseFen4, parsePgn };

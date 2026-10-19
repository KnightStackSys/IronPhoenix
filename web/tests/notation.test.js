'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { normalizeMove, parseFen4, parsePgn } = require('../notation');
const FEN = 'R-0,0,0,0-0,0,0,0-0,0,0,0-0,0,0,0-0-' +
  'x,x,x,8,x,x,x/x,x,x,8,x,x,x/x,x,x,8,x,x,x/' +
  '14/14/14/14/14/14/14/14/' +
  'x,x,x,8,x,x,x/x,x,x,8,x,x,x/x,x,x,8,x,x,x';

test('valid IronPhoenix FEN4 board and side to move', () => {
  const parsed = parseFen4(FEN);
  assert.equal(parsed.board.length, 14);
  assert.equal(parsed.board[3].length, 14);
  assert.equal(parsed.turn, 'R');
});
test('rejects a regular two-player chess FEN', () => {
  assert.throws(() => parseFen4('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'), /FEN4/);
});
test('accepts coordinate moves including promotions', () => {
  assert.equal(normalizeMove('D2-D4'), 'd2d4');
  assert.equal(normalizeMove('g10xh11=Q+'), 'g10h11q');
  assert.equal(normalizeMove('Nf3'), null);
});
test('PGN tag plus coordinate movetext and annotations', () => {
  const game = parsePgn('[Event "Friendly"]\n[FEN4 "' + FEN + '"]\n\n1. d2-d4 {comment} d12d10 2. g2g4 $3 1-0');
  assert.equal(game.fen, FEN);
  assert.deepEqual(game.moves, ['d2d4', 'd12d10', 'g2g4']);
  assert.equal(game.tags.event, 'Friendly');
});
test('reports unsupported SAN explicitly', () => {
  assert.throws(() => parsePgn('[Event "Chess"]\n1. e4 Nf3'), /Unsupported PGN move/);
});

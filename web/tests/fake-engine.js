'use strict';

// Deterministic UCI fixture for web bridge tests. Does not play chess.
const readline = require('node:readline');
const START = 'R-0,0,0,0-0,0,0,0-0,0,0,0-0,0,0,0-0-' +
  'x,x,x,8,x,x,x/x,x,x,8,x,x,x/x,x,x,8,x,x,x/' +
  '14/14/14/14/14/14/14/14/' +
  'x,x,x,8,x,x,x/x,x,x,8,x,x,x/x,x,x,8,x,x,x';
let position = START;
let moves = [];
let timer = null;
function reply(text) { process.stdout.write(text + '\n'); }
readline.createInterface({ input: process.stdin }).on('line', line => {
  if (line === 'uci') { reply('id name IronPhoenix-test'); reply('uciok'); }
  else if (line === 'isready') reply('readyok');
  else if (line.startsWith('position ')) {
    moves = line.includes(' moves ') ? line.split(' moves ')[1].split(' ') : [];
    position = START;
    if (line.startsWith('position fen INVALID')) reply('info string FEN4 error: bad fixture');
  }
  else if (line === 'fen') reply(position);
  else if (line === 'moves') reply('legal moves (2): d2d4 d2d3');
  else if (line.startsWith('go ')) {
    reply('info depth 1 seldepth 2 score cp 42 nodes 500 nps 15000 time 33 pv d2d4 d12d10');
    timer = setTimeout(() => reply('bestmove d2d4'), 80);
  }
  else if (line === 'stop') { if (timer) clearTimeout(timer); reply('bestmove d2d4'); }
});

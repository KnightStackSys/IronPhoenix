'use strict';
document.documentElement.classList.add('js');

const burger = document.querySelector('.navbar-burger');
const menu = document.querySelector('#main-menu');
function closeMenu() {
  burger.classList.remove('is-active');
  menu.classList.remove('is-active');
  burger.setAttribute('aria-expanded', 'false');
}
burger.addEventListener('click', () => {
  const expanded = burger.getAttribute('aria-expanded') !== 'true';
  burger.classList.toggle('is-active', expanded);
  menu.classList.toggle('is-active', expanded);
  burger.setAttribute('aria-expanded', String(expanded));
});
menu.querySelectorAll('a').forEach(link => link.addEventListener('click', closeMenu));
document.addEventListener('keydown', event => {
  if (event.key === 'Escape' && burger.getAttribute('aria-expanded') === 'true') {
    closeMenu();
    burger.focus();
  }
});

if (window.isSecureContext && navigator.clipboard) {
  document.querySelectorAll('[data-copy]').forEach(button => {
    button.hidden = false;
    button.addEventListener('click', async () => {
      const status = document.querySelector('#copy-status');
      try {
        await navigator.clipboard.writeText(document.getElementById(button.dataset.copy).textContent);
        status.textContent = `${button.getAttribute('aria-label')}: copied.`;
        button.textContent = 'Copied';
        setTimeout(() => { button.textContent = 'Copy'; }, 2000);
      } catch {
        status.textContent = 'Copy unavailable. Select and copy the commands directly.';
      }
    });
  });
}

// Decorative starting formation. The board is an illustration, not a playable UI.
const board = document.querySelector('#chessboard');
const backRank = ['♜', '♞', '♝', '♛', '♚', '♝', '♞', '♜'];
for (let row = 0; row < 14; row++) {
  for (let col = 0; col < 14; col++) {
    const square = document.createElement('span');
    square.setAttribute('aria-hidden', 'true');
    const corner = (row < 3 || row > 10) && (col < 3 || col > 10);
    square.className = corner ? 'square void' : `square ${(row + col) % 2 ? 'dark' : 'light'}`;
    if (!corner) {
      if (row === 0 || row === 1) {
        square.classList.add('yellow');
        square.textContent = row === 0 ? backRank[col - 3] : '♟';
      } else if (row === 12 || row === 13) {
        square.classList.add('red');
        square.textContent = row === 13 ? backRank[col - 3] : '♟';
      } else if (col === 0 || col === 1) {
        square.classList.add('blue');
        square.textContent = col === 0 ? backRank[row - 3] : '♟';
      } else if (col === 12 || col === 13) {
        square.classList.add('green');
        square.textContent = col === 13 ? backRank[row - 3] : '♟';
      }
    }
    board.append(square);
  }
}

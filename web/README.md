# IronPhoenix localhost analysis studio

A **local-only**, dependency-free Node.js web GUI for IronPhoenix's
four-player Teams engine. It displays the 14x14 cross board, lets you play
legal moves, shows analysis PV arrows and MultiPV scores, and imports FEN4
and long-algebraic PGN/PGN4 (including Chess.com-style files).

## Quick start on Windows

1. Build the IronPhoenix engine in x64 Release.
2. Copy **`ironphoenix.exe` into this `web/` directory**, beside
   `server.js`. If you use PhoenixNet, also copy **`ironphoenix.nnue`**
   into `web/` (the engine reads it from the working directory).
3. Install **Node.js 18+** if you haven't already.
4. Double-click `web/start.bat` or run:

   ```cmd
   cd web
   node server.js
   ```

5. Open **http://127.0.0.1:8765**.

On Linux/macOS, put the executable named `ironphoenix` into `web/`,
make it executable with `chmod +x web/ironphoenix`, and run
`node web/server.js`. The server binds **only to 127.0.0.1**, not your LAN
or the public internet. No npm install, external CDNs, or cloud services.

### Using the GUI

- Select a piece and then a highlighted legal square to make a move.
- Click **Analyze position** to show up to four MultiPV lines and arrows.
  Quick mode defaults to **one PV line and 1,500 ms** of search. To search
  to a chosen depth instead, set **Move time** to `0`; the **Max depth**
  field is then used. Depth-10/MultiPV-3 is much more computationally
  expensive on four-player positions and can appear unresponsive.
  These settings limit analysis work; they don't change engine NPS.
- **Stop** sends the UCI `stop` command.
- Paste an **IronPhoenix FEN4** into the FEN tab (orthodox 8x8 chess FEN
  is not compatible). Copy the current normalized FEN4 with one click.
- Paste or open a **PGN / PGN4** with legal four-player long algebraic moves.
  Examples: `d2d4`, `d2-d4`, `Qn8-m8`, `Qn8xQi4+`.
  `[StartFen4 "4PC"]`, `[StartFen4 "<full FEN4>"]` and
  `[FEN4 "..."]` are recognized. Navigate moves with the arrows or move
  chips. The C++ engine validates every move.
- Ordinary SAN moves such as `Nf3`, `e4` or `O-O` still require
  conversion, and Free-For-All PGN4 is not supported by this Teams engine.
  Unsupported imports display an error rather than inventing positions.

All board-state changes are resolved by IronPhoenix itself using its
`position`, `fen`, and `moves` commands. The browser does not
reimplement move rules, so promotions and captures stay consistent with
the engine. Clicking a move after loading a game truncates the remaining
moves if you play a different continuation.

## Architecture and security

`server.js` launches one persistent UCI subprocess from **this web folder**,
serves `index.html`, `style.css` and `app.js`, and exposes:

| Endpoint | Purpose |
| --- | --- |
| `GET /api/state` | Start/ready engine, return current FEN4 and legal moves |
| `POST /api/position` | Load FEN4 / coordinate move sequence |
| `POST /api/pgn` | Parse and load coordinate-move PGN/PGN4 |
| `POST /api/analyze` | Start depth or movetime search, set MultiPV |
| `POST /api/stop` | Stop an active search |
| `GET /events` | Server-sent events for UCI scores, PV arrows, best moves |

The server uses **no shell invocation**, does not accept arbitrary
executable paths from HTTP requests, serves only allowlisted files,
rejects cross-origin POST requests and listens only on loopback.
It is intended for one trusted local user, not remote deployment.

Only one analysis session is active at a time; separate browser tabs share
the same engine process. Closing the local Node server terminates the engine.

## Tests

From the repository root:

```cmd
node --test web/tests/*.test.js
```

Tests use a deterministic mock UCI process; the separate C++ CI still
checks engine builds and search regressions.

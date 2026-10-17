<div align="center">
  <img src="bin/iron-phoenix.png" alt="Iron Phoenix logo" width="700">

  <h1>🔥 IronPhoenix</h1>

  <p><strong>IronPhoenix</strong> is a high-performance <strong>4-player chess Teams engine</strong> written in modern C++20.</p>

  <p>It is built around the chess.com-style 4PC cross board, team-aware search, fast move generation, handcrafted evaluation, and <strong>PhoenixNet</strong>, a purpose-built NNUE evaluator for four-player Teams chess.</p>

  <hr>

  <h2>♟️ What IronPhoenix Plays</h2>

  <p>IronPhoenix is designed for <strong>4-player Teams chess</strong>:</p>

  <p>
    <strong>Red + Yellow</strong> vs <strong>Blue + Green</strong><br>
    Turn order: <strong>Red → Blue → Yellow → Green</strong><br>
    14×14 cross-shaped board<br>
    160 playable squares<br>
    Team-aware king safety and terminal king-capture handling<br>
    Four-player castling, promotion, en-passant, and move-generation rules<br>
    Four-player FEN parsing and position setup
  </p>

  <p>The engine evaluates positions from the <strong>side-to-move team perspective</strong>: positive scores favor the team whose player is currently moving.</p>

  <hr>

  <h2>⚡ Engine Features</h2>

  <h3>🔎 Search</h3>

  <p>IronPhoenix includes a modern alpha-beta search stack designed specifically for four-player Teams chess:</p>

  <p>
    Negamax / PVS-style search<br>
    Quiescence search<br>
    Aspiration Windows<br>
    Transposition tables<br>
    History-based move ordering<br>
    Static Exchange Evaluation (SEE)<br>
    Late Move Reductions (LMR)<br>
    Late Move Pruning (LMP)<br>
    Null Move Pruning (NMP)<br>
    History Move Pruning (HMP)<br>
    Search pruning and reductions<br>
    Iterative deepening<br>
    Principal variation reporting<br>
    Time / depth search limits<br>
    MultiPV root search
  </p>

  <p><code>MultiPV=1</code> keeps the normal single-PV search path. Higher values use the dedicated MultiPV root search.</p>

  <pre><code>setoption name MultiPV value 4
go depth 8</code></pre>

  <hr>

  <h2>🧮 Evaluation</h2>

  <p>IronPhoenix supports two evaluation paths.</p>

  <h3>🛠️ Handcrafted Evaluation</h3>

  <p>The HCE currently provides the engine's deterministic fallback and PhoenixNet teacher foundation, including:</p>

  <p>
    Material<br>
    Mobility<br>
    Team-relative scoring
  </p>

  <h3>🧠 PhoenixNet NNUE</h3>

  <p><strong>PhoenixNet</strong> is IronPhoenix's native neural evaluator.</p>

  <p>Current PhoenixNet v1 architecture:</p>

  <p>
    65,280 sparse input features<br>
    17 king buckets<br>
    4 relative piece colors<br>
    6 piece types<br>
    Own-king feature stream<br>
    Partner-king feature stream<br>
    128-wide shared feature transformer<br>
    <code>256 → 32 → 32 → 1</code> dense network<br>
    Centipawn-like side-to-move-team output
  </p>

  <p>If <code>ironphoenix.nnue</code> is available beside the executable, IronPhoenix loads it automatically. Otherwise it falls back to HCE.</p>

  <p>For the full PhoenixNet architecture, dataset format, training flow, export format, append/resume support, and generator options, see <strong><a href="nnue/README.md">🧠 PhoenixNet NNUE documentation</a></strong>.</p>

  <hr>

  <h2>🏗️ Building IronPhoenix</h2>

  <h3>📋 Requirements</h3>

  <p>
    CMake <strong>3.24+</strong><br>
    A C++20 compiler<br>
    Visual Studio / MSVC, Clang, or GCC<br>
    Threads support
  </p>

  <h3>🪟 Windows / Visual Studio</h3>

  <p>From the project root:</p>

  <pre><code>cmake -S . -B build -DIRONPHOENIX_BUILD_DATASET_GENERATOR=ON
cmake --build build --config Release</code></pre>

  <p>To build only the engine:</p>

  <pre><code>cmake --build build --config Release --target ironphoenix</code></pre>

  <p>To build only the PhoenixNet dataset generator:</p>

  <pre><code>cmake --build build --config Release --target ironphoenix_dataset</code></pre>

  <p>If you are using the Visual Studio CMake <code>x64-release</code> layout used during development:</p>

  <pre><code>cmake --build out\build\x64-release --target ironphoenix
cmake --build out\build\x64-release --target ironphoenix_dataset</code></pre>

  <hr>

  <h2>▶️ Running the Engine</h2>

  <p>Launch the engine executable and enter UCI-style commands:</p>

  <pre><code>uci
isready
position startpos
go depth 8</code></pre>

  <p>Enable MultiPV:</p>

  <pre><code>setoption name MultiPV value 4
position startpos
go depth 8</code></pre>

  <p>Useful engine options include:</p>

  <pre><code>setoption name Hash value 256
setoption name MultiPV value 4
setoption name Clear Hash</code></pre>

  <hr>

  <h2>🧪 PhoenixNet Dataset Generation</h2>

  <p>The project builds a native dataset generator:</p>

  <pre><code>ironphoenix_dataset</code></pre>

  <p>It generates self-play positions using the engine search as a teacher and stores the exact sparse features consumed by PhoenixNet.</p>

  <h3>🚀 Example dataset run</h3>

  <pre><code>ironphoenix_dataset.exe --positions 500000 --depth 8 --workers 8 --hash 16 --random-plies 8 --skip-plies 12 --sample-every 3 --exploration 0.15 --exploration-multipv 4 --exploration-temperature 120 --exploration-max-loss 250 --dedup --output "..\..\..\nnue\phoenix-v1.ipd"</code></pre>

  <p>The generator supports:</p>

  <p>
    Parallel self-play workers<br>
    Controlled MultiPV exploration<br>
    Single-PV search for non-exploration positions<br>
    NNUE-feature deduplication<br>
    Randomized opening plies<br>
    Native compact <code>.ipd</code> datasets<br>
    Safe append mode<br>
    Resume-to-total mode<br>
    Graceful <code>Ctrl+C</code> stopping<br>
    Recovery of complete records from older interrupted runs<br>
    Progress reporting every 10 saved positions
  </p>

  <h3>🛑 Stop and resume safely</h3>

  <p>Press <strong>Ctrl+C once</strong>. The generator stops active searches, joins its workers, flushes records, and commits the current dataset count.</p>

  <p>Resume toward a total target:</p>

  <pre><code>ironphoenix_dataset.exe --resume --positions 500000 --depth 8 --workers 8 --hash 16 --dedup --output "..\..\..\nnue\phoenix-v1.ipd"</code></pre>

  <p>If the file already contains 50,000 positions, this produces only the remaining 450,000.</p>

  <hr>

  <h2>🏋️ Training PhoenixNet</h2>

  <p>Create a Python environment:</p>

  <pre><code>py -m venv nnue\.venv
.\nnue\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install torch numpy</code></pre>

  <p>Train:</p>

  <pre><code>python nnue\train.py nnue\phoenix-v1.ipd `
    --epochs 20 `
    --batch-size 1024 `
    --lr 0.001 `
    -o nnue\phoenixnet-v1.pt</code></pre>

  <p>Export to the native IronPhoenix NNUE format:</p>

  <pre><code>python nnue\export.py nnue\phoenixnet-v1.pt `
    -o ironphoenix.nnue `
    --output-scale 400</code></pre>

  <p>Place <code>ironphoenix.nnue</code> beside <code>ironphoenix.exe</code> and start the engine. A successful load prints:</p>

  <pre><code>PhoenixNet loaded: ironphoenix.nnue</code></pre>

  <hr>

  <h2>📁 Project Layout</h2>

  <pre><code>IronPhoenix/
├── include/ironphoenix/     # Public engine headers
├── src/                     # Engine implementation
├── tools/                   # Dataset generation tools
├── tests/                   # Correctness tests and benchmarks
├── nnue/                    # PhoenixNet model, trainer, exporter, docs
├── CMakeLists.txt           # Build configuration
└── README.md                # Project overview</code></pre>

  <hr>

  <h2>🧭 Development Direction</h2>

  <p>IronPhoenix is under active development. Current and planned work includes:</p>

  <p>
    Stronger handcrafted evaluation terms<br>
    Larger and more diverse PhoenixNet datasets<br>
    Incremental NNUE accumulators<br>
    Quantized NNUE weights<br>
    SIMD / AVX2 inference<br>
    Search tuning and SPRT testing<br>
    Opening-book / explorer integration<br>
    Continued search-performance work
  </p>

  <p>PhoenixNet v1 intentionally keeps a straightforward full-refresh inference path as a correctness baseline before aggressive optimization.</p>

  <hr>

  <h2>❤️ Author</h2>

  <p><strong>IronPhoenix</strong> is developed by <strong>Nick / KnightStackSys</strong> as a dedicated high-performance engine for 4-player Teams chess.</p>

  <p>🔥 <strong>Build. Search. Learn. Rise.</strong></p>
</div>

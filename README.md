# 🔥 IronPhoenix

**IronPhoenix** is a high-performance **4-player chess Teams engine** written in modern C++20.

It is built around the chess.com-style 4PC cross board, team-aware search, fast move generation, handcrafted evaluation, and **PhoenixNet**, a purpose-built NNUE evaluator for four-player Teams chess.

> ❤️ Built with love by Nick.

---

## ♟️ What IronPhoenix Plays

IronPhoenix is designed for **4-player Teams chess**:

- 🔴 **Red + Yellow** vs 🔵 **Blue + Green**
- 🔄 Turn order: **Red → Blue → Yellow → Green**
- 🧭 14×14 cross-shaped board
- 🟩 160 playable squares
- 👑 Team-aware king safety and terminal king-capture handling
- 🏁 Four-player castling, promotion, en-passant, and move-generation rules
- 📄 Four-player FEN parsing and position setup

The engine evaluates positions from the **side-to-move team perspective**: positive scores favor the team whose player is currently moving.

---

## ⚡ Engine Features

### 🔎 Search

IronPhoenix includes a modern alpha-beta search stack designed specifically for four-player Teams chess:

- 🧠 Negamax / PVS-style search
- 🔇 Quiescence search
- 🗃️ Transposition tables
- 📚 History-based move ordering
- 🎯 Static Exchange Evaluation (SEE)
- 📉 Late Move Reductions (LMR)
- ✂️ Search pruning and reductions
- 🧵 Iterative deepening
- 🧭 Principal variation reporting
- ⏱️ Time / depth search limits
- 🔀 MultiPV root search

`MultiPV=1` keeps the normal single-PV search path. Higher values use the dedicated MultiPV root search.

```text
setoption name MultiPV value 4
go depth 8
```

---

## 🧮 Evaluation

IronPhoenix supports two evaluation paths.

### 🛠️ Handcrafted Evaluation

The HCE currently provides the engine's deterministic fallback and PhoenixNet teacher foundation, including:

- ♟️ Material
- 🏃 Mobility
- 🤝 Team-relative scoring

### 🧠 PhoenixNet NNUE

**PhoenixNet** is IronPhoenix's native neural evaluator.

Current PhoenixNet v1 architecture:

- 🗺️ 65,280 sparse input features
- 👑 17 king buckets
- 🎨 4 relative piece colors
- ♟️ 6 piece types
- 🧩 Own-king feature stream
- 🤝 Partner-king feature stream
- 🔢 128-wide shared feature transformer
- 🧠 `256 → 32 → 32 → 1` dense network
- 📏 Centipawn-like side-to-move-team output

If `ironphoenix.nnue` is available beside the executable, IronPhoenix loads it automatically. Otherwise it falls back to HCE.

For the full PhoenixNet architecture, dataset format, training flow, export format, append/resume support, and generator options, see **[🧠 PhoenixNet NNUE documentation](nnue/README.md)**.

---

## 🏗️ Building IronPhoenix

### 📋 Requirements

- CMake **3.24+**
- A C++20 compiler
- Visual Studio / MSVC, Clang, or GCC
- Threads support

### 🪟 Windows / Visual Studio

From the project root:

```powershell
cmake -S . -B build -DIRONPHOENIX_BUILD_DATASET_GENERATOR=ON
cmake --build build --config Release
```

To build only the engine:

```powershell
cmake --build build --config Release --target ironphoenix
```

To build only the PhoenixNet dataset generator:

```powershell
cmake --build build --config Release --target ironphoenix_dataset
```

If you are using the Visual Studio CMake `x64-release` layout used during development:

```cmd
cmake --build out\build\x64-release --target ironphoenix
cmake --build out\build\x64-release --target ironphoenix_dataset
```

---

## ▶️ Running the Engine

Launch the engine executable and enter UCI-style commands:

```text
uci
isready
position startpos
go depth 8
```

Enable MultiPV:

```text
setoption name MultiPV value 4
position startpos
go depth 8
```

Useful engine options include:

```text
setoption name Hash value 256
setoption name MultiPV value 4
setoption name Clear Hash
```

---

## 🧪 PhoenixNet Dataset Generation

The project builds a native dataset generator:

```text
ironphoenix_dataset
```

It generates self-play positions using the engine search as a teacher and stores the exact sparse features consumed by PhoenixNet.

### 🚀 Example dataset run

```cmd
ironphoenix_dataset.exe --positions 500000 --depth 8 --workers 8 --hash 16 --random-plies 8 --skip-plies 12 --sample-every 3 --exploration 0.15 --exploration-multipv 4 --exploration-temperature 120 --exploration-max-loss 250 --dedup --output "..\..\..\nnue\phoenix-v1.ipd"
```

The generator supports:

- 🧵 Parallel self-play workers
- 🔀 Controlled MultiPV exploration
- ⚡ Single-PV search for non-exploration positions
- 🧹 NNUE-feature deduplication
- 🎲 Randomized opening plies
- 💾 Native compact `.ipd` datasets
- ➕ Safe append mode
- ▶️ Resume-to-total mode
- 🛑 Graceful `Ctrl+C` stopping
- ♻️ Recovery of complete records from older interrupted runs
- 📊 Progress reporting every 10 saved positions

### 🛑 Stop and resume safely

Press **Ctrl+C once**. The generator stops active searches, joins its workers, flushes records, and commits the current dataset count.

Resume toward a total target:

```cmd
ironphoenix_dataset.exe --resume --positions 500000 --depth 8 --workers 8 --hash 16 --dedup --output "..\..\..\nnue\phoenix-v1.ipd"
```

If the file already contains 50,000 positions, this produces only the remaining 450,000.

---

## 🏋️ Training PhoenixNet

Create a Python environment:

```powershell
py -m venv nnue\.venv
.\nnue\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install torch numpy
```

Train:

```powershell
python nnue\train.py nnue\phoenix-v1.ipd `
    --epochs 20 `
    --batch-size 1024 `
    --lr 0.001 `
    -o nnue\phoenixnet-v1.pt
```

Export to the native IronPhoenix NNUE format:

```powershell
python nnue\export.py nnue\phoenixnet-v1.pt `
    -o ironphoenix.nnue `
    --output-scale 400
```

Place `ironphoenix.nnue` beside `ironphoenix.exe` and start the engine. A successful load prints:

```text
PhoenixNet loaded: ironphoenix.nnue
```

---

## 📁 Project Layout

```text
IronPhoenix/
├── include/ironphoenix/     # Public engine headers
├── src/                     # Engine implementation
├── tools/                   # Dataset generation tools
├── tests/                   # Correctness tests and benchmarks
├── nnue/                    # PhoenixNet model, trainer, exporter, docs
├── CMakeLists.txt           # Build configuration
└── README.md                # Project overview
```

---

## 🧭 Development Direction

IronPhoenix is under active development. Current and planned work includes:

- 🧠 Stronger handcrafted evaluation terms
- 🧪 Larger and more diverse PhoenixNet datasets
- ⚙️ Incremental NNUE accumulators
- 📦 Quantized NNUE weights
- 🚀 SIMD / AVX2 inference
- 🔬 Search tuning and SPRT testing
- 📚 Opening-book / explorer integration
- 📈 Continued search-performance work

PhoenixNet v1 intentionally keeps a straightforward full-refresh inference path as a correctness baseline before aggressive optimization.

---

## ❤️ Author

**IronPhoenix** is developed by **Nick / KnightStackSys** as a dedicated high-performance engine for 4-player Teams chess.

🔥 **Build. Search. Learn. Rise.**

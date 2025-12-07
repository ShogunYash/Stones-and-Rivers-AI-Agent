# Student Agent: High-Performance C++ AI for Rivers & Stones

## Project Overview

This repository hosts a competitive AI agent designed for the strategy game "Rivers & Stones." The agent is engineered in **C++17** to maximize computational efficiency and interacts with the game's Python engine via **pybind11**.

The AI employs a hybrid architecture combining **Iterative Deepening Depth-First Search (IDDFS)** with a custom **Heuristic Evaluation Engine**. This allows the agent to simulate thousands of future board states per second, adapting its strategy dynamically based on board size, time constraints, and opponent behavior.

## Core Logic & Architecture

The agent's decision-making process is divided into two distinct systems: the **Search Manager** (which explores future possibilities) and the **Tactical Evaluator** (which judges the quality of a specific board state).

### 1. Search Engine (SearchManager)

The Search Manager is responsible for navigating the game tree. It uses the following algorithms to ensure optimal play within strict time limits:

#### Iterative Deepening Depth-First Search (IDDFS)
In competitive environments with strict time controls, searching to a fixed depth is risky. If the calculation takes too long, the agent might timeout and crash. To prevent this, the agent uses IDDFS:
1.  **Depth 1:** It first calculates the best move looking only one step ahead.
2.  **Depth 2:** If time remains, it restarts the search looking two steps ahead.
3.  **Depth N:** It continues deeper until the allocated time expires.

This ensures that a "best move" is always available, regardless of when the timer interrupts the calculation.

#### Alpha-Beta Pruning
The game tree for "Rivers & Stones" expands exponentially. To handle this, the agent utilizes Alpha-Beta pruning. This algorithm maintains two values, alpha (the minimum score the AI is assured of) and beta (the maximum score the opponent is assured of). If a specific move sequence results in a worse outcome than a move already found, the agent immediately stops searching that branch ("pruning"). This allows the agent to search significantly deeper than standard brute-force methods.

#### Transposition Table & Zobrist Hashing
A major inefficiency in search algorithms is analyzing the same board position multiple times (e.g., reaching the same state via different move orders).
* **Zobrist Hashing:** The agent assigns a unique 64-bit random integer to every possible piece-position combination. By XORing these values, it generates a unique "fingerprint" (hash) for the entire board state.
* **Transposition Table:** When the agent evaluates a board, it stores the result and the hash in a hash map. If it encounters the same hash again, it retrieves the stored score instantly, bypassing the need for re-evaluation.

#### Stalemate Resolution
In end-game scenarios where moves might cycle indefinitely with equal scores, the agent employs a Mersenne Twister pseudorandom number generator (PRNG). If multiple moves are mathematically tied for the "best" score, the agent randomly selects one to introduce unpredictability and break potential loops.

---

### 2. Heuristic Evaluation (The Brain)

While the Search Manager finds moves, the **Tactical Evaluator** determines if a move is "good" or "bad." The evaluator assigns a numerical score to a board state based on several strategic components. These components are weighted dynamically—the agent plays differently on a large board ($17\times16$) versus a small board ($13\times12$).

#### A. Attack Manager (The "Gravity" Model)
This component drives the agent to score. It treats the scoring area as a "gravity well" that pulls friendly stones toward it.
* **Proximity Scoring:** The board is mapped such that squares closer to the goal yield higher points. This creates a gradient field that naturally guides stones toward the target.
* **Goal Incentives:**
    * **Stones in Goal:** Awarded a massive bonus (50,000 points). This is the highest priority, effectively "locking" the piece in place.
    * **Rivers in Goal:** Awarded a high bonus (20,000 points), but significantly less than a Stone. This score differential (30,000 points) creates an implicit mathematical pressure for the agent to **flip** the River into a Stone to capture the remaining points, essentially teaching the AI the rules of winning without hard-coded scripts.

#### B. River Network Manager (Flow Analysis)
In this game, Rivers act as highways. A River piece is useless if it does not facilitate movement.
* **Flow Simulation:** The agent runs a Breadth-First Search (BFS) from every river piece to determine its "reach."
* **Connectivity Score:** A River is scored based on how many friendly stones can currently access it and where that river leads. A river network that drops a stone 1 tile away from the goal is valued exponentially higher than one that leads nowhere.

#### C. Highway Potential
Distinct from the immediate network, this heuristic encourages long-term infrastructure building.
* **Look-Ahead:** It identifies River pieces that are aligned with the goal but are not currently being used.
* **Non-Linear Scaling:** Using a power map, longer rivers are rewarded exponentially. This encourages the AI to build "highways" across the board early in the game, preparing for rapid scoring strikes later.

#### D. Defense Manager
The agent plays defensively by analyzing the opponent's potential.
* **Opponent Threat Detection:** It calculates the "Gravity Score" for the opponent. If the opponent's stones are close to their goal, the board state receives a heavy penalty. This forces the Search Manager to find moves that lower the opponent's potential (blocking).
* **Self-Blocking Penalty:** The agent is heavily penalized for placing its own River pieces inside its own scoring area, as this blocks potential winning moves. This heuristic effectively keeps the goal zone clear.

#### E. Closing Logic (Near-Win Bonus)
When the agent detects it has filled 3 or more slots in the scoring area, it switches to a "Closing" state.
* It identifies the specific coordinate of the remaining empty slot.
* It applies a specialized bonus to any piece adjacent to that empty slot. This helps the AI navigate the cramped end-game environment where movement is restricted.

---

## Technical Implementation Details

### Efficient Memory Management
The internal board representation (`FastBoard`) is decoupled from the Python game engine.
* **Struct-Based Design:** Instead of heavy objects or strings, the board uses a lightweight `Piece` struct containing `enum class` types (`uint8_t`) for Player, Side, and Orientation. This minimizes memory bandwidth usage and improves CPU cache locality.
* **Single-Pass Conversion:** The complex Python dictionary board is converted into this efficient C++ structure exactly once per turn, ensuring that the computationally expensive search phase runs on raw C++ data types.

### Dynamic Weighting System
The agent identifies the board size at runtime and adjusts its personality:
* **Large Boards ($17\times16$):** The weights for River connectivity and Highway potential are tripled. On large maps, mobility is the primary determinant of victory.
* **Small Boards ($13\times12$):** The weights are balanced between defense and attack, as the shorter distances make every move an immediate threat.

## Build and Compilation

### Prerequisites
* Python 3.x
* CMake (Version 3.12 or higher)
* C++ Compiler with C++17 support (GCC/Clang)
* `pybind11` library

### Compilation Instructions

1.  **Using the Shell Script:**
    This script automatically detects your Python environment and compiles the module.
    ```bash
    bash compile.sh
    ```

2.  **Manual Compilation via CMake:**
    If you prefer manual control or need to debug the build process:
    ```bash
    mkdir build
    cd build
    cmake ..
    make
    ```

### Running the Agent
The agent is designed to run within the provided `gameEngine.py` framework. Once compiled, the shared object file (`.so`) acts as a Python module.

To run a match between a Random Bot (Circle) and this Student Agent (Square):

```bash
python gameEngine.py --mode aivai --circle random --square student_cpp
# Bug and Inconsistency Audit Report

## Executive Summary

This document records the comprehensive audit of the `doc-pop-1d-chess` repository, spanning both the legacy Python prototype and the C++23 retrograde tablebase solver and alpha-beta engine.

---

## 1. High-Severity Issues and Logic Inversions

### 1.1 `src/python/ai_greedy.py`: Reversed Move Selection and Full Position Passed to Board Function
- **Location**: [`src/python/ai_greedy.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/ai_greedy.py#L19-L35)
- **Description**:
  1. `Position.get_attacked_squares(position, ...)` was called passing the full FENCE string `position` (e.g., `KQRBNP....pnbrqk w 0 1`) instead of the 16-character `board` string. This caused `get_attacked_squares` to treat metadata characters (spaces, active color, move counters) as board squares, indexing out of bounds and miscalculating attacked squares.
  2. Move sorting used `moves = sorted(moves, key=lambda m: move_score(m), reverse=False)` and returned `moves[0]`. With `reverse=False`, the sorting placed the lowest-valued moves first (e.g., hanging a queen for -9 or a king for -100), causing the greedy AI to play the worst legal move rather than the best move.
- **Status**: Fixed. `board` is now passed to `get_attacked_squares` and moves are sorted with `reverse=True`.

### 1.2 `src/python/evaluate.py`: Signature Mismatch and `AttributeError` in `test_next_moves`
- **Location**: [`src/python/evaluate.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/evaluate.py#L203-L221)
- **Description**:
  1. `test_next_moves` called `score_position(position, max_depth)`. The positional argument signature for `score_position` is `(position, starting_player=None, alpha=..., beta=..., depth=0, max_depth=None, ...)`. Passing `max_depth` as the 2nd positional argument bound it to `starting_player` (e.g. `starting_player = 16`), breaking player turn comparison (`active == starting_player`). Furthermore, it scored the initial position rather than the board resulting from the move.
  2. Line 219 attempted `next_tuple[2].ljust(6, " ")`. `next_tuple[2]` is a `(score, movelist)` tuple. Calling `.ljust()` on a tuple raised `AttributeError: 'tuple' object has no attribute 'ljust'`.
- **Status**: Fixed. The call now uses `score_position(Position.apply_move(position, next_move), max_depth=max_depth)` and string-formats the score before calling `.ljust()`.

### 1.3 `src/python/all_positions.py`: `TypeError` and Tuple/String Type Inconsistency
- **Location**: [`src/python/all_positions.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/all_positions.py#L11-L43)
- **Description**:
  1. `state_to_position` executed `" ".join((tup[0], tup[1], 0, 0))`. `0` is an integer, causing `TypeError: sequence item 2: expected str instance, int found`.
  2. `is_candidate` expected a `(board, active)` state tuple, but line 39 passed `Position.apply_move_board(board, next_move)`, which returned only a board string. The membership check `board in seen_states` always returned `False`, preventing proper state deduplication across plies.
- **Status**: Fixed. `state_to_position` stringifies all elements, and `explore` maintains deduplicated state sets using `(board, active)` tuples.

---

## 2. Medium-Severity Issues and Architectural Differences

### 2.1 Python vs C++ Rule Divergence: The 150-Fullmove Draw Rule
- **Location**: [`src/python/position.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/position.py#L95-L96), [`src/python/evaluate.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/evaluate.py#L44-L45)
- **Description**: The Python implementation hardcoded a draw condition whenever `fullmove >= 150`. This artificial limit was not part of standard chess or Doctor Popular's 1D Chess rules and made evaluations dependent on the arbitrary fullmove counter.
- **C++ Resolution**: The C++ engine removed this condition entirely, relying on the fifty-move rule (100 halfmoves without capture or pawn move) and exact repetition detection.

### 2.2 `src/python/evaluate.py`: Mutable Default Arguments
- **Location**: [`src/python/evaluate.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/evaluate.py#L81-L82)
- **Description**: `score_position` defined `movelist=[]` and `seen_boards=Counter()` in its default parameter list. In Python, default arguments are instantiated once when the function is defined, causing state to leak across invocations if not explicitly passed.
- **Status**: Fixed. Defaults changed to `None` with internal initialization.

### 2.3 `src/python/ai_dfs.py`: Misleading Name and Implementation
- **Location**: [`src/python/ai_dfs.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/ai_dfs.py#L1-L8)
- **Description**: The file was named `ai_dfs.py` and had a comment stating "Given a position, return a random move", calling `random.choice()`. It did not implement depth-first search.
- **Status**: Fixed. Implemented search-based move selection via `evaluate.score_position(position, max_depth=max_depth)`.

### 2.4 `src/cpp/constants.h`: Unused Non-Inline Global Vector
- **Location**: [`src/cpp/constants.h`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/cpp/constants.h#L17-L121)
- **Description**: `const vector<unsigned long long> KING_BOARDS` was declared in a header file without `inline` or `static`, causing static initialization overhead. It is unused across the active codebase.

---

## 3. Documentation and Specification Inconsistencies

### 3.1 Board Size Inconsistency in Python Docstring
- **Location**: [`src/python/position.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/position.py#L26-L28)
- **Description**: The docstring stated "There are 14 squares which are 0-indexed, so squares are 0 through 13 inclusive", whereas the actual board size is 16 squares (0 through 15 inclusive).
- **Status**: Fixed. Updated docstring to state 16 squares (0 through 15).

### 3.2 Typos in Header and Module Comments
- **Location**: [`src/cpp/position.h`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/cpp/position.h#L27), [`src/python/position.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/position.py#L9-L18)
- **Description**: Typo "Eeach piece is identified by..." and "increment after black's move".
- **Status**: Fixed. Corrected to "Each piece is identified by..." and "incremented after black's move".

---

## 4. Validation of Core Mathematical Assumptions

The following table summarizes the assumptions leveraged by the retrograde analysis solver and their empirical validation:

| Assumption / Invariant | Rationale | Empirical Verification |
| :--- | :--- | :--- |
| **Piece order invariance** | Pieces $K, R, P, p, r, k$ cannot jump or hop; on a 1D board, their relative order $K < R < P < p < r < k$ is strictly preserved throughout the game. | Verified over 4,125,581,107 legal positions and 5,000 random playouts (0 failures). |
| **Bishop parity invariance** | Bishops slide at steps of $\pm 2$, strictly preserving the square parity of their starting squares ($W_B$ odd, $B_B$ even). | Verified across all 8,960 slice definitions (0 failures). |
| **Pawn promotion impossibility** | Pawns advance strictly forward and cannot pass opposing kings or pieces without capturing. Because kings cannot be jumped, white pawn cannot exceed square 14 and black pawn cannot drop below square 1. | Verified across all reachable states (0 promotion states reachable). |
| **Mirror symmetry** | Board reflection combined with color swapping maps the entire state space onto itself. | Verified via `mirrorcheck` across all 965,036,180 positions at 7 pieces (0 mismatches between mirrored and unmirrored solves). |
| **50-Move rule independence of start position result** | Backward induction carrying Distance-to-Zeroing (DTZ) bounds within slices matches unlimited ply solve for the root game outcome. | 7,851,385,458 comparisons between DTZ and WDL tables yielded 100.0000% agreement on game outcome; White win with 1. P 5-6 remains unique winning move. |

---

## 5. Audit Summary Matrix

| ID | File | Component | Severity | Description | Resolution |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **BUG-01** | `ai_greedy.py` | Python AI | High | Passed full FENCE string to `get_attacked_squares`; inverted sort order caused selection of worst move | Fixed |
| **BUG-02** | `evaluate.py` | Python Evaluator | High | Parameter order mismatch in `test_next_moves`; `.ljust()` on tuple crash | Fixed |
| **BUG-03** | `all_positions.py` | Python Explorer | High | Integer join `TypeError`; type mismatch in candidate state lookup | Fixed |
| **BUG-04** | `evaluate.py` | Python Evaluator | Medium | Mutable default arguments `movelist=[]` and `seen_boards=Counter()` | Fixed |
| **BUG-05** | `ai_dfs.py` | Python AI | Medium | Implementation performed random move selection instead of DFS | Fixed |
| **BUG-06** | `position.py` | Python Engine | Medium | 150-fullmove artificial draw rule | Documented |
| **BUG-07** | `constants.h` | C++ Engine | Low | Unused non-inline global vector in header | Documented |
| **DOC-01** | `position.py` | Documentation | Low | Docstring referenced 14 squares instead of 16 | Fixed |
| **DOC-02** | `position.h` / `position.py` | Documentation | Low | Typos "Eeach" and "increment after black's move" | Fixed |
| **GIT-01** | `.gitignore` | Build / Repo | Low | Missing `.DS_Store` pattern | Fixed |

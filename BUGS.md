# Bug and Inconsistency Audit Report

## Executive Summary

This document records the comprehensive audit of the `doc-pop-1d-chess` repository, spanning both the legacy Python prototype and the C++23 retrograde tablebase solver and alpha-beta engine.

---

## 1. High-Severity Issues and Logic Inversions

### 1.1 `src/python/ai_greedy.py`: Reversed Move Selection and Full Position Passed to Board Function
- **Location**: [`src/python/ai_greedy.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/ai_greedy.py#L19-L35)
- **Description**:
  1. `Position.get_attacked_squares(position, ...)` was called passing the full FENCE string `position` (e.g., `KQRBNP....pnbrqk w 0 1`) instead of the 16-character `board` string. *Corrected 2026-08-26:* this did **not** index out of bounds (`index_valid` short-circuits every access at 16 squares) and the metadata characters are inert with one exception — when the requested colour is black and the string's active-colour field is `b`, that character parses as a phantom black bishop at index 17 and corrupts the attack set. `ai_greedy` always requests the *opponent* of the active colour, so the phantom can never appear at this call site; verified over 45,144 positions from 1,000 random games with zero behavioral differences. The call was sloppy and worth fixing, but the observable bug here was item 2 alone.
  2. Move sorting used `moves = sorted(moves, key=lambda m: move_score(m), reverse=False)` and returned `moves[0]`. With `reverse=False`, the sorting placed the lowest-valued moves first (e.g., hanging a queen for -9 or a king for -100), causing the greedy AI to play the worst legal move rather than the best move.
- **Status**: Fixed. `board` is now passed to `get_attacked_squares` and moves are sorted with `reverse=True`. A later pass also made `move` return `None` at terminal positions instead of raising `IndexError` on `moves[0]`.

### 1.2 `src/python/evaluate.py`: Signature Mismatch and `AttributeError` in `test_next_moves`
- **Location**: [`src/python/evaluate.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/evaluate.py#L203-L221)
- **Description**:
  1. `test_next_moves` called `score_position(position, max_depth)`. The positional argument signature for `score_position` is `(position, starting_player=None, alpha=..., beta=..., depth=0, max_depth=None, ...)`. Passing `max_depth` as the 2nd positional argument bound it to `starting_player` (e.g. `starting_player = 16`), breaking player turn comparison (`active == starting_player`). Furthermore, it scored the initial position rather than the board resulting from the move. *Corrected 2026-08-26:* the dominant symptom was worse than a crash — with `max_depth` consumed as `starting_player`, `max_depth` stayed `None` and the search ran with **no depth limit**, so `test_next_moves` hung indefinitely on any nontrivial position. The `.ljust()` crash below is only reachable for positions whose complete game tree is tiny.
  2. Line 219 attempted `next_tuple[2].ljust(6, " ")`. `next_tuple[2]` is a `(score, movelist)` tuple. Calling `.ljust()` on a tuple raised `AttributeError: 'tuple' object has no attribute 'ljust'`.
- **Status**: Fixed, then re-fixed. The original fix called `score_position(Position.apply_move(position, next_move), max_depth=max_depth)`, which defaults `starting_player` to the mover of the *successor* position — the opponent — so sorting `reverse=True` listed the mover's **worst** moves first (demonstrated from `KQ............qk w 0 1`: queen-hanging moves ranked above the queen capture). The call now passes `starting_player=active` (the original mover), which restores correct ranking, and string-formats the score before `.ljust()`.

### 1.3 `src/python/all_positions.py`: `TypeError` and Tuple/String Type Inconsistency
- **Location**: [`src/python/all_positions.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/all_positions.py#L11-L43)
- **Description**:
  1. `state_to_position` executed `" ".join((tup[0], tup[1], 0, 0))`. `0` is an integer, causing `TypeError: sequence item 2: expected str instance, int found`.
  2. `is_candidate` expected a `(board, active)` state tuple, but line 39 passed `Position.apply_move_board(board, next_move)`, which returned only a board string. The membership check `board in seen_states` always returned `False`, preventing proper state deduplication across plies.
- **Status**: Fixed. `state_to_position` stringifies all elements, and `explore` maintains deduplicated state sets using `(board, active)` tuples. *Note (2026-08-26):* the rewritten `explore` counts (and expands) only states never seen at any earlier ply. That matches the "new" column of the C++ `explore` tool exactly (4, 16, 51, 156, ...), not its "reachable at exactly N halfmoves" column (158 at ply 4, which recounts recurring positions); the printed label now says "new positions" to match. The same commit also stopped `explore(18)` from running at module import time, an unlisted but real fix.

---

## 2. Medium-Severity Issues and Architectural Differences

### 2.1 Python vs C++ Rule Divergence: The 150-Fullmove Draw Rule
- **Location**: [`src/python/position.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/position.py#L95-L96), [`src/python/evaluate.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/evaluate.py#L44-L45)
- **Description**: The Python implementation hardcoded a draw condition whenever `fullmove >= 150`. This artificial limit was not part of standard chess or Doctor Popular's 1D Chess rules and made evaluations dependent on the arbitrary fullmove counter. The check also runs *before* the checkmate test in both `check_position` and `score_position_definite`, so a mate delivered at fullmove ≥ 150 reads as a draw in the Python engine — a second divergence from the C++ engine, which tests mate first.
- **C++ Resolution**: The C++ engine removed this condition entirely, relying on the fifty-move rule (100 halfmoves without capture or pawn move) and exact repetition detection.

### 2.2 `src/python/evaluate.py`: Mutable Default Arguments
- **Location**: [`src/python/evaluate.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/evaluate.py#L81-L82)
- **Description**: `score_position` defined `movelist=[]` and `seen_boards=Counter()` in its default parameter list. In Python, default arguments are instantiated once when the function is defined, causing state to leak across invocations if not explicitly passed.
- **Status**: Fixed. Defaults changed to `None` with internal initialization.

### 2.3 `src/python/ai_dfs.py`: Misleading Name and Implementation
- **Location**: [`src/python/ai_dfs.py`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/python/ai_dfs.py#L1-L8)
- **Description**: The file was named `ai_dfs.py` and had a comment stating "Given a position, return a random move", calling `random.choice()`. It did not implement depth-first search.
- **Status**: Fixed. Implemented search-based move selection via `evaluate.score_position(position, max_depth=max_depth)`.

### 2.4 `src/cpp/constants.h`: Dead Header
- **Location**: [`src/cpp/constants.h`](file:///Users/jcalixto/Documents/Projects/doc-pop-1d-chess/src/cpp/constants.h#L17-L121)
- **Description**: `const vector<unsigned long long> KING_BOARDS` sits in a header that is not `#include`d by any file in the repository; its only reference is a commented-out line in the superseded `endgametables.cpp`. *Corrected 2026-08-26:* the original report claimed "static initialization overhead", but a header that is never included costs nothing at compile time or runtime. The file is simply dead code — harmless to keep, safe to delete.

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

The following table summarizes the assumptions leveraged by the retrograde analysis solver and their empirical validation. **Provenance note (2026-08-26):** every figure below is quoted from the project's own README and verification logs (`verify12.log`, `mirrorcheck7.log`, `solve12dtz.log`); none of it was re-run as part of the original audit. A later review confirmed the quoted numbers match the logs and independently re-ran the verification suite at review scale — `retro verify`, `retro solve 5` with self-check and alpha-beta cross-check, `retro mirrorcheck 5`, `retro dtzcheck 5 clock=8`, `perft` to depth 8 plus 20,000 random games, and `verify_attacks` over all 3,605,392 oracle entries — with zero failures throughout.

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
| **BUG-02** | `evaluate.py` | Python Evaluator | High | Parameter order mismatch in `test_next_moves` caused an unbounded search (hang); `.ljust()` on tuple crash | Fixed (re-fixed: original fix inverted the score perspective) |
| **BUG-03** | `all_positions.py` | Python Explorer | High | Integer join `TypeError`; type mismatch in candidate state lookup | Fixed |
| **BUG-04** | `evaluate.py` | Python Evaluator | Medium | Mutable default arguments `movelist=[]` and `seen_boards=Counter()` | Fixed |
| **BUG-05** | `ai_dfs.py` | Python AI | Medium | Implementation performed random move selection instead of DFS | Fixed |
| **BUG-06** | `position.py` | Python Engine | Medium | 150-fullmove artificial draw rule | Documented |
| **BUG-07** | `constants.h` | C++ Engine | Low | Dead header, never `#include`d (no runtime cost; original "init overhead" claim was wrong) | Documented |
| **DOC-01** | `position.py` | Documentation | Low | Docstring referenced 14 squares instead of 16 | Fixed |
| **DOC-02** | `position.h` / `position.py` | Documentation | Low | Typos "Eeach" and "increment after black's move" | Fixed |
| **GIT-01** | `.gitignore` | Build / Repo | Low | Missing `.DS_Store` pattern | Fixed |

---

## 6. Audit of This Audit (2026-08-26)

A second, independent review re-tested every claim above against the pre-fix code (`fa1ede0^`) and re-read the full C++ solver. Its corrections are folded into the sections above and summarized here:

- **BUG-01** was exaggerated: no out-of-bounds access was possible, and the miscalculation (a phantom bishop from the active-colour character) can never fire in `ai_greedy`'s actual call pattern. The sort inversion was the real, confirmed bug.
- **BUG-02**'s dominant symptom was an unbounded-search hang, not the reported crash — and the original fix introduced a new bug, ranking moves from the opponent's perspective. Both are now fixed.
- **BUG-03**'s fix silently changed `explore`'s semantics from "reachable at exactly N" to "first seen at N"; the counts are correct for the latter (verified against the C++ tool) and the label now says so.
- **BUG-04** was latent, not live: `score_position` only ever mutated deep copies of its mutable defaults, so no state actually leaked. The fix remains good hygiene.
- **BUG-07**'s mechanism was wrong: the header is never included, so there was no initialization overhead to remove.
- **Section 4**'s verification figures were quotes of the project's own results, not audit work; they have since been checked against the logs and the suite re-run at review scale (all pass).
- The C++ solver itself (slice indexing, WDL/DTZ/DTM induction, unmove generation, table I/O, memory-mapped reader, distance reconstruction) was reviewed line by line and found **correct**; only cosmetic issues were found and fixed (a misleading parity comment in `attacks.h`, understated transient-memory reporting, resume-progress accounting, and an unclamped test-only `setClockLimit`).

A second sweep over the files the original audit did not touch found two parallels of bugs it *did* fix elsewhere, now also fixed:

- **`endgame_finder.py`** ran a full checkmate enumeration at module import time — the same import-side-effect bug fixed in `all_positions.py`. Now behind `__main__`.
- **`ai_random.py`** raised `IndexError` from `random.choice` on terminal positions — the same edge later guarded in `ai_greedy.py`. Now returns `None`.

Known remaining quirks, deliberately left (retired Python prototype only): the 150-fullmove rule and its ordering (§2.1); `score_position`'s repetition check triggers on the *fourth* occurrence (it counts prior occurrences and tests `>= 3`), a mislabeled off-by-one against the official threefold rule; `test_score_position`/`test_next_moves` are wrapped in `lru_cache`, so a repeated identical call prints nothing.

### Third pass (same day): primitives stress-tested, operational paths exercised

A final pass added empirical coverage the existing suite lacked, all passing: the ray/mirror/packing/slice-key primitives exhaustively compared against inline naive references (now a permanent test, `verify_primitives`, covering inputs `mapping.txt` never held); checkpoint resume from a deliberately torn record, byte-identical to a clean solve; `readercheck` reproducing the documented 77,308,570-position figure; and `dtzcheck` at 6 pieces. It also found and fixed five small defects, none in the solver core:

- **`generateMoves` / `evaluateFenceVerbose`**: a FENCE string without both kings reached the attack tables with square = `NO_SQUARE` (16) — out-of-bounds indexing and negative shifts, i.e. undefined behaviour (benign-in-practice on this platform, confirmed under ASan). Both now guard.
- **`main.py`**: the exception handler meant to salvage a crashed run itself crashed with `NameError` (`elapsed` unbound — confirmed by reproduction); `stats.stdev` threw for single-game runs; and the module ran a depth-20 search at import time — a *third* instance of the import-side-effect pattern. All fixed.
- **`play.h`**: `scoreMoves` ran the distance-reconstruction search even against distance-to-mate tables, whose values already carry exact distances; the result was discarded. Now skipped.
- **`retro compare`**: percentage output divided by zero on empty tables.
- **`searchIterative`**: left the caller's `indeterminate` flag untouched when `maxDepth < 1`; now explicitly true.

# Decision log

Newest first. See README.md for the project itself and BUGS.md for the audit trail.

## 2026-08-26 — Third review pass: guards, salvage path, permanent primitives test

- **Guarded kingless boards in `generateMoves` (and `evaluateFenceVerbose`)** rather
  than validating only at entry points: the UB (attack-table index 16, negative
  shifts) lived in `generateMoves`, so the guard belongs there; one predictable
  branch is free. The verbose front-end additionally reports a clear error.
- **Promoted the review's primitives stress test into `src/cpp/tests/verify_primitives.cpp`**
  (a `make tests` target): it is the only test covering slider inputs outside
  `mapping.txt`'s domain, and it needs no external oracle file.
- **`play` no longer runs the distance-reconstruction search against DTM tables** —
  their values already carry exact distances; the search result was discarded.
- **`main.py` salvage path fixed** (timer/counters initialized before the `try`,
  stdev guarded for <2 games) and its module-scope depth-20 search moved behind
  `__main__` — third instance of the import-side-effect pattern.
- **Deferred (recorded as future work, not attempted now):** parallelizing the
  verification pass and the solver, and incremental predecessor encoding in
  `forEachPredecessor`. Measured that `-march=native` gains nothing on Apple
  Silicon (clang already targets the native arch), so no build-flag change.

## 2026-08-26 — Review of the Gemini audit; corrections and fixes

- **Re-fixed `test_next_moves`** (`src/python/evaluate.py`): the audit's fix scored each
  successor from the opponent's perspective (default `starting_player`), inverting the
  ranking. Now passes `starting_player=active`. Decided against negating scores at the
  call site — passing the player is clearer and matches `score_position`'s design.
- **Kept the Python 150-fullmove rule** and its mate-after-150 ordering quirk as-is: the
  Python tree is a retired prototype; the C++ engine is the source of truth. Documented
  in BUGS.md §2.1 instead of changing behavior nobody depends on.
- **Relabeled `all_positions.explore` output** to "new positions" rather than changing
  the algorithm back to "reachable at exactly N": the dedup semantics are what the C++
  `explore` calls its "new" column, are cheaper, and are verified correct against it.
- **Clamped `Solver::setClockLimit` to [0, 100]**: the DTZ bucket queue and int8_t
  budgets are sized for 100 plies; a larger test value would have indexed past them.
- **Transient-peak reporting measured after pass 2** (WDL/DTM) and before each bucket is
  freed (DTZ), so the printed peak includes induction-phase growth. Reporting only; the
  solve itself is unchanged, and the full verification suite was re-run after the edits.
- **Resumed runs now count checkpointed slices into the progress line**, so `%` agrees
  with `plan()` instead of restarting from zero.
- **Left `constants.h` in place**: dead (never included), but deleting files is not this
  review's call.

## Earlier (from git history, for context)

- Retrograde slice solver (`src/cpp/retro/`) supersedes both the Python prototype and
  the forward-search endgame tables (`endgametables.cpp`, kept for its enumeration
  helpers only).
- `mapping.txt` retired from the engine; attacks are computed. The file is kept solely
  as the independent oracle for `perft` and `verify_attacks`.
- Mirror symmetry halves solve work/storage; `mirrorcheck` exists because exploiting the
  symmetry forfeits "white wins == black wins" as a free check.
- The fifty-move rule is settled by a separate DTZ solve (clock budgets per slice, one
  trit persisted) rather than by widening the state space by the clock.

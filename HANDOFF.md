# Handoff

Last session: 2026-08-26 — full code review + audit of the Gemini "BUGS.md" commit
(fa1ede0), fixes applied on top, then a third pass (primitives stress tests,
checkpoint/reader exercises, five more small fixes, `verify_primitives` test added).
Everything committed and pushed at session end.

## State

- **The game is solved and verified.** `tb12.bin` (WDL) and `tb12-dtz.bin` (fifty-move
  rule) both present, both probe correctly: White wins, `P 5-6` is the only winning
  first move. C++ solver reviewed line-by-line: correct.
- Working tree has uncommitted changes from the review (see below). Everything rebuilt
  and re-verified after the edits: `verify`, `solve 5` (self-check + alpha-beta
  cross-check), `mirrorcheck 5`, `dtzcheck 5 clock=8`, `perft` depth 8 + 20k games,
  `verify_attacks` — all PASS; Python `test_position` all pass.

## Done this session

- Audited every BUGS.md claim against the pre-fix code (`fa1ede0^`); corrections folded
  into BUGS.md (see its §6 addendum).
- Fixed the perspective inversion the audit's fix introduced in
  `evaluate.test_next_moves` (now passes `starting_player=active`).
- `ai_greedy.move` returns `None` at terminal positions instead of IndexError.
- `all_positions.py`: dead imports removed, output relabeled "new positions".
- `attacks.h`: corrected the backwards parity comment on `PARITY_RAY`.
- `solver.h`: clamped `setClockLimit`; transient-peak reporting now includes pass-2
  growth; resumed runs count loaded slices into progress.
- Second sweep over files the audit skipped: `endgame_finder.py` no longer runs its
  enumeration at import time (`__main__` guard); `ai_random.py` returns `None` at
  terminal positions instead of raising. Legacy quirks left in place are listed in
  BUGS.md §6 (Python-only: fourth-occurrence repetition check, lru_cache on printing
  test helpers).
- Added DECISIONS.md and this file.
- Added HISTORY.md — commit-by-commit narrative of the project (2021 Python
  prototype → 2022 C++ port → 2026 retrograde solve), with concepts and techniques
  explained for a mildly technical audience.

## Won't-dos (deliberate)

- Not deleting `constants.h` / `endgametables.cpp` (dead but harmless; owner's call).
- Not changing the Python engine's 150-fullmove rule or its ordering quirk (retired
  prototype; documented in BUGS.md §2.1).
- Not re-running the full-game solves or the 7.85B-position `compare` (hours of
  compute; existing logs + review-scale re-verification deemed sufficient).

## Done in the third pass

- New permanent test `src/cpp/tests/verify_primitives.cpp` (`make tests`): exhaustive
  ray attacks vs naive walk, mirror/packing/FENCE/slice-key laws — all pass.
- Exercised checkpoint resume (kill + torn record → byte-identical to clean solve),
  `readercheck` (77,308,570 positions, 0 mismatches), `dtzcheck 6 clock=12` (pass).
- Fixed: kingless-FENCE UB guard in `generateMoves`/`evaluateFenceVerbose`;
  `main.py` salvage-path NameError, stdev crash, and import-time search;
  redundant distance search on DTM tables in `play.h`; compare's zero-division;
  `searchIterative`'s untouched indeterminate flag at `maxDepth < 1`.

## Next steps

- Parallelize the verification pass (read-only, embarrassingly parallel; 50 min → minutes),
  then the solver itself (`PackedWdl::set` needs write partitioning by word — see
  README "Known limitations").
- Consider incremental predecessor encoding in `forEachPredecessor` (biggest
  single-thread win available; predecessors differ by one quiet move).
- Optional: delete `constants.h` and `src/python/endgame_finder.py` if truly abandoned.
- `-march=native` measured: no gain on Apple Silicon; don't bother.

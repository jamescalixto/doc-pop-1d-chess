# A History of Solving 1D Chess

This is the story of this repository: five years of commits, three programming
languages, several wrong answers, and one final correct one. It is written for a
reader who is comfortable with code but not necessarily with chess engines — every
technique is explained as it appears.

**The game.** [Doctor Popular's 1D Chess](https://gumroad.com/l/1DChess) is played on
a single row of 16 squares. Each side has one king, queen, rook, bishop, knight and
pawn, lined up facing each other:

```
K Q R B N P . . . . p n b r q k
```

Rooks slide along the row; bishops slide two squares at a time (so they hop over
neighbors and stay on "their" color forever); knights jump 2 or 3 squares; kings step
one; pawns push one square forward and capture the square directly ahead. Normal chess
endings apply: checkmate, stalemate, draws by repetition and the fifty-move rule.

**The result.** The game is a forced win for White, and the *only* winning first move
is the single pawn push `P 5-6`. Pushing the pawn two squares only draws; either
knight move loses. Proving that took everything below.

---

## Act I — A rules engine in Python (June 2021)

*Commits `9e5be11` through `7f0a2bd`, June 16–19, 2021.*

The project begins as a small Python program. The first working version
(`c1075ea "working code"`, June 17) already has the shape the project would keep for
years: a rules module (then `board.py`, renamed to `position.py` in
`547d8e2`), a game-runner `main.py`, and two toy opponents — `ai_random`, which picks
any legal move, and `ai_greedy`, which grabs the most valuable capture it can see.

Two early design decisions matter for everything that follows:

**Positions are plain strings.** A position is written in "FENCE" notation —
Forsyth–Edwards Notation, Calixto Extension — such as `KQRBNP....pnbrqk w 0 1`:
the 16 squares left to right, whose turn it is, a *halfmove clock* (moves since the
last capture or pawn push, for the fifty-move rule), and the move number. Using an
immutable string as the position means recursion never needs defensive copying: you
can hand a position to a function and know it can't be mutated behind your back.
It's slow, but it is very hard to get wrong, and "hard to get wrong" is the correct
priority for a reference implementation.

**Moves are just `(from, to)` pairs.** No castling, no en passant, and — because a
pawn can never get past the enemy king on a one-dimensional board — no promotion.
The rules of this game are genuinely small, and the code stays small with them.

The rest of the week adds tests, docstrings, a move-playback utility, and a fix to
the halfmove clock's definition (`649b0ec`) — the kind of rule pedantry that pays off
years later, when two independent implementations are compared move for move.

## Act II — Learning to search (June–July 2021)

*Commits `5f51577` through `7266239`, June 22 – July 24, 2021.*

With the rules in place, the obvious question: who wins? `evaluate.py` appears
(`5f51577`) and grows through a dozen commits into a real, if slow, game-tree search.

The core idea, **minimax**, is simple: to score a position, score every position one
move away (recursively), then assume each player picks the move best for them —
maximize on your turn, minimize on your opponent's. Scored all the way to checkmate
this is perfect play; in practice you stop at some depth and guess at the leaves with
a **heuristic** — here, material count (queen 9, rook 5, bishop and knight 3, pawn 1).

Three refinements land in July:

- **Alpha–beta pruning** (`a1ef94d`, July 19). Plain minimax examines every branch.
  Alpha–beta tracks the best score each side is already guaranteed (α and β) and
  stops examining a branch the moment it proves worse than something already
  available — "if this move lets my opponent win a queen, I don't care which of their
  replies wins it." Same answer as minimax, but with good move ordering it explores
  roughly the *square root* of the positions. This is the single most important
  algorithm in classical game AI.
- **Move-ordering heuristics** (`c17f00d`). Alpha–beta prunes more the sooner it sees
  the best move, so trying likely-good moves (captures) first makes the same search
  much cheaper.
- **Shortest-line tracking** (`37bce28`–`aa4f506`). The search returns not just a
  score but the actual line of moves, preferring the quickest win. (This feature
  would later prove subtly buggy in combination with pruning — see Act V.)

There is also a first attempt at brute-force enumeration: `all_positions.py`
(`a1ef94d`) tries to count every position reachable after N moves. Keep an eye on
this file; it is quietly wrong, and it will take a year to notice.

## Interlude — Why searching forward can never finish

It's worth pausing on why the July 2021 approach, however optimized, cannot solve
this game — this is the constraint the whole rest of the history is shaped by.

A game of 1D chess can be *extremely* long. The fifty-move rule only ends the game
after 100 half-moves with no capture or pawn push — and there are about 10 possible
non-king captures and 20 possible pawn steps, so the clock can be reset roughly 30
times. That allows on the order of **3,000 half-moves** of legal play. A forward
search that reaches depth 20 or even 28 is nowhere near the end of the game; whatever
score it reports at its horizon is a *guess by the material heuristic*, not a proof.
Searching deeper helps the guess, but no reachable depth turns it into an answer.

Solving the game would need a fundamentally different idea. It arrives in Act V.

## Act III — Hitting the wall, porting to C++ (October 2022 – January 2023)

*Commits `f29cbb2` through `ea8271e`.*

After a year's pause, profiling (`f29cbb2`, October 2022) confirms the obvious:
Python is the bottleneck. Two big changes follow.

**Precomputed attack tables.** The notebook `test.ipynb` generates `mapping.txt`: for
every piece type, every square, and every possible arrangement of occupied squares
(the *occupancy*, a 16-bit mask), it stores the set of squares that piece attacks —
also as a 16-bit mask. Sliding-piece movement, the fiddliest part of move generation,
becomes a single table lookup. The file starts at ~515K rows and grows to its full
3.6 million rows in November (`91779a0`). At 42 MB it is a blunt instrument, but a
fast and — crucially — *testable* one.

**The C++ port** (`ec05a5b` onward). The Python position becomes a single 64-bit
integer: 16 squares × 4 bits per square, each *nibble* encoding one piece. The
encoding is chosen so the bits mean something — one bit for color, one for "moves
like a rook," one for "moves like a bishop" (a queen is both) — which lets several
checks compile down to a mask instead of a branch. Occupancy, attack sets, and move
targets are all 16-bit masks, so set operations ("which of these squares are empty?")
are single AND/OR instructions. This is the 1D version of what chess programmers call
**bitboards**.

The port is not smooth, and the commit log is honest about it: *"this code runs, but
it is wrong"* (`42d873d`), *"fixed a bug!!"*, *"another bug fixed!!"*, *"this works
but i am still not sure it is right"* — all in one November weekend. Then the payoff
of having two implementations: `fe463b8` — *"it's working! the python was wrong!!"*
The C++ and Python position-counters disagreed, and cross-checking proved the year-old
Python `all_positions.py` had a broken deduplication check (a bug that would
be "rediscovered" by an AI audit four years later — see Act VI). Independent
reimplementation as a bug-finder becomes this project's signature move.

By December the C++ engine works end to end (*"working code holy heck"*, `8355161`),
gets organized into headers (`1deacee`), and gets its first big constant-factor win:
`bc5e3c2` replaces the `std::map` holding the attack table with a flat `vector`
indexed directly by the packed (piece, square, occupancy) key. A map lookup is a
pointer-chasing tree walk; a vector lookup is one array access. Same table, several
times faster. A C++ alpha–beta searcher follows (*"doesn't work, hope it does soon"*
→ *"holy heck I think it works"*, `442aa78`/`4ebdaf2`), and by January 2023 the
enumerator is churning through 14 plies of the opening (*"it's so much faster!!"*).

The Python-to-C++ move itself is worth roughly **two orders of magnitude** (a typical
figure for this kind of tight integer code, consistent with the depth the C++ search
reaches in seconds versus Python's minutes at much shallower depth); the flat-table
and representation work multiplied that further.

## Act IV — Refinement, and a long slow burn (December 2023 – February 2026)

*Commits `ca20971` through `4c6e8a0`.*

Progress continues in bursts, a few days a year:

- **December 2023**: code cleanup, splitting `evaluate` into a header, and pruning
  dead code (*"Much faster! Wow"* mostly meant deleting a slow duplicate enumerator
  that was being run alongside the real one). `endgametables.cpp` appears
  (`cb84eff`, `fcecd8e`): the idea of *tabulating* endgame positions — running the
  searcher on every position with few pieces and saving the results. The idea is
  right; the mechanism (a fixed-depth heuristic search per position) can't produce
  proofs. It is the seed of Act V, planted two and a half years early.
- **April 2024**: development moves to a Mac (`6d16b6d`).
- **February 2025**: a small but philosophically important feature (`3eb6331`,
  `64dbbf4`): the search now returns an **indeterminate flag** alongside its score —
  an honest bit saying "this number leaned on the horizon heuristic" versus "this is
  a proof." The project is starting to insist on knowing *which* of its answers are
  real. (Getting the flag's polarity right took two commits, naturally.)
- **February 2026**: a Makefile (`4c6e8a0`). The project can now be built by someone
  other than its author.

## Act V — The Claude sprint: solved in a weekend (August 11–12, 2026)

*Commits `cc6257d`, `dbbc14c`, `0eaabac`, `a558ffd`, `0f230e6`.*

In August 2026 the project is handed to Claude for a collaborative sprint, and
fifteen years of chess-programming lore lands on the codebase at once.

### Making the forward search three orders of magnitude faster (`dbbc14c`)

The commit message claims 1000×; the measured numbers back it up. Alpha–beta on the
start position, same machine, same evaluation:

| depth | before  | after   | speedup |
| ----- | ------- | ------- | ------- |
| 12    | 3.57 s  | 0.042 s | 85×     |
| 16    | 386.6 s | 0.211 s | 1,832×  |

What changed:

- **Computed attacks replaced the 42 MB table** (`attacks.h`). On a 16-square board,
  a slider's reach can be computed with a handful of bit tricks (find the nearest
  blocker on each side using count-leading/trailing-zeros instructions; everything
  between is reachable). The entire attack system now fits in L1 cache instead of
  spraying random reads across 42 MB of RAM — and startup no longer parses a 42 MB
  text file. A neat unification: a rook slides over every square and a bishop over
  every *other* square, so one ray routine serves both, parameterized by a parity
  mask. `mapping.txt` is kept solely as an independent oracle for tests.
- **A transposition table** (`tt.h`). In this game most move sequences commute —
  countless different move orders reach the same position. A transposition table is a
  big hash map from position to "score, depth, bound" so each position is searched
  once instead of once per path to it. Because the board already *is* a 64-bit
  integer, no hashing scheme (like chess's Zobrist keys) is needed — a bijective
  bit-mixer of the board word is a perfect key. Entries are only reused at the same
  halfmove clock, because the fifty-move rule genuinely makes value depend on the
  clock.
- **Zero allocation in the search tree.** Move lists live in one fixed buffer per
  ply; the principal variation (the best line found) is written into a triangular
  array instead of copied vectors; `std::function` indirection is gone. Allocation
  in an inner loop is death by a thousand `malloc`s.
- **Mate scores carry their distance** (`SCORE_MATE − ply`), so a faster mate wins on
  the score itself — replacing the old shortest-line bookkeeping, which had interacted
  badly with pruning (pruning can discard a branch that is equal on score but shorter,
  exactly the branch that machinery wanted).
- **Repetition along the current line scores as a draw**, collapsing the enormous
  shuffling subtrees — with care taken around the classic *graph-history interaction*
  trap: a repetition draw is a property of the path, not the position, so values that
  depended on one are never stored in the transposition table.
- **Tests as first-class citizens**: `perft.cpp` walks the whole game tree comparing
  the new move generator against a faithful copy of the old one at every node
  (`perft(12) = 578,736,921` — a fingerprint of the rules themselves), and
  `verify_attacks.cpp` re-derives all 3,605,392 rows of `mapping.txt` from the
  computed attacks. Zero mismatches, ever.

Depth 28 — 7.4 billion nodes — is now 18 minutes. And, per the Interlude, its score
is still a heuristic guess. The speedup didn't solve the game; it proved the game
couldn't be solved this way, and cleared the stage for the idea that could.

### Retrograde analysis: solving the game backwards (`dbbc14c` → `0eaabac`)

The same commit introduces `src/cpp/retro/` — a solver built on **retrograde
analysis**, the technique behind chess endgame tablebases. Instead of searching
forward from the start and guessing at a horizon, work *backwards from the endings*,
where values are known by definition:

1. A position with no legal moves is a **checkmate** (a loss for the side to move)
   or a **stalemate** (a draw). No guessing required.
2. A position is a **win** if *some* move reaches a position already known lost for
   the opponent.
3. A position is a **loss** if *every* move reaches a position already known won for
   the opponent.
4. Repeat until nothing changes. Whatever is still unlabeled is a **draw** — which is
   exactly right, because an unlabeled position is precisely one where neither side
   can force anything, i.e. the game can be held forever.

No heuristic, no horizon. The catch: you must enumerate *every* position, and the
naive count is hopeless. Three observations about this particular game make it
tractable — each an invariant the code *builds into its indexing* rather than checks
afterwards:

- **The piece order never changes.** King, rook and pawn can neither jump nor hop, so
  on one row none of them can ever get past another. Whatever survives of
  `WK < WR < WP < bp < br < bk` stays in that left-to-right order for the whole game.
  Enumerating only correctly-ordered arrangements shrinks the space enormously — this
  one fact is most of why the game fits on one machine.
- **Bishops are stuck on their color.** Moving ±2 squares preserves parity: White's
  bishop lives on odd squares forever, Black's on even ones.
- **Kings can never be adjacent** (they'd be giving check), so they can't pass each
  other either.

The state space is then **partitioned into slices** keyed by (which pieces remain,
White pawn square, Black pawn square). The insight: every move either stays inside
its slice (a quiet non-pawn move) or jumps to a strictly "later" slice — a capture
removes a piece, a pawn push advances a pawn, and neither can ever be undone. So the
slices form a **directed acyclic graph**, and solving them in reverse topological
order means every slice only ever needs answers already computed. Within a slice, a
two-level **combinatorial index** (enumerate the order-constrained pieces as a
"chain," then rank the free pieces among remaining squares in a mixed-radix number)
gives a perfect, gapless numbering of positions — position number ↔ board, no hash
table, no wasted entries.

Total: **17,980,766,876 positions across 8,960 slices**. Interestingly the count
peaks at 9–10 pieces, not 12 — fewer pieces means fewer ordering constraints, so the
middlegame has the most combinatorial freedom.

For the backward step, the solver needs "which positions lead *to* this one?" —
**unmove generation**. Here the 1D geometry pays off again: every piece that can make
a quiet move (king, knight, bishop, rook, queen) attacks symmetrically, so the
positions that reach board X by moving piece P to square S are found by asking where
P could have come *from* — the same attack computation, run in reverse.

### Fitting it in memory (`0eaabac`)

The first solver stored two bytes per position (a full distance-to-mate). Two changes
make the whole game fit comfortably:

- **Two bits per position.** For the final answer only win/draw/loss is needed —
  four states fit in 2 bits, 32 positions per 64-bit word. 8× smaller.
- **Mirror symmetry.** Reflect the board left-to-right and swap the colors, and you
  get 1D chess again, exactly — pawn directions, bishop parities, and the piece-order
  chain all map onto themselves. So a position and its mirror always have the same
  value, and only one slice of each mirrored pair needs solving; the other is a
  reflection away. Only 64 of 8,960 slices are their own mirror, so this cuts work
  and storage almost exactly in half: 9.47 billion positions to actually solve,
  **2.20 GiB** of tables.

A subtle cost is paid knowingly: "White's wins must equal Black's wins" had been a
free sanity check, and exploiting the symmetry makes it true by construction. So the
sprint adds `mirrorcheck` — solve the same material three ways (mirrored, unmirrored,
and with the separately-written distance-to-mate solver, deliberately implemented
twice so agreement means something) and compare every position. At 7 pieces:
965,036,180 comparisons, zero mismatches.

A last speed detail from this commit: when a move leaves a slice, the destination
slice is now computed *arithmetically from the move* (what was captured, how far the
pawn stepped) instead of re-scanning the board — a 128-entry cache covers every case
a slice can produce. The solve rate rose from 3.3 to 4.0 million positions/second on
the 6-piece test just from this.

### Surviving a two-day run (`a558ffd`)

Solving 9.47 billion positions at ~3.7 million/second is about 45 minutes — long
enough to fear a crash at minute 44. This commit adds operational armor:

- `retro plan` predicts a run's cost *before* spending it — slices, positions, table
  size, peak memory, time — from pure combinatorics in a fraction of a second. (Its
  prediction for the 8-piece run matched exactly on counts and within 2% on time.)
- **Checkpointing**: each slice is appended to the output file the moment it is
  solved, so a killed run resumes instead of restarting. A half-written record left
  by the kill is trimmed to the last clean boundary; a file written under different
  settings is refused rather than clobbered.
- Progress reporting with honest rates and ETAs.

### The answer (`0f230e6`, August 12, 2026 — "Solved at long last!!")

The full solve: **4,512 slices, 9,470,299,328 positions, 44 minutes.** White wins;
`P 5-6` is the only winning first move. Then the paranoia budget gets spent:

- Every one of the **4,125,581,107 legal positions** is re-derived from its own move
  list and compared with the stored value — the table checked against the *definition
  of the game* rather than against the code that produced it. Zero failures (50
  minutes; verifying took longer than solving).
- **The fifty-move rule.** The main solve ignores it, as tablebases traditionally do.
  Could a "win" in the table secretly require more than 100 clock plies and thus
  really be a draw? The sprint's answer is a second, separately-written solver:
  **distance-to-zeroing (DTZ)**. The key insight is that the clock resets on exactly
  the moves that *leave a slice* (captures and pawn pushes) — so a slice is always
  entered with the clock at zero, and the rule becomes a 100-ply depth bound on the
  backward pass *inside* each slice (tracked with a bucket queue of "clock budgets"),
  not a hundredfold blowup of the state space. The whole game is solved again this
  way (49 minutes), compared position by position against the first solve:
  **7,851,385,458 comparisons, 100.0000% unchanged.** The win survives the
  fifty-move rule, and `P 5-6` is still the unique winning move.
- **Playing against it** (`play.h`, `tablereader.h`). The 2.20 GiB table is
  memory-mapped rather than loaded — the OS pages in only what a probe touches, so
  the whole game opens in 0.08 s. And since a win/draw/loss table knows *which* moves
  win but not which win *fastest*, the interactive board reconstructs distances on
  demand with a clever trick: a small search that uses the table as a perfect oracle,
  where the winning side only ever considers moves the table already calls winning.
  Every non-winning branch dies at depth one, so the search merely *measures* a line
  whose existence is already proven — exact mate distances out to 24 plies, from a
  table that stores none, verified against the distance-to-mate solver over 3 million
  positions (0 wrong).

Just over five years after `board.py`, the game is solved, twice, and both answers
agree everywhere.

## Act VI — The audits (August 26, 2026)

*Commits `5d05387`, `fa1ede0`, and the review that followed.*

With the mathematics settled, attention turns to hygiene. A Gemini-authored audit
(`fa1ede0`) sweeps the long-retired Python prototype and finds real, old bugs: the
greedy AI had been sorting its moves *ascending* — literally playing the worst move
it could find, for five years — `ai_dfs.py` was secretly random, `all_positions.py`
still carried type errors, and `test_next_moves` crashed. It fixes them and writes
`BUGS.md`.

A second review then audits the audit — in the project's own tradition of trusting
nothing that hasn't been cross-checked. Verdict: most fixes real, but one bug
description was exaggerated (the claimed out-of-bounds indexing was impossible; the
real latent defect — a phantom bishop parsed from the position string's metadata —
could never fire at the call site in question), one finding misdiagnosed (the
"initialization overhead" header is never even `#include`d), and one fix had quietly
introduced a *new* bug: `test_next_moves` now scored every move from the opponent's
perspective, ranking the mover's worst moves first — demonstrated, then re-fixed. A
second sweep caught two parallels the audit missed in files it never opened, the C++
core was re-read line by line (clean), and the whole verification suite was re-run
before and after. The corrections live in `BUGS.md` §6.

A fitting final chapter: the project that once discovered its Python was wrong by
porting it to C++ ends by applying the same method to its own code review.

---

## The techniques, in one table

| Technique | Where | What it bought |
| --- | --- | --- |
| Immutable string positions | Python, 2021 | Correctness-first reference implementation |
| Minimax + alpha–beta pruning | `evaluate.py`, 2021 | Same answer as brute force at ~√ the cost |
| Move-ordering heuristics | 2021 & 2026 | Pruning bites earlier; superlinear with depth |
| Precomputed attack table | `mapping.txt`, 2022 | Sliding moves as one lookup (later retired to test-oracle duty) |
| Nibble-packed 64-bit board ("1D bitboards") | C++ port, 2022 | Position ops become single integer instructions |
| Flat vector over `std::map` | 2022 | One array access instead of a tree walk |
| Differential testing (Python vs C++, old vs new movegen, solver vs solver) | throughout | Found the bugs nothing else would have |
| Computed attacks via bit tricks | `attacks.h`, 2026 | Attack generation in L1 cache; 42 MB table deleted |
| Transposition table | `tt.h`, 2026 | Each position searched once, not once per move order |
| Zero-allocation search, triangular PV | `evaluate.h`, 2026 | Constant-factor speed; 85×–1,832× combined with the above |
| Mate-distance-in-score | 2026 | Shortest mate wins on comparison alone; no fragile bookkeeping |
| Retrograde analysis | `retro/`, 2026 | Proofs instead of horizon guesses — the thing that solved the game |
| Slice DAG by irreversible moves | `slice.h` | State space solvable piecewise in topological order |
| Order/parity/adjacency invariants baked into a combinatorial index | `slice.h` | 17.98B positions, perfectly numbered, no hashing |
| Symmetric unmove generation | `solver.h` | Backward edges from the same attack code, run in reverse |
| 2-bit packed values + mirror symmetry | 2026 | ~16× less storage: the whole game in 2.20 GiB |
| DTZ / clock-budget bucket queue | `solver.h` | Fifty-move rule settled exactly, without ×100 state space |
| Checkpoint/resume, plan-before-run | 2026 | Multi-hour runs made killable and predictable |
| Memory-mapped tables, lazy slice indexes | `tablereader.h` | 2.20 GiB openable in 0.08 s |
| Oracle-guided distance search | `play.h` | Exact mate distances from a table that stores none |

## The speedups, roughly

| Step | Gain | Confidence |
| --- | --- | --- |
| Python → C++ port with lookup tables (2022) | ~100× | estimated (typical for this workload; consistent with observed depths) |
| `std::map` → flat vector for attacks (2022) | several × | observed at the time |
| 2026 forward-search overhaul (computed attacks, TT, ordering, no allocation) | 85× at depth 12, 1,832× at depth 16, growing with depth | measured |
| Arithmetic successor-slice derivation (2026) | 3.3 → 4.0 M positions/s | measured |
| 2-bit values (2026) | 8× storage | exact |
| Mirror symmetry (2026) | ~1.9× work and storage | exact |
| Forward search → retrograde analysis | unbounded — from "can never finish" to 44 minutes | the whole point |

That last row is the moral of the project. Every constant-factor win mattered, but
the game was not solved by making the wrong approach faster — it was solved by
noticing which quantities in this game only ever move one direction (material down,
pawns forward), and building the entire computation around that arrow.

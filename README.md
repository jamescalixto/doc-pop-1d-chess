# doc-pop-1d-chess

Analyzing — and working toward exhaustively solving — [Doctor Popular's 1D Chess](https://gumroad.com/l/1DChess):
a chess variant on a single row of 16 squares, each side with one king, queen, rook,
bishop, knight and pawn.

## Building

```
make          # main, explore, retro
make tests    # perft, verify_attacks (these need the retired mapping.txt)
```

Requires a C++23 compiler. There are no dependencies and nothing is read from disk.

## The two approaches

**Forward search** (`main`) is alpha-beta with a material heuristic. It is useful for
analyzing a given position, but it cannot solve the game. Material only decreases and
pawns only advance, so there are at most 10 non-king captures and 20 pawn steps — about
30 resets of the halfmove clock, and therefore on the order of 3000 halfmoves before the
fifty-move rule ends the game. No forward search reaches that from move one, and any
score it prints before then is a guess at the horizon, not a proof.

**Retrograde analysis** (`retro`) does terminate. Partition every position by

```
(material signature, white pawn square, black pawn square)
```

Every legal move either stays inside its slice (a quiet non-pawn move) or lands in a
strictly later one (a capture drops a piece, a pawn move advances a pawn, and both are
irreversible), so the slices form a DAG. Solve them in reverse topological order,
labelling mates and stalemates and running backward induction until the labels stop
changing. Whatever is unlabelled at the fixpoint is a draw. No heuristic, no horizon.

## Why the state space is small enough

Three invariants of this game, exploited by the index rather than checked afterwards:

- **Piece order.** None of `K R P p r k` can jump or hop, so on a single row none can
  ever pass another. Their left-to-right order is fixed for the whole game. This is by
  far the largest saving.
- **Bishop parity.** A bishop moves along every other square, so it never leaves the
  colour it started on. White's lives on odd squares, black's on even ones; they can
  never contest a square.
- **Kings.** Kings can never be adjacent, so they can never pass each other either.

`retro count` sizes the result exactly:

| pieces | slices | positions |
| --- | --- | --- |
| 2 | 1 | 210 |
| 6 | 1,568 | 132,471,960 |
| 9 | 1,366 | 5,700,961,024 |
| 12 | 15 | 341,040,000 |
| **total** | **8,960** | **17,980,766,876** |

That is 4.19 GiB as a 2-bit win/draw/loss table for the entire game — small enough for
one machine. Note the count peaks at 9–10 pieces, not 12: fewer pieces means fewer
ordering constraints, so the combinatorial freedom is greatest in the middlegame.

## Performance

Alpha-beta on the start position, same machine, same evaluation (both report −1 pawn):

| depth | before | after | speedup |
| --- | --- | --- | --- |
| 12 | 3.57 s | 0.042 s | 85× |
| 16 | 386.6 s | 0.211 s | 1832× |

The gap widens with depth because the transposition table and capture-first move ordering
prune superlinearly. What made this possible:

- Attacks are computed instead of read from a 42 MB table, which removed both the startup
  I/O (`evaluateFenceVerbose` re-read and re-parsed `mapping.txt` on every call) and a
  random access into 42 MB of heap for every piece on every node.
- Nothing in the tree allocates: one move buffer per ply, a triangular PV array, and no
  `std::function` indirection.
- Move generation runs once per node instead of twice.
- Legality is decided by asking which squares attack the king rather than by generating
  every enemy attack set, and the king square, occupancy and board all update in constant
  time per move.

Depth 28 is reachable in about 18 minutes (7.4 billion nodes) — and the score there is
still a horizon estimate, which is the whole argument for the retrograde approach.

Retrograde solving runs at about 3.3M positions/s single-threaded, so the 18 billion
positions of the full game are roughly 1.5 hours of compute. Memory, not time, is the
binding constraint.

## Usage

```
./src/cpp/compiled/retro count                 # size every slice
./src/cpp/compiled/retro verify [games]        # check the invariants and the index
./src/cpp/compiled/retro solve [pieces] [wdl|dtm] [nomirror] [quiet]
./src/cpp/compiled/retro mirrorcheck [pieces]  # solve the same material three ways and compare
./src/cpp/compiled/retro probe "<fence>" [pieces]

./src/cpp/compiled/main [depth] [fence]        # alpha-beta search
./src/cpp/compiled/explore [plies]             # breadth-first position counts
```

`solve` defaults to win/draw/loss at two bits per position with mirror symmetry on, which
is the path that scales to the whole game. `dtm` additionally carries distance to mate at
two bytes per position; it is a verification mode for small material, deliberately written
as a separate implementation of the same definition so that comparing the two means
something.

## Storage

Three things multiply together, measured at 6 pieces:

| | positions | tables |
| --- | --- | --- |
| distance to mate, no mirror | 145.6M | 277.6 MiB |
| win/draw/loss, no mirror | 145.6M | 34.7 MiB |
| win/draw/loss, mirrored | 77.3M | **18.4 MiB** |

Two bits instead of two bytes is 8×; mirror symmetry is very nearly another 2×. Solving
6 pieces went from 44.6 s to 19.2 s, and the per-position rate from 3.3M/s to 4.0M/s
because the successor slice is now derived arithmetically from the move rather than by
rescanning the board.

## Notation

Positions use FENCE (Forsyth-Edwards Notation, Calixto Extension): piece placement with
white on the left and empty squares written as periods, then active colour, halfmove
clock and fullmove number. The start position is `KQRBNP....pnbrqk w 0 1`. See the
comment at the top of `src/cpp/position.h`.

## Correctness

Nothing here is trusted without a check that could fail:

- `verify_attacks` reproduces all 3,605,392 entries of the original `mapping.txt` attack
  table from computed attacks.
- `perft` compares the current move generator against the original implementation at
  every node of the tree to depth 12 (86.9M positions) plus 20,000 random games.
  `perft(12) = 578,736,921`.
- `retro verify` round-trips the slice index over every placement in the small slices,
  and confirms on real games that the invariants the index assumes actually hold.
- `retro solve` re-derives every solved position's value from its own move list, and
  spot-checks against alpha-beta, which shares no code with it beyond move generation.
- White wins and black wins must come out exactly equal, since reversing the board and
  swapping colours maps the state space onto itself.
- `retro mirrorcheck` solves the same material three ways — mirrored, unmirrored, and
  with the separate distance-to-mate solver — and compares every position, plus every
  position against its own reflection.

Note that exploiting the mirror symmetry costs it as a check: once only one slice of each
pair is solved, "white wins equals black wins" is true by construction whether or not the
solver is right. `retro solve` says so rather than printing a reassuring tick, and
`mirrorcheck` is what earns the guarantee back.

## Known limitations

- The retrograde solver ignores the fifty-move rule, as tablebases conventionally do.
  Draws by repetition are exact. The fifty-move rule can only turn some of these wins
  into draws; capturing it needs distance-to-zeroing-move rather than distance-to-mate.
- `PackedWdl::set` is a read-modify-write on a shared 64-bit word, so parallelising the
  solve needs the write side partitioned by word.
- A mate reported by the search is always a real mate, but its *distance* is only minimal
  once the depth searched covers it. The transposition table can carry a value in from a
  shallower ply and prove a mate further away than the nominal depth; a shorter one may
  still lie below the horizon. `searchIterative` keeps deepening until the distance fits
  the depth, so its final answer is minimal — but a single fixed-depth `search` call is
  not guaranteed to be.
- `mapping.txt` is no longer used by the engine — attacks are computed. It is kept only
  because the `perft` and `verify_attacks` tests use it as an independent oracle.

## License

This work is heavily based on [Doctor Popular's 1D Chess](https://gumroad.com/l/1DChess). As such, this work is also licensed under the Creative Commons Attribution-NonCommercial 4.0 International License.

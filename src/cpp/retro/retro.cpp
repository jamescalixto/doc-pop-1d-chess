#include "evaluate.h"
#include "retro/slice.h"
#include "retro/solver.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

/*
Driver for the retrograde side of the project.

  retro count            size every slice and total the state space
  retro verify [games]   check the invariants the index is built on, and the index itself
  retro solve [pieces]   solve every slice up to a piece count, then self-check it
  retro probe "<fence>"  look a position up in the solved tables

`count` is the one to run first. Everything else is only worth building if the numbers
it prints are the numbers we think they are.
*/

using namespace retro;

static double seconds(std::chrono::steady_clock::time_point from)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count();
}

/* ------------------------------------------------------------------------------------
   count
   ------------------------------------------------------------------------------------ */

static int commandCount()
{
    const auto started = std::chrono::steady_clock::now();
    const std::vector<SliceKey> keys = allSliceKeys();

    unsigned long long total = 0, largest = 0, built = 0, empty = 0;
    SliceKey largestKey;
    unsigned long long byPieces[13] = {0};
    unsigned long long slicesByPieces[13] = {0};

    Slice slice;
    for (const SliceKey &key : keys)
    {
        if (!slice.build(key))
        {
            ++empty;
            continue;
        }
        ++built;
        const unsigned long long size = slice.size();
        total += size;
        byPieces[key.pieceCount()] += size;
        slicesByPieces[key.pieceCount()] += 1;
        if (size > largest)
        {
            largest = size;
            largestKey = key;
        }
    }

    std::printf("%8s %10s %20s %20s\n", "pieces", "slices", "placements", "positions (x2)");
    for (unsigned int n = 2; n <= 12; ++n)
    {
        if (byPieces[n])
        {
            std::printf("%8u %10llu %20llu %20llu\n", n, slicesByPieces[n], byPieces[n],
                        2 * byPieces[n]);
        }
    }
    std::printf("%8s %10llu %20llu %20llu\n", "TOTAL", built, total, 2 * total);
    std::printf("\n%llu slice keys were empty (their invariants admit no placement)\n", empty);
    std::printf("largest slice: %s with %llu placements (%llu positions)\n",
                Solver::describeSlice(largestKey).c_str(), largest, 2 * largest);

    std::printf("\nstorage for the whole game:\n");
    const double positions = static_cast<double>(2 * total);
    std::printf("  2-bit win/draw/loss   %8.2f GiB\n", positions * 2 / 8 / 1073741824.0);
    std::printf("  1-byte win/draw/loss  %8.2f GiB\n", positions / 1073741824.0);
    std::printf("  2-byte distance-to-mate %6.2f GiB\n", positions * 2 / 1073741824.0);
    std::printf("\ncounted in %.2fs\n", seconds(started));
    return 0;
}

/* ------------------------------------------------------------------------------------
   verify
   ------------------------------------------------------------------------------------ */

/*
The invariants the index is built on. If any of these can be violated in a real game,
the index is not a bijection over reachable positions and every table built on it is
wrong — so this is checked against actual play rather than assumed.
*/
static bool checkInvariants(unsigned long long board, string &reason)
{
    unsigned int previous = 0;
    bool seenAny = false;

    // K < R < P < p < r < k, over whichever of them are still on the board. None of the
    // six can jump or hop, so on one row none can ever pass another.
    const unsigned int chain[] = {PIECE_KING,
                                  PIECE_ROOK,
                                  PIECE_PAWN,
                                  PIECE_PAWN | COLOUR_BLACK,
                                  PIECE_ROOK | COLOUR_BLACK,
                                  PIECE_KING | COLOUR_BLACK};
    for (const unsigned int piece : chain)
    {
        const unsigned int square = findNibble(board, piece);
        if (square == NO_SQUARE)
        {
            continue;
        }
        if (seenAny && square <= previous)
        {
            reason = "piece order K<R<P<p<r<k violated";
            return false;
        }
        previous = square;
        seenAny = true;
    }

    // Bishops never leave the colour they started on.
    const unsigned int whiteBishop = findNibble(board, PIECE_BISHOP);
    if (whiteBishop != NO_SQUARE && (whiteBishop % 2) != WHITE_BISHOP_PARITY)
    {
        reason = "white bishop left its starting colour";
        return false;
    }
    const unsigned int blackBishop = findNibble(board, PIECE_BISHOP | COLOUR_BLACK);
    if (blackBishop != NO_SQUARE && (blackBishop % 2) != BLACK_BISHOP_PARITY)
    {
        reason = "black bishop left its starting colour";
        return false;
    }

    // Kings can never be adjacent.
    const unsigned int whiteKing = findNibble(board, PIECE_KING);
    const unsigned int blackKing = findNibble(board, PIECE_KING | COLOUR_BLACK);
    if (whiteKing + 2 > blackKing)
    {
        reason = "kings adjacent or out of order";
        return false;
    }

    // Pawn travel limits, which also rule out promotion.
    const unsigned int whitePawn = findNibble(board, PIECE_PAWN);
    if (whitePawn != NO_SQUARE && (whitePawn < WHITE_PAWN_MIN || whitePawn > WHITE_PAWN_MAX))
    {
        reason = "white pawn outside its reachable range";
        return false;
    }
    const unsigned int blackPawn = findNibble(board, PIECE_PAWN | COLOUR_BLACK);
    if (blackPawn != NO_SQUARE && (blackPawn < BLACK_PAWN_MIN || blackPawn > BLACK_PAWN_MAX))
    {
        reason = "black pawn outside its reachable range";
        return false;
    }
    return true;
}

static int commandVerify(int games)
{
    unsigned long long failures = 0;

    // 1. Round-trip the index on a sample of slices, exhaustively for the small ones.
    std::printf("index round-trip\n");
    const std::vector<SliceKey> keys = allSliceKeys();
    Slice slice;
    unsigned long long roundTripped = 0, slicesChecked = 0;
    for (const SliceKey &key : keys)
    {
        if (key.pieceCount() > 5 || !slice.build(key))
        {
            continue;
        }
        ++slicesChecked;
        for (unsigned long long index = 0; index < slice.size(); ++index)
        {
            const unsigned long long board = slice.decode(index);
            if (slice.encode(board) != index)
            {
                if (failures < 5)
                {
                    std::printf("  ROUND TRIP FAILED in %s at index %llu (%s)\n",
                                Solver::describeSlice(key).c_str(), index,
                                varsToFence(board, true, 0, 1).c_str());
                }
                ++failures;
            }
            if (sliceKeyOf(board).packed() != key.packed())
            {
                if (failures < 5)
                {
                    std::printf("  SLICE KEY MISMATCH at index %llu of %s\n", index,
                                Solver::describeSlice(key).c_str());
                }
                ++failures;
            }
            ++roundTripped;
        }
    }
    std::printf("  %llu placements across %llu slices, %llu failures\n", roundTripped,
                slicesChecked, failures);

    // 2. Play real games and check that everything the index assumes actually holds.
    std::printf("invariants over %d random games\n", games);
    std::mt19937_64 rng(20260811);
    unsigned long long visited = 0, invariantFailures = 0, indexFailures = 0;
    unsigned char moves[MAX_MOVES];

    for (int game = 0; game < games; ++game)
    {
        unsigned long long board = START_BOARD;
        bool player = true;
        for (int ply = 0; ply < 600; ++ply)
        {
            ++visited;

            string reason;
            if (!checkInvariants(board, reason))
            {
                if (invariantFailures < 5)
                {
                    std::printf("  INVARIANT BROKEN (%s) at %s\n", reason.c_str(),
                                varsToFence(board, player, 0, 1).c_str());
                }
                ++invariantFailures;
            }

            // The position must land in its own slice and survive a round trip.
            const SliceKey key = sliceKeyOf(board);
            if (slice.build(key))
            {
                const unsigned long long index = slice.encode(board);
                if (index == Slice::INVALID || index >= slice.size() ||
                    slice.decode(index) != board)
                {
                    if (indexFailures < 5)
                    {
                        std::printf("  INDEX FAILED for %s in slice %s\n",
                                    varsToFence(board, player, 0, 1).c_str(),
                                    Solver::describeSlice(key).c_str());
                    }
                    ++indexFailures;
                }
            }
            else
            {
                if (indexFailures < 5)
                {
                    std::printf("  SLICE MISSING for %s\n", varsToFence(board, player, 0, 1).c_str());
                }
                ++indexFailures;
            }

            const unsigned int count = generateMoves(board, player, moves);
            if (count == 0)
            {
                break;
            }
            board = applyMoveToBoard(board, moves[rng() % count]);
            player = !player;
        }
    }
    std::printf("  %llu positions, %llu invariant violations, %llu index failures\n", visited,
                invariantFailures, indexFailures);
    failures += invariantFailures + indexFailures;

    std::printf("\n%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------------------------
   solve
   ------------------------------------------------------------------------------------ */

/*
Check the solved tables against the definition of the game rather than against the code
that produced them: every position's value must equal the best its own legal moves can
reach. A bug in the index, the unmove generator or the fixpoint shows up here.
*/
static unsigned long long selfCheck(const Solver &solver, unsigned int maxPieces)
{
    unsigned long long checked = 0, failures = 0;
    unsigned char moves[MAX_MOVES];

    for (const SliceKey &key : allSliceKeys())
    {
        if (key.pieceCount() > maxPieces)
        {
            continue;
        }
        const SolvedSlice *entry = solver.find(key);
        if (!entry)
        {
            continue; // mirrored away, or above the piece limit
        }

        for (unsigned long long index = 0; index < entry->slice.size(); ++index)
        {
            const unsigned long long board = entry->slice.decode(index);
            for (unsigned int side = 0; side < 2; ++side)
            {
                const bool whiteToMove = (side == 0);
                const Value stored = entry->at(index, whiteToMove);
                if (stored == VALUE_ILLEGAL)
                {
                    continue;
                }
                ++checked;

                const unsigned int count = generateMoves(board, whiteToMove, moves);
                Value expected;
                if (count == 0)
                {
                    expected = isInCheck(board, whiteToMove) ? static_cast<Value>(-1) : VALUE_DRAW;
                }
                else
                {
                    int bestOrder = -2000000;
                    expected = VALUE_DRAW;
                    for (unsigned int i = 0; i < count; ++i)
                    {
                        const Value successor =
                            solver.lookup(applyMoveToBoard(board, moves[i]), !whiteToMove);
                        const Value ours = solver.negate(successor);
                        if (valueOrder(ours) > bestOrder)
                        {
                            bestOrder = valueOrder(ours);
                            expected = ours;
                        }
                    }
                    // Without distances the table stores only the sign, so flatten the
                    // expectation the same way before comparing. A position mated where
                    // it stands is already -1, which is exactly VALUE_LOSS.
                    if (!solver.tracksDistance() && expected != VALUE_DRAW)
                    {
                        expected = (expected > 0) ? VALUE_WIN : VALUE_LOSS;
                    }
                }

                if (stored != expected)
                {
                    if (failures < 10)
                    {
                        std::printf("  SELF-CHECK FAILED %s: stored %s, moves say %s\n",
                                    varsToFence(board, whiteToMove, 0, 1).c_str(),
                                    solver.describe(stored).c_str(),
                                    solver.describe(expected).c_str());
                    }
                    ++failures;
                }
            }
        }
    }
    std::printf("  %llu positions checked against their own move lists, %llu failures\n", checked,
                failures);
    return failures;
}

/*
Independent confirmation: alpha-beta, which shares no code with the retrograde solver
beyond move generation, must agree wherever it can prove a result. It applies the
fifty-move rule and the solver does not, so long wins are reported rather than failed.
*/
static unsigned long long crossCheckAgainstSearch(const Solver &solver, unsigned int maxPieces,
                                                  int samples)
{
    std::mt19937_64 rng(987654321);
    Searcher searcher(20);

    // Match the solver's semantics exactly. The solver resolves repetition by leaving
    // positions unlabelled at the fixpoint rather than by spotting them on the path, so
    // a repetition-aware search would legitimately report different distances.
    searcher.setRepetitionAware(false);

    unsigned long long compared = 0, disagreements = 0, fiftyMoveGap = 0, unproven = 0;

    std::vector<SliceKey> candidates;
    for (const SliceKey &key : allSliceKeys())
    {
        if (key.pieceCount() <= maxPieces && solver.find(key))
        {
            candidates.push_back(key);
        }
    }
    if (candidates.empty())
    {
        return 0;
    }

    for (int sample = 0; sample < samples; ++sample)
    {
        const SliceKey &key = candidates[rng() % candidates.size()];
        const SolvedSlice *entry = solver.find(key);
        const unsigned long long index = rng() % entry->slice.size();
        const bool whiteToMove = (rng() & 1) != 0;

        const Value stored = entry->at(index, whiteToMove);
        if (stored == VALUE_ILLEGAL)
        {
            continue;
        }
        const unsigned long long board = entry->slice.decode(index);

        // Give the search enough depth to prove the result the table claims. Proving a
        // draw means expanding the whole subtree, which is only affordable shallow, so
        // deep results are left to the self-check rather than stalling here. Without
        // distances there is nothing to size the budget from, so a fixed one is used and
        // whatever the search cannot reach is counted as unproven.
        const int depth = !solver.tracksDistance() ? 14
                          : (stored == VALUE_DRAW) ? 14
                                                   : distanceToMate(stored) + 2;
        if (depth > 20)
        {
            ++unproven;
            continue;
        }
        // Each comparison starts from an empty table. Carrying entries between unrelated
        // positions lets one sample's depth-limited mate score be handed to the next,
        // which reports a mate that is real but longer than the shortest one.
        searcher.transpositionTable().clear();

        vector<unsigned int> pv;
        bool indeterminate = true;
        const int score = searcher.searchIterative(board, whiteToMove, 0, depth, pv, indeterminate);

        if (indeterminate)
        {
            ++unproven;
            continue;
        }
        ++compared;

        const bool searchWin = isMateScore(score) && score > 0;
        const bool searchLoss = isMateScore(score) && score < 0;
        const bool tableWin = stored > 0, tableLoss = stored < 0;

        bool agree = (searchWin == tableWin) && (searchLoss == tableLoss);
        if (agree && isMateScore(score) && solver.tracksDistance())
        {
            // Distances must match exactly too, not just the outcome — but only where the
            // table carries them.
            agree = (SCORE_MATE - (score > 0 ? score : -score)) == distanceToMate(stored);
        }

        if (!agree)
        {
            // A table win longer than the fifty-move rule allows is a known, expected
            // divergence rather than a bug.
            if (tableWin && !searchWin && solver.tracksDistance() && distanceToMate(stored) >= 100)
            {
                ++fiftyMoveGap;
                continue;
            }
            if (disagreements < 10)
            {
                std::printf("  DISAGREEMENT %s: table %s, search %s\n",
                            varsToFence(board, whiteToMove, 0, 1).c_str(),
                            solver.describe(stored).c_str(), Searcher::describeScore(score).c_str());
            }
            ++disagreements;
        }
    }

    std::printf("  %llu positions proven by alpha-beta and compared, %llu disagreements\n", compared,
                disagreements);
    std::printf("  (%llu the search could not prove at the depth given, %llu differ only by the "
                "fifty-move rule)\n",
                unproven, fiftyMoveGap);
    return disagreements;
}

static int commandSolve(unsigned int maxPieces, Mode mode, bool mirror, bool verbose)
{
    const auto started = std::chrono::steady_clock::now();
    Solver solver(mode, mirror);

    std::printf("solving every slice with at most %u pieces  [%s%s]\n", maxPieces,
                mode == Mode::Wdl ? "win/draw/loss, 2 bits" : "distance to mate, 2 bytes",
                mirror ? ", mirror symmetry" : ", no mirror");
    std::fflush(stdout);
    solver.solve(maxPieces, verbose);

    const SolveStats &s = solver.stats();
    const double solveSeconds = seconds(started);
    std::printf("\n%llu slices, %llu positions in %.2fs (%.1fM positions/s)\n", s.slices,
                s.positions, solveSeconds,
                static_cast<double>(s.positions) / solveSeconds / 1e6);
    std::printf("  white wins %llu\n  black wins %llu\n  draws      %llu\n  illegal    %llu\n",
                s.whiteWins, s.blackWins, s.draws, s.illegal);
    std::printf("  tables     %.2f MiB resident, peak %.2f MiB transient for one slice\n",
                s.tableBytes / 1048576.0, s.peakTransientBytes / 1048576.0);
    if (s.encodeFailures)
    {
        std::printf("  WARNING: %llu positions could not be indexed\n", s.encodeFailures);
    }

    // Reversing the board and swapping colours maps the state space onto itself and
    // preserves every value, so white wins and black wins must come out equal. This is a
    // real end-to-end check ONLY when the solver did not assume the symmetry; once half
    // the slices are mirrored away it is true by construction and proves nothing. Use
    // `retro mirrorcheck` for the version that still has teeth.
    if (!mirror)
    {
        std::printf("  mirror symmetry: %s\n",
                    (s.whiteWins == s.blackWins) ? "holds" : "BROKEN — results are suspect");
    }
    else
    {
        std::printf("  mirror symmetry: assumed, not tested (run `retro mirrorcheck`)\n");
    }
    std::fflush(stdout);

    auto phase = std::chrono::steady_clock::now();
    std::printf("\nself-check\n");
    std::fflush(stdout);
    const unsigned long long selfFailures = selfCheck(solver, maxPieces);
    std::printf("  %.2fs\n", seconds(phase));
    std::fflush(stdout);

    phase = std::chrono::steady_clock::now();
    std::printf("cross-check against alpha-beta\n");
    std::fflush(stdout);
    const unsigned long long searchFailures = crossCheckAgainstSearch(solver, maxPieces, 300);
    std::printf("  %.2fs\n", seconds(phase));

    const bool ok = (selfFailures == 0 && searchFailures == 0 && s.encodeFailures == 0);
    std::printf("\n%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------------------------
   mirrorcheck
   ------------------------------------------------------------------------------------ */

/*
Exploiting the mirror symmetry costs us the symmetry as a check: once only one slice of
each pair is solved, "white wins equals black wins" holds by construction whether or not
the solver is correct. This earns it back by solving the same material twice — once
mirrored, once not — and comparing every position, and by running the win/draw/loss and
distance-to-mate solvers against each other, which are separate implementations of the
same definition.
*/
static int commandMirrorCheck(unsigned int maxPieces)
{
    std::printf("solving up to %u pieces four ways\n", maxPieces);
    std::fflush(stdout);

    Solver plain(Mode::Wdl, false);
    plain.solve(maxPieces, false);
    std::printf("  wdl, no mirror : %llu positions, %.2f MiB\n", plain.stats().positions,
                plain.stats().tableBytes / 1048576.0);
    std::fflush(stdout);

    Solver mirrored(Mode::Wdl, true);
    mirrored.solve(maxPieces, false);
    std::printf("  wdl, mirrored  : %llu positions, %.2f MiB\n", mirrored.stats().positions,
                mirrored.stats().tableBytes / 1048576.0);
    std::fflush(stdout);

    Solver dtm(Mode::Dtm, false);
    dtm.solve(maxPieces, false);
    std::printf("  dtm, no mirror : %llu positions, %.2f MiB\n", dtm.stats().positions,
                dtm.stats().tableBytes / 1048576.0);
    std::fflush(stdout);

    std::printf("  unmirrored table is %.2fx the mirrored one\n",
                static_cast<double>(plain.stats().tableBytes) / mirrored.stats().tableBytes);

    // The unmirrored run never assumed the symmetry, so this still means something.
    std::printf("  mirror symmetry in the unmirrored run: %s\n",
                (plain.stats().whiteWins == plain.stats().blackWins) ? "holds" : "BROKEN");

    unsigned long long compared = 0, mirrorFailures = 0, dtmFailures = 0, reflectFailures = 0;
    Slice slice;
    for (const SliceKey &key : allSliceKeys())
    {
        if (key.pieceCount() > maxPieces || !slice.build(key))
        {
            continue;
        }
        for (unsigned long long index = 0; index < slice.size(); ++index)
        {
            const unsigned long long board = slice.decode(index);
            for (unsigned int side = 0; side < 2; ++side)
            {
                const bool whiteToMove = (side == 0);
                const Value a = plain.lookup(board, whiteToMove);
                const Value b = mirrored.lookup(board, whiteToMove);
                const Value c = dtm.lookup(board, whiteToMove);
                ++compared;

                if (a != b)
                {
                    if (mirrorFailures < 5)
                    {
                        std::printf("  MIRROR MISMATCH %s: plain %s, mirrored %s\n",
                                    varsToFence(board, whiteToMove, 0, 1).c_str(),
                                    describeValue(a, false).c_str(), describeValue(b, false).c_str());
                    }
                    ++mirrorFailures;
                }

                // Compare the two solvers on outcome; only the dtm one carries distances.
                const int wdlSign = (a == VALUE_ILLEGAL) ? 2 : (a > 0) - (a < 0);
                const int dtmSign = (c == VALUE_ILLEGAL) ? 2 : (c > 0) - (c < 0);
                if (wdlSign != dtmSign)
                {
                    if (dtmFailures < 5)
                    {
                        std::printf("  WDL/DTM MISMATCH %s: wdl %s, dtm %s\n",
                                    varsToFence(board, whiteToMove, 0, 1).c_str(),
                                    describeValue(a, false).c_str(), describeValue(c, true).c_str());
                    }
                    ++dtmFailures;
                }

                // A position and its reflection must carry the same value outright.
                const Value reflected = plain.lookup(mirrorBoard(board), !whiteToMove);
                if (a != reflected)
                {
                    if (reflectFailures < 5)
                    {
                        std::printf("  REFLECTION MISMATCH %s: %s vs %s\n",
                                    varsToFence(board, whiteToMove, 0, 1).c_str(),
                                    describeValue(a, false).c_str(),
                                    describeValue(reflected, false).c_str());
                    }
                    ++reflectFailures;
                }
            }
        }
    }

    std::printf("\n  %llu positions compared\n", compared);
    std::printf("    mirrored vs unmirrored solve : %llu mismatches\n", mirrorFailures);
    std::printf("    wdl solver vs dtm solver     : %llu mismatches\n", dtmFailures);
    std::printf("    position vs its reflection   : %llu mismatches\n", reflectFailures);

    const bool ok = (mirrorFailures == 0 && dtmFailures == 0 && reflectFailures == 0 &&
                     plain.stats().whiteWins == plain.stats().blackWins);
    std::printf("\n%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------------------------
   probe
   ------------------------------------------------------------------------------------ */

static int commandProbe(const string &fence, unsigned int maxPieces, Mode mode)
{
    Solver solver(mode, true);
    solver.solve(maxPieces, false);

    unsigned long long board = 0;
    bool active = true;
    unsigned int halfmove = 0, fullmove = 1;
    tie(board, active, halfmove, fullmove) = fenceToVars(fence, board, active, halfmove, fullmove);

    bool found = false;
    const Value value = solver.lookup(board, active, &found);
    if (!found)
    {
        std::printf("%s is not in the solved tables (needs a slice with more than %u pieces)\n",
                    fence.c_str(), maxPieces);
        return 1;
    }
    std::printf("%s\n  %s\n", fence.c_str(), solver.describe(value).c_str());

    // Show how each option holds up.
    unsigned char moves[MAX_MOVES];
    const unsigned int count = generateMoves(board, active, moves);
    for (unsigned int i = 0; i < count; ++i)
    {
        const Value successor = solver.lookup(applyMoveToBoard(board, moves[i]), !active);
        std::printf("  (%u,%u) -> %s\n", moves[i] >> 4, moves[i] & 15,
                    solver.describe(solver.negate(successor)).c_str());
    }
    return 0;
}

int main(int argc, char **argv)
{
    const string command = (argc > 1) ? argv[1] : "count";

    // Trailing flags, accepted by any command that cares about them.
    Mode mode = Mode::Wdl;
    bool mirror = true, verbose = true;
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "dtm") == 0) mode = Mode::Dtm;
        else if (std::strcmp(argv[i], "wdl") == 0) mode = Mode::Wdl;
        else if (std::strcmp(argv[i], "nomirror") == 0) mirror = false;
        else if (std::strcmp(argv[i], "quiet") == 0) verbose = false;
    }
    const unsigned int pieces =
        (argc > 2 && std::atoi(argv[2]) > 0) ? static_cast<unsigned int>(std::atoi(argv[2])) : 5;

    if (command == "count")
    {
        return commandCount();
    }
    if (command == "verify")
    {
        return commandVerify((argc > 2) ? std::atoi(argv[2]) : 5000);
    }
    if (command == "solve")
    {
        return commandSolve(pieces, mode, mirror, verbose);
    }
    if (command == "mirrorcheck")
    {
        return commandMirrorCheck(pieces);
    }
    if (command == "probe")
    {
        if (argc < 3)
        {
            std::fprintf(stderr, "probe needs a FENCE string\n");
            return 2;
        }
        unsigned int probePieces = 5;
        for (int i = 3; i < argc; ++i)
        {
            if (std::atoi(argv[i]) > 0) probePieces = static_cast<unsigned int>(std::atoi(argv[i]));
        }
        return commandProbe(argv[2], probePieces, mode);
    }

    std::fprintf(stderr,
                 "usage: retro count\n"
                 "       retro verify [games]\n"
                 "       retro solve [pieces] [wdl|dtm] [nomirror] [quiet]\n"
                 "       retro mirrorcheck [pieces]\n"
                 "       retro probe \"<fence>\" [pieces] [wdl|dtm]\n");
    return 2;
}

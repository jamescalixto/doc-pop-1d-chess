#include "evaluate.h"
#include "retro/slice.h"
#include "retro/solver.h"
#include "retro/play.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <random>
#include <unordered_map>

/*
Driver for the retrograde side of the project.

  retro count            size every slice and total the state space
  retro verify [games]   check the invariants the index is built on, and the index itself
  retro solve [pieces]   solve every slice up to a piece count, then self-check it
  retro probe "<fence>"  look a position up in the solved tables
  retro play             an interactive board over the solved tables

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

static string formatBytes(uint64_t bytes)
{
    char buffer[32];
    if (bytes < (1ull << 20))
    {
        std::snprintf(buffer, sizeof(buffer), "%.1f KiB", bytes / 1024.0);
    }
    else if (bytes < (1ull << 30))
    {
        std::snprintf(buffer, sizeof(buffer), "%.1f MiB", bytes / 1048576.0);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%.2f GiB", bytes / 1073741824.0);
    }
    return buffer;
}

/*
What a run will cost, printed before it starts. Slice construction is pure combinatorics,
so this is a fraction of a second even for the whole game — much better than discovering
at minute seventy that the largest slice does not fit.
*/
static void printPlan(const SolvePlan &plan, Mode mode, bool mirror, double ratePerSecond)
{
    std::printf("plan: %llu slices, %.3fB positions  [%s%s]\n", plan.slices, plan.positions / 1e9,
                mode == Mode::Wdl   ? "win/draw/loss, 2 bits"
                : mode == Mode::Dtz ? "win/draw/loss WITH the fifty-move rule, 2 bits"
                                    : "distance to mate, 2 bytes",
                mirror ? ", mirror symmetry" : ", no mirror");
    std::printf("  tables                 %s resident for the whole run\n",
                formatBytes(plan.tableBytes).c_str());
    std::printf("  largest slice          %s, %.1fM positions\n",
                Solver::describeSlice(plan.largestSlice).c_str(),
                plan.largestSlicePositions / 1e6);
    std::printf("  its working memory     %s (worst case; measured peaks run near half this)\n",
                formatBytes(plan.largestTransientBytes).c_str());
    std::printf("  projected peak         %s\n", formatBytes(plan.projectedPeakBytes()).c_str());
    std::printf("  projected time         %s at %.1fM positions/s\n",
                Solver::formatDuration(plan.positions / ratePerSecond).c_str(),
                ratePerSecond / 1e6);
    std::fflush(stdout);
}

static int commandPlan(unsigned int maxPieces, Mode mode, bool mirror)
{
    Solver solver(mode, mirror);
    printPlan(solver.plan(maxPieces), mode, mirror, 3.7e6);
    return 0;
}

static int commandSolve(unsigned int maxPieces,
                        Mode mode,
                        bool mirror,
                        bool verbose,
                        bool runChecks,
                        const string &tablePath)
{
    Solver solver(mode, mirror);

    const SolvePlan expected = solver.plan(maxPieces);
    printPlan(expected, mode, mirror, 3.7e6);
    std::printf("\n");

    if (!tablePath.empty() && !solver.openCheckpoint(tablePath))
    {
        std::fprintf(stderr, "could not open %s for checkpointing\n", tablePath.c_str());
        return 2;
    }

    const auto started = std::chrono::steady_clock::now();
    solver.solve(maxPieces, verbose);
    solver.closeCheckpoint();

    const SolveStats &s = solver.stats();
    const double solveSeconds = seconds(started);
    std::printf("\n%llu slices, %llu positions in %s (%.1fM positions/s)\n", s.slices,
                s.positions, Solver::formatDuration(solveSeconds).c_str(),
                static_cast<double>(s.positions) / solveSeconds / 1e6);
    std::printf("  white wins %llu\n  black wins %llu\n  draws      %llu\n  illegal    %llu\n",
                s.whiteWins, s.blackWins, s.draws, s.illegal);
    std::printf("  tables     %s resident, peak %s transient for one slice\n",
                formatBytes(solver.residentBytes()).c_str(),
                formatBytes(s.peakTransientBytes).c_str());
    if (!tablePath.empty())
    {
        std::printf("  written to %s\n", tablePath.c_str());
    }
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

    if (!runChecks)
    {
        std::printf("\nchecks skipped\n");
        return s.encodeFailures == 0 ? 0 : 1;
    }

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
   dtzcheck
   ------------------------------------------------------------------------------------ */

/*
Independent check on the fifty-move solver: play the game out with the clock carried
explicitly in the state, which is the plain definition the slice machinery is supposed to
be a clever encoding of. Memoised on (board, side to move, clock).

If the reasoning the fast path rests on — a slice can only be entered by a zeroing move,
so the clock is zero on entry and the rule becomes a depth bound inside the slice — is
wrong anywhere, these two disagree.
*/
struct ClockKey
{
    unsigned long long board;
    unsigned int packed; // side to move and clock

    bool operator==(const ClockKey &o) const { return board == o.board && packed == o.packed; }
};
struct ClockKeyHash
{
    std::size_t operator()(const ClockKey &k) const
    {
        return static_cast<std::size_t>(tt::mix64(k.board ^ (0x9E3779B97F4A7C15ull * k.packed)));
    }
};

static int bruteForceFifty(unsigned long long board,
                           bool whiteToMove,
                           unsigned int clock,
                           unsigned int clockLimit,
                           std::unordered_map<ClockKey, int, ClockKeyHash> &memo)
{
    unsigned char moves[MAX_MOVES];
    const unsigned int count = generateMoves(board, whiteToMove, moves);
    if (count == 0)
    {
        return isInCheck(board, whiteToMove) ? -1 : 0; // mated, or stalemate
    }
    if (clock >= clockLimit)
    {
        return 0; // fifty-move rule
    }

    const ClockKey key{board, (clock << 1) | (whiteToMove ? 1u : 0u)};
    const auto it = memo.find(key);
    if (it != memo.end())
    {
        return it->second;
    }
    // Well founded without any depth limit: a quiet move raises the clock and anything
    // else drops material or advances a pawn, so no state can recur.
    memo.emplace(key, 0);

    int best = -1;
    for (unsigned int i = 0; i < count; ++i)
    {
        const unsigned int start = moves[i] >> 4, end = moves[i] & 15u;
        const bool zeroes =
            isPawn(getNthNibble(board, start)) || !isEmpty(getNthNibble(board, end));
        const int reply = bruteForceFifty(applyMoveToBoard(board, moves[i]), !whiteToMove,
                                          zeroes ? 0u : clock + 1, clockLimit, memo);
        best = std::max(best, -reply);
        if (best == 1)
        {
            break;
        }
    }
    memo[key] = best;
    return best;
}

static int commandDtzCheck(unsigned int maxPieces, int samples, int clockLimit)
{
    std::printf("solving up to %u pieces with a clock limit of %d plies\n", maxPieces,
                clockLimit);
    std::fflush(stdout);
    Solver fifty(Mode::Dtz, true);
    fifty.setClockLimit(clockLimit);
    fifty.solve(maxPieces, false);

    std::printf("solving the same material without it\n");
    std::fflush(stdout);
    Solver plain(Mode::Wdl, true);
    plain.solve(maxPieces, false);

    // The rule can only ever turn a win into a draw: never create a win, never flip a win
    // into a loss, never disturb something already drawn. Checked over every position.
    unsigned long long compared = 0, violations = 0, weakened = 0;
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
                if (a == VALUE_ILLEGAL)
                {
                    continue;
                }
                const Value b = fifty.lookup(board, whiteToMove);
                ++compared;
                if (b != a && b != VALUE_DRAW)
                {
                    if (violations < 5)
                    {
                        std::printf("  IMPOSSIBLE %s: without rule %s, with rule %s\n",
                                    varsToFence(board, whiteToMove, 0, 1).c_str(),
                                    describeValue(a, false).c_str(),
                                    describeValue(b, false).c_str());
                    }
                    ++violations;
                }
                else if (b != a)
                {
                    ++weakened;
                }
            }
        }
    }
    std::printf("  %llu positions: %llu weakened to draws, %llu impossible transitions\n",
                compared, weakened, violations);
    std::fflush(stdout);

    std::mt19937_64 rng(4242);
    std::vector<SliceKey> candidates;
    for (const SliceKey &key : allSliceKeys())
    {
        if (key.pieceCount() <= maxPieces && slice.build(key) && slice.size())
        {
            candidates.push_back(key);
        }
    }

    unsigned long long checked = 0, mismatches = 0;
    for (int sample = 0; sample < samples && !candidates.empty(); ++sample)
    {
        const SliceKey &key = candidates[rng() % candidates.size()];
        if (!slice.build(key))
        {
            continue;
        }
        const unsigned long long index = rng() % slice.size();
        const bool whiteToMove = (rng() & 1) != 0;
        const unsigned long long board = slice.decode(index);
        if (isInCheck(board, !whiteToMove))
        {
            continue;
        }

        std::unordered_map<ClockKey, int, ClockKeyHash> memo;
        const int brute =
            bruteForceFifty(board, whiteToMove, 0, static_cast<unsigned int>(clockLimit), memo);
        const Value stored = fifty.lookup(board, whiteToMove);
        const int table = (stored > 0) ? 1 : (stored < 0) ? -1 : 0;
        ++checked;

        if (brute != table)
        {
            if (mismatches < 10)
            {
                std::printf("  MISMATCH %s: brute force %s, table %s\n",
                            varsToFence(board, whiteToMove, 0, 1).c_str(),
                            brute > 0 ? "win" : brute < 0 ? "loss" : "draw",
                            describeValue(stored, false).c_str());
            }
            ++mismatches;
        }
    }
    std::printf("  %llu positions replayed with the clock in the state, %llu mismatches\n",
                checked, mismatches);

    const bool ok = (violations == 0 && mismatches == 0);
    std::printf("\n%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------------------------
   compare
   ------------------------------------------------------------------------------------ */

static int commandCompare(const string &pathA, const string &pathB)
{
    Solver a(Mode::Wdl, true), b(Mode::Dtz, true);
    if (!a.load(pathA) || !b.load(pathB))
    {
        std::fprintf(stderr, "could not load both tables\n");
        return 2;
    }

    unsigned long long compared = 0, same = 0, weakened = 0, impossible = 0;
    Slice slice;
    for (const SliceKey &key : allSliceKeys())
    {
        if (!slice.build(key))
        {
            continue;
        }
        for (unsigned long long index = 0; index < slice.size(); ++index)
        {
            const unsigned long long board = slice.decode(index);
            for (unsigned int side = 0; side < 2; ++side)
            {
                const bool whiteToMove = (side == 0);
                const Value x = a.lookup(board, whiteToMove);
                if (x == VALUE_ILLEGAL)
                {
                    continue;
                }
                const Value y = b.lookup(board, whiteToMove);
                ++compared;
                if (x == y) { ++same; }
                else if (y == VALUE_DRAW) { ++weakened; }
                else { ++impossible; }
            }
        }
    }
    const double denominator = compared ? static_cast<double>(compared) : 1.0;
    std::printf("%llu legal positions compared\n", compared);
    std::printf("  unchanged by the fifty-move rule   %llu (%.4f%%)\n", same,
                100.0 * same / denominator);
    std::printf("  wins turned into draws by it       %llu (%.4f%%)\n", weakened,
                100.0 * weakened / denominator);
    std::printf("  impossible transitions             %llu%s\n", impossible,
                impossible ? "   <-- BUG" : "");
    return impossible == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------------------------
   line
   ------------------------------------------------------------------------------------ */

/*
Walk a table-optimal line from a position and print it.

A win/draw/loss table says which moves keep a win but gives no measure of progress, so
following "any winning move" can shuffle forever without ever mating. This prefers moves
that zero the halfmove clock — captures and pawn moves — among the moves that preserve
the result, since those are the ones that make irreversible progress, and it stops if the
line repeats rather than looping.

That makes the line it prints a real winning line but not necessarily the shortest one.
Its main use is the fifty-move question: the solver ignores that rule, so a win is only a
win under FIDE if it can be forced without the halfmove clock reaching 100. A line that
never comes close is evidence the rule does not bite here; proving it needs DTZ.
*/
static int commandLine(const string &fence, unsigned int maxPieces, Mode mode,
                       const string &tablePath, int maxPlies)
{
    Solver solver(mode, true);
    if (tablePath.empty() || !solver.load(tablePath))
    {
        solver.solve(maxPieces, false);
    }

    unsigned long long board = 0;
    bool active = true;
    unsigned int halfmove = 0, fullmove = 1;
    tie(board, active, halfmove, fullmove) = fenceToVars(fence, board, active, halfmove, fullmove);

    bool found = false;
    const Value start = solver.lookup(board, active, &found);
    if (!found)
    {
        std::fprintf(stderr, "%s is not in the tables\n", fence.c_str());
        return 1;
    }
    std::printf("%s  ->  %s\n\n", fence.c_str(), solver.describe(start).c_str());
    std::printf("%4s  %-18s %5s  %s\n", "ply", "position", "clock", "move");

    std::vector<unsigned long long> seen;
    unsigned int worstClock = 0;
    unsigned char moves[MAX_MOVES];

    for (int ply = 0; ply < maxPlies; ++ply)
    {
        worstClock = std::max(worstClock, halfmove);
        seen.push_back(board);

        const unsigned int count = generateMoves(board, active, moves);
        if (count == 0)
        {
            std::printf("%4d  %-18s %5u  %s\n", ply, varsToFence(board, active, 0, 1).substr(0, 16).c_str(),
                        halfmove, isInCheck(board, active) ? "checkmate" : "stalemate");
            break;
        }

        // Best result available, then prefer the moves that make irreversible progress.
        int bestOrder = -2000000;
        unsigned char best = moves[0];
        bool bestZeroes = false;
        for (unsigned int i = 0; i < count; ++i)
        {
            const unsigned int start2 = moves[i] >> 4, end = moves[i] & 15u;
            const bool zeroes = isPawn(getNthNibble(board, start2)) ||
                                !isEmpty(getNthNibble(board, end));
            const Value ours =
                solver.negate(solver.lookup(applyMoveToBoard(board, moves[i]), !active));
            const int order = valueOrder(ours);
            if (order > bestOrder || (order == bestOrder && zeroes && !bestZeroes))
            {
                bestOrder = order;
                best = moves[i];
                bestZeroes = zeroes;
            }
        }

        std::printf("%4d  %-18s %5u  (%u,%u)\n", ply,
                    varsToFence(board, active, 0, 1).substr(0, 16).c_str(), halfmove,
                    best >> 4, best & 15);

        tie(board, active, halfmove, fullmove) =
            applyMove(board, active, halfmove, fullmove, best);

        if (std::find(seen.begin(), seen.end(), board) != seen.end())
        {
            std::printf("      position repeats — this table has no progress measure, so the\n"
                        "      line cannot be continued without distance-to-mate or DTZ\n");
            break;
        }
    }

    std::printf("\nhighest halfmove clock reached: %u\n", worstClock);
    std::printf(
        "\nRead this line carefully. Win/draw/loss records no distance, so every losing move\n"
        "looks alike to the side that is lost: the defender here picks arbitrarily among\n"
        "moves that all lose rather than resisting as long as possible. Every position in\n"
        "the line is labelled exactly and the win is real, but the LENGTH of the line is not\n"
        "the distance to mate, and the clock above is not the clock optimal defence would\n"
        "produce — a defender trying to survive would avoid captures and pawn moves to run\n"
        "the clock toward the fifty-move draw. Settling that needs DTZ.\n");
    return 0;
}

/* ------------------------------------------------------------------------------------
   readercheck
   ------------------------------------------------------------------------------------ */

/*
The memory-mapped reader and the solver's own loader must agree on every position in a
table, or the interactive board is showing something the solve never said.

They share no code below the slice index: the loader copies payloads onto the heap and
reads them through PackedWdl, while the reader leaves them in the mapping and decodes the
same two bits by hand. Comparing them over a whole file is what makes the second path
trustworthy.
*/
static int commandReaderCheck(const string &tablePath, Mode mode)
{
    if (tablePath.empty())
    {
        std::fprintf(stderr, "readercheck needs a table: retro readercheck in=FILE\n");
        return 2;
    }

    TableReader reader;
    if (!reader.open(tablePath))
    {
        return 1;
    }

    Solver solver(mode, reader.mirrored());
    if (!solver.load(tablePath))
    {
        std::fprintf(stderr, "the solver could not load %s\n", tablePath.c_str());
        return 1;
    }

    const auto started = std::chrono::steady_clock::now();
    unsigned long long compared = 0, mismatches = 0, missing = 0;

    for (const SliceKey &key : allSliceKeys())
    {
        const SolvedSlice *entry = solver.find(key);
        if (!entry)
        {
            continue;
        }
        for (unsigned long long index = 0; index < entry->slice.size(); ++index)
        {
            const unsigned long long board = entry->slice.decode(index);
            for (int side = 0; side < 2; ++side)
            {
                const bool whiteToMove = (side == 0);
                bool found = false;
                const Value fromReader = reader.lookup(board, whiteToMove, &found);
                const Value fromSolver = solver.lookup(board, whiteToMove);
                ++compared;
                if (!found)
                {
                    ++missing;
                }
                else if (fromReader != fromSolver)
                {
                    if (mismatches < 5)
                    {
                        std::printf("  %s  reader %s, solver %s\n",
                                    varsToFence(board, whiteToMove, 0, 1).c_str(),
                                    describeValue(fromReader, mode == Mode::Dtm).c_str(),
                                    describeValue(fromSolver, mode == Mode::Dtm).c_str());
                    }
                    ++mismatches;
                }
            }
        }
    }

    std::printf("%llu positions compared, %llu mismatches, %llu not found by the reader\n",
                compared, mismatches, missing);
    std::printf("  %.1fs\n", seconds(started));
    std::printf("%s\n", (mismatches == 0 && missing == 0) ? "PASS" : "FAIL");
    return (mismatches == 0 && missing == 0) ? 0 : 1;
}

/* ------------------------------------------------------------------------------------
   distcheck
   ------------------------------------------------------------------------------------ */

/*
The interactive board reconstructs distance to mate from a table that does not store it,
by searching with the win/draw/loss table as a perfect oracle. That claim is only worth
making if the distances it produces are the real ones.

This checks them against the distance-to-mate solver, which computes the same quantity by
an entirely different route — backward induction with a priority queue over the whole
slice, rather than a forward search from one position. Every position whose true distance
is inside the search horizon must come back exactly equal; a position beyond the horizon
must report that it does not know, and must never report a number.
*/
static int commandDistCheck(const string &wdlPath, const string &dtmPath, unsigned long long limit)
{
    TableReader wdl;
    if (!wdl.open(wdlPath))
    {
        return 1;
    }
    if (wdl.mode() == Mode::Dtm)
    {
        std::fprintf(stderr, "%s is a distance-to-mate table; give the win/draw/loss one first\n",
                     wdlPath.c_str());
        return 2;
    }

    Solver truth(Mode::Dtm, true);
    if (!truth.load(dtmPath))
    {
        std::fprintf(stderr, "could not load %s as a distance-to-mate table\n", dtmPath.c_str());
        return 1;
    }

    const auto started = std::chrono::steady_clock::now();
    unsigned long long checked = 0, agreed = 0, wrong = 0, beyondHorizon = 0, invented = 0;
    int deepestAgreed = 0;

    for (const SliceKey &key : allSliceKeys())
    {
        const SolvedSlice *entry = truth.find(key);
        if (!entry || checked >= limit)
        {
            continue;
        }
        for (unsigned long long index = 0; index < entry->slice.size() && checked < limit; ++index)
        {
            const unsigned long long board = entry->slice.decode(index);
            for (int side = 0; side < 2 && checked < limit; ++side)
            {
                const bool whiteToMove = (side == 0);
                const Value exact = truth.lookup(board, whiteToMove);
                if (exact == VALUE_DRAW || exact == VALUE_ILLEGAL || exact == VALUE_UNRESOLVED)
                {
                    continue;
                }

                ++checked;
                const int trueDistance = distanceToMate(exact);

                DistanceSearch search(wdl);
                const int reported = search.distance(board, whiteToMove, MATE_SEARCH_PLIES);

                if (reported < 0)
                {
                    // Only acceptable if the true distance really was out of reach.
                    if (trueDistance <= 6)
                    {
                        if (wrong < 5)
                        {
                            std::printf("  %s  true %d, search gave up\n",
                                        varsToFence(board, whiteToMove, 0, 1).c_str(), trueDistance);
                        }
                        ++wrong;
                    }
                    else
                    {
                        ++beyondHorizon;
                    }
                }
                else if (reported != trueDistance)
                {
                    if (wrong < 5)
                    {
                        std::printf("  %s  true %d, search said %d\n",
                                    varsToFence(board, whiteToMove, 0, 1).c_str(),
                                    trueDistance, reported);
                    }
                    ++wrong;
                    ++invented;
                }
                else
                {
                    ++agreed;
                    deepestAgreed = std::max(deepestAgreed, trueDistance);
                }
            }
        }
    }

    std::printf("%llu decided positions checked against the distance-to-mate solver\n", checked);
    std::printf("  %llu exact matches (deepest %d plies)\n", agreed, deepestAgreed);
    std::printf("  %llu beyond the %d-ply search horizon, reported as unknown\n",
                beyondHorizon, MATE_SEARCH_PLIES);
    std::printf("  %llu wrong (%llu of them a number that disagrees)\n", wrong, invented);
    std::printf("  %.1fs\n", seconds(started));
    std::printf("%s\n", wrong == 0 ? "PASS" : "FAIL");
    return wrong == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------------------------
   probe
   ------------------------------------------------------------------------------------ */

static int commandProbe(const string &fence, unsigned int maxPieces, Mode mode,
                        const string &tablePath)
{
    Solver solver(mode, true);
    if (tablePath.empty() || !solver.load(tablePath))
    {
        if (!tablePath.empty())
        {
            std::fprintf(stderr, "could not read %s; solving instead\n", tablePath.c_str());
        }
        solver.solve(maxPieces, false);
    }

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
    bool mirror = true, verbose = false, runChecks = true;
    string tablePath;
    unsigned int pieces = 5;
    bool piecesGiven = false;

    for (int i = 2; i < argc; ++i)
    {
        const string argument = argv[i];
        if ((command == "probe" || command == "line") && i == 2) continue; // the position
        if (argument == "dtm") mode = Mode::Dtm;
        else if (argument == "dtz") mode = Mode::Dtz;
        else if (argument == "wdl") mode = Mode::Wdl;
        else if (argument == "nomirror") mirror = false;
        else if (argument == "quiet") verbose = false;
        else if (argument == "loud") verbose = true;
        else if (argument == "nocheck") runChecks = false;
        else if (argument.rfind("out=", 0) == 0) tablePath = argument.substr(4);
        else if (argument.rfind("in=", 0) == 0) tablePath = argument.substr(3);
        else if (!piecesGiven && std::atoi(argv[i]) > 0)
        {
            pieces = static_cast<unsigned int>(std::atoi(argv[i]));
            piecesGiven = true;
        }
    }

    if (command == "count")
    {
        return commandCount();
    }
    if (command == "verify")
    {
        return commandVerify(piecesGiven ? static_cast<int>(pieces) : 5000);
    }
    if (command == "plan")
    {
        return commandPlan(piecesGiven ? pieces : 12, mode, mirror);
    }
    if (command == "solve")
    {
        return commandSolve(pieces, mode, mirror, verbose, runChecks, tablePath);
    }
    if (command == "dtzcheck")
    {
    {
        int clockLimit = MAX_CLOCK_BUDGET;
        for (int i = 3; i < argc; ++i)
        {
            if (string(argv[i]).rfind("clock=", 0) == 0)
            {
                clockLimit = std::atoi(argv[i] + 6);
            }
        }
        return commandDtzCheck(piecesGiven ? pieces : 5, 200, clockLimit);
    }
    }
    if (command == "compare")
    {
        if (argc < 4)
        {
            std::fprintf(stderr, "compare needs two table files: <wdl> <dtz>\n");
            return 2;
        }
        return commandCompare(argv[2], argv[3]);
    }
    if (command == "mirrorcheck")
    {
        return commandMirrorCheck(pieces);
    }
    if (command == "line")
    {
        if (argc < 3)
        {
            std::fprintf(stderr, "line needs a FENCE string\n");
            return 2;
        }
        return commandLine(argv[2], pieces, mode, tablePath, 400);
    }
    if (command == "probe")
    {
        if (argc < 3)
        {
            std::fprintf(stderr, "probe needs a FENCE string\n");
            return 2;
        }
        return commandProbe(argv[2], pieces, mode, tablePath);
    }
    if (command == "distcheck")
    {
        if (argc < 4)
        {
            std::fprintf(stderr, "distcheck needs two table files: <wdl> <dtm>\n");
            return 2;
        }
        return commandDistCheck(argv[2], argv[3], piecesGiven ? pieces : 200000);
    }
    if (command == "readercheck")
    {
        return commandReaderCheck(tablePath, mode);
    }
    if (command == "play")
    {
        if (tablePath.empty())
        {
            std::fprintf(stderr,
                         "play needs a solved table: retro play in=tb12.bin [\"<fence>\"]\n");
            return 2;
        }
        // A quoted position may follow the command; flags are matched by prefix, not position.
        string startFence;
        for (int i = 2; i < argc; ++i)
        {
            const string argument = argv[i];
            if (argument.find(' ') != string::npos)
            {
                startFence = argument;
                break;
            }
        }
        return commandPlay(tablePath, startFence);
    }

    std::fprintf(stderr,
                 "usage: retro count\n"
                 "       retro verify [games]\n"
                 "       retro plan [pieces] [wdl|dtm] [nomirror]\n"
                 "       retro solve [pieces] [wdl|dtm] [nomirror] [loud] [nocheck] [out=FILE]\n"
                 "       retro mirrorcheck [pieces]\n"
                 "       retro dtzcheck [pieces] [clock=N]\n"
                 "       retro compare <wdl-table> <dtz-table>\n"
                 "       retro probe \"<fence>\" [pieces] [wdl|dtm] [in=FILE]\n"
                 "       retro line \"<fence>\" [in=FILE]\n"
                 "       retro play in=FILE [\"<fence>\"]\n"
                 "       retro readercheck in=FILE [dtm]\n"
                 "\n"
                 "solve writes each slice to out=FILE as it finishes, so a run that dies can be\n"
                 "resumed by repeating the same command. probe reads in=FILE instead of solving.\n");
    return 2;
}

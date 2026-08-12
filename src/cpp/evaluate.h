#pragma once

#include "position.h"
#include "tt.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>

using std::max, std::min;

/*
Alpha-beta search over the one-dimensional game.

What changed from the original, and why:

  - No allocations anywhere in the tree. Move lists live in one buffer per ply and the
    principal variation is written into a triangular array only when a move improves on
    the best so far, instead of copying a vector down every edge.
  - No std::function indirection, which also lets the evaluator inline.
  - Move generation happens once per node; the terminal test takes the count it produced
    rather than regenerating.
  - A transposition table, which matters more here than in normal chess because quiet
    moves in one dimension commute almost freely.
  - Mate scores carry their distance (SCORE_MATE - ply), so shorter mates win on score
    alone. That replaces the old `findShortestLine` bookkeeping, which compared move-list
    lengths and interacted badly with pruning.
  - Repetition along the current line scores as a draw. Besides being closer to the real
    rules, it collapses the enormous shuffling subtrees the old search wandered into.
  - The invented "draw at 150 fullmoves" rule is gone. It was not a rule of the game and
    it made results depend on the fullmove counter.

`indeterminate` reports whether the returned score leaned on a horizon estimate. It is
deliberately conservative: it can be true for a score that happens to be provable, but it
is never false for one that is not. A `false` therefore means the value is a proof.

A note on repetition and the transposition table. A repetition draw is a property of the
path, not of the position, so a value derived from one must not be handed to a different
path that reaches the same position — the classic graph-history interaction problem. Left
unchecked it makes the search report mates that are real but longer than the true
distance. Nodes whose value depended on a repetition are therefore never stored. That
costs some table hits and is the reason `repetitionAware` can be turned off: with it off
the search matches the retrograde solver's semantics exactly, which is what makes the two
comparable move for move.
*/

// Score scale, in centipawns. Mate scores sit far above any material total so they can
// never be confused with one; the distance-to-mate is folded in so that a mate found
// sooner scores higher.
const int SCORE_MATE = 30000;
const int SCORE_DRAW = 0;
const int SCORE_INFINITE = 32000;
const int MAX_PLY = 128;
const int SCORE_MATE_FLOOR = SCORE_MATE - MAX_PLY;

// Kept for callers that still speak the old vocabulary.
const int SCORE_WIN = SCORE_MATE;
const int SCORE_LOSS = -SCORE_MATE;

inline bool isMateScore(int score)
{
    return score >= SCORE_MATE_FLOOR || score <= -SCORE_MATE_FLOOR;
}

// Full moves until mate, given a mate score. Positive means the side to move mates.
inline int mateDistanceInMoves(int score)
{
    const int plies = SCORE_MATE - (score > 0 ? score : -score);
    return (score > 0) ? (plies + 1) / 2 : -((plies + 1) / 2);
}

/*
Material value of a piece nibble, in centipawns. The king is worth nothing here because
both are always on the board, so its value would cancel anyway.
*/
inline int pieceValue(unsigned int nibble)
{
    switch (nibble & 7u)
    {
    case 1:
        return 100; // pawn
    case 2:
        return 300; // knight
    case 5:
        return 300; // bishop
    case 6:
        return 500; // rook
    case 7:
        return 900; // queen
    default:
        return 0;
    }
}

/*
Given a board, score it from `player`'s point of view using a material estimate.
*/
inline int scorePositionEstimate(bool player, unsigned long long board)
{
    int white = 0, black = 0;
    for (unsigned int pieces = getOccupancy(board); pieces; pieces &= pieces - 1)
    {
        const unsigned int nibble = getNthNibble(board, attacks::lowestSquare(pieces));
        ((nibble & COLOUR_BLACK) ? black : white) += pieceValue(nibble);
    }
    const int score = white - black;
    return player ? score : -score;
}

struct SearchStats
{
    unsigned long long nodes = 0;
    unsigned long long terminals = 0;
    unsigned long long horizonEvals = 0;
    unsigned long long ttHits = 0;
    double seconds = 0.0;
};

/*
A reusable search. Construct once and reuse across positions so the transposition table
and the per-ply buffers survive; nothing inside the search touches the allocator.
*/
class Searcher
{
public:
    explicit Searcher(unsigned int ttLog2Entries = 22) : table(ttLog2Entries) {}

    tt::Table &transpositionTable() { return table; }
    const SearchStats &stats() const { return lastStats; }

    /*
    Whether a position repeating along the current line scores as a draw. On by default,
    which is both closer to the real rules and a large pruning win. Turn it off to match
    the retrograde solver, which resolves repetition by leaving positions unlabelled
    rather than by detecting them on the path.
    */
    void setRepetitionAware(bool enabled) { repetitionAware = enabled; }

    /*
    Search `board` to `maxDepth` plies. Returns the score from the side to move's point
    of view; `pvOut` receives the principal variation and `indeterminateOut` whether the
    score depended on a horizon estimate.
    */
    int search(unsigned long long board,
               bool active,
               unsigned int halfmove,
               int maxDepth,
               vector<unsigned int> &pvOut,
               bool &indeterminateOut)
    {
        const auto started = std::chrono::steady_clock::now();
        lastStats = SearchStats{};

        unsigned int flags = 0;
        const int score = negamax(board, active, halfmove, maxDepth, 0,
                                  -SCORE_INFINITE, SCORE_INFINITE, flags);

        pvOut.assign(pv[0].begin(), pv[0].begin() + pvLength[0]);

        // The root searches a full window, so its score is exact rather than a bound. A
        // mate score can then only have come from a real checkmate propagated up the
        // tree, which makes it a proof no matter how many siblings hit the horizon.
        indeterminateOut = (flags & FLAG_HORIZON) && !isMateScore(score);
        lastStats.ttHits = table.hitCount();
        lastStats.seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return score;
    }

    /*
    Iterative deepening. Each pass seeds the next with transposition-table entries, so
    the deepest pass gets far better move ordering than a single deep search would.
    Returns the score at `maxDepth`.
    */
    int searchIterative(unsigned long long board,
                        bool active,
                        unsigned int halfmove,
                        int maxDepth,
                        vector<unsigned int> &pvOut,
                        bool &indeterminateOut,
                        bool verbose = false)
    {
        int score = 0;
        SearchStats totals;
        for (int depth = 1; depth <= maxDepth; ++depth)
        {
            score = search(board, active, halfmove, depth, pvOut, indeterminateOut);
            totals.nodes += lastStats.nodes;
            totals.terminals += lastStats.terminals;
            totals.horizonEvals += lastStats.horizonEvals;
            totals.seconds += lastStats.seconds;
            if (verbose)
            {
                std::printf("  depth %2d  %-14s  %12llu nodes  %7.2fs  %s\n",
                            depth, describeScore(score).c_str(), lastStats.nodes,
                            lastStats.seconds, indeterminateOut ? "estimate" : "proven");
                std::fflush(stdout);
            }

            // A proven result cannot change by looking further — with one caveat. The
            // transposition table can carry a value across from a shallower ply, so a
            // depth-d search can prove a mate further away than d. That mate is real, but
            // a shorter one may exist below the horizon, so the distance is only
            // trustworthy once it fits inside the depth actually searched. Outcomes are
            // sound either way; this is about the distance being minimal.
            if (!indeterminateOut &&
                (!isMateScore(score) || (SCORE_MATE - std::abs(score)) <= depth))
            {
                break;
            }
        }
        totals.ttHits = table.hitCount();
        lastStats = totals;
        return score;
    }

    static string describeScore(int score)
    {
        if (isMateScore(score))
        {
            const int moves = mateDistanceInMoves(score);
            return string("mate in ") + std::to_string(moves > 0 ? moves : -moves) +
                   (moves > 0 ? " (win)" : " (loss)");
        }
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%+.2f", score / 100.0);
        return string(buffer);
    }

private:
    // Where a node's value came from. A horizon estimate makes it a guess; a repetition
    // makes it true only of the path that reached it.
    static constexpr unsigned int FLAG_HORIZON = 1u;
    static constexpr unsigned int FLAG_REPETITION = 2u;

    tt::Table table;
    SearchStats lastStats;
    bool repetitionAware = true;

    std::array<std::array<unsigned char, MAX_MOVES>, MAX_PLY> moveBuffers{};
    std::array<std::array<int, MAX_MOVES>, MAX_PLY> moveScores{};
    std::array<std::array<unsigned char, MAX_PLY>, MAX_PLY> pv{};
    std::array<unsigned char, MAX_PLY> pvLength{};
    std::array<unsigned long long, MAX_PLY> lineBoards{};

    /*
    Whether this board already occurred earlier in the current line. Only positions with
    the same side to move can repeat, so the scan steps back two plies at a time, and it
    stops at the last irreversible move because nothing before it can recur.
    */
    bool isRepetition(unsigned long long board, int ply, unsigned int halfmove) const
    {
        const int earliest = max(0, ply - static_cast<int>(halfmove));
        for (int back = ply - 2; back >= earliest; back -= 2)
        {
            if (lineBoards[back] == board)
            {
                return true;
            }
        }
        return false;
    }

    /*
    Order moves so the cheapest refutations come first: the transposition table's move,
    then captures worth most relative to what they risk, then everything else.
    */
    void scoreMoves(unsigned long long board, int ply, unsigned int count, unsigned char ttMove)
    {
        for (unsigned int i = 0; i < count; ++i)
        {
            const unsigned char move = moveBuffers[ply][i];
            if (move == ttMove)
            {
                moveScores[ply][i] = 1 << 20;
                continue;
            }
            const unsigned int victim = getNthNibble(board, move & 15u);
            moveScores[ply][i] = victim ? (10000 + 10 * pieceValue(victim) -
                                           pieceValue(getNthNibble(board, move >> 4)))
                                        : 0;
        }
    }

    // Selection sort one move at a time: with cutoffs common, most of the list is never
    // examined, so sorting it all up front would be wasted work.
    void selectMove(int ply, unsigned int index, unsigned int count)
    {
        unsigned int best = index;
        for (unsigned int i = index + 1; i < count; ++i)
        {
            if (moveScores[ply][i] > moveScores[ply][best])
            {
                best = i;
            }
        }
        if (best != index)
        {
            std::swap(moveBuffers[ply][index], moveBuffers[ply][best]);
            std::swap(moveScores[ply][index], moveScores[ply][best]);
        }
    }

    int negamax(unsigned long long board,
                bool active,
                unsigned int halfmove,
                int depth,
                int ply,
                int alpha,
                int beta,
                unsigned int &flags)
    {
        ++lastStats.nodes;
        pvLength[ply] = 0;
        flags = 0;

        // Repetition is a draw, and it is checked before anything else so that shuffling
        // lines terminate instead of being re-searched at every depth. The result belongs
        // to this path rather than to the position, which is what FLAG_REPETITION records.
        if (repetitionAware && ply > 0 && isRepetition(board, ply, halfmove))
        {
            ++lastStats.terminals;
            flags |= FLAG_REPETITION;
            return SCORE_DRAW;
        }

        unsigned char *moves = moveBuffers[ply].data();
        const unsigned int count = generateMoves(board, active, moves);

        const int state = checkPosition(board, active, halfmove, count);
        if (state != 0)
        {
            ++lastStats.terminals;
            // 8 and 9 name the winning colour; either way the side to move is mated.
            if (state == 8 || state == 9)
            {
                return -(SCORE_MATE - ply);
            }
            return SCORE_DRAW;
        }

        if (depth <= 0 || ply + 1 >= MAX_PLY)
        {
            ++lastStats.horizonEvals;
            flags |= FLAG_HORIZON;
            return scorePositionEstimate(active, board);
        }

        const unsigned long long key = tt::positionKey(board, active);
        unsigned char ttMove = 0;
        if (const tt::Entry *entry = table.find(key, halfmove))
        {
            ttMove = entry->move;
            if (entry->depth >= static_cast<unsigned int>(depth))
            {
                // Mate scores are stored relative to the entry's own node, so shift them
                // back to this node's frame before use.
                int score = entry->score;
                if (score >= SCORE_MATE_FLOOR)
                {
                    score -= ply;
                }
                else if (score <= -SCORE_MATE_FLOOR)
                {
                    score += ply;
                }

                const bool usable = (entry->bound == tt::BOUND_EXACT) ||
                                    (entry->bound == tt::BOUND_LOWER && score >= beta) ||
                                    (entry->bound == tt::BOUND_UPPER && score <= alpha);
                if (usable)
                {
                    // Stored entries are never repetition-tainted, so only the horizon
                    // flag can survive a table hit.
                    flags |= entry->indeterminate ? FLAG_HORIZON : 0u;
                    return score;
                }
            }
        }

        lineBoards[ply] = board;
        scoreMoves(board, ply, count, ttMove);

        int bestScore = -SCORE_INFINITE;
        unsigned char bestMove = 0;
        const int originalAlpha = alpha;

        for (unsigned int i = 0; i < count; ++i)
        {
            selectMove(ply, i, count);
            const unsigned char move = moves[i];

            const unsigned int start = move >> 4, end = move & 15u;
            const unsigned int moved = getNthNibble(board, start);
            const unsigned int captured = getNthNibble(board, end);
            const unsigned int nextHalfmove =
                (isPawn(moved) || !isEmpty(captured)) ? 0 : halfmove + 1;

            unsigned int childFlags = 0;
            const int score = -negamax(applyMoveToBoard(board, move),
                                       !active,
                                       nextHalfmove,
                                       depth - 1,
                                       ply + 1,
                                       -beta,
                                       -alpha,
                                       childFlags);

            // Conservative: any child we consulted can have shaped the answer.
            flags |= childFlags;

            if (score > bestScore)
            {
                bestScore = score;
                bestMove = move;

                pv[ply][0] = move;
                const unsigned int childLength = pvLength[ply + 1];
                for (unsigned int j = 0; j < childLength; ++j)
                {
                    pv[ply][j + 1] = pv[ply + 1][j];
                }
                pvLength[ply] = static_cast<unsigned char>(childLength + 1);
            }

            alpha = max(alpha, score);
            if (alpha >= beta)
            {
                break; // fail high
            }
        }

        const tt::Bound bound = (bestScore <= originalAlpha) ? tt::BOUND_UPPER
                                : (bestScore >= beta)        ? tt::BOUND_LOWER
                                                             : tt::BOUND_EXACT;

        int stored = bestScore;
        if (stored >= SCORE_MATE_FLOOR)
        {
            stored += ply;
        }
        else if (stored <= -SCORE_MATE_FLOOR)
        {
            stored -= ply;
        }
        // A value that leaned on a repetition is true of this path only; handing it to a
        // different path that reaches the same position is what makes a search report
        // mates longer than they really are.
        if (!(flags & FLAG_REPETITION))
        {
            table.store(key, stored, static_cast<unsigned int>(depth), bound, halfmove, bestMove,
                        (flags & FLAG_HORIZON) != 0);
        }

        return bestScore;
    }
};

/*
Evaluates the given FENCE string to the given depth and prints the result.
*/
inline void evaluateFenceVerbose(string fence, int maxDepth, bool verbose = true)
{
    static Searcher searcher;

    unsigned long long board = 0;
    bool active = true;
    unsigned int halfmove = 0, fullmove = 1;
    tie(board, active, halfmove, fullmove) = fenceToVars(fence, board, active, halfmove, fullmove);

    vector<unsigned int> pv;
    bool indeterminate = true;
    const int score =
        searcher.searchIterative(board, active, halfmove, maxDepth, pv, indeterminate, verbose);

    const SearchStats &s = searcher.stats();
    std::printf("[%s] %s (%s) (depth=%d)  %llu nodes, %llu tt hits, %.2fs\n",
                active ? "w" : "b",
                Searcher::describeScore(score).c_str(),
                indeterminate ? "hit maxdepth" : "proven",
                maxDepth, s.nodes, s.ttHits, s.seconds);
    std::printf("%s  start\n", varsToFence(board, active, halfmove, fullmove).c_str());
    for (unsigned int m : pv)
    {
        tie(board, active, halfmove, fullmove) = applyMove(board, active, halfmove, fullmove, m);
        std::printf("%s  after (%u,%u)\n", varsToFence(board, active, halfmove, fullmove).c_str(),
                    m >> 4, m & 15);
    }
    std::printf("\n");
}

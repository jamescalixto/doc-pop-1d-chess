#pragma once

#include "retro/slice.h"

#include <cstdint>
#include <cstdio>
#include <queue>
#include <unordered_map>
#include <vector>

/*
Retrograde solver over the slice DAG.

Values are exact win/draw/loss with distance to mate, computed by the classic backward
induction:

    a position WINS  iff some legal move reaches a position that LOSES;
    a position LOSES iff it has legal moves and every one of them reaches a WIN;
    otherwise it DRAWS.

Nothing here is a heuristic and there is no horizon. The fixpoint is reached when no
further labels change, and everything still unlabelled is a draw — which is the right
answer, because a position that is neither a win nor a loss is exactly one that either
side can hold forever by repetition.

Slices are solved in `solveRank` order, so by the time a slice is reached every move that
leaves it lands in a slice already on the shelf. Within a slice, only quiet non-pawn
moves stay put, and those are the edges the backward pass walks.

Distance to mate is exact because positions are committed in ascending distance order:
the priority queue pops the shortest mate first, so the first value proposed for a
position is the true one and later proposals are discarded.

CAVEAT: this ignores the fifty-move rule, as tablebases conventionally do. Draws by
repetition are handled exactly. The fifty-move rule can only convert some of these wins
into draws; resolving that needs distance-to-zeroing-move rather than distance-to-mate.
*/
namespace retro
{

// Value of a position from the side to move's point of view.
//   0            draw
//   +(k + 1)     side to move mates in k plies
//   -(k + 1)     side to move is mated in k plies (so -1 means mated right now)
using Value = int16_t;

inline constexpr Value VALUE_DRAW = 0;
inline constexpr Value VALUE_UNRESOLVED = 32767;
inline constexpr Value VALUE_ILLEGAL = 32766;

inline bool isResolved(Value v) { return v != VALUE_UNRESOLVED; }
inline bool isPlayable(Value v) { return v != VALUE_ILLEGAL && v != VALUE_UNRESOLVED; }

/*
The same position seen from the other side of the move that produced it: the sign flips
and the distance grows by one ply.
*/
inline Value negateValue(Value v)
{
    if (v == VALUE_DRAW)
    {
        return VALUE_DRAW;
    }
    return static_cast<Value>(-(v + (v > 0 ? 1 : -1)));
}

// Distance to mate, in plies. Only meaningful for a decisive value.
inline int distanceToMate(Value v) { return (v > 0 ? v : -v) - 1; }

/*
Total order on values from the mover's point of view, so that "pick the best move" is a
plain maximum: win beats draw beats loss, a faster win beats a slower one, and a slower
loss beats a faster one.
*/
inline int valueOrder(Value v)
{
    if (v > 0)
    {
        return 1000000 - v;
    }
    if (v < 0)
    {
        return -1000000 - v;
    }
    return 0;
}

inline string describeValue(Value v)
{
    if (v == VALUE_DRAW)
    {
        return "draw";
    }
    if (v == VALUE_ILLEGAL)
    {
        return "illegal";
    }
    if (v == VALUE_UNRESOLVED)
    {
        return "unresolved";
    }
    const int plies = distanceToMate(v);
    return (v > 0 ? "win in " : "loss in ") + std::to_string(plies) + " plies";
}

struct SolvedSlice
{
    Slice slice;
    std::vector<Value> values; // 2 per placement: [2 * index + (whiteToMove ? 0 : 1)]

    Value at(unsigned long long index, bool whiteToMove) const
    {
        return values[2 * index + (whiteToMove ? 0 : 1)];
    }
};

struct SolveStats
{
    unsigned long long slices = 0;
    unsigned long long positions = 0;
    unsigned long long illegal = 0;
    unsigned long long whiteWins = 0;
    unsigned long long blackWins = 0;
    unsigned long long draws = 0;
    unsigned long long encodeFailures = 0;
};

class Solver
{
public:
    /*
    Solve every slice with at most `maxPieces` pieces on the board, kings included.
    */
    void solve(unsigned int maxPieces, bool verbose = true)
    {
        for (const SliceKey &key : allSliceKeys())
        {
            if (key.pieceCount() > maxPieces)
            {
                continue;
            }
            solveSlice(key, verbose);
        }
    }

    const SolvedSlice *find(const SliceKey &key) const
    {
        const auto it = solved.find(key.packed());
        return (it == solved.end()) ? nullptr : &it->second;
    }

    /*
    Value of an arbitrary board, if its slice has been solved.
    */
    Value lookup(unsigned long long board, bool whiteToMove, bool *found = nullptr) const
    {
        const SolvedSlice *entry = find(sliceKeyOf(board));
        if (!entry)
        {
            if (found)
            {
                *found = false;
            }
            return VALUE_UNRESOLVED;
        }
        const unsigned long long index = entry->slice.encode(board);
        if (index == Slice::INVALID)
        {
            if (found)
            {
                *found = false;
            }
            return VALUE_UNRESOLVED;
        }
        if (found)
        {
            *found = true;
        }
        return entry->at(index, whiteToMove);
    }

    const SolveStats &stats() const { return totals; }
    std::size_t sliceCount() const { return solved.size(); }

private:
    std::unordered_map<uint32_t, SolvedSlice> solved;
    SolveStats totals;

    struct Pending
    {
        int32_t distance;
        unsigned long long slot;
        Value value;
        bool operator>(const Pending &other) const { return distance > other.distance; }
    };

    static bool leavesSlice(unsigned long long board, unsigned char move)
    {
        const unsigned int moved = getNthNibble(board, move >> 4);
        const unsigned int captured = getNthNibble(board, move & 15u);
        return isPawn(moved) || !isEmpty(captured);
    }

    void solveSlice(const SliceKey &key, bool verbose)
    {
        SolvedSlice entry;
        if (!entry.slice.build(key))
        {
            return; // no placements satisfy this slice's invariants
        }

        const unsigned long long n = entry.slice.size();
        const unsigned long long slots = 2 * n;
        entry.values.assign(slots, VALUE_UNRESOLVED);

        std::vector<uint8_t> counter(slots, 0);
        std::vector<Value> lossValue(slots, VALUE_UNRESOLVED);
        std::priority_queue<Pending, std::vector<Pending>, std::greater<Pending>> queue;

        const auto propose = [&](unsigned long long slot, Value value)
        { queue.push({distanceToMate(value), slot, value}); };

        unsigned long long encodeFailures = 0;
        unsigned char moves[MAX_MOVES];

        // Pass 1: terminal positions, plus everything decided by a move that leaves the
        // slice into a table already solved.
        for (unsigned long long index = 0; index < n; ++index)
        {
            const unsigned long long board = entry.slice.decode(index);
            for (unsigned int side = 0; side < 2; ++side)
            {
                const bool whiteToMove = (side == 0);
                const unsigned long long slot = 2 * index + side;

                // A position where the side that just moved left its own king attacked
                // could never have arisen.
                if (isInCheck(board, !whiteToMove))
                {
                    entry.values[slot] = VALUE_ILLEGAL;
                    continue;
                }

                const unsigned int count = generateMoves(board, whiteToMove, moves);
                if (count == 0)
                {
                    if (isInCheck(board, whiteToMove))
                    {
                        // Mated where it stands. Queued rather than committed so that
                        // predecessors still see it in distance order.
                        propose(slot, static_cast<Value>(-1));
                    }
                    else
                    {
                        entry.values[slot] = VALUE_DRAW; // stalemate
                    }
                    continue;
                }

                counter[slot] = static_cast<uint8_t>(count);

                for (unsigned int i = 0; i < count; ++i)
                {
                    if (!leavesSlice(board, moves[i]))
                    {
                        continue; // resolved by the backward pass below
                    }
                    const unsigned long long next = applyMoveToBoard(board, moves[i]);
                    bool found = false;
                    const Value successor = lookup(next, !whiteToMove, &found);
                    if (!found || !isPlayable(successor))
                    {
                        // Only happens if a slice was skipped; treat as unusable rather
                        // than silently wrong.
                        ++encodeFailures;
                        continue;
                    }
                    const Value ours = negateValue(successor);
                    if (successor < 0)
                    {
                        propose(slot, ours); // they are lost, so we win
                    }
                    else if (successor > 0)
                    {
                        lossValue[slot] = std::min(lossValue[slot], ours);
                        if (--counter[slot] == 0)
                        {
                            propose(slot, lossValue[slot]);
                        }
                    }
                    // A drawn successor removes the chance of losing, so the counter
                    // deliberately stays put: it can now never reach zero.
                }
            }
        }

        // Pass 2: backward induction inside the slice, shortest mates committed first.
        while (!queue.empty())
        {
            const Pending pending = queue.top();
            queue.pop();
            if (isResolved(entry.values[pending.slot]))
            {
                continue; // a shorter distance already claimed this position
            }
            entry.values[pending.slot] = pending.value;

            const unsigned long long index = pending.slot / 2;
            const bool whiteToMove = (pending.slot % 2) == 0;
            const unsigned long long board = entry.slice.decode(index);

            forEachPredecessor(
                board, whiteToMove, entry.slice,
                [&](unsigned long long predecessor, unsigned long long predecessorIndex)
                {
                    const unsigned long long predecessorSlot =
                        2 * predecessorIndex + (whiteToMove ? 1 : 0);
                    if (isResolved(entry.values[predecessorSlot]))
                    {
                        return;
                    }
                    (void)predecessor;

                    if (pending.value < 0)
                    {
                        // The mover here is lost, so whoever moved into it has a win.
                        propose(predecessorSlot, negateValue(pending.value));
                    }
                    else if (pending.value > 0)
                    {
                        const Value ours = negateValue(pending.value);
                        lossValue[predecessorSlot] = std::min(lossValue[predecessorSlot], ours);
                        if (counter[predecessorSlot] > 0 && --counter[predecessorSlot] == 0)
                        {
                            propose(predecessorSlot, lossValue[predecessorSlot]);
                        }
                    }
                },
                encodeFailures);
        }

        // Whatever survived the fixpoint unlabelled is a draw: neither side can force a
        // result, so the game goes on forever.
        for (unsigned long long slot = 0; slot < slots; ++slot)
        {
            if (entry.values[slot] == VALUE_UNRESOLVED)
            {
                entry.values[slot] = VALUE_DRAW;
            }
        }

        // Accounting, reported from white's point of view so the numbers are comparable
        // across slices.
        unsigned long long illegal = 0, whiteWins = 0, blackWins = 0, draws = 0;
        for (unsigned long long slot = 0; slot < slots; ++slot)
        {
            const Value value = entry.values[slot];
            if (value == VALUE_ILLEGAL)
            {
                ++illegal;
                continue;
            }
            const bool whiteToMove = (slot % 2) == 0;
            if (value == VALUE_DRAW)
            {
                ++draws;
            }
            else if ((value > 0) == whiteToMove)
            {
                ++whiteWins;
            }
            else
            {
                ++blackWins;
            }
        }

        totals.slices += 1;
        totals.positions += slots;
        totals.illegal += illegal;
        totals.whiteWins += whiteWins;
        totals.blackWins += blackWins;
        totals.draws += draws;
        totals.encodeFailures += encodeFailures;

        if (verbose)
        {
            std::printf("  %-26s %10llu positions  W %-10llu B %-10llu draw %-10llu illegal %llu%s\n",
                        describeSlice(key).c_str(), slots, whiteWins, blackWins, draws, illegal,
                        encodeFailures ? "  [ENCODE FAILURES]" : "");
            std::fflush(stdout);
        }

        solved.emplace(key.packed(), std::move(entry));
    }

public:
    static string describeSlice(const SliceKey &key)
    {
        string name = "K";
        const struct
        {
            unsigned int bit;
            char letter;
        } whitePieces[] = {{MAT_WQ, 'Q'}, {MAT_WR, 'R'}, {MAT_WB, 'B'}, {MAT_WN, 'N'}, {MAT_WP, 'P'}};
        for (const auto &piece : whitePieces)
        {
            if (key.material & piece.bit)
            {
                name += piece.letter;
            }
        }
        name += "k";
        const struct
        {
            unsigned int bit;
            char letter;
        } blackPieces[] = {{MAT_BQ, 'q'}, {MAT_BR, 'r'}, {MAT_BB, 'b'}, {MAT_BN, 'n'}, {MAT_BP, 'p'}};
        for (const auto &piece : blackPieces)
        {
            if (key.material & piece.bit)
            {
                name += piece.letter;
            }
        }
        if (key.whitePawn != NO_SQUARE)
        {
            name += " P@" + std::to_string(key.whitePawn);
        }
        if (key.blackPawn != NO_SQUARE)
        {
            name += " p@" + std::to_string(key.blackPawn);
        }
        return name;
    }

    /*
    Visit every position inside this slice from which `board` is reachable by one quiet,
    non-pawn move — the only kind of move that does not leave the slice.

    Unmove generation is the mirror of move generation because every piece that can stay
    in a slice (king, knight, bishop, rook, queen) is a symmetric mover: if a piece
    standing on `to` attacks `from`, then the same piece standing on `from` attacks `to`,
    and the ray between them is equally clear either way. The origin must be empty, since
    a quiet move leaves nothing behind.
    */
    template <typename Visitor>
    static void forEachPredecessor(unsigned long long board,
                                   bool whiteToMove,
                                   const Slice &slice,
                                   Visitor &&visit,
                                   unsigned long long &encodeFailures)
    {
        const bool mover = !whiteToMove; // whoever moved into this position
        const unsigned int occupancy = getOccupancy(board);
        const unsigned int empty = ~occupancy & 0xFFFFu;

        for (unsigned int pieces = getPlayerOccupancy(board, mover); pieces; pieces &= pieces - 1)
        {
            const unsigned int to = attacks::lowestSquare(pieces);
            const unsigned int piece = getNthNibble(board, to);
            if (isPawn(piece))
            {
                continue; // a pawn move would have left the slice
            }

            for (unsigned int origins = attacks::pieceAttacks(piece, to, occupancy) & empty;
                 origins; origins &= origins - 1)
            {
                const unsigned int from = attacks::lowestSquare(origins);
                unsigned long long predecessor = blankNthNibble(board, to);
                predecessor = insertNthNibble(predecessor, piece, from);

                // The side that was not to move must not have been in check, or the
                // position could never have been on the board.
                if (isInCheck(predecessor, whiteToMove))
                {
                    continue;
                }

                const unsigned long long index = slice.encode(predecessor);
                if (index == Slice::INVALID)
                {
                    ++encodeFailures;
                    continue;
                }
                visit(predecessor, index);
            }
        }
    }
};

} // namespace retro

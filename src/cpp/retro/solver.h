#pragma once

#include "retro/slice.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <queue>
#include <unordered_map>
#include <vector>

/*
Retrograde solver over the slice DAG.

Values are exact win/draw/loss, computed by the classic backward induction:

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

TWO MODES, DELIBERATELY WRITTEN TWICE

`Mode::Wdl` is the production path: two bits per position, and a plain worklist, since
without distances there is nothing to order. It is what scales to the whole game.

`Mode::Dtm` additionally carries distance to mate in two bytes per position, which needs
a priority queue so that positions commit in ascending distance and the first value
proposed for a position is the true one.

The two share the slice machinery but their fixpoints are written out separately rather
than merged behind a flag. That is on purpose: they are independent implementations of
the same definition, so running both over the same material and comparing is a real
check rather than a tautology.

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
// In Wdl mode only the sign carries meaning: wins are +1 and losses -1.
using Value = int16_t;

inline constexpr Value VALUE_DRAW = 0;
inline constexpr Value VALUE_WIN = 1;
inline constexpr Value VALUE_LOSS = -1;
inline constexpr Value VALUE_UNRESOLVED = 32767;
inline constexpr Value VALUE_ILLEGAL = 32766;

enum class Mode
{
    Wdl,
    Dtm,
};

inline bool isResolved(Value v) { return v != VALUE_UNRESOLVED; }
inline bool isPlayable(Value v) { return v != VALUE_ILLEGAL && v != VALUE_UNRESOLVED; }

/*
The same position seen from the other side of the move that produced it: the sign flips
and, when distances are tracked, the distance grows by one ply.
*/
inline Value negateValue(Value v, bool trackDistance = true)
{
    if (v == VALUE_DRAW)
    {
        return VALUE_DRAW;
    }
    if (!trackDistance)
    {
        return (v > 0) ? VALUE_LOSS : VALUE_WIN;
    }
    return static_cast<Value>(-(v + (v > 0 ? 1 : -1)));
}

// Distance to mate, in plies. Only meaningful in Dtm mode.
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

inline string describeValue(Value v, bool exactDistance = true)
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
    if (!exactDistance)
    {
        return (v > 0) ? "win" : "loss";
    }
    return (v > 0 ? "win in " : "loss in ") + std::to_string(distanceToMate(v)) + " plies";
}

/*
Two bits per position: the whole game is 4.19 GiB this way, or 2.20 GiB once mirror
symmetry removes one slice of every pair.

Writes are read-modify-write on a shared word, so this is not safe to update from several
threads at once without partitioning by word.
*/
class PackedWdl
{
public:
    enum : uint8_t
    {
        CODE_DRAW = 0,
        CODE_WIN = 1,
        CODE_LOSS = 2,
        CODE_ILLEGAL = 3,
    };

    void assign(uint64_t count)
    {
        entries = count;
        words.assign((count + 31) / 32, 0);
    }

    uint64_t size() const { return entries; }
    uint64_t bytes() const { return words.size() * sizeof(uint64_t); }

    uint8_t get(uint64_t i) const
    {
        return static_cast<uint8_t>((words[i >> 5] >> ((i & 31) * 2)) & 3);
    }

    void set(uint64_t i, uint8_t code)
    {
        const unsigned int shift = (i & 31) * 2;
        uint64_t &word = words[i >> 5];
        word = (word & ~(uint64_t{3} << shift)) | (uint64_t{code} << shift);
    }

private:
    std::vector<uint64_t> words;
    uint64_t entries = 0;
};

struct SolvedSlice
{
    Slice slice;
    PackedWdl wdl;            // Wdl mode
    std::vector<Value> dtm;   // Dtm mode; empty otherwise

    Value at(unsigned long long index, bool whiteToMove) const
    {
        const unsigned long long slot = 2 * index + (whiteToMove ? 0 : 1);
        if (!dtm.empty())
        {
            return dtm[slot];
        }
        switch (wdl.get(slot))
        {
        case PackedWdl::CODE_WIN:
            return VALUE_WIN;
        case PackedWdl::CODE_LOSS:
            return VALUE_LOSS;
        case PackedWdl::CODE_ILLEGAL:
            return VALUE_ILLEGAL;
        default:
            return VALUE_DRAW;
        }
    }

    uint64_t bytes() const
    {
        return dtm.empty() ? wdl.bytes() : dtm.size() * sizeof(Value);
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
    uint64_t tableBytes = 0;
    uint64_t peakTransientBytes = 0;
};

class Solver
{
public:
    explicit Solver(Mode solveMode = Mode::Wdl, bool mirror = true)
        : mode(solveMode), useMirror(mirror)
    {
    }

    bool tracksDistance() const { return mode == Mode::Dtm; }
    bool mirroring() const { return useMirror; }
    Value negate(Value v) const { return negateValue(v, tracksDistance()); }
    string describe(Value v) const { return describeValue(v, tracksDistance()); }

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
            if (useMirror && !isCanonicalSlice(key))
            {
                continue; // its mirror carries the answer
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
    Value of an arbitrary board, if its slice has been solved. When mirroring, a board
    whose slice is not the canonical one is reflected first; the reflection swaps the
    colours *and* the side to move, so the value is read out unchanged.
    */
    Value lookup(unsigned long long board, bool whiteToMove, bool *found = nullptr) const
    {
        SliceKey key = sliceKeyOf(board);
        if (useMirror && !isCanonicalSlice(key))
        {
            board = mirrorBoard(board);
            whiteToMove = !whiteToMove;
            key = mirrorSliceKey(key);
        }

        const SolvedSlice *entry = find(key);
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
                visit(index);
            }
        }
    }

private:
    // Labels used while a slice is being solved, before it is packed down.
    enum : uint8_t
    {
        STATE_UNRESOLVED = 0,
        STATE_DRAW = 1,
        STATE_WIN = 2,
        STATE_LOSS = 3,
        STATE_ILLEGAL = 4,
    };

    /*
    Where a move that leaves the slice lands. Resolved once per kind of move rather than
    once per move: the destination slice depends only on which piece was captured and how
    far a pawn stepped, so a 128-entry table covers every possibility a slice has.
    */
    struct SuccessorTarget
    {
        const SolvedSlice *slice = nullptr;
        bool mirrored = false;
        bool resolved = false;
    };

    Mode mode;
    bool useMirror;
    std::unordered_map<uint32_t, SolvedSlice> solved;
    SolveStats totals;

    /*
    The slice a move lands in, derived arithmetically from the slice it left. The board
    scan this replaces was the hottest thing in the solver, and it was recomputing
    something already implied by the move.
    */
    static SliceKey successorKey(const SliceKey &key,
                                 unsigned int moved,
                                 unsigned int captured,
                                 unsigned int end)
    {
        SliceKey next = key;
        if (captured)
        {
            next.material &= ~materialBitOf(captured);
            if (captured == PIECE_PAWN)
            {
                next.whitePawn = static_cast<unsigned char>(NO_SQUARE);
            }
            else if (captured == (PIECE_PAWN | COLOUR_BLACK))
            {
                next.blackPawn = static_cast<unsigned char>(NO_SQUARE);
            }
        }
        if (moved == PIECE_PAWN)
        {
            next.whitePawn = static_cast<unsigned char>(end);
        }
        else if (moved == (PIECE_PAWN | COLOUR_BLACK))
        {
            next.blackPawn = static_cast<unsigned char>(end);
        }
        return next;
    }

    // Compact code for "what kind of slice-leaving move is this", used to index the cache.
    static unsigned int successorCode(const SliceKey &key,
                                      unsigned int moved,
                                      unsigned int captured,
                                      unsigned int end)
    {
        unsigned int pawnStep = 0;
        if (moved == PIECE_PAWN)
        {
            pawnStep = 1 + (end - key.whitePawn); // 2 or 3
        }
        else if (moved == (PIECE_PAWN | COLOUR_BLACK))
        {
            pawnStep = 3 + (key.blackPawn - end); // 4 or 5
        }
        return captured | (pawnStep << 4);
    }

    const SuccessorTarget &resolveSuccessor(std::array<SuccessorTarget, 128> &cache,
                                            const SliceKey &key,
                                            unsigned int moved,
                                            unsigned int captured,
                                            unsigned int end) const
    {
        SuccessorTarget &target = cache[successorCode(key, moved, captured, end)];
        if (!target.resolved)
        {
            SliceKey next = successorKey(key, moved, captured, end);
            target.mirrored = useMirror && !isCanonicalSlice(next);
            if (target.mirrored)
            {
                next = mirrorSliceKey(next);
            }
            target.slice = find(next);
            target.resolved = true;
        }
        return target;
    }

    Value successorValue(const SuccessorTarget &target, unsigned long long board, bool whiteToMove) const
    {
        if (!target.slice)
        {
            return VALUE_UNRESOLVED;
        }
        if (target.mirrored)
        {
            board = mirrorBoard(board);
            whiteToMove = !whiteToMove;
        }
        const unsigned long long index = target.slice->slice.encode(board);
        if (index == Slice::INVALID)
        {
            return VALUE_UNRESOLVED;
        }
        return target.slice->at(index, whiteToMove);
    }

    static bool leavesSlice(unsigned long long board, unsigned char move)
    {
        return isPawn(getNthNibble(board, move >> 4)) || !isEmpty(getNthNibble(board, move & 15u));
    }

    void solveSlice(const SliceKey &key, bool verbose)
    {
        SolvedSlice entry;
        if (!entry.slice.build(key))
        {
            return; // no placements satisfy this slice's invariants
        }

        unsigned long long encodeFailures = 0;
        uint64_t transient = 0;
        const auto started = std::chrono::steady_clock::now();

        std::vector<uint8_t> state;
        if (mode == Mode::Wdl)
        {
            transient = solveSliceWdl(entry, key, state, encodeFailures);
        }
        else
        {
            transient = solveSliceDtm(entry, key, state, encodeFailures);
        }

        // Accounting, reported from white's point of view so the numbers compare across
        // slices. A mirrored run only solves half the slices, so these are the counts for
        // what was actually stored, not for the whole game.
        const unsigned long long slots = 2 * entry.slice.size();
        unsigned long long illegal = 0, whiteWins = 0, blackWins = 0, draws = 0;
        for (unsigned long long slot = 0; slot < slots; ++slot)
        {
            const uint8_t label = state[slot];
            if (label == STATE_ILLEGAL)
            {
                ++illegal;
                continue;
            }
            const bool whiteToMove = (slot % 2) == 0;
            if (label == STATE_WIN)
            {
                (whiteToMove ? whiteWins : blackWins) += 1;
            }
            else if (label == STATE_LOSS)
            {
                (whiteToMove ? blackWins : whiteWins) += 1;
            }
            else
            {
                ++draws;
            }
        }

        totals.slices += 1;
        totals.positions += slots;
        totals.illegal += illegal;
        totals.whiteWins += whiteWins;
        totals.blackWins += blackWins;
        totals.draws += draws;
        totals.encodeFailures += encodeFailures;
        totals.tableBytes += entry.bytes();
        totals.peakTransientBytes = std::max(totals.peakTransientBytes, transient);

        if (verbose)
        {
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            std::printf("  %-28s %11llu positions  W %-10llu B %-10llu draw %-10llu  %6.2fs%s\n",
                        describeSlice(key).c_str(), slots, whiteWins, blackWins, draws, elapsed,
                        encodeFailures ? "  [ENCODE FAILURES]" : "");
            std::fflush(stdout);
        }

        solved.emplace(key.packed(), std::move(entry));
    }

    /*
    Win/draw/loss only. Without distances there is nothing to order, so a plain worklist
    replaces the priority queue and a position is committed the moment it is decided.
    */
    uint64_t solveSliceWdl(SolvedSlice &entry,
                           const SliceKey &key,
                           std::vector<uint8_t> &state,
                           unsigned long long &encodeFailures)
    {
        const unsigned long long n = entry.slice.size();
        const unsigned long long slots = 2 * n;

        // The worklist stores slot numbers as 32-bit values. The largest slice in this
        // game has 645,765,120 of them, comfortably inside the range, but a wider board
        // would break that silently rather than loudly.
        if (slots > 0xFFFFFFFFull)
        {
            std::fprintf(stderr, "slice %s has %llu slots, too many for a 32-bit worklist\n",
                         describeSlice(key).c_str(), slots);
            std::abort();
        }

        state.assign(slots, STATE_UNRESOLVED);
        std::vector<uint8_t> counter(slots, 0);
        std::vector<uint32_t> worklist;

        std::array<SuccessorTarget, 128> successors{};
        unsigned char moves[MAX_MOVES];

        // Pass 1: terminal positions, plus everything already decided by a move that
        // leaves the slice into a table solved earlier.
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
                    state[slot] = STATE_ILLEGAL;
                    continue;
                }

                const unsigned int count = generateMoves(board, whiteToMove, moves);
                if (count == 0)
                {
                    if (isInCheck(board, whiteToMove))
                    {
                        state[slot] = STATE_LOSS; // mated where it stands
                        worklist.push_back(static_cast<uint32_t>(slot));
                    }
                    else
                    {
                        state[slot] = STATE_DRAW; // stalemate
                    }
                    continue;
                }

                counter[slot] = static_cast<uint8_t>(count);

                for (unsigned int i = 0; i < count; ++i)
                {
                    const unsigned int start = moves[i] >> 4, end = moves[i] & 15u;
                    const unsigned int moved = getNthNibble(board, start);
                    const unsigned int captured = getNthNibble(board, end);
                    if (!isPawn(moved) && isEmpty(captured))
                    {
                        continue; // stays in the slice; the backward pass handles it
                    }

                    const SuccessorTarget &target =
                        resolveSuccessor(successors, key, moved, captured, end);
                    const Value successor =
                        successorValue(target, applyMoveToBoard(board, moves[i]), !whiteToMove);
                    if (!isPlayable(successor))
                    {
                        ++encodeFailures;
                        continue;
                    }

                    if (successor < 0)
                    {
                        state[slot] = STATE_WIN; // they are lost, so we win
                        worklist.push_back(static_cast<uint32_t>(slot));
                        break;
                    }
                    if (successor > 0)
                    {
                        --counter[slot];
                    }
                    // A drawn successor removes the chance of losing, so the counter
                    // deliberately stays put: it can now never reach zero.
                }

                if (state[slot] == STATE_UNRESOLVED && counter[slot] == 0)
                {
                    state[slot] = STATE_LOSS; // every move walked into a win for them
                    worklist.push_back(static_cast<uint32_t>(slot));
                }
            }
        }

        const uint64_t peak = state.capacity() + counter.capacity() +
                              static_cast<uint64_t>(worklist.capacity()) * sizeof(uint32_t);

        // Pass 2: backward induction inside the slice.
        while (!worklist.empty())
        {
            const uint32_t slot = worklist.back();
            worklist.pop_back();

            const bool winning = state[slot] == STATE_WIN;
            const bool whiteToMove = (slot % 2) == 0;
            const unsigned long long board = entry.slice.decode(slot / 2);

            forEachPredecessor(
                board, whiteToMove, entry.slice,
                [&](unsigned long long predecessorIndex)
                {
                    const unsigned long long predecessorSlot =
                        2 * predecessorIndex + (whiteToMove ? 1 : 0);
                    if (state[predecessorSlot] != STATE_UNRESOLVED)
                    {
                        return;
                    }
                    if (!winning)
                    {
                        // The mover here is lost, so whoever moved into it has a win.
                        state[predecessorSlot] = STATE_WIN;
                        worklist.push_back(static_cast<uint32_t>(predecessorSlot));
                    }
                    else if (counter[predecessorSlot] > 0 && --counter[predecessorSlot] == 0)
                    {
                        state[predecessorSlot] = STATE_LOSS;
                        worklist.push_back(static_cast<uint32_t>(predecessorSlot));
                    }
                },
                encodeFailures);
        }

        // Whatever survived the fixpoint unlabelled is a draw: neither side can force a
        // result, so the game goes on forever.
        entry.wdl.assign(slots);
        for (unsigned long long slot = 0; slot < slots; ++slot)
        {
            switch (state[slot])
            {
            case STATE_WIN:
                entry.wdl.set(slot, PackedWdl::CODE_WIN);
                break;
            case STATE_LOSS:
                entry.wdl.set(slot, PackedWdl::CODE_LOSS);
                break;
            case STATE_ILLEGAL:
                entry.wdl.set(slot, PackedWdl::CODE_ILLEGAL);
                break;
            default:
                entry.wdl.set(slot, PackedWdl::CODE_DRAW);
                state[slot] = STATE_DRAW;
                break;
            }
        }
        return peak;
    }

    /*
    Win/draw/loss with exact distance to mate. Positions must commit in ascending
    distance for the distances to come out minimal, which is what the priority queue
    buys; the cost is two bytes per position instead of two bits.
    */
    uint64_t solveSliceDtm(SolvedSlice &entry,
                           const SliceKey &key,
                           std::vector<uint8_t> &state,
                           unsigned long long &encodeFailures)
    {
        const unsigned long long n = entry.slice.size();
        const unsigned long long slots = 2 * n;

        entry.dtm.assign(slots, VALUE_UNRESOLVED);
        std::vector<uint8_t> counter(slots, 0);
        std::vector<Value> lossValue(slots, VALUE_UNRESOLVED);

        struct Pending
        {
            int32_t distance;
            unsigned long long slot;
            Value value;
            bool operator>(const Pending &other) const { return distance > other.distance; }
        };
        std::priority_queue<Pending, std::vector<Pending>, std::greater<Pending>> queue;
        const auto propose = [&](unsigned long long slot, Value value)
        { queue.push({distanceToMate(value), slot, value}); };

        std::array<SuccessorTarget, 128> successors{};
        unsigned char moves[MAX_MOVES];

        for (unsigned long long index = 0; index < n; ++index)
        {
            const unsigned long long board = entry.slice.decode(index);
            for (unsigned int side = 0; side < 2; ++side)
            {
                const bool whiteToMove = (side == 0);
                const unsigned long long slot = 2 * index + side;

                if (isInCheck(board, !whiteToMove))
                {
                    entry.dtm[slot] = VALUE_ILLEGAL;
                    continue;
                }

                const unsigned int count = generateMoves(board, whiteToMove, moves);
                if (count == 0)
                {
                    if (isInCheck(board, whiteToMove))
                    {
                        // Queued rather than committed so predecessors still see it in
                        // distance order.
                        propose(slot, static_cast<Value>(-1));
                    }
                    else
                    {
                        entry.dtm[slot] = VALUE_DRAW;
                    }
                    continue;
                }

                counter[slot] = static_cast<uint8_t>(count);

                for (unsigned int i = 0; i < count; ++i)
                {
                    const unsigned int start = moves[i] >> 4, end = moves[i] & 15u;
                    const unsigned int moved = getNthNibble(board, start);
                    const unsigned int captured = getNthNibble(board, end);
                    if (!isPawn(moved) && isEmpty(captured))
                    {
                        continue;
                    }

                    const SuccessorTarget &target =
                        resolveSuccessor(successors, key, moved, captured, end);
                    const Value successor =
                        successorValue(target, applyMoveToBoard(board, moves[i]), !whiteToMove);
                    if (!isPlayable(successor))
                    {
                        ++encodeFailures;
                        continue;
                    }

                    const Value ours = negateValue(successor, true);
                    if (successor < 0)
                    {
                        propose(slot, ours);
                    }
                    else if (successor > 0)
                    {
                        lossValue[slot] = std::min(lossValue[slot], ours);
                        if (--counter[slot] == 0)
                        {
                            propose(slot, lossValue[slot]);
                        }
                    }
                }
            }
        }

        const uint64_t peak = entry.dtm.capacity() * sizeof(Value) + counter.capacity() +
                              lossValue.capacity() * sizeof(Value) +
                              queue.size() * sizeof(Pending);

        while (!queue.empty())
        {
            const Pending pending = queue.top();
            queue.pop();
            if (isResolved(entry.dtm[pending.slot]))
            {
                continue; // a shorter distance already claimed this position
            }
            entry.dtm[pending.slot] = pending.value;

            const bool whiteToMove = (pending.slot % 2) == 0;
            const unsigned long long board = entry.slice.decode(pending.slot / 2);

            forEachPredecessor(
                board, whiteToMove, entry.slice,
                [&](unsigned long long predecessorIndex)
                {
                    const unsigned long long predecessorSlot =
                        2 * predecessorIndex + (whiteToMove ? 1 : 0);
                    if (isResolved(entry.dtm[predecessorSlot]))
                    {
                        return;
                    }
                    if (pending.value < 0)
                    {
                        propose(predecessorSlot, negateValue(pending.value, true));
                    }
                    else
                    {
                        const Value ours = negateValue(pending.value, true);
                        lossValue[predecessorSlot] = std::min(lossValue[predecessorSlot], ours);
                        if (counter[predecessorSlot] > 0 && --counter[predecessorSlot] == 0)
                        {
                            propose(predecessorSlot, lossValue[predecessorSlot]);
                        }
                    }
                },
                encodeFailures);
        }

        state.assign(slots, STATE_UNRESOLVED);
        for (unsigned long long slot = 0; slot < slots; ++slot)
        {
            if (entry.dtm[slot] == VALUE_UNRESOLVED)
            {
                entry.dtm[slot] = VALUE_DRAW;
            }
            const Value value = entry.dtm[slot];
            state[slot] = (value == VALUE_ILLEGAL) ? STATE_ILLEGAL
                          : (value > 0)            ? STATE_WIN
                          : (value < 0)            ? STATE_LOSS
                                                   : STATE_DRAW;
        }
        return peak;
    }
};

} // namespace retro

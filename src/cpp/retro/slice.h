#pragma once

#include "position.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

/*
Slice decomposition and indexing for retrograde analysis.

WHY SLICES

Forward search from the start position cannot terminate: the game runs for thousands of
plies and alpha-beta at any reachable depth is still guessing at the horizon. Retrograde
analysis inverts the problem — start from the positions whose value is known by
definition (mate, stalemate) and work backwards — but that only works on a state space
you can close over.

The closure comes from the fact that material only ever decreases and pawns only ever
advance. Partition every position by the key

    (material signature, white pawn square, black pawn square)

and every legal move either stays inside its own slice (a quiet non-pawn move) or lands
in a strictly later one (a capture drops a piece; a pawn move advances a pawn; a pawn
capture does both). So the slices form a DAG. Solve them in reverse topological order and
each slice only ever needs values it already has.

WHY THE INDEX LOOKS LIKE THIS

A slice needs a dense bijection with [0, size). The naive one — rank each piece among the
free squares — makes the full-material slices about P(14,10) = 3.6e9 entries each, which
is hopeless once multiplied across slices. Three invariants of this game shrink it
enormously, and the index is built around them rather than checking them afterwards:

  1. ORDER. None of K, R, P, p, r, k can jump or hop, so on a single row none of them can
     ever pass another. Their left-to-right order is fixed for the entire game:
         WK < WR < WP < BP < BR < BK
     (over whichever of them are still on the board). This is by far the biggest saving,
     and it is why the whole game fits in a few GiB instead of hundreds.
  2. BISHOP PARITY. A bishop moves along every other square, so it never leaves the
     colour it started on. The white bishop began on square 3 and lives on odd squares;
     the black bishop began on 12 and lives on even ones. They can never contest a square.
  3. KINGS. Kings can never be adjacent, so they can never pass each other either; WK < BK
     with a gap is already implied by (1).

The index is therefore two-level. The outer level enumerates "chain configurations" —
placements of the order-constrained pieces — and the inner level is a mixed-radix number
over the remaining pieces, whose radices depend on which squares the chain left free. A
prefix sum over chain configurations glues them into one dense range with no gaps.

WHAT THIS DOES NOT MODEL

The solver over these slices computes win/draw/loss ignoring the fifty-move rule. Draws
by repetition come out correctly and for free, because a position that is neither a win
nor a loss at the fixpoint is exactly one that can be held forever. The fifty-move rule
can only turn some of those wins into draws; capturing it exactly needs distance-to-zero
rather than distance-to-mate, which the DAG structure supports but this does not yet do.
*/
namespace retro
{

// The ten pieces that may or may not be on the board. Kings are always present.
enum Material : unsigned int
{
    MAT_WQ = 1u << 0,
    MAT_WR = 1u << 1,
    MAT_WB = 1u << 2,
    MAT_WN = 1u << 3,
    MAT_WP = 1u << 4,
    MAT_BQ = 1u << 5,
    MAT_BR = 1u << 6,
    MAT_BB = 1u << 7,
    MAT_BN = 1u << 8,
    MAT_BP = 1u << 9,
    MAT_COUNT = 1u << 10,
};

// Bishops never leave the colour they started on: white's began on square 3, black's on 12.
inline constexpr unsigned int WHITE_BISHOP_PARITY = 1;
inline constexpr unsigned int BLACK_BISHOP_PARITY = 0;

// Pawn travel limits. Neither pawn can pass the enemy king, so neither can ever promote.
inline constexpr unsigned int WHITE_PAWN_MIN = 5, WHITE_PAWN_MAX = 14;
inline constexpr unsigned int BLACK_PAWN_MIN = 1, BLACK_PAWN_MAX = 10;

inline unsigned int materialPieceNibble(unsigned int bit)
{
    switch (bit)
    {
    case MAT_WQ:
        return PIECE_QUEEN;
    case MAT_WR:
        return PIECE_ROOK;
    case MAT_WB:
        return PIECE_BISHOP;
    case MAT_WN:
        return PIECE_KNIGHT;
    case MAT_WP:
        return PIECE_PAWN;
    case MAT_BQ:
        return PIECE_QUEEN | COLOUR_BLACK;
    case MAT_BR:
        return PIECE_ROOK | COLOUR_BLACK;
    case MAT_BB:
        return PIECE_BISHOP | COLOUR_BLACK;
    case MAT_BN:
        return PIECE_KNIGHT | COLOUR_BLACK;
    case MAT_BP:
        return PIECE_PAWN | COLOUR_BLACK;
    default:
        return 0;
    }
}

/*
The material bit a piece nibble accounts for; the inverse of materialPieceNibble. Kings
are not optional, so they map to nothing.
*/
inline unsigned int materialBitOf(unsigned int nibble)
{
    switch (nibble)
    {
    case PIECE_QUEEN:
        return MAT_WQ;
    case PIECE_ROOK:
        return MAT_WR;
    case PIECE_BISHOP:
        return MAT_WB;
    case PIECE_KNIGHT:
        return MAT_WN;
    case PIECE_PAWN:
        return MAT_WP;
    case PIECE_QUEEN | COLOUR_BLACK:
        return MAT_BQ;
    case PIECE_ROOK | COLOUR_BLACK:
        return MAT_BR;
    case PIECE_BISHOP | COLOUR_BLACK:
        return MAT_BB;
    case PIECE_KNIGHT | COLOUR_BLACK:
        return MAT_BN;
    case PIECE_PAWN | COLOUR_BLACK:
        return MAT_BP;
    default:
        return 0;
    }
}

/*
Rank of square `s` among the set squares of `mask`, counting from square 0 upwards.
*/
inline unsigned int rankInMask(unsigned int mask, unsigned int s)
{
    return static_cast<unsigned int>(std::popcount(mask >> (attacks::squareToBit(s) + 1)));
}

/*
The square holding the `r`-th set bit of `mask`, counting from square 0 upwards.
*/
inline unsigned int selectInMask(unsigned int mask, unsigned int r)
{
    for (unsigned int s = 0; s < BOARD_SIZE; ++s)
    {
        if (mask & attacks::squareBit(s))
        {
            if (r == 0)
            {
                return s;
            }
            --r;
        }
    }
    return NO_SQUARE;
}

inline unsigned int oddSquares(unsigned int mask)
{
    unsigned int count = 0;
    for (unsigned int s = 1; s < BOARD_SIZE; s += 2)
    {
        count += (mask & attacks::squareBit(s)) ? 1 : 0;
    }
    return count;
}

inline unsigned int parityMaskOf(unsigned int mask, unsigned int parity)
{
    unsigned int out = 0;
    for (unsigned int s = parity; s < BOARD_SIZE; s += 2)
    {
        out |= mask & attacks::squareBit(s);
    }
    return out;
}

/*
Identifies one slice of the state space.
*/
struct SliceKey
{
    unsigned int material = 0;
    unsigned char whitePawn = static_cast<unsigned char>(NO_SQUARE);
    unsigned char blackPawn = static_cast<unsigned char>(NO_SQUARE);

    unsigned int pieceCount() const
    {
        return 2 + static_cast<unsigned int>(std::popcount(material));
    }

    // Packs into 20 bits, for use as a map key.
    uint32_t packed() const
    {
        return material | (static_cast<uint32_t>(whitePawn) << 10) |
               (static_cast<uint32_t>(blackPawn) << 15);
    }

    bool operator==(const SliceKey &other) const { return packed() == other.packed(); }

    /*
    Reverse topological rank. Every irreversible move — the only kind that leaves a slice
    — either removes a piece or advances a pawn, so a successor slice always sorts
    strictly earlier under this ordering. Solve in ascending order and every slice finds
    its out-of-slice successors already solved.
    */
    uint32_t solveRank() const
    {
        const unsigned int advance =
            ((whitePawn == NO_SQUARE) ? 0u : (whitePawn - WHITE_PAWN_MIN)) +
            ((blackPawn == NO_SQUARE) ? 0u : (BLACK_PAWN_MAX - blackPawn));
        return (pieceCount() << 8) | (31u - advance);
    }
};

/*
The slice key of the mirrored position: colours swap and squares reverse.

Mirroring maps the state space onto itself, so only one slice of each mirror pair has to
be solved — the other is a board reflection away. Only 64 of the 8,960 non-empty slices
are their own mirror, so this removes 47% of both the work and the storage.
*/
inline SliceKey mirrorSliceKey(const SliceKey &key)
{
    SliceKey mirrored;
    const struct
    {
        unsigned int white, black;
    } pairs[] = {{MAT_WQ, MAT_BQ}, {MAT_WR, MAT_BR}, {MAT_WB, MAT_BB}, {MAT_WN, MAT_BN},
                 {MAT_WP, MAT_BP}};
    for (const auto &pair : pairs)
    {
        if (key.material & pair.white)
        {
            mirrored.material |= pair.black;
        }
        if (key.material & pair.black)
        {
            mirrored.material |= pair.white;
        }
    }
    mirrored.whitePawn = (key.blackPawn == NO_SQUARE)
                             ? static_cast<unsigned char>(NO_SQUARE)
                             : static_cast<unsigned char>(BOARD_SIZE - 1 - key.blackPawn);
    mirrored.blackPawn = (key.whitePawn == NO_SQUARE)
                             ? static_cast<unsigned char>(NO_SQUARE)
                             : static_cast<unsigned char>(BOARD_SIZE - 1 - key.whitePawn);
    return mirrored;
}

/*
One slice of each mirror pair is nominated as the one that gets solved. The choice is
arbitrary as long as it is stable; a slice that is its own mirror is always canonical.
*/
inline bool isCanonicalSlice(const SliceKey &key)
{
    return key.packed() <= mirrorSliceKey(key).packed();
}

/*
The slice a board belongs to.
*/
inline SliceKey sliceKeyOf(unsigned long long board)
{
    SliceKey key;
    for (unsigned int pieces = getOccupancy(board); pieces; pieces &= pieces - 1)
    {
        const unsigned int square = attacks::lowestSquare(pieces);
        switch (getNthNibble(board, square))
        {
        case PIECE_QUEEN:
            key.material |= MAT_WQ;
            break;
        case PIECE_ROOK:
            key.material |= MAT_WR;
            break;
        case PIECE_BISHOP:
            key.material |= MAT_WB;
            break;
        case PIECE_KNIGHT:
            key.material |= MAT_WN;
            break;
        case PIECE_PAWN:
            key.material |= MAT_WP;
            key.whitePawn = static_cast<unsigned char>(square);
            break;
        case PIECE_QUEEN | COLOUR_BLACK:
            key.material |= MAT_BQ;
            break;
        case PIECE_ROOK | COLOUR_BLACK:
            key.material |= MAT_BR;
            break;
        case PIECE_BISHOP | COLOUR_BLACK:
            key.material |= MAT_BB;
            break;
        case PIECE_KNIGHT | COLOUR_BLACK:
            key.material |= MAT_BN;
            break;
        case PIECE_PAWN | COLOUR_BLACK:
            key.material |= MAT_BP;
            key.blackPawn = static_cast<unsigned char>(square);
            break;
        default:
            break;
        }
    }
    return key;
}

/*
A slice, together with the bijection between its positions and [0, size).

Pieces are laid down in a fixed order:

  - the chain (WK, WR, BR, BK, with the pawns pinned by the slice key) is enumerated
    whole, because its members constrain each other;
  - then the bishops, each ranked among the free squares of its own colour;
  - then the queens and knights, each ranked among whatever squares are still free.

`size()` counts placements, not positions: each placement is two positions, one per side
to move. The solver indexes those as 2 * placementIndex + sideToMove.
*/
class Slice
{
public:
    static constexpr unsigned long long INVALID = ~0ull;

    bool build(SliceKey sliceKey)
    {
        key = sliceKey;
        chains.clear();
        prefix.clear();
        packedChains.clear();
        totalSize = 0;

        // Which pieces are placed after the chain, in placement order. Bishops first so
        // their parity-restricted radices are taken against the whole free set.
        placement.clear();
        for (const unsigned int bit : {MAT_WB, MAT_BB, MAT_WQ, MAT_WN, MAT_BQ, MAT_BN})
        {
            if (key.material & bit)
            {
                placement.push_back(bit);
            }
        }

        // The chain, left to right, with pawns pinned to the squares the key names.
        chainSlots.clear();
        chainSlots.push_back({PIECE_KING, NO_SQUARE});
        if (key.material & MAT_WR)
        {
            chainSlots.push_back({PIECE_ROOK, NO_SQUARE});
        }
        if (key.material & MAT_WP)
        {
            chainSlots.push_back({PIECE_PAWN, key.whitePawn});
        }
        if (key.material & MAT_BP)
        {
            chainSlots.push_back({PIECE_PAWN | COLOUR_BLACK, key.blackPawn});
        }
        if (key.material & MAT_BR)
        {
            chainSlots.push_back({PIECE_ROOK | COLOUR_BLACK, NO_SQUARE});
        }
        chainSlots.push_back({PIECE_KING | COLOUR_BLACK, NO_SQUARE});

        // Consistency: a pawn named by the key must actually be able to stand there.
        if ((key.material & MAT_WP) &&
            (key.whitePawn < WHITE_PAWN_MIN || key.whitePawn > WHITE_PAWN_MAX))
        {
            return false;
        }
        if ((key.material & MAT_BP) &&
            (key.blackPawn < BLACK_PAWN_MIN || key.blackPawn > BLACK_PAWN_MAX))
        {
            return false;
        }

        std::vector<unsigned char> assignment(chainSlots.size(), 0);
        enumerateChain(0, 0, assignment);

        prefix.reserve(chains.size() + 1);
        unsigned long long running = 0;
        for (const ChainConfig &config : chains)
        {
            prefix.push_back(running);
            running += config.subSize;
        }
        prefix.push_back(running);
        totalSize = running;
        return totalSize > 0;
    }

    const SliceKey &sliceKey() const { return key; }
    unsigned long long size() const { return totalSize; }
    std::size_t chainCount() const { return chains.size(); }

    /*
    Board for a placement index. The index must be < size().
    */
    unsigned long long decode(unsigned long long index) const
    {
        const std::size_t chain = chainOfIndex(index);
        const ChainConfig &config = chains[chain];
        unsigned long long sub = index - prefix[chain];

        unsigned long long board = config.board;
        unsigned int free = config.freeMask;

        for (std::size_t i = 0; i < placement.size(); ++i)
        {
            const unsigned int radix = config.radix[i];
            const unsigned int digit = static_cast<unsigned int>(sub % radix);
            sub /= radix;

            const unsigned int bit = placement[i];
            const unsigned int candidates = maskForPlacement(bit, free);
            const unsigned int square = selectInMask(candidates, digit);
            board = insertNthNibble(board, materialPieceNibble(bit), square);
            free &= ~attacks::squareBit(square);
        }
        return board;
    }

    /*
    Placement index for a board, or INVALID if the board is not in this slice.
    */
    unsigned long long encode(unsigned long long board) const
    {
        unsigned char squares[4];
        unsigned int count = 0;
        for (const ChainSlot &slot : chainSlots)
        {
            if (slot.fixed != NO_SQUARE)
            {
                continue;
            }
            const unsigned int square = findNibble(board, slot.piece);
            if (square == NO_SQUARE)
            {
                return INVALID;
            }
            squares[count++] = static_cast<unsigned char>(square);
        }

        const std::size_t chain = chainOfPacked(packSquares(squares, count));
        if (chain == chains.size())
        {
            return INVALID;
        }
        const ChainConfig &config = chains[chain];

        unsigned long long sub = 0, multiplier = 1;
        unsigned int free = config.freeMask;
        for (std::size_t i = 0; i < placement.size(); ++i)
        {
            const unsigned int bit = placement[i];
            const unsigned int square = findNibble(board, materialPieceNibble(bit));
            if (square == NO_SQUARE)
            {
                return INVALID;
            }
            const unsigned int candidates = maskForPlacement(bit, free);
            if (!(candidates & attacks::squareBit(square)))
            {
                return INVALID;
            }
            sub += multiplier * rankInMask(candidates, square);
            multiplier *= config.radix[i];
            free &= ~attacks::squareBit(square);
        }
        return prefix[chain] + sub;
    }

private:
    struct ChainSlot
    {
        unsigned int piece;
        unsigned int fixed; // NO_SQUARE when the square is chosen rather than pinned
    };

    struct ChainConfig
    {
        unsigned long long board = 0; // kings, rooks and pawns already placed
        unsigned int freeMask = 0;
        unsigned int radix[6] = {0, 0, 0, 0, 0, 0};
        unsigned long long subSize = 0;
    };

    SliceKey key;
    std::vector<ChainSlot> chainSlots;
    std::vector<unsigned int> placement;
    std::vector<ChainConfig> chains;
    std::vector<unsigned long long> prefix;
    std::vector<uint16_t> packedChains; // sorted, parallel to `chains`
    unsigned long long totalSize = 0;

    static uint16_t packSquares(const unsigned char *squares, unsigned int count)
    {
        uint16_t packed = 0;
        for (unsigned int i = 0; i < count; ++i)
        {
            packed = static_cast<uint16_t>((packed << 4) | squares[i]);
        }
        return packed;
    }

    unsigned int maskForPlacement(unsigned int bit, unsigned int free) const
    {
        if (bit == MAT_WB)
        {
            return parityMaskOf(free, WHITE_BISHOP_PARITY);
        }
        if (bit == MAT_BB)
        {
            return parityMaskOf(free, BLACK_BISHOP_PARITY);
        }
        return free;
    }

    std::size_t chainOfIndex(unsigned long long index) const
    {
        const auto it = std::upper_bound(prefix.begin(), prefix.end(), index);
        return static_cast<std::size_t>(it - prefix.begin()) - 1;
    }

    std::size_t chainOfPacked(uint16_t packed) const
    {
        const auto it = std::lower_bound(packedChains.begin(), packedChains.end(), packed);
        if (it == packedChains.end() || *it != packed)
        {
            return chains.size();
        }
        return static_cast<std::size_t>(it - packedChains.begin());
    }

    /*
    Walk the chain left to right assigning strictly increasing squares. Pinned pawns must
    fall in place naturally, which is what enforces "no piece ever passes another".
    */
    void enumerateChain(std::size_t slot, unsigned int minSquare, std::vector<unsigned char> &assignment)
    {
        if (slot == chainSlots.size())
        {
            addChain(assignment);
            return;
        }

        const ChainSlot &current = chainSlots[slot];
        if (current.fixed != NO_SQUARE)
        {
            if (current.fixed < minSquare)
            {
                return; // pinned pawn would have to sit left of a piece it can never pass
            }
            assignment[slot] = static_cast<unsigned char>(current.fixed);
            enumerateChain(slot + 1, current.fixed + 1, assignment);
            return;
        }

        // Leave room for the slots still to come.
        const unsigned int remaining = static_cast<unsigned int>(chainSlots.size() - slot - 1);
        for (unsigned int square = minSquare; square + remaining < BOARD_SIZE; ++square)
        {
            assignment[slot] = static_cast<unsigned char>(square);
            enumerateChain(slot + 1, square + 1, assignment);
        }
    }

    void addChain(const std::vector<unsigned char> &assignment)
    {
        // Kings are the outermost chain members and can never be adjacent.
        const unsigned int whiteKing = assignment.front();
        const unsigned int blackKing = assignment.back();
        if (blackKing < whiteKing + 2)
        {
            return;
        }

        ChainConfig config;
        config.freeMask = 0xFFFFu;
        unsigned char freeChainSquares[4];
        unsigned int freeChainCount = 0;

        for (std::size_t i = 0; i < chainSlots.size(); ++i)
        {
            const unsigned int square = assignment[i];
            config.board = insertNthNibble(config.board, chainSlots[i].piece, square);
            config.freeMask &= ~attacks::squareBit(square);
            if (chainSlots[i].fixed == NO_SQUARE)
            {
                freeChainSquares[freeChainCount++] = static_cast<unsigned char>(square);
            }
        }

        // Radices for the pieces placed after the chain, in placement order.
        unsigned int remainingFree = static_cast<unsigned int>(std::popcount(config.freeMask));
        config.subSize = 1;
        for (std::size_t i = 0; i < placement.size(); ++i)
        {
            const unsigned int bit = placement[i];
            unsigned int radix;
            if (bit == MAT_WB)
            {
                radix = oddSquares(config.freeMask);
            }
            else if (bit == MAT_BB)
            {
                radix = static_cast<unsigned int>(std::popcount(config.freeMask)) -
                        oddSquares(config.freeMask);
            }
            else
            {
                radix = remainingFree;
            }
            if (radix == 0)
            {
                config.subSize = 0;
                break;
            }
            config.radix[i] = radix;
            config.subSize *= radix;
            --remainingFree;
        }

        if (config.subSize == 0)
        {
            return;
        }

        chains.push_back(config);
        packedChains.push_back(packSquares(freeChainSquares, freeChainCount));
    }
};

/*
Every slice of the state space, in an order safe to solve in.
*/
inline std::vector<SliceKey> allSliceKeys()
{
    std::vector<SliceKey> keys;
    for (unsigned int material = 0; material < MAT_COUNT; ++material)
    {
        const bool hasWhitePawn = material & MAT_WP;
        const bool hasBlackPawn = material & MAT_BP;

        for (unsigned int wp = WHITE_PAWN_MIN; wp <= WHITE_PAWN_MAX; ++wp)
        {
            if (!hasWhitePawn && wp != WHITE_PAWN_MIN)
            {
                break;
            }
            for (unsigned int bp = BLACK_PAWN_MIN; bp <= BLACK_PAWN_MAX; ++bp)
            {
                if (!hasBlackPawn && bp != BLACK_PAWN_MIN)
                {
                    break;
                }
                SliceKey key;
                key.material = material;
                key.whitePawn = hasWhitePawn ? static_cast<unsigned char>(wp)
                                             : static_cast<unsigned char>(NO_SQUARE);
                key.blackPawn = hasBlackPawn ? static_cast<unsigned char>(bp)
                                             : static_cast<unsigned char>(NO_SQUARE);
                keys.push_back(key);
            }
        }
    }

    std::stable_sort(keys.begin(), keys.end(),
                     [](const SliceKey &a, const SliceKey &b)
                     { return a.solveRank() < b.solveRank(); });
    return keys;
}

} // namespace retro

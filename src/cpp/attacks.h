#pragma once

#include <array>
#include <bit>

/*
Attack generation for the one-dimensional, 16-square board.

This replaces the 42 MB `mapping.txt` lookup table that used to be read from disk at
startup. Every attack set here is either a 16-entry compile-time table (pawn, knight,
king) or a handful of bit operations (bishop, rook, queen), so the whole thing lives in
L1 instead of spraying random accesses across 42 MB of heap.

Square and mask conventions match the rest of the engine:

  - A square `s` is the s-th nibble from the LEFT of the board word, s = 0..15.
  - A square set is a 16-bit mask in which square `s` is bit (15 - s). So bit 15 is the
    leftmost square and bit 0 is the rightmost. Moving right along the board means
    moving toward the least significant bit.

`rayAttacks` is shared by rooks and bishops. A rook slides over every square; a bishop
slides over every other square, which in mask space is just the bits with the same
parity as its own. Restricting the occupancy to that parity is exactly the "ignoring any
and all pieces on [the other colour] squares" rule, so one routine covers both.
*/
namespace attacks
{

inline constexpr unsigned int BOARD_SIZE = 16;

// Every square, used as the "ray" for rook-like movement.
inline constexpr unsigned int FULL_RAY = 0xFFFFu;

// Every other square, used as the ray for bishop-like movement. Indexed by bit parity,
// which is the same as square parity because the two differ by the odd constant 15.
inline constexpr unsigned int PARITY_RAY[2] = {0x5555u, 0xAAAAu};

// Mask of the single square `s`.
inline constexpr unsigned int squareBit(unsigned int s)
{
    return 1u << (BOARD_SIZE - 1 - s);
}

// Bit position of square `s`, and the inverse.
inline constexpr unsigned int squareToBit(unsigned int s) { return BOARD_SIZE - 1 - s; }
inline constexpr unsigned int bitToSquare(unsigned int b) { return BOARD_SIZE - 1 - b; }

// Square index of the lowest set bit of a non-empty mask; i.e. its rightmost square.
inline unsigned int lowestSquare(unsigned int mask)
{
    return bitToSquare(static_cast<unsigned int>(std::countr_zero(mask)));
}

/*
Squares reachable by a sliding piece at bit position `bit`, given `occupancy` and a
`ray` restricting which squares the piece travels over. Blockers stop the slide but are
included in the result, since they may be captured; the caller masks out its own pieces.
*/
inline unsigned int rayAttacks(unsigned int bit, unsigned int occupancy, unsigned int ray)
{
    occupancy &= ray;

    const unsigned int above = FULL_RAY & ~((1u << (bit + 1)) - 1); // squares left of `bit`
    const unsigned int below = (1u << bit) - 1;                     // squares right of `bit`

    // Nearest blocker on each side, defaulting to the edge when the ray is clear.
    const unsigned int blockersAbove = occupancy & above;
    const unsigned int blockersBelow = occupancy & below;
    const unsigned int nearestAbove = blockersAbove
                                          ? static_cast<unsigned int>(std::countr_zero(blockersAbove))
                                          : BOARD_SIZE - 1;
    const unsigned int nearestBelow = blockersBelow
                                          ? 31u - static_cast<unsigned int>(std::countl_zero(blockersBelow))
                                          : 0u;

    const unsigned int reachAbove = ((1u << (nearestAbove + 1)) - 1) & above;
    const unsigned int reachBelow = ~((1u << nearestBelow) - 1) & below;
    return (reachAbove | reachBelow) & ray;
}

/*
Compile-time table for a piece that jumps to a fixed set of offsets.
*/
template <int... Deltas>
constexpr std::array<unsigned int, BOARD_SIZE> makeLeaperTable()
{
    std::array<unsigned int, BOARD_SIZE> table{};
    for (unsigned int s = 0; s < BOARD_SIZE; ++s)
    {
        for (const int delta : {Deltas...})
        {
            const int target = static_cast<int>(s) + delta;
            if (target >= 0 && target < static_cast<int>(BOARD_SIZE))
            {
                table[s] |= squareBit(static_cast<unsigned int>(target));
            }
        }
    }
    return table;
}

// Knights jump 2 or 3 squares in either direction, over anything in the way.
inline constexpr auto KNIGHT_ATTACKS = makeLeaperTable<-3, -2, 2, 3>();

// Kings move one square in either direction.
inline constexpr auto KING_ATTACKS = makeLeaperTable<-1, 1>();

// Pawns move (and capture) one square forward only. White advances toward square 15.
inline constexpr auto WHITE_PAWN_ATTACKS = makeLeaperTable<1>();
inline constexpr auto BLACK_PAWN_ATTACKS = makeLeaperTable<-1>();

// The squares a pawn of the given colour could be standing on to attack square `s`.
// A white pawn attacks from the left, so its origins are the black pawn's targets.
inline constexpr auto WHITE_PAWN_ORIGINS = BLACK_PAWN_ATTACKS;
inline constexpr auto BLACK_PAWN_ORIGINS = WHITE_PAWN_ATTACKS;

inline unsigned int bishopAttacks(unsigned int square, unsigned int occupancy)
{
    const unsigned int bit = squareToBit(square);
    return rayAttacks(bit, occupancy, PARITY_RAY[bit & 1]);
}

inline unsigned int rookAttacks(unsigned int square, unsigned int occupancy)
{
    return rayAttacks(squareToBit(square), occupancy, FULL_RAY);
}

inline unsigned int queenAttacks(unsigned int square, unsigned int occupancy)
{
    const unsigned int bit = squareToBit(square);
    return rayAttacks(bit, occupancy, FULL_RAY) | rayAttacks(bit, occupancy, PARITY_RAY[bit & 1]);
}

/*
Squares attacked by the piece whose nibble is `piece`, standing on `square`. Pawn pushes
count as attacks here because in one dimension a pawn captures onto the very square it
would advance to, so movement and attack coincide. Empty and unused nibbles yield 0.
*/
inline unsigned int pieceAttacks(unsigned int piece, unsigned int square, unsigned int occupancy)
{
    switch (piece & 7u)
    {
    case 1: // pawn; bit 3 is the colour.
        return (piece & 8u) ? BLACK_PAWN_ATTACKS[square] : WHITE_PAWN_ATTACKS[square];
    case 2:
        return KNIGHT_ATTACKS[square];
    case 3:
        return KING_ATTACKS[square];
    case 5:
        return bishopAttacks(square, occupancy);
    case 6:
        return rookAttacks(square, occupancy);
    case 7:
        return queenAttacks(square, occupancy);
    default:
        return 0;
    }
}

} // namespace attacks

#pragma once

#include "attacks.h"

#include <array>
#include <bit>
#include <bitset>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using std::string;
using std::tuple, std::make_tuple, std::tie;
using std::vector;

/*
Externally, position is expressed as a string in a format similar to FEN (Forsyth-
Edwards Notation). I will call this FENCE notation — short for Forsyth-Edwards Notation
(Calixto Extension).

A "record" contains a particular game position, all in a single text line.

A record contains four fields, separated by a space. The fields are:
  1. Piece placement with white starting on the left. Eeach piece is identified by a
    single letter (identical to FEN); i.e., P = pawn, N = knight, B = bishop, R =
    rook, Q = queen, K = king. White pieces are denoted using uppercase letters and
    black pieces use lowercase. Empty spaces are individually noted using periods
    (unlike FEN notation).
  2. Active color. "w" means white moves next, b means black moves next.
  3. Halfmove clock; i.e., the number of halfmoves since the last capture or pawn
    advance, used for the fifty-move rule.
  4. Fullmove number; i.e., the number of the full move. It starts at 1 and is
    incremented after black's move.

Note that castling and en passant fields, which are in FEN, are excluded due to their
irrelevance. Promotion is also impossible, as a pawn has no way around the opponent
king. Since promotion is impossible, there is a maximum of one of each piece per side.

The start position is "KQRBNP....pnbrqk w 0 1".

Moves are specified as a tuple of (start, end) square. There are 16 squares and we
0-index the board, so squares are 0 through 15 inclusive. There is no special notation
for a capture.


Internally, position is expressed with four different parameters that directly map to
the four different fields of FENCE. We have: board, active, halfmove, fullmove.

The game board is represented by an unsigned long long (64-bit number). Each nibble
represents a space on the board. Pieces are represented as follows:

C    color
 M   multiple moves (can move multiple squares at a time)
  R  rook moves (can move like rook)
   B bishop moves (can move like bishop)
---- ----
0000 empty
0001 pawn (white)
0010 knight (white)
0011 king (white)
0100 UNUSED
0101 bishop (white)
0110 rook (white)
0111 queen (white)
1000 UNUSED
1001 pawn (black)
1010 knight (black)
1011 king (black)
1100 UNUSED
1101 bishop (black)
1110 rook (black)
1111 queen (black)

Note that we attempt a rough logical ordering for the pieces. While not perfect, this
lets us perform some calculations faster than other, more arbitrary orderings.

Active is a boolean that is true if white is to play and false if black is to play.

Halfmove and fullmove are unsigned ints that have the same meaning as in FENCE.

Moves are expressed as a byte of XXXXYYYY, where the XXXX nibble indicates the starting
space and the YYYY nibble indicates the ending space.

Square sets (occupancy, attack sets, ...) are 16-bit masks in which square s is bit
(15 - s); see attacks.h for the rationale.
*/

// Handy constants.
const unsigned int BOARD_SIZE = 16;
const string START_FENCE = "KQRBNP....pnbrqk w 0 1";
const unsigned long long START_BOARD = 3991632928627678971;
const unsigned long long FIRST_NIBBLE_BITMASK = 240; // bitmask to get first nibble (of a byte).
const unsigned long long LAST_NIBBLE_BITMASK = 15;   // bitmask to get last nibble.

// Returned by square-finding helpers when there is nothing to find.
const unsigned int NO_SQUARE = BOARD_SIZE;

// Piece nibbles. OR in COLOUR_BLACK to get the black equivalent.
const unsigned int PIECE_PAWN = 1, PIECE_KNIGHT = 2, PIECE_KING = 3;
const unsigned int PIECE_BISHOP = 5, PIECE_ROOK = 6, PIECE_QUEEN = 7;
const unsigned int COLOUR_BLACK = 8;

// Pawn starting squares, and the two squares each pawn leaps over on its double move.
const unsigned int WHITE_PAWN_START = 5, BLACK_PAWN_START = 10;
const unsigned int WHITE_PAWN_LEAP_PATH = attacks::squareBit(6) | attacks::squareBit(7);
const unsigned int BLACK_PAWN_LEAP_PATH = attacks::squareBit(9) | attacks::squareBit(8);

// No position has more legal moves than this; see generateMoves for the bound.
const unsigned int MAX_MOVES = 64;

/*
Because I can.
*/
void print(auto i)
{
    std::cout << i << std::endl;
}
void printMove(unsigned int m)
{
    std::cout << "(" << (m >> 4) << "," << (m & 15) << ")" << std::endl;
}
void print(vector<unsigned int> v)
{
    for (unsigned int i : v)
    {
        printMove(i);
    }
}

/*
Debug function. Print bitwise representation of a number.
*/
void debugPrint(unsigned long long i)
{
    std::cout << std::bitset<8 * sizeof(i)>(i) << std::endl;
}

/*
Helper function to check valid index.
*/
inline bool indexValid(unsigned int i)
{
    return i < BOARD_SIZE;
}

/*
Helper function to grab the last nibble of an unsigned int.
*/
inline unsigned int getLastNibble(unsigned int i)
{
    return i & LAST_NIBBLE_BITMASK;
}

/*
Helper function to extract the nth nibble of num, assuming the given number is
BOARD_SIZE bits long.
*/
inline unsigned long long getNthNibble(unsigned long long num, unsigned int n)
{
    unsigned int bitshifts = 4 * (BOARD_SIZE - n - 1); // number of bitshifts to do.
    return ((num >> bitshifts) & LAST_NIBBLE_BITMASK);
}

/*
Helper function to blank the nth nibble of num, assuming the given number is BOARD_SIZE
bits long.
*/
inline unsigned long long blankNthNibble(unsigned long long num, unsigned int n)
{
    unsigned int bitshifts = 4 * (BOARD_SIZE - n - 1);                // number of bitshifts to do.
    unsigned long long blanker = ~(LAST_NIBBLE_BITMASK << bitshifts); // all 1s except for nth nibble.
    return (num & blanker);
}

/*
Helper function to insert a nibble as the nth nibble of num, assuming the given number
is BOARD_SIZE bits long.
*/
inline unsigned long long insertNthNibble(unsigned long long num, unsigned long long nibble, unsigned int n)
{
    unsigned int bitshifts = 4 * (BOARD_SIZE - n - 1); // number of bitshifts to do.
    unsigned long long inserter = nibble << bitshifts; // move nibble to correct spot.
    return (blankNthNibble(num, n) | inserter);
}

/*
Collapse a word that has at most the low bit of each nibble set into a 16-bit square
set. Nibble k of the word becomes bit k of the result, which is square (15 - k) — the
same orientation every other mask in the engine uses.
*/
inline unsigned int compactNibbleFlags(unsigned long long x)
{
    x = (x | (x >> 3)) & 0x0303030303030303ULL;
    x = (x | (x >> 6)) & 0x000F000F000F000FULL;
    x = (x | (x >> 12)) & 0x000000FF000000FFULL;
    x = (x | (x >> 24)) & 0xFFFFULL;
    return static_cast<unsigned int>(x);
}

/*
Square set of every square holding the given piece nibble. Branch-free: XOR makes the
matching nibbles zero, then the OR-reduce marks which nibbles were zero.
*/
inline unsigned int getPieceSquares(unsigned long long board, unsigned int piece)
{
    const unsigned long long spread = 0x1111111111111111ULL * piece;
    const unsigned long long diff = board ^ spread;
    const unsigned long long nonzero = diff | (diff >> 1) | (diff >> 2) | (diff >> 3);
    return compactNibbleFlags(~nonzero & 0x1111111111111111ULL);
}

/*
Helper function to find a nibble n in i and return the index of it, assuming the given
number is BOARD_SIZE bits long. Returns NO_SQUARE if the nibble is not found.
*/
inline unsigned int findNibble(unsigned long long num, unsigned long long nibble)
{
    const unsigned int squares = getPieceSquares(num, static_cast<unsigned int>(nibble));
    return squares ? attacks::lowestSquare(squares) : NO_SQUARE;
}

/*
Square the given player's king stands on. Every legal position has both kings, so this
returning NO_SQUARE means the caller built an impossible board.
*/
inline unsigned int findKing(unsigned long long board, bool player)
{
    return findNibble(board, player ? PIECE_KING : (PIECE_KING | COLOUR_BLACK));
}

/*
Given a character representing a piece, return the numerical representation.
*/
unsigned int pieceToBits(char c)
{
    switch (c)
    {
    case '.':
        return 0;
    case 'P':
        return 1;
    case 'N':
        return 2;
    case 'B':
        return 5;
    case 'R':
        return 6;
    case 'Q':
        return 7;
    case 'K':
        return 3;
    case 'p':
        return 9;
    case 'n':
        return 10;
    case 'b':
        return 13;
    case 'r':
        return 14;
    case 'q':
        return 15;
    case 'k':
        return 11;
    default:
        return 0;
    }
}

/*
Given an int representing a piece, return the character representation.
*/
char bitsToPiece(unsigned int i)
{
    switch (i)
    {
    case 0:
        return '.';
    case 1:
        return 'P';
    case 2:
        return 'N';
    case 5:
        return 'B';
    case 6:
        return 'R';
    case 7:
        return 'Q';
    case 3:
        return 'K';
    case 9:
        return 'p';
    case 10:
        return 'n';
    case 13:
        return 'b';
    case 14:
        return 'r';
    case 15:
        return 'q';
    case 11:
        return 'k';
    default:
        return '.';
    }
}

/*
Given an unsigned int representing a piece, return the bitflag for it for piece-set purposes.
This follows the format KQRBNPkqrbnp.
*/
unsigned int bitsToPieceSet(unsigned int i)
{
    switch (i)
    {
    case 0:
        return 0;
    case 1:
        return 64;
    case 2:
        return 128;
    case 5:
        return 256;
    case 6:
        return 512;
    case 7:
        return 1024;
    case 3:
        return 2048;
    case 9:
        return 1;
    case 10:
        return 2;
    case 13:
        return 4;
    case 14:
        return 8;
    case 15:
        return 16;
    case 11:
        return 32;
    default:
        return 0;
    }
}

/*
Check if a given piece set represents a position with insufficient material. Empirically
determined.
*/
bool isInsufficientMaterialPieceSet(unsigned int pieceSet)
{
    return (pieceSet == 2080    // kings only.
            || pieceSet == 2336 // kings and white bishop.
            || pieceSet == 2084 // kings and black bishop.
    );
}

/*
Helper functions to check pieces or piece properties.
*/
inline bool isEmpty(unsigned int nibble)
{
    return nibble == 0;
}
inline bool isPieceOfPlayer(unsigned int nibble, bool player)
{
    return !isEmpty(nibble) && ((nibble >> 3) != player);
}
inline bool isPawn(unsigned int nibble)
{
    return nibble == 1 || nibble == 9;
}
inline bool isKnight(unsigned int nibble)
{
    return nibble == 2 || nibble == 10;
}
inline bool isBishop(unsigned int nibble)
{
    return nibble == 5 || nibble == 13;
}
inline bool isRook(unsigned int nibble)
{
    return nibble == 6 || nibble == 14;
}
inline bool isQueen(unsigned int nibble)
{
    return nibble == 7 || nibble == 15;
}
inline bool isKing(unsigned int nibble)
{
    return nibble == 3 || nibble == 11;
}

/*
Given a FENCE string, transform it into a numerical board representation.
*/
unsigned long long fenceToBoard(string fence)
{
    // Turn board string into unsigned long long.
    unsigned long long board = 0; // clear the board, as we use bitwise operators instead of assignment.
    string::iterator it;
    for (it = fence.begin(); it != fence.end(); it++)
    {
        board = board << 4;                          // leftshift one nibble.
        unsigned int pieceAsBits = pieceToBits(*it); // get numerical representation.
        board |= pieceAsBits;                        // OR operator to add the new piece.
    }
    return board;
}

/*
Given a FENCE string, transform it into a numerical board representation, a move
indicator flag, and halfmove and fullmove counts. Store them in the provided variables.
*/
tuple<unsigned long long, bool, unsigned int, unsigned int> fenceToVars(
    string fence,
    unsigned long long board,
    bool active,
    unsigned int halfmove,
    unsigned int fullmove)
{
    // Temporary storage of variables that need to be operated on.
    string boardString;
    char activeChar;

    // Unpack everything from the string.
    std::stringstream ss;
    ss << fence;
    ss >> boardString >> activeChar >> halfmove >> fullmove;

    // Turn active flag into boolean.
    active = (activeChar == 'w');

    // Turn board string into unsigned long long.
    board = fenceToBoard(boardString);

    return make_tuple(board, active, halfmove, fullmove);
}

/*
Given a numerical board representation, a move indicator flag, and halfmove and fullmove
counts, return a FENCE string. The inverse of bitsToPiece.
*/
string varsToFence(
    unsigned long long board,
    bool active,
    unsigned int halfmove,
    unsigned int fullmove)
{
    // Turn unsigned long long into board string.
    string boardString = "";
    for (unsigned int i = 0; i < BOARD_SIZE; i++)
    {
        unsigned int firstNibble = getNthNibble(board, i); // get last nibble.
        boardString += bitsToPiece(firstNibble);           // store char representation.
    }

    // Turn active flag into string.
    string activeString = active ? "w" : "b";

    return boardString + " " + activeString + " " + std::to_string(halfmove) + " " + std::to_string(fullmove);
}

/*
Given a board, return a set of all pieces present in a given position.
This is given as a 12-bit integer with bitflags in the format:

KQRBNPkqrbnp

so if white had a queen and black had a rook, this would be 110000101000 = 3112.
*/
unsigned int getPieceSet(unsigned long long board)
{
    unsigned int pieceSet = 0;
    for (unsigned int i = 0; i < BOARD_SIZE; i++)
    {
        unsigned int lastNibble = getLastNibble(board); // get last nibble.
        pieceSet |= bitsToPieceSet(lastNibble);         // store pieceset representation.
        board = board >> 4;
    }
    return pieceSet;
}

/*
Get (as a bitflag) occupancy as a 16-bit number.
*/
inline unsigned int getOccupancy(unsigned long long board)
{
    const unsigned long long nonzero = board | (board >> 1) | (board >> 2) | (board >> 3);
    return compactNibbleFlags(nonzero & 0x1111111111111111ULL);
}

/*
Get (as a bitflag) occupancy for black. Bit 3 of a nibble is the colour bit and an empty
square is all zeroes, so the colour bit alone identifies black pieces.
*/
inline unsigned int getBlackOccupancy(unsigned long long board)
{
    return compactNibbleFlags((board >> 3) & 0x1111111111111111ULL);
}

/*
Get (as a bitflag) occupancy, for a given player, as a 16-bit number.
*/
inline unsigned int getPlayerOccupancy(unsigned long long board, bool player)
{
    const unsigned int black = getBlackOccupancy(board);
    return player ? (getOccupancy(board) & ~black) : black;
}

/*
Get (as a bitflag) squares attacked by the given player. Includes squares occupied by
pieces belonging to both players. No piece attacks its own square.
*/
inline unsigned int getAttackedSquares(unsigned long long board, bool player)
{
    const unsigned int occupancy = getOccupancy(board);
    unsigned int attacked = 0;
    for (unsigned int pieces = getPlayerOccupancy(board, player); pieces; pieces &= pieces - 1)
    {
        const unsigned int square = attacks::lowestSquare(pieces);
        attacked |= attacks::pieceAttacks(getNthNibble(board, square), square, occupancy);
    }
    return attacked;
}

/*
Whether `square` is attacked by the given player.

This asks the question from the target square outwards rather than generating every
enemy attack set, which is what makes legality checking cheap: a knight or king attacks
a square from exactly the squares it would attack (both movers are symmetric), a pawn
has one possible origin, and along each sliding ray only the nearest blocker can be the
attacker. That bounds the work at roughly a dozen O(1) nibble reads regardless of how
crowded the board is.
*/
inline bool isSquareAttacked(
    unsigned long long board,
    unsigned int occupancy,
    unsigned int square,
    bool byPlayer)
{
    const unsigned int colour = byPlayer ? 0u : COLOUR_BLACK;

    // Pawns. A white pawn attacks from the left, a black pawn from the right.
    const unsigned int pawnOrigins = byPlayer ? attacks::WHITE_PAWN_ORIGINS[square]
                                              : attacks::BLACK_PAWN_ORIGINS[square];
    if (pawnOrigins && getNthNibble(board, attacks::lowestSquare(pawnOrigins)) == (PIECE_PAWN | colour))
    {
        return true;
    }

    // Knights and kings.
    for (unsigned int set = attacks::KNIGHT_ATTACKS[square]; set; set &= set - 1)
    {
        if (getNthNibble(board, attacks::lowestSquare(set)) == (PIECE_KNIGHT | colour))
        {
            return true;
        }
    }
    for (unsigned int set = attacks::KING_ATTACKS[square]; set; set &= set - 1)
    {
        if (getNthNibble(board, attacks::lowestSquare(set)) == (PIECE_KING | colour))
        {
            return true;
        }
    }

    // Sliders. Intersecting a ray with the occupancy leaves only its blockers, which is
    // at most one square per direction.
    for (unsigned int set = attacks::rookAttacks(square, occupancy) & occupancy; set; set &= set - 1)
    {
        const unsigned int nibble = getNthNibble(board, attacks::lowestSquare(set));
        if (nibble == (PIECE_ROOK | colour) || nibble == (PIECE_QUEEN | colour))
        {
            return true;
        }
    }
    for (unsigned int set = attacks::bishopAttacks(square, occupancy) & occupancy; set; set &= set - 1)
    {
        const unsigned int nibble = getNthNibble(board, attacks::lowestSquare(set));
        if (nibble == (PIECE_BISHOP | colour) || nibble == (PIECE_QUEEN | colour))
        {
            return true;
        }
    }

    return false;
}

/*
Return whether the given player is in check in the given board. Assumes that the
position is valid.
*/
inline bool isInCheck(unsigned long long board, bool player)
{
    const unsigned int king = findKing(board, player);
    if (king == NO_SQUARE)
    {
        return false;
    }
    return isSquareAttacked(board, getOccupancy(board), king, !player);
}

/*
Naively apply a move to the board; i.e., assume the position and move are both valid and
legal. Used when other elements of the position do not matter; i.e. when testing check.
Return the new board.
*/
inline unsigned long long applyMoveToBoard(unsigned long long board, unsigned int move)
{
    // Get indices.
    unsigned int end_index = getLastNibble(move);
    unsigned int start_index = move >> 4; // get first nibble by rightshifting the move.

    // Get nibble at start.
    unsigned int start_nibble = getNthNibble(board, start_index);

    // Replace and return.
    unsigned long long boardBlankStart = blankNthNibble(board, start_index);
    return insertNthNibble(boardBlankStart, start_nibble, end_index);
}

/*
Naively apply a move to the position; i.e., assume the position and move are both valid
and legal. Return a tuple containing elements of the new position.
*/
tuple<unsigned long long, bool, unsigned int, unsigned int> applyMove(
    unsigned long long board,
    bool active,
    unsigned int halfmove,
    unsigned int fullmove,
    unsigned int move)
{
    // Get indices.
    unsigned int end_index = getLastNibble(move);
    unsigned int start_index = move >> 4; // get first nibble by rightshifting the move.

    // Get nibbles.
    unsigned int start_nibble = getNthNibble(board, start_index);
    unsigned int end_nibble = getNthNibble(board, end_index);

    // Update halfmove if move is a capture or pawn move.
    if (isPawn(start_nibble) || !isEmpty(end_nibble))
    {
        halfmove = 0;
    }
    else
    {
        halfmove += 1;
    }

    // Update fullmove.
    if (!active)
    {
        fullmove += 1;
    }

    unsigned long long boardBlankStart = blankNthNibble(board, start_index);
    board = insertNthNibble(boardBlankStart, start_nibble, end_index);
    active = !active;
    return make_tuple(board, active, halfmove, fullmove);
}

/*
Write every legal move for `player` into `moves` and return how many there were.

The caller supplies the buffer, so a search can keep one array per ply and never
allocate. MAX_MOVES is comfortably above the real ceiling: a queen reaches at most 15
squares, a rook 15, a bishop 7, a knight 4, the king 2 and the pawn 2, for 45.
*/
inline unsigned int generateMoves(unsigned long long board, bool player, unsigned char *moves)
{
    const unsigned int occupancy = getOccupancy(board);
    const unsigned int black = getBlackOccupancy(board);
    const unsigned int ownOccupancy = player ? (occupancy & ~black) : black;
    const unsigned int kingSquare = findKing(board, player);

    unsigned int count = 0;
    for (unsigned int pieces = ownOccupancy; pieces; pieces &= pieces - 1)
    {
        const unsigned int start = attacks::lowestSquare(pieces);
        const unsigned int piece = getNthNibble(board, start);
        unsigned int targets = attacks::pieceAttacks(piece, start, occupancy) & ~ownOccupancy;

        // The pawn's double first move. Both squares ahead must be empty, which also
        // enforces that the double move can never capture.
        if (piece == PIECE_PAWN && start == WHITE_PAWN_START && !(occupancy & WHITE_PAWN_LEAP_PATH))
        {
            targets |= attacks::squareBit(WHITE_PAWN_START + 2);
        }
        else if (piece == (PIECE_PAWN | COLOUR_BLACK) && start == BLACK_PAWN_START &&
                 !(occupancy & BLACK_PAWN_LEAP_PATH))
        {
            targets |= attacks::squareBit(BLACK_PAWN_START - 2);
        }

        const unsigned int startBit = attacks::squareBit(start);
        for (; targets; targets &= targets - 1)
        {
            const unsigned int end = attacks::lowestSquare(targets);

            // Legality: the move is legal exactly when it does not leave our own king
            // attacked. Board, occupancy and king square all update in constant time.
            const unsigned long long next = applyMoveToBoard(board, (start << 4) | end);
            const unsigned int nextOccupancy = (occupancy & ~startBit) | attacks::squareBit(end);
            const unsigned int nextKing = (start == kingSquare) ? end : kingSquare;
            if (!isSquareAttacked(next, nextOccupancy, nextKing, !player))
            {
                moves[count++] = static_cast<unsigned char>((start << 4) | end);
            }
        }
    }
    return count;
}

/*
Get a vector of ints representing all legal moves by the given player. Convenience
wrapper over generateMoves for callers that are not in a hot loop.
*/
vector<unsigned int> getMoves(unsigned long long board, bool player)
{
    unsigned char buffer[MAX_MOVES];
    const unsigned int count = generateMoves(board, player, buffer);
    return vector<unsigned int>(buffer, buffer + count);
}

/*
Get a vector of unsigned long longs representing all legal next boards, given a
player to move next.
*/
vector<unsigned long long> getNextBoards(unsigned long long board, bool player)
{
    unsigned char buffer[MAX_MOVES];
    const unsigned int count = generateMoves(board, player, buffer);
    vector<unsigned long long> nextBoards;
    nextBoards.reserve(count);
    for (unsigned int i = 0; i < count; i++)
    {
        nextBoards.push_back(applyMoveToBoard(board, buffer[i]));
    }
    return nextBoards;
}

/*
Check if a position is an ended game, via stalemate or checkmate. Returns an integer as
follows:

C    checkmate flag
 D   draw flag
  R  reasoning flag, 1
   R reasoning flag, 2
---- - ----
0000 0 game still in progress
1001 9 white victory
1000 8 black victory
0101 5 draw, stalemate
0110 6 draw, 50-move rule
0111 7 draw, insufficient material

Important precondition: this function assumes that the given position has been generated
from an actual chess game and ONLY checks if the player to move has been checkmated.

E.g., in a real game both players cannot be in checkmate at the same time, as this means
that either the game did not stop at a previous checkmate or that one player played a
move that resulted in both players being in checkmate. This attempts to handle these
situations by checking if the player to move is checkmated first, but in a real game the
previous turn would have been prevented.

Unlike the official rules of chess, the 50-move rule is automatically enforced as a
draw. Checkmate is tested first, so mating on the hundredth halfmove is still a win.

Threefold repetition cannot be tested within a single position, so it is excluded here;
the search tracks it along the current line instead.

The caller passes the number of legal moves it has already generated, so the move list
is never built twice for the same node.
*/
inline int checkPosition(
    unsigned long long board,
    bool active,
    unsigned int halfmove,
    unsigned int legalMoveCount)
{
    if (legalMoveCount == 0)
    {
        if (isInCheck(board, active)) // if player to move is in check...
        {
            return active ? 8 : 9; // player not to move is the winner.
        }
        return 5; // stalemate.
    }
    if (halfmove >= 100)
    {
        return 6; // 50-move rule.
    }
    if (isInsufficientMaterialPieceSet(getPieceSet(board)))
    {
        return 7; // insufficient material.
    }
    return 0; // game still in progress.
}

/*
Convenience overload that generates the move list itself.
*/
inline int checkPosition(
    unsigned long long board,
    bool active,
    unsigned int halfmove,
    unsigned int /*fullmove*/,
    bool /*disambiguate*/)
{
    unsigned char buffer[MAX_MOVES];
    return checkPosition(board, active, halfmove, generateMoves(board, active, buffer));
}

/*
Given a position and an iterable of moves, print a nicely formatted playback of those
moves, applying them naively.
*/
void playbackMoves(
    unsigned long long board,
    bool active,
    unsigned int halfmove,
    unsigned int fullmove,
    vector<unsigned int> moves)
{
    std::cout << "0123456789012345" << std::endl; // makes it easier to see move indices.
    std::cout << varsToFence(board, active, halfmove, fullmove) << std::endl;
    for (unsigned int move : moves)
    {
        tie(board, active, halfmove, fullmove) = applyMove(board, active, halfmove, fullmove, move);
        unsigned int end_index = getLastNibble(move);
        unsigned int start_index = move >> 4; // get first nibble by rightshifting the move.
        std::cout << varsToFence(board, active, halfmove, fullmove);
        std::cout << " after " << start_index << " -> " << end_index << std::endl;
    }
}

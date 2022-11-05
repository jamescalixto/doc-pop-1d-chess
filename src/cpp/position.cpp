#include <bitset>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using std::string;
using std::tuple, std::make_tuple, std::tie;
using std::vector;

#include <typeinfo>

/*
Externally, position is expressed as a string in a format analogous to FEN (Forsyth-
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
    increment after black's move.

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
*/

// Handy constants.
const int BOARD_SIZE = 16;
const unsigned long long START_POSITION = 3991632928627678971;
const unsigned long long FIRST_NIBBLE_BITMASK = 240; // bitmask to get first nibble (of a byte).
const unsigned long long LAST_NIBBLE_BITMASK = 15;   // bitmask to get last nibble.

// 0-indexed start positions of pawns. Used to determine if they can move two spaces.
int PAWN_START_WHITE = 5;
int PAWN_START_BLACK = 10;

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
bool indexValid(int i)
{
    return 0 <= i < BOARD_SIZE;
}

/*
Helper function to grab the last nibble of an int.
*/
int getLastNibble(int i)
{
    return i & LAST_NIBBLE_BITMASK;
}

/*
Helper function to find a nibble n in i and return the index of it, assuming the given
number is BOARD_SIZE bits long. Returns -1 if the nibble is not found.
*/
int findNibble(unsigned long long i, unsigned long long nibble)
{
    for (int i = 0; i < BOARD_SIZE; i++)
    {
        if (i & LAST_NIBBLE_BITMASK == nibble)
        {
            return BOARD_SIZE - i - 1;
        }
        else
        {
            i = i >> 4;
        }
    }
    return -1;
}

/*
Helper function to extract the nth nibble of i, assuming the given number is BOARD_SIZE
bits long.
*/
unsigned long long getNthNibble(unsigned long long i, int n)
{
    int bitshifts = 4 * (BOARD_SIZE - n - 1); // number of bitshifts to do.
    return (i >> bitshifts) & LAST_NIBBLE_BITMASK;
}

/*
Helper function to blank the nth nibble of i, assuming the given number is BOARD_SIZE
bits long.
*/
unsigned long long blankNthNibble(unsigned long long i, int n)
{
    int bitshifts = 4 * (BOARD_SIZE - n - 1);                         // number of bitshifts to do.
    unsigned long long blanker = ~(LAST_NIBBLE_BITMASK << bitshifts); // all 1s except for nth nibble.
    return i & blanker;
}

/*
Helper function to insert a nibble as the nth nibble of i, assuming the given number is
BOARD_SIZE bits long.
*/
unsigned long long insertNthNibble(unsigned long long i, unsigned long long nibble, int n)
{
    int bitshifts = 4 * (BOARD_SIZE - n - 1);          // number of bitshifts to do.
    unsigned long long inserter = nibble << bitshifts; // move nibble to correct spot.
    return blankNthNibble(i, n) | inserter;
}

/*
Given a character representing a piece, return the numerical representation.
*/
int pieceToBits(char c)
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
char bitsToPiece(int i)
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
Given an int representing a piece, return the bitflag for it for piece-set purposes.
This follows the format KQRBNPkqrbnp.
*/
int bitsToPieceSet(int i)
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
bool isInsufficientMaterialPieceSet(int pieceSet)
{
    return (pieceSet == 2080    // kings only.
            || pieceSet == 2336 // kings and white bishop.
            || pieceSet == 2084 // kings and black bishop.
    )
}

/*
Helper functions to check pieces or piece properties.
*/
bool isEmpty(int nibble)
{
    return nibble == 0;
}
bool isPieceOfPlayer(int nibble, bool player)
{
    return !isEmpty(nibble) && (nibble >> 3 != player);
}
bool isPawn(int nibble)
{
    return nibble & 7 == 1;
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
    board = 0; // clear the board, as we use bitwise operators instead of assignment.
    string::iterator it;
    for (it = boardString.begin(); it != boardString.end(); it++)
    {
        board = board << 4;                 // leftshift one nibble.
        int pieceAsBits = pieceToBits(*it); // get numerical representation.
        board |= pieceAsBits;               // OR operator to add the new piece.
    }

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
    string boardString;
    for (int i = 0; i < BOARD_SIZE; i++)
    {
        int lastNibble = getLastNibble(board);  // get last nibble.
        boardString += bitsToPiece(lastNibble); // store char representation.
        board = board >> 4;                     // leftshift one nibble.
    }
    std::reverse(boardString.begin(), boardString.end()); // reverse string;

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
int getPieceSet(unsigned long long board)
{
    int pieceSet = 0;
    for (int i = 0; i < BOARD_SIZE; i++)
    {
        int lastNibble = getLastNibble(board);  // get last nibble.
        pieceSet |= bitsToPieceSet(lastNibble); // store pieceset representation.
        board = board >> 4;
    }
    return pieceSet;
}

/*
Get (as a bitflag) squares attacked by the given player. Includes squares occupied by
pieces belonging to both players. No piece attacks its own square.
*/
int getAttackedSquares(unsigned long long board, bool player)
{
    // TODO: implement this.
}

/*
Return whether the given player is in check in the given board. Assumes that the
position is valid.
*/
bool isInCheck(unsigned long long board, bool player)
{
    // TODO: implement this.
}

/*
G
*/
void getMoves(unsigned long long board, bool player)
{
    // TODO: think about how to define this.
}

/*
Naively apply a move to the board; i.e., assume the position and move are both valid and
legal. Used when other elements of the position do not matter; i.e. when testing check.
Return the new board.
*/
unsigned long long applyMoveToBoard(unsigned long long board, int move)
{
    // Get indices.
    int start_index = getLastNibble(move);
    int end_index = move >> 4; // get first nibble by rightshifting the move.

    // Get nibble at start.
    int start_nibble = getNthNibble(board, start_index);

    // Replace and return.
    return insertNthNibble(board, start_nibble, end_index);
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
    int move)
{
    // Get indices.
    int start_index = getLastNibble(move);
    int end_index = move >> 4; // get first nibble by rightshifting the move.

    // Get nibbles.
    int start_nibble = getNthNibble(board, start_index);
    int end_nibble = getNthNibble(board, end_index);

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

    board = insertNthNibble(board, start_nibble, end_index);
    active != active;
    return make_tuple(board, active, halfmove, fullmove);
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
0100 4 draw, 150+ fullmove
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
draw. The game is also a draw at 150 fullmoves.

Threefold repetition cannot be tested within a single position.
*/
int checkPosition(
    unsigned long long board,
    bool active,
    unsigned int halfmove,
    unsigned int fullmove)
{
    if (fullmove >= 150)
    {
        return 4; // hard cap at 150 fullmoves.
    }
    else if (false) // TODO: implement move checks.
    {
        if (isInCheck(board, active)) // if player to move is in check...
        {
            return active ? 8 : 9; // player not to move is the winner.
        }
        else
        {
            return 5; // stalemate.
        }
    }
    else if (halfmove >= 100)
    {
        return 6; // 50-move rule.
    }
    else if (isInsufficientMaterialPieceSet(getPieceSet(board)))
    {
        return 7; // insufficient material.
    }
    else
    {
        return 0; // game still in progress.
    }
}

int main()
{
    string fence = "KQRBNP....pnbrqk w 0 1";

    // Declare variables to store position information.
    unsigned long long board;
    bool active;
    unsigned int halfmove, fullmove;

    tie(board, active, halfmove, fullmove) = fenceToVars(fence, board, active, halfmove, fullmove);

    debugPrint(board);
    std::cout << varsToFence(board, active, fullmove, halfmove) << std::endl;
}
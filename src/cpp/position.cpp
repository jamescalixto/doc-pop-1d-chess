#include <iostream>
#include <string>
#include <sstream>

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
0010 king (white)
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
*/

const int BOARD_SIZE = 16;

void fenceToVars(
    std::string fence,
    std::string &board,
    bool &active,
    unsigned int &halfmove,
    unsigned int &fullmove)
{
    std::stringstream ss;
    ss << fence;
    ss >> board, active, halfmove, fullmove;
}

std::string varsToFence(
    unsigned long long board,
    bool active,
    unsigned int halfmove,
    unsigned int fullmove)
{
}

int main()
{
    std::string fence = "KQRBNP....pnbrqk w 0 1";

    // Declare variables to store position informatin.
    std::string board;
    bool active;
    unsigned int halfmove, fullmove;
    fenceToVars(fence, board, active, halfmove, fullmove);

    std::cout << "board: " << board << std::endl;
    std::cout << "active: " << active << std::endl;
    std::cout << "halfmove: " << halfmove << std::endl;
    std::cout << "fullmove: " << fullmove << std::endl;
}
# The board is represented in notation similar to FEN notation, but with a few
# notable differences. A "record" contains a particular game position, all in a single
# text line.
#
# A record contains four fields, separated by a space. The fields are:
#   1. Piece placement with white starting on the left. Eeach piece is identified by a
#       single letter (identical to FEN); i.e., P = pawn, N = knight, B = bishop, R =
#       rook, Q = queen, K = king. White pieces are denoted using uppercase letters and
#       black pieces use lowercase. Empty spaces are individually noted using periods
#       (unlike FEN notation).
#   2. Active color. "w" means white moves next, b means black moves next.
#   3. Halfmove clock; i.e., the number of halfmoves since the last capture or pawn
#       advance, used for the fifty-move rule.
#   4. Fullmove number; i.e., the number of the full move. It starts at 1 and is
#       increment after black's move.
# Note that castling and en passant fields, which are in FEN notation, are excluded due
# to their irrelevance. Promotion is also impossible, as a pawn has no way around the
# enemy king.
#
# The start position is "KQRBNP....pnbrqk w 0 1".
#
# Moves are specified as a tuple of (start, end) square. There are 14 squares which are
# 0-indexed, so squares are 0 through 13 inclusive. There is no special notation for a
# capture.

# Define constants.
BOARD_SIZE = 16  # number of squares on the board.

START_POSITION = "KQRBNP....pnbrqk w 0 1"
PAWN_START_WHITE = 5
PAWN_START_BLACK = 10

NOTATION_PIECES = {"K", "Q", "R", "B", "N", "P"}
NOTATION_EMPTY = "."


def index_valid(i):  # helper function to check valid index.
    return 0 <= i < BOARD_SIZE


def opposite_color(color):  # helper function to return opposite color.
    return "b" if color == "w" else "w"


def check_position(position):
    """Check if a position is an ended game, via stalemate or checkmate. Returns a tuple
    where the first element is "w" or "b" to indicate a winner, "d" to indicate a draw,
    or None to indicate that the game is not ended. The second element of the tuple is
    a more verbose explanation, etc. "50-move rule".

    Unlike the official rules of chess, the 50-move rule is automatically enforced as a
    draw.

    Threefold repetition cannot be tested within a single position."""
    board, active, halfmove, fullmove = position.split(" ")
    moves = get_moves(position, active)
    if len(moves) == 0:  # no valid moves.
        if is_in_check(position, active):  # see whether this is checkmate or stalemate.
            return (opposite_color(active), "checkmate")
        else:
            return ("d", "stalemate")
    if halfmove == 51:
        return ("d", "50-move rule")

    return (None, None)


def get_attacked_squares(position, player):
    """Get a list of squares attacked by the given player. Includes squares occupied by
    pieces belonging to both players. No piece attacks its own square."""
    board = list(position.split(" ")[0])
    player = player == "w"  # for boolean convenience, True if considering white.
    attacked_squares = set()  # set of attacked squares.
    for i, square in enumerate(board):
        if square.upper() in NOTATION_PIECES and (square.isupper() == player):

            def traverse(increment):
                """Mini function to traverse the board via some increment and add
                attackable squares."""
                test_i = i
                while True:
                    test_i += increment
                    attacked_squares.add(test_i)
                    if (
                        not index_valid(test_i)
                        or board[test_i].upper() in NOTATION_PIECES
                    ):  # if we've run into any piece or out of the board, stop.
                        break

            if square.upper() == "K":
                attacked_squares.update({i - 1, i + 1})
            if square.upper() == "R" or square.upper() == "Q":
                traverse(-1)
                traverse(1)
            if square.upper() == "B" or square.upper() == "Q":
                traverse(-2)
                traverse(2)
            if square.upper() == "N":
                attacked_squares.update({i - 3, i - 2, i + 2, i + 3})
            if square.upper() == "P":
                if player:  # if white, pawns attack to the right.
                    attacked_squares.add(i + 1)
                else:  # if black, pawns attack to the left.
                    attacked_squares.add(i - 1)
    attacked_squares = {
        square for square in attacked_squares if index_valid(square)
    }  # trim squares outside board bounds.
    return attacked_squares


def is_in_check(position, player):
    """Return true if the given player is in check in the given position. Assumes that
    the position is valid."""
    board = position.split(" ")[0]
    king_position = board.find("K" if player == "w" else "k")
    attacked_squares = get_attacked_squares(position, ("b" if player == "w" else "w"))
    return king_position in attacked_squares


def get_moves(position, player):
    """Get a list of tuples representing all legal moves by the given player."""
    board = list(position.split(" ")[0])
    enemy_attacked_squares = get_attacked_squares(
        position, opposite_color(player)
    )  # get squares the enemy is attacking.
    player = player == "w"  # for boolean convenience, True if considering white.
    moves = set()  # set of possible moves.
    for i, square in enumerate(board):
        if square.upper() in NOTATION_PIECES and (square.isupper() == player):

            def is_not_same_color(test_i):
                """Helper function that returns true if the piece at index test_i is a
                different color than the player to move, or if test_i is empty."""
                return (
                    board[test_i].isupper() != player or board[test_i] == NOTATION_EMPTY
                )

            def traverse(increment):
                """Helper function to traverse the board via some increment and add
                potential moves."""
                test_i = i
                while True:
                    test_i += increment
                    if not index_valid(test_i):
                        break
                    elif (
                        board[test_i].upper() in NOTATION_PIECES
                    ):  # check whether we can capture piece, stop in any case.
                        if is_not_same_color(test_i):  # different color piece.
                            moves.add((i, test_i))  # we can capture it.
                        break
                    else:  # otherwise this is a valid move.
                        moves.add((i, test_i))

            if square.upper() == "K":
                for test_i in [i - 1, i + 1]:
                    if test_i not in enemy_attacked_squares and is_not_same_color(
                        test_i
                    ):  # king can't move into check.
                        moves.add((i, test_i))
            if square.upper() == "R" or square.upper() == "Q":
                traverse(-1)
                traverse(1)
            if square.upper() == "B" or square.upper() == "Q":
                traverse(-2)
                traverse(2)
            if square.upper() == "N":
                for test_i in [i - 3, i - 2, i + 2, i + 3]:
                    if is_not_same_color(test_i):
                        moves.add((i, test_i))
            if square.upper() == "P":
                pawn_start = PAWN_START_WHITE if player else PAWN_START_BLACK
                increment = 1 if player else -1
                if is_not_same_color(i + increment):
                    moves.add((i, i + increment))
                if i == pawn_start:
                    if (
                        board[i + increment] == NOTATION_EMPTY
                        and board[i + increment * 2] == NOTATION_EMPTY
                    ):
                        moves.add((i, i + increment * 2))

    moves = {
        move for move in moves if index_valid(move[1])
    }  # trim moves outside board bounds.
    moves = {
        move
        for move in moves
        if not is_in_check(apply_move(position, move), "w" if player else "b")
    }  # eliminate moves that result in check.
    return moves


def apply_move(position, move):
    """Naively apply a move to the position; i.e., assume the position and move are both
    valid and legal."""
    start, end = move
    board, active, halfmove, fullmove = position.split(" ")

    # Update halfmove if move is a capture or pawn advance.
    halfmove = int(halfmove)
    if board[start].upper() == "P" or board[end] != NOTATION_EMPTY:
        halfmove = 0
    else:
        halfmove += 1

    # Update fullmove.
    fullmove = int(fullmove)
    if active == "b":
        fullmove += 1

    # Update board from move.
    board = list(board)
    board[start], board[end] = NOTATION_EMPTY, board[start]  # swap start, end.
    board = "".join(board)

    # Update player to move.
    active = "w" if active == "b" else "b"

    return " ".join(str(elem) for elem in [board, active, halfmove, fullmove])


print(apply_move(START_POSITION, (4, 7)))
print(get_moves(START_POSITION, "w"))

print(get_moves("K......Nr......w w 0 1", "w"))
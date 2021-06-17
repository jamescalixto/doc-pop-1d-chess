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
NOTATION_PIECES = {"K", "Q", "R", "B", "N", "P"}
NOTATION_EMPTY = "."


def check_position(position):
    """Check if a position is an ended game, via stalemate or checkmate. Returns a tuple
    where the first element is "w" or "b" to indicate a winner, "d" to indicate a draw,
    or None to indicate that the game is not ended. The second element of the tuple is
    a more verbose explanation, etc. "50-move rule"."""
    pass


def get_attacked_squares(position, player):
    """Get a list of squares attacked by the given player. Includes squares occupied by
    pieces belonging to both players."""
    board = list(position.split(" ")[0])
    player = player == "w"  # for boolean convenience, True if considering white.
    for square in board:
        if square.upper() in NOTATION_PIECES and (square.isupper() == player):
            pass


def get_moves(position):
    """Get a list of tuples representing all moves, legal or otherwise, by the side to
    move."""
    board = list(position.split(" ")[0])
    for square in board:
        if square in NOTATION_PIECES:
            pass


def get_legal_moves(position):
    """Get a list of tuples representing legal moves by the side to move."""
    pass


def apply_move_naive(position, move):
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


print(apply_move_naive(START_POSITION, (4, 7)))
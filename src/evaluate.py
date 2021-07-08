import functools
import position as Position

CACHE_SIZE = 1048576  # size of LRU caching for functions.
MAX_FULLMOVES = 150  # maximum fullmove depth.

# Value of game outcomes.
SCORE_WHITE_WIN = 100
SCORE_BLACK_WIN = -1 * SCORE_WHITE_WIN
SCORE_DRAW = 0

# Values of pieces.
PIECE_VALUES = {
    "K": 50,
    "Q": 9,
    "R": 5,
    "B": 3,
    "N": 3,
    "P": 1,
    ".": 0,
}  # taken from regular chess, unsure if these hold up.


@functools.lru_cache(maxsize=CACHE_SIZE)
def score_position_estimate(position):
    """Given a position, score it using an estimate."""
    pieces_white = Position.get_current_pieces(position, "w")
    pieces_black = Position.get_current_pieces(position, Position.opposite_color("b"))
    score_white = sum(PIECE_VALUES[piece.upper()] for piece in pieces_white)
    score_black = sum(PIECE_VALUES[piece.upper()] for piece in pieces_black)
    return score_white - score_black


@functools.lru_cache(maxsize=CACHE_SIZE)
def score_position_definite(position):
    """Given a position, score it if the game is over."""
    board, active, halfmove, fullmove = position.split(" ")
    state = Position.check_position(position)
    if int(fullmove) >= MAX_FULLMOVES:
        return SCORE_DRAW
    elif state[0] == "w":
        return SCORE_WHITE_WIN
    elif state[0] == "b":
        return SCORE_BLACK_WIN
    elif state[0] == "d":
        return SCORE_DRAW
    else:
        return None


@functools.lru_cache(maxsize=CACHE_SIZE)
def score_position_bfs(
    position, depth=0, max_depth=None, max_depth_estimator=score_position_estimate
):
    """Given a position, score it (assuming that the opponent plays optimally) and
    return the path to that end state. Uses breadth-first-search recursively with a
    depth limit, after which it estimates the position using an estimator function."""
    board, active, halfmove, fullmove = position.split(" ")

    if Position.check_position(position) != (None, None):  # game is over
        pass


def test_score_position(position):
    score, moves = score_position_bfs(position)
    print("score={}".format(score))
    Position.playback_moves(position, moves)


print(test_score_position("K....n.........k b 0 1"))
# print(test_score_position("K.....nbP......k w 0 1"))
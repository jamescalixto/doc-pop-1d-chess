from collections import Counter
from copy import deepcopy
import functools
import position as Position

CACHE_SIZE = 1048576  # size of LRU caching for functions.
MAX_FULLMOVES = 150  # maximum fullmove depth.

# Value of game outcomes.
SCORE_WIN = 100
SCORE_LOSS = -1 * SCORE_WIN
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
def score_position_estimate(position, player):
    """Given a position, score it for the given player using an estimate."""
    pieces_white = Position.get_current_pieces(position, "w")
    pieces_black = Position.get_current_pieces(position, "b")
    score_white = sum(PIECE_VALUES[piece.upper()] for piece in pieces_white)
    score_black = sum(PIECE_VALUES[piece.upper()] for piece in pieces_black)
    if player == "w":
        return score_white - score_black
    elif player == "b":
        return score_black - score_white


@functools.lru_cache(maxsize=CACHE_SIZE)
def score_position_definite(position, player):
    """Given a position, score it for the given player if the game is over."""
    board, active, halfmove, fullmove = position.split(" ")
    state = Position.check_position(position)
    if int(fullmove) >= MAX_FULLMOVES:
        return SCORE_DRAW
    elif state[0] == "w":
        return SCORE_WIN if player == "w" else SCORE_LOSS
    elif state[0] == "b":
        return SCORE_WIN if player == "b" else SCORE_LOSS
    elif state[0] == "d":
        return SCORE_DRAW
    else:
        return None


@functools.lru_cache(maxsize=CACHE_SIZE)
def test_score_position(position, max_depth=4):
    print("")
    score, moves = score_position(position, max_depth=max_depth)
    print("score={}".format(score))
    Position.playback_moves(position, moves)
    print("")
from collections import Counter
from copy import deepcopy
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
def score_position_dfs(
    position,  # position to score.
    starting_player=None,  # starting player to optimize score for.
    depth=0,  # current depth.
    max_depth=None,  # maximum depth to search.
    max_depth_estimator=score_position_estimate,  # function to use to estimate score.
    seen_boards=Counter(),  # counter of seen boards; used for threefold repetition.
):
    """Given a position, score it (assuming that the opponent plays optimally) and
    return the path to that end state. Uses breadth-first-search recursively with a
    depth limit, after which it estimates the position using an estimator function."""
    board, active, halfmove, fullmove = position.split(" ")

    # Set starting player in the initial call so we know who to optimize for. This
    # isn't overwritten (it persists) in subsequent recursive calls.
    if starting_player is None:
        starting_player = active

    # Check for draw via threefold repetition using the boards we've seen.
    if seen_boards.get(board) >= 3:
        return SCORE_DRAW

    # Check if game is over by other means.
    definite_score = score_position_definite(position)
    if definite_score is not None:
        return definite_score

    # If not, the game isn't over so we need to score the position.
    # If we are max depth, use the estimator to score.
    if depth == max_depth:
        return max_depth_estimator(position)

    # Otherwise we need to use depth-first-search to score.
    #
    # We want the best possible score for the starting player, but we also assume that
    # the opponent plays optimally. Thus if it's the starting player's turn, pick the
    # move that gives the best score for the starting player; if it's the opponent's
    # turn, pick the move that gives the worst score for the starting player.
    #
    # We can save time by returning SCORE_WHITE_WIN or SCORE_BLACK_WIN immediately, if
    # it's the best/worst score as above (because we know that other branches can't
    # beat it).
    potential_moves = Position.get_current_moves(position)
    for potential_move in potential_moves:
        # Make the new position.
        potential_position = Position.apply_move(position, potential_move)

        # Make a deep copy of seen boards and increment the current board in it.
        potential_seen_boards = deepcopy(seen_boards)
        potential_seen_boards[board] += 1

        # Get the score of this potential position via recursion.
        potential_score = score_position_dfs(
            potential_position,  # use the new position.
            starting_player,  # use the same starting player.
            depth + 1,  # increment the depth by 1.
            max_depth,  # use the same max depth.
            max_depth_estimator,  # use the same estimator function for max depth cases.
            potential_seen_boards,  # use the new deep copy of seen boards.
        )


def test_score_position(position):
    score, moves = score_position_dfs(position)
    print("score={}".format(score))
    Position.playback_moves(position, moves)


print(test_score_position("K....n.........k b 0 1"))
# print(test_score_position("K.....nbP......k w 0 1"))
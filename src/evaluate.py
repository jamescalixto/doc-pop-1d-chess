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
    pieces_black = Position.get_current_pieces(position, "b")
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


def score_position_dfs(
    position,  # position to score.
    starting_player=None,  # starting player to optimize score for.
    movelist=[],  # moves already made.
    depth=0,  # current depth.
    max_depth=None,  # maximum depth to search, in ply (a turn by a single player).
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
    if board in seen_boards and seen_boards.get(board) >= 3:
        return (SCORE_DRAW, movelist)

    # Check if game is over by other means.
    definite_score = score_position_definite(position)
    if definite_score is not None:
        return (definite_score, movelist)

    # If not, the game isn't over so we need to score the position.
    # If we are max depth, use the estimator to score.
    if depth == max_depth:
        return (max_depth_estimator(position), movelist)

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
    predicted_scores_and_movelist = (
        dict()
    )  # stores moves and their corresponding stores.
    for potential_move in potential_moves:
        # Make the new position.
        potential_position = Position.apply_move(position, potential_move)

        # Make a deep copy of the potential movelist and add the current move to it.
        potential_movelist = deepcopy(movelist)
        potential_movelist.append(potential_move)

        # Make a deep copy of seen boards and increment the current board in it.
        potential_seen_boards = deepcopy(seen_boards)
        potential_seen_boards[board] += 1

        # Get the score of this potential position via recursion.
        predicted_score, predicted_movelist = score_position_dfs(
            potential_position,  # use the new position.
            starting_player,  # use the same starting player.
            potential_movelist,  # use the potential movelist.
            depth + 1,  # increment the depth by 1.
            max_depth,  # use the same max depth.
            max_depth_estimator,  # use the same estimator function for max depth cases.
            potential_seen_boards,  # use the new deep copy of seen boards.
        )

        # Add the score to the dictionary.
        predicted_scores_and_movelist[potential_move] = (
            predicted_score,
            predicted_movelist,
        )

        # # If it's the starting player's turn and this branch gives the best score for
        # # them, or if it's the opponent's turn and this branch gives the worst score for
        # # them, then return the score immediately.
        # if starting_player == "w":
        #     if active == "w" and predicted_score == SCORE_WHITE_WIN:
        #         return (SCORE_WHITE_WIN, predicted_movelist)
        #     elif active == "b" and predicted_score == SCORE_BLACK_WIN:
        #         return (SCORE_BLACK_WIN, predicted_movelist)
        # elif starting_player == "b":
        #     if active == "b" and predicted_score == SCORE_BLACK_WIN:
        #         return (SCORE_BLACK_WIN, predicted_movelist)
        #     elif active == "w" and predicted_score == SCORE_WHITE_WIN:
        #         return (SCORE_WHITE_WIN, predicted_movelist)

    # If we're here, we haven't returned yet, so return the move corresponding to the
    # best score if the starting player is active, otherwise return the move
    # corresponding to the worst score.
    potential_movelist = deepcopy(movelist)
    if active == "w":
        predicted_move = max(
            predicted_scores_and_movelist,
            key=lambda k: (
                predicted_scores_and_movelist[k][0],
                len(predicted_scores_and_movelist[k][1]),
            ),
        )
    elif active == "b":
        predicted_move = min(
            predicted_scores_and_movelist,
            key=lambda k: (
                predicted_scores_and_movelist[k][0],
                len(predicted_scores_and_movelist[k][1]),
            ),
        )

    # Append the predicted move to the movelist and return.
    return predicted_scores_and_movelist[predicted_move]


@functools.lru_cache(maxsize=CACHE_SIZE)
def test_score_position(position):
    print("")
    score, moves = score_position_dfs(position, max_depth=12)
    print("score={}".format(score))
    Position.playback_moves(position, moves)
    print("")
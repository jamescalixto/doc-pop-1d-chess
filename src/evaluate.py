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
def next_move_heuristic_estimate(position, starting_player):
    """Use the position scoring estimator to return the list of moves in order from best
    to worst."""
    potential_moves = Position.get_current_moves(position)
    return sorted(
        potential_moves,
        key=lambda move: score_position_estimate(
            Position.apply_move(position, move), starting_player
        ),  # sort by score for the starting player after applying the move.
        reverse=True,  # we want the best moves to be first.
    )


def score_position(
    position,  # position to score.
    starting_player=None,  # starting player to optimize score for.
    alpha=SCORE_LOSS - 1,  # minimum score that the maximizing player is assured of.
    beta=SCORE_WIN + 1,  # maximum score that the minimizing player is assured of.
    depth=0,  # current depth.
    max_depth=None,  # maximum depth to search, in ply (a turn by a single player).
    max_depth_heuristic=score_position_estimate,  # function to use to estimate score.
    next_move_heuristic=lambda position, _: Position.get_current_moves(
        position
    ),  # heuristic to return moves in order of preference.
    movelist=[],  # list of moves made so far.
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
        return SCORE_DRAW, movelist

    # Check if game is over by other means.
    definite_score = score_position_definite(position, starting_player)
    if definite_score is not None:
        return definite_score, movelist

    # If not, the game isn't over so we need to score the position.
    # If we are max depth, use the estimator to score.
    if depth == max_depth:
        return max_depth_heuristic(position, starting_player), movelist

    # Otherwise, we are not at max depth, so we need to score the position.
    # We want the best possible score for the starting player, but we also assume that
    # the opponent plays optimally. Thus if it's the starting player's turn, pick the
    # move that gives the best score for the starting player; if it's the opponent's
    # turn, pick the move that gives the worst score for the starting player.
    #
    # We can save time by returning SCORE_WHITE_WIN or SCORE_BLACK_WIN immediately, if
    # it's the best/worst score as above (because we know that other branches can't
    # beat it).
    potential_moves = next_move_heuristic(position, starting_player)

    # Store best score to compare against and the move that leads to it.
    if active == starting_player:
        best_score = SCORE_LOSS - 1
    else:
        best_score = SCORE_WIN + 1
    best_movelist = None

    for potential_move in potential_moves:
        # Make the new position.
        potential_position = Position.apply_move(position, potential_move)

        # Make a deepcopy of the movelist and add the current move.
        potential_movelist = deepcopy(movelist)
        potential_movelist.append(potential_move)

        # Make a deep copy of seen boards and increment the current board in it.
        potential_seen_boards = deepcopy(seen_boards)
        potential_seen_boards[board] += 1

        # Get the score of this potential position via recursion.
        predicted_score, predicted_movelist = score_position(
            position=potential_position,  # use the new position.
            starting_player=starting_player,  # use the same starting player.
            alpha=alpha,  # use the same alpha and beta values.
            beta=beta,
            depth=depth + 1,  # increment the depth by 1.
            max_depth=max_depth,  # use the same max depth.
            max_depth_heuristic=max_depth_heuristic,  # use the same estimator function for max depth cases.
            movelist=potential_movelist,  # use the same movelist.
            seen_boards=potential_seen_boards,  # use the new deep copy of seen boards.
        )

        # Alpha-beta pruning.
        if active == starting_player:  # maximizing player.
            if predicted_score > best_score or (
                predicted_score == best_score
                and len(predicted_movelist) < len(best_movelist)
            ):
                best_score = predicted_score
                best_movelist = predicted_movelist
            if best_score >= beta and len(predicted_movelist) >= len(best_movelist):
                break  # prune.
            alpha = max(alpha, best_score)
        else:
            if predicted_score < best_score or (
                predicted_score == best_score
                and len(predicted_movelist) < len(best_movelist)
            ):
                best_score = predicted_score
                best_movelist = predicted_movelist
            if best_score <= alpha and len(predicted_movelist) >= len(best_movelist):
                break  # prune.
            beta = min(beta, best_score)

    return best_score, best_movelist


@functools.lru_cache(maxsize=CACHE_SIZE)
def test_score_position(
    position, max_depth=8, next_move_heuristic=next_move_heuristic_estimate
):
    print("")
    score, moves = score_position(
        position, max_depth=max_depth, next_move_heuristic=next_move_heuristic
    )
    print("score={}".format(score))
    Position.playback_moves(position, moves)
    print("")
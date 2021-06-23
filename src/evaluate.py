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
    """Given a position, score it for the given player if the game is over."""
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
def score_position_dfs(position):
    """Given a position, score the best possible score for the current player
    (assuming that the opponent plays optimally) and return the path to that end state.
    Uses depth-first-search recursively."""
    board, active, halfmove, fullmove = position.split(" ")

    # Base case: return score if game is over.
    definite_score = score_position_definite(position)
    if definite_score is not None:
        return definite_score, []

    # Otherwise, the score is the inverse of the best score the opponent can make.
    moves = Position.get_current_moves(position)
    potential_responses = []
    for move in moves:
        # Make the move and score the new position for the other player.
        # Remember that we want potential_score to be as HIGH as possible if white is
        # the active player and LOW as possible if black is the active player.
        new_position = Position.apply_move(position, move)
        potential_score, potential_moves = score_position(new_position)

        # If the opponent loses with this move, then this move wins this position.
        # This is the best possible outcome so we don't have to search anymore.
        # Return that we win and also return this move, plus the moves that lead to that
        # end state.
        if active == "w" and potential_score == SCORE_WHITE_WIN:
            return SCORE_WHITE_WIN, [move] + potential_moves
        if active == "b" and potential_score == SCORE_BLACK_WIN:
            return SCORE_BLACK_WIN, [move] + potential_moves

        # Otherwise, keep track of the potential responses.
        else:
            potential_responses.append((potential_score, potential_moves))

    # Get the shortest move path.
    potential_responses = sorted(
        potential_responses,
        key=lambda response: len(response[1]),
        reverse=False,
    )
    # Now sort. We pick the first element in this array so we want the first element to
    # be the lowest if black is the active player.
    potential_responses = sorted(
        potential_responses, key=lambda response: response[0], reverse=(active == "w")
    )

    best_response = potential_responses[0]

    return best_response[0], ([move] + best_response[1])


def test_score_position(position):
    score, moves = score_position_dfs(position)
    print("score={}".format(score))
    Position.playback_moves(position, moves)


print(test_score_position("K....n.........k b 0 1"))
# print(test_score_position("K.....nbP......k w 0 1"))
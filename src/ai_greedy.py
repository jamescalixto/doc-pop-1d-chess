import position as Position
import random

PIECE_VALUES = {
    "K": 100,
    "Q": 9,
    "R": 5,
    "B": 3,
    "N": 3,
    "P": 1,
    ".": 0,
}  # taken from regular chess, unsure if these hold up.


def move(position):
    board, active, halfmove, fullmove = position.split(" ")
    moves = list(Position.get_current_moves(position))  # list of possible moves.
    random.shuffle(moves)  # randomize.
    enemy_attacked_squares = Position.get_attacked_squares(
        position, Position.opposite_color(active)
    )  # list of squares enemy is attacking.

    def move_score(m):
        """Scoring function for a move. If target square is attacked by enemy, then the
        benefit is the value of the target square minus the value of the origin piece.
        Otherwise it is just the value of the target square."""
        origin_value = PIECE_VALUES.get(board[m[0]].upper())
        target_value = PIECE_VALUES.get(board[m[1]].upper())
        if m[1] in enemy_attacked_squares:
            return target_value - origin_value
        else:
            return target_value

    moves = sorted(moves, key=lambda m: move_score(m), reverse=False)
    return moves[0]
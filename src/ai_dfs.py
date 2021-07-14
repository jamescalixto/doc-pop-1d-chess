import position as Position
import random


def move(position):
    # Given a position, return a random move.
    random_move = random.choice(tuple(Position.get_current_moves(position)))
    return random_move
import position as Position
import random


def move(position):
    moves = Position.get_current_moves(position)
    return random.choice(moves) if moves else None
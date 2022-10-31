import position as Position
import random


def move(position):
    move = random.choice(tuple(Position.get_current_moves(position)))
    return move
import board
import random


def move(position):
    move = random.choice(tuple(board.get_current_moves(position)))
    return move
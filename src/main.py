import board
from collections import Counter
import random
from timeit import default_timer as timer

from ai_random import move as ai_random
from ai_greedy import move as ai_greedy

# Set up AI and parameters.
AI_WHITE = ai_random
AI_BLACK = ai_random


def run_game(verbose=True):
    """Run a single game of chess."""

    def print_verbose(string):
        """Prints a string... sometimes."""
        if verbose:
            print(string)

    position = board.START_POSITION
    while True:
        print_verbose(position)

        # Get the computer to play a move.
        active = position.split(" ")[1]
        moves = board.get_current_moves(position)
        if active == "w":
            move = AI_WHITE(position)
        else:
            move = AI_BLACK(position)

        # Check if it is valid.
        if move in moves:
            position = board.apply_move(position, move)
        else:
            print_verbose((board.opposite_color(active)), "illegal move")

        # Check whether to continue.
        state = board.check_position(position)
        if state[0] is not None:
            print_verbose(position)
            print_verbose(state)
            break

    return state


def run_games(num_games):
    start = timer()
    c = Counter()
    for n in range(num_games):
        outcome = run_game(verbose=False)
        c[outcome[0]] += 1
        print(n, outcome)
    end = timer()
    elapsed = end - start
    print("Match record:", "-".join(str(i) for i in [c["w"], c["b"], c["d"]]))
    print(
        "Elapsed: {}s ({}s/game)".format(
            round(elapsed, 2), round(elapsed / num_games, 3)
        )
    )


run_games(1000000)
# 220-241-539
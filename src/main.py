import position as Position
from collections import Counter
import random
from timeit import default_timer as timer

from ai_random import move as ai_random
from ai_greedy import move as ai_greedy

# Set up AI for each side.
AI_WHITE = ai_random
AI_BLACK = ai_greedy


def run_game(verbose=True, check_valid=False):
    """Run a single game of chess. Optionally write each position out and enforce
    checking if the move is valid."""

    def print_verbose(string):
        """Prints a string... if we want it to."""
        if verbose:
            print(string)

    position = Position.START_POSITION  # initialize start position.
    while True:
        print_verbose(position)

        # Get the computer to play a move.
        active = position.split(" ")[1]
        if active == "w":
            move = AI_WHITE(position)
        else:
            move = AI_BLACK(position)

        # Check if it is a valid move.
        if check_valid:
            moves = Position.get_current_moves(position)
            if move in moves:  # valid move.
                position = Position.apply_move(position, move)
            else:  # not valid move and active player forfeits.
                state = (Position.opposite_color(active), "illegal move")
                break
        else:
            position = Position.apply_move(position, move)

        # Check whether game is concluded or if it should keep going.
        state = Position.check_position(position)
        if state[0] is not None:  # only None when game is not complete.
            print_verbose(position)
            print_verbose(state)
            break

    return state


def run_games(num_games):
    """Run multiple games of chess."""

    def print_games_info(c, elapsed):  # helper function to print game information.
        print("Match record:", "-".join(str(i) for i in [c["w"], c["b"], c["d"]]))
        print(
            "Elapsed: {}s ({}s/game)".format(
                round(elapsed, 2), round(elapsed / num_games, 3)
            )
        )

    try:
        start = timer()
        c = Counter()
        for n in range(num_games):
            outcome = run_game(verbose=False)
            c[outcome[0]] += 1
            print(n, outcome)
        end = timer()
        elapsed = end - start  # elapsed time to run all games.
        print_games_info(c, elapsed)

    except Exception as e:
        # In case there's an exception, try to salvage the run data.
        print(e)
        print(c)
        print_games_info(c, elapsed)


run_games(100)
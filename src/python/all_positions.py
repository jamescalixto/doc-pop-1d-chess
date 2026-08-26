import cProfile
import functools
import position as Position


def position_to_state(position):
    board, active, halfmove, fullmove = position.split(" ")
    return (board, active)


def state_to_position(tup):
    return " ".join((tup[0], tup[1], "0", "0"))


def explore(max_level):
    """Explore and enumerate the game tree.
    We use "states" — the more lightweight (board, active) tuple — instead of the full
    position string.
    """
    seen_states = set()
    current_level = 0
    states = {position_to_state(Position.START_POSITION)}

    while len(states) > 0 and current_level < max_level:
        seen_states = seen_states.union(states)
        next_states = {
            (
                Position.apply_move_board(board, next_move),
                Position.opposite_color(active),
            )
            for (board, active) in states
            for next_move in Position.get_moves(board, active)
        }
        states = {s for s in next_states if s not in seen_states}
        current_level += 1
        print(
            "# positions reachable after {} halfmoves = {}".format(
                str(current_level).rjust(3), len(states)
            )
        )
    print("No more traversable positions after this depth.")


if __name__ == "__main__":
    explore(6)

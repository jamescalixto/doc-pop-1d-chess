import cProfile
import functools
import position as Position


def position_to_state(position):
    board, active, halfmove, fullmove = position.split(" ")
    return (board, active)


def state_to_position(tup):
    return " ".join((tup[0], tup[1], 0, 0))


def explore(max_level):
    """Explore and enumerate the game tree.
    We use "states" — the more lightweight (board, active) tuple — instead of the full
    position string.
    """
    seen_states = set()
    current_level = 0
    states = {position_to_state(Position.START_POSITION)}
    next_states = set()

    @functools.lru_cache(maxsize=1)  # avoid double call.
    def is_candidate(state):
        return state not in seen_states and state not in next_states

    while len(states) > 0 and current_level < max_level:
        active = "w" if current_level % 2 == 0 else "b"
        seen_states = seen_states.union(states)
        next_states = {
            (
                Position.apply_move_board(board, next_move),
                Position.opposite_color(active),
            )
            for (board, active) in states
            for next_move in Position.get_moves(board, active)
            if is_candidate(Position.apply_move_board(board, next_move))
        }
        states = next_states
        next_states = set()
        current_level += 1
        print(
            "# new positions reachable after {} halfmoves = {}".format(
                str(current_level).rjust(3), len(states)
            )
        )
        # for s in states:
        #     print(" ".join((str(i) for i in (s[0], s[1], 0, 0))))
    print("No more traversable positions after this depth.")


# cProfile.run("explore(5)")
explore(8)

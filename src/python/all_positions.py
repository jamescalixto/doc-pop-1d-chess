import cProfile
import functools
import position as Position


def explore(max_level):
    """Explore and enumerate the game tree."""
    seen_boards = set()
    current_level = 0
    positions = {Position.START_POSITION}
    next_positions = set()

    @functools.lru_cache(maxsize=1)  # avoid double call.
    def is_candidate(position):
        board, active, halfmove, fullmove = position.split(" ")
        return (board, active) not in seen_boards and position not in next_positions

    while len(positions) > 0 and current_level < max_level:
        seen_boards = seen_boards.union(
            {(position.split(" ")[0], position.split(" ")[1]) for position in positions}
        )
        next_positions = {
            Position.apply_move(position, next_move)
            for position in positions
            for next_move in Position.get_current_moves(position)
            if is_candidate(Position.apply_move(position, next_move))
        }
        positions = next_positions
        next_positions = set()
        current_level += 1
        print(
            "# positions reachable after {} halfmoves = {}".format(
                str(current_level).rjust(3), len(positions)
            )
        )
    print("No more traversable positions after this depth.")


# cProfile.run("explore(5)")
explore(5)

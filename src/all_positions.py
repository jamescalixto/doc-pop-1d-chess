import functools
import position as Position
from timeit import default_timer as timer


def explore():
    """Explore and enumerate the game tree."""
    seen_positions = set()
    current_level = 0
    positions = {Position.START_POSITION}
    next_positions = set()

    @functools.lru_cache(maxsize=1)  # avoid double call.
    def is_candidate(position):
        return position not in seen_positions and position not in next_positions

    while len(positions) > 0:
        seen_positions = seen_positions.union(positions)
        print(
            "# positions reachable after {} halfmoves = {}".format(
                str(current_level).rjust(3), len(positions)
            )
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
    print("No more traversable positions after this depth.")


explore()
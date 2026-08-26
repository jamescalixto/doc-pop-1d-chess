import evaluate
import position as Position


def move(position, max_depth=6):
    """Given a position, return the best move determined by depth-first search (alpha-beta)."""
    score, movelist = evaluate.score_position(position, max_depth=max_depth)
    if movelist:
        return movelist[0]
    moves = Position.get_current_moves(position)
    return moves[0] if moves else None
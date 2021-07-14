import position as Position


def unique_permutations(seq):
    """
    Yield only unique permutations of seq in an efficient way.

    A python implementation of Knuth's "Algorithm L", also known from the
    std::next_permutation function of C++, and as the permutation algorithm
    of Narayana Pandita.

    From https://stackoverflow.com/questions/12836385/how-can-i-interleave-or-create-unique-permutations-of-two-strings-without-recur/12837695#12837695
    """

    # Precalculate the indices we'll be iterating over for speed
    i_indices = list(range(len(seq) - 1, -1, -1))
    k_indices = i_indices[1:]

    # The algorithm specifies to start with a sorted version
    seq = sorted(seq)

    while True:
        yield seq

        # Working backwards from the last-but-one index,           k
        # we find the index of the first decrease in value.  0 0 1 0 1 1 1 0
        for k in k_indices:
            if seq[k] < seq[k + 1]:
                break
        else:
            # Introducing the slightly unknown python for-else syntax:
            # else is executed only if the break statement was never reached.
            # If this is the case, seq is weakly decreasing, and we're done.
            return

        # Get item from sequence only once, for speed
        k_val = seq[k]

        # Working backwards starting with the last item,           k     i
        # find the first one greater than the one at k       0 0 1 0 1 1 1 0
        for i in i_indices:
            if k_val < seq[i]:
                break

        # Swap them in the most efficient way
        (seq[k], seq[i]) = (seq[i], seq[k])  #       k     i
        # 0 0 1 1 1 1 0 0

        # Reverse the part after but not                           k
        # including k, also efficiently.                     0 0 1 1 0 0 1 1
        seq[k + 1 :] = seq[-1:k:-1]


def build_permutations_from_additional_pieces(
    additional_pieces, find_checkmates=True, find_stalemates=True
):
    """Given an iterator of additional pieces (i.e., not either king), return all positions
    with kings and those extra pieces that are either checkmate or stalemate."""
    all_pieces = ["K", "k"]
    all_pieces.extend(additional_pieces)

    # Add in spaces to pad the board.
    spaces = ["."] * (Position.BOARD_SIZE - len(all_pieces))
    all_pieces.extend(spaces)

    # Build all possible permutations of the board and test if they are checkmate or
    # stalemate.
    for permutation in unique_permutations(all_pieces):
        # Kings can't be adjacent.
        if abs(permutation.index("K") - permutation.index("k")) == 1:
            continue

        # Pawns can't move backwards.
        if (
            "P" in permutation and permutation.index("P") < Position.PAWN_START_WHITE
        ) or (
            "p" in permutation and permutation.index("p") > Position.PAWN_START_BLACK
        ):
            continue

        # Pawns can't move over kings.
        if "P" in permutation and permutation.index("P") > permutation.index("k"):
            continue
        if "p" in permutation and permutation.index("p") < permutation.index("K"):
            continue

        # Rule out simultaneous check.
        if Position.is_in_check("".join(permutation), "w") and Position.is_in_check(
            "".join(permutation), "b"
        ):
            continue

        position_white = "".join(permutation) + " w 0 1"
        position_black = "".join(permutation) + " b 0 1"

        result_white = Position.check_position(position_white)
        result_black = Position.check_position(position_black)
        if result_white != (None, None):
            if (find_checkmates and result_white[1] == "checkmate") or (
                find_stalemates and result_white[1] == "stalemate"
            ):
                print(position_white, result_white)
        if result_black != (None, None):
            if (find_checkmates and result_black[1] == "checkmate") or (
                find_stalemates and result_black[1] == "stalemate"
            ):
                print(position_black, result_black)


build_permutations_from_additional_pieces("bp", find_stalemates=False)
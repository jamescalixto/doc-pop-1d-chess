import position as Position


def test_index_valid():
    assert Position.index_valid(0) == True
    assert Position.index_valid(Position.BOARD_SIZE) == False


def test_opposite_color():
    assert Position.opposite_color("w") == "b"
    assert Position.opposite_color("b") == "w"


def test_get_pieces():
    tests = {
        "KQRBNP....pnbrqk w 0 1": set(
            ["K", "Q", "R", "B", "N", "P", "k", "q", "r", "b", "n", "p"]
        ),
        "KQRBNP.......... w 0 1": set(["K", "Q", "R", "B", "N", "P"]),
        "..........pnbrqk w 0 1": set(["k", "q", "r", "b", "n", "p"]),
        "................ w 0 1": set(),
        "..KpQnRbBrNqP..k w 0 1": set(
            ["K", "Q", "R", "B", "N", "P", "k", "q", "r", "b", "n", "p"]
        ),
        "..KpQnbBrNP..k w 0 1": set(["K", "Q", "B", "N", "P", "k", "r", "b", "n", "p"]),
    }
    for test in tests:
        assert Position.get_pieces(test) == tests[test]


def test_get_current_pieces():
    tests = {
        ("KQRBNP....pnbrqk w 0 1", None): set(["K", "Q", "R", "B", "N", "P"]),
        ("KQRBNP....pnbrqk w 0 1", "w"): set(["K", "Q", "R", "B", "N", "P"]),
        ("KQRBNP....pnbrqk w 0 1", "b"): set(["k", "q", "r", "b", "n", "p"]),
        ("KQRBNP....pnbrqk b 0 1", None): set(["k", "q", "r", "b", "n", "p"]),
        ("KQRBNP....pnbrqk b 0 1", "w"): set(["K", "Q", "R", "B", "N", "P"]),
        ("KQRBNP....pnbrqk b 0 1", "b"): set(["k", "q", "r", "b", "n", "p"]),
        ("KQRBNP.......... w 0 1", None): set(["K", "Q", "R", "B", "N", "P"]),
        ("KQRBNP.......... w 0 1", "w"): set(["K", "Q", "R", "B", "N", "P"]),
        ("KQRBNP.......... w 0 1", "b"): set(),
        ("KQRBNP.......... b 0 1", None): set(),
        ("KQRBNP.......... b 0 1", "w"): set(["K", "Q", "R", "B", "N", "P"]),
        ("KQRBNP.......... b 0 1", "b"): set(),
        ("..........pnbrqk w 0 1", None): set(),
        ("..........pnbrqk w 0 1", "w"): set(),
        ("..........pnbrqk w 0 1", "b"): set(["k", "q", "r", "b", "n", "p"]),
        ("..........pnbrqk b 0 1", None): set(["k", "q", "r", "b", "n", "p"]),
        ("..........pnbrqk b 0 1", "w"): set(),
        ("..........pnbrqk b 0 1", "b"): set(["k", "q", "r", "b", "n", "p"]),
        ("..KpQnbBrNP..k w 0 1", None): set(["K", "Q", "B", "N", "P"]),
        ("..KpQnbBrNP..k w 0 1", "w"): set(["K", "Q", "B", "N", "P"]),
        ("..KpQnbBrNP..k w 0 1", "b"): set(["k", "r", "b", "n", "p"]),
        ("..KpQnbBrNP..k b 0 1", None): set(["k", "r", "b", "n", "p"]),
        ("..KpQnbBrNP..k b 0 1", "w"): set(["K", "Q", "B", "N", "P"]),
        ("..KpQnbBrNP..k b 0 1", "b"): set(["k", "r", "b", "n", "p"]),
    }
    for test in tests:
        assert Position.get_current_pieces(*test) == tests[test]


def test_check_position():
    # Note that the comment (???) refers to test cases with surprising and unintuitive
    # results; refer to the function comment on why these cases resolve such.
    tests = {
        "KQRBNP....pnbrqk w 0 1": (None, None),
        "KQRBNP....pnbrqk w 1 1": (None, None),
        "KQRBNP....pnbrqk w 50 26": (None, None),
        "KQRBNP....pnbrqk w 51 26": ("d", "50-move rule"),
        "K.k............q w 39 20": ("d", "stalemate"),
        "K.k............q b 39 20": (None, None),
        "K.k...........q. w 39 20": ("d", "stalemate"),
        "K.k...........q. b 39 20": (None, None),
        "K.kn............ w 39 20": ("b", "checkmate"),
        "K..........N..Pk w 39 20": (None, None),  # (???)
        "K..........N..Pk b 39 20": ("w", "checkmate"),
        "K.qr........RQ.k w 40 20": ("b", "checkmate"),  # (???)
        "K.qr........RQ.k b 40 20": ("w", "checkmate"),  # (???)
    }
    for test in tests:
        assert Position.check_position(test) == tests[test]
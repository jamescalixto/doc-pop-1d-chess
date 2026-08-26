#pragma once

#include "retro/tablereader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

/*
An interactive board over the solved tables.

Two problems have to be solved to make this usable, and they are separate.

FIRST, GETTING AT THE ANSWERS QUICKLY. `TableReader` maps the file rather than loading
it, so a session opens instantly and stays small. Everything here is a probe or two per
legal move, which is a handful of page touches.

SECOND, PLAYING WELL, which the table alone does not give you. A win/draw/loss table says
which moves win but not which wins fastest, so "pick any winning move" can shuffle back
and forth forever without approaching mate — the position stays won the whole time and
nothing progresses. That is the limitation `retro line` carries.

The fix here is to use the table as a perfect oracle inside a small search rather than as
the answer by itself. `mateDistance` below runs a bounded depth-first search in which the
winner only ever considers moves the table already calls winning, and the defender is
allowed everything. Because the table prunes every non-winning branch at depth one, the
tree is tiny — the search is only measuring the length of a line whose existence is
already proven, never looking for it. That yields exact distance-to-mate on demand from a
table that does not store distances, for the neighbourhood of the current position, which
is all an explorer needs.

Move choice is then: take the table's verdict first and never trade a win away for a
faster anything; among moves that tie on the verdict, pick the shortest mate when winning
and the longest resistance when losing; and if the horizon is not deep enough to see a
mate, fall back to preferring irreversible progress (a capture or a pawn push moves to a
strictly later slice and can never be undone).
*/
namespace retro
{

// How deep the on-demand distance search may look before giving up, in plies.
inline constexpr int MATE_SEARCH_PLIES = 24;

/*
Ceiling on nodes for one distance query. This is what keeps the prompt responsive: a mate
that is close is found long before the budget runs out, and one that is far away costs a
bounded amount to give up on. Raising it buys deeper distances at the cost of a pause on
every position whose mate is out of reach — which, from the opening, is all of them.
*/
inline constexpr unsigned long long MATE_SEARCH_NODES = 250000;

struct ScoredMove
{
    unsigned char move = 0;
    Value value = VALUE_UNRESOLVED; // from the mover's point of view, after negation
    int distance = -1;              // plies to mate, or -1 if not resolved within the horizon
    bool zeroing = false;           // capture or pawn move: resets the clock, leaves the slice
    bool legalInTable = false;
};

/*
Exact plies to mate from a position the table already calls decided, or -1 if that is
further away than `limit`.

Memoised on (board, side to move). The memo is per query: values here are distances under
optimal play from this position, and mixing them across queries would be sound but the
table is cheap enough that a fresh map per query is simpler and costs nothing measurable.
*/
class DistanceSearch
{
public:
    explicit DistanceSearch(const TableReader &tables) : reader(tables) {}

    /*
    Iterative deepening, so the cost is set by how close the mate is rather than by how
    far we were willing to look. A mate in 7 is proved by the depth-7 pass and never pays
    for depth 24; a mate beyond the horizon spends the node budget once and reports that
    it does not know. The memo is kept across passes — an entry records the limit it was
    computed at, so a deeper pass only reuses what was resolved at least as deeply.
    */
    int distance(unsigned long long board, bool active, int limit)
    {
        bool found = false;
        const Value value = reader.lookup(board, active, &found);
        if (!found || value == VALUE_DRAW || value == VALUE_ILLEGAL ||
            value == VALUE_UNRESOLVED)
        {
            return -1;
        }

        nodes = 0;
        aborted = false;
        for (int depth = 1; depth <= limit; ++depth)
        {
            const int result = (value > 0) ? searchWin(board, active, depth)
                                           : searchLoss(board, active, depth);
            if (aborted)
            {
                return -1; // a truncated search can only be a guess, so say nothing
            }
            if (result >= 0)
            {
                return result;
            }
        }
        return -1;
    }

    unsigned long long nodesSearched() const { return nodes; }
    bool ranOutOfBudget() const { return aborted; }

private:
    /*
    What is known about a position, in the only two forms that are sound to reuse.

    `exact` is the true distance and never expires. `failedAt` is the deepest limit at
    which this position was searched without resolving, so -1 may be reused for any
    shallower question but not a deeper one.

    Keeping these apart matters. A single "value plus the limit it came from" field
    tempts you into handing a caller a distance that overshoots the depth it asked for,
    and that quietly breaks the search: a winning node takes the minimum over its
    branches, so if one branch answers from the memo while its shorter sibling is cut off
    at the horizon, the node returns the longer mate as though it were the fastest.
    */
    struct Memo
    {
        int exact = -1;
        int failedAt = -1;
    };

    /*
    One map per side to move, rather than one map keyed on the board with the side folded
    in. A board already fills all 64 bits, so there is no spare bit to carry the side:
    any scheme that perturbs the board to encode it — an XOR with a constant, a flipped
    low bit — can land on another perfectly legal board, and the two positions would then
    share a memo entry and silently exchange distances. Indexing by side keeps the board
    itself as the whole key, so a collision is not possible rather than merely unlikely.
    */
    std::unordered_map<unsigned long long, Memo> memo[2];

    // A resolved distance, "not within `limit`" as -1, or -1 with `aborted` set.
    bool consultMemo(unsigned long long board, bool active, int limit, int &answer) const
    {
        const auto &side = memo[active ? 1 : 0];
        const auto it = side.find(board);
        if (it == side.end())
        {
            return false;
        }
        if (it->second.exact >= 0)
        {
            // Exact, but only an answer to *this* question if it fits the depth asked for.
            answer = (it->second.exact <= limit) ? it->second.exact : -1;
            return true;
        }
        if (it->second.failedAt >= limit)
        {
            answer = -1;
            return true;
        }
        return false;
    }

    void recordMemo(unsigned long long board, bool active, int limit, int result)
    {
        if (aborted)
        {
            return; // a result reached after the budget ran out is not knowledge
        }
        Memo &entry = memo[active ? 1 : 0][board];
        if (result >= 0)
        {
            entry.exact = result;
        }
        else
        {
            entry.failedAt = std::max(entry.failedAt, limit);
        }
    }

    // The side to move wins: it needs one move to a position lost for the opponent.
    int searchWin(unsigned long long board, bool active, int limit)
    {
        if (limit <= 0)
        {
            return -1;
        }
        if (++nodes > MATE_SEARCH_NODES)
        {
            aborted = true;
            return -1;
        }
        int cached = -1;
        if (consultMemo(board, active, limit, cached))
        {
            return cached;
        }

        unsigned char moves[MAX_MOVES];
        const unsigned int count = generateMoves(board, active, moves);

        int best = -1;
        for (unsigned int i = 0; i < count; ++i)
        {
            const unsigned long long child = applyMoveToBoard(board, moves[i]);
            bool found = false;
            const Value value = reader.lookup(child, !active, &found);
            if (!found || value >= 0 || value == VALUE_ILLEGAL)
            {
                continue; // only moves that leave the opponent lost can be on the fastest line
            }
            const int reply = searchLoss(child, !active, limit - 1);
            if (reply < 0)
            {
                continue;
            }
            if (best < 0 || reply + 1 < best)
            {
                best = reply + 1;
            }
        }

        recordMemo(board, active, limit, best);
        return best;
    }

    // The side to move is lost: it picks whichever move survives longest.
    int searchLoss(unsigned long long board, bool active, int limit)
    {
        if (++nodes > MATE_SEARCH_NODES)
        {
            aborted = true;
            return -1;
        }

        unsigned char moves[MAX_MOVES];
        const unsigned int count = generateMoves(board, active, moves);
        if (count == 0)
        {
            return 0; // mated now; a stalemate would have been a draw, not a loss
        }
        if (limit <= 0)
        {
            return -1;
        }

        int cached = -1;
        if (consultMemo(board, active, limit, cached))
        {
            return cached;
        }

        int worst = -1;
        for (unsigned int i = 0; i < count; ++i)
        {
            const unsigned long long child = applyMoveToBoard(board, moves[i]);
            const int reply = searchWin(child, !active, limit - 1);
            if (reply < 0)
            {
                worst = -1; // this defence escapes the horizon, so the distance is unknown
                break;
            }
            worst = std::max(worst, reply + 1);
        }

        recordMemo(board, active, limit, worst);
        return worst;
    }

    const TableReader &reader;
    unsigned long long nodes = 0;
    bool aborted = false;
};

/*
A game in progress: the position, everything that led to it, and enough history to call
repetition and the fifty-move rule the way the real rules do.
*/
class Game
{
public:
    struct Step
    {
        unsigned long long board;
        bool active;
        unsigned int halfmove;
        unsigned int fullmove;
        unsigned char move; // the move played FROM this step
    };

    Game() { reset(START_FENCE); }

    bool reset(const string &fence)
    {
        unsigned long long parsedBoard = 0;
        bool parsedActive = true;
        unsigned int parsedHalfmove = 0, parsedFullmove = 1;
        tie(parsedBoard, parsedActive, parsedHalfmove, parsedFullmove) =
            fenceToVars(fence, parsedBoard, parsedActive, parsedHalfmove, parsedFullmove);

        // A board with no kings means the string did not parse as a position.
        if (findNibble(parsedBoard, PIECE_KING) == NO_SQUARE ||
            findNibble(parsedBoard, PIECE_KING | COLOUR_BLACK) == NO_SQUARE)
        {
            return false;
        }

        board = parsedBoard;
        active = parsedActive;
        halfmove = parsedHalfmove;
        fullmove = parsedFullmove;
        history.clear();
        seen.clear();
        seen[board][active ? 1 : 0] = 1;
        return true;
    }

    void play(unsigned char move)
    {
        history.push_back({board, active, halfmove, fullmove, move});

        const bool resets = isPawn(getNthNibble(board, move >> 4)) ||
                            !isEmpty(getNthNibble(board, move & 15u));
        board = applyMoveToBoard(board, move);
        halfmove = resets ? 0 : halfmove + 1;
        if (!active)
        {
            ++fullmove;
        }
        active = !active;
        ++seen[board][active ? 1 : 0];
    }

    bool undo()
    {
        if (history.empty())
        {
            return false;
        }
        unsigned int &counter = seen[board][active ? 1 : 0];
        if (counter > 0)
        {
            --counter;
        }
        const Step &previous = history.back();
        board = previous.board;
        active = previous.active;
        halfmove = previous.halfmove;
        fullmove = previous.fullmove;
        history.pop_back();
        return true;
    }

    unsigned int repetitions() const
    {
        const auto it = seen.find(board);
        return (it == seen.end()) ? 0 : it->second[active ? 1 : 0];
    }

    // Non-empty when the game is over, describing how.
    string outcome() const
    {
        unsigned char moves[MAX_MOVES];
        const unsigned int count = generateMoves(board, active, moves);
        if (count == 0)
        {
            if (isInCheck(board, active))
            {
                return active ? "checkmate — black wins" : "checkmate — white wins";
            }
            return "stalemate — draw";
        }
        if (repetitions() >= 3)
        {
            return "draw by threefold repetition";
        }
        if (halfmove >= 100)
        {
            return "draw by the fifty-move rule";
        }
        return "";
    }

    string fence() const { return varsToFence(board, active, halfmove, fullmove); }

    unsigned long long boardValue() const { return board; }
    bool whiteToMove() const { return active; }
    unsigned int halfmoveClock() const { return halfmove; }
    unsigned int fullmoveNumber() const { return fullmove; }
    const std::vector<Step> &steps() const { return history; }

private:
    unsigned long long board = 0;
    bool active = true;
    unsigned int halfmove = 0;
    unsigned int fullmove = 1;
    std::vector<Step> history;
    /*
    How often each position has occurred, for the threefold rule. Keyed by board with the
    side to move as a subscript rather than mixed into the key: perturbing a board to
    carry one extra bit can produce another legal board, which would let two unrelated
    positions share a repetition count.
    */
    std::unordered_map<unsigned long long, std::array<unsigned int, 2>> seen;
};

/* ------------------------------------------------------------------------------------
   rendering
   ------------------------------------------------------------------------------------ */

inline string squareRuler()
{
    string out = "   ";
    for (unsigned int i = 0; i < BOARD_SIZE; ++i)
    {
        char cell[8];
        std::snprintf(cell, sizeof(cell), "%3u ", i);
        out += cell;
    }
    return out;
}

inline void printBoard(const Game &game, unsigned char highlightFrom = 255,
                       unsigned char highlightTo = 255)
{
    const string fence = game.fence();
    const string placement = fence.substr(0, BOARD_SIZE);

    std::printf("\n%s\n", squareRuler().c_str());
    std::printf("  +");
    for (unsigned int i = 0; i < BOARD_SIZE; ++i)
    {
        std::printf("---+");
    }
    std::printf("\n  |");
    for (unsigned int i = 0; i < BOARD_SIZE; ++i)
    {
        const char piece = placement[i];
        const bool marked = (i == highlightFrom || i == highlightTo);
        std::printf("%c%c%c|", marked ? '[' : ' ', piece, marked ? ']' : ' ');
    }
    std::printf("\n  +");
    for (unsigned int i = 0; i < BOARD_SIZE; ++i)
    {
        std::printf("---+");
    }
    std::printf("\n\n");
}

inline string moveName(unsigned long long board, unsigned char move)
{
    const unsigned int from = move >> 4;
    const unsigned int to = move & 15u;
    const unsigned int piece = getNthNibble(board, from);
    const bool capture = !isEmpty(getNthNibble(board, to));

    char out[32];
    std::snprintf(out, sizeof(out), "%c %u%c%u", bitsToPiece(piece), from, capture ? 'x' : '-', to);
    return out;
}

/*
Is `a` the better move to play? The verdict always comes first — no distance is worth
trading a win for a draw — and only moves that tie on it are separated by length.

The asymmetry is the point. A winner wants the shortest mate, so an unresolved distance
(the mate is further off than the search looked) is the worst news available. A defender
wants the longest, so for them an unresolved distance is the *best* news: it means the
opponent needs more than the whole horizon to finish the job.
*/
inline bool moveIsBetter(const ScoredMove &a, const ScoredMove &b)
{
    const int orderA = valueOrder(a.value == VALUE_UNRESOLVED ? VALUE_DRAW : a.value);
    const int orderB = valueOrder(b.value == VALUE_UNRESOLVED ? VALUE_DRAW : b.value);
    if (orderA != orderB)
    {
        return orderA > orderB;
    }

    const bool defending = (a.value < 0);

    if ((a.distance >= 0) != (b.distance >= 0))
    {
        return defending ? (a.distance < 0) : (a.distance >= 0);
    }
    if (a.distance >= 0 && a.distance != b.distance)
    {
        return defending ? (a.distance > b.distance) : (a.distance < b.distance);
    }

    // Nothing to separate them on length: when winning, a capture or pawn push is
    // irreversible progress and a quiet move may just be shuffling.
    if (a.value > 0 && a.zeroing != b.zeroing)
    {
        return a.zeroing;
    }
    return a.move < b.move;
}

// Equally good as far as the table and the distance search can tell, ignoring tiebreaks.
inline bool sameQuality(const ScoredMove &a, const ScoredMove &b)
{
    const int orderA = valueOrder(a.value == VALUE_UNRESOLVED ? VALUE_DRAW : a.value);
    const int orderB = valueOrder(b.value == VALUE_UNRESOLVED ? VALUE_DRAW : b.value);
    return orderA == orderB && a.distance == b.distance;
}

/*
Every legal move with the table's verdict on it, best first. Distances are filled in only
when asked for, because each one is a search.
*/
inline std::vector<ScoredMove> scoreMoves(const TableReader &reader, const Game &game,
                                          bool withDistance)
{
    const unsigned long long board = game.boardValue();
    const bool active = game.whiteToMove();

    unsigned char moves[MAX_MOVES];
    const unsigned int count = generateMoves(board, active, moves);

    std::vector<ScoredMove> scored;
    scored.reserve(count);

    // One search across every move: sibling subtrees overlap heavily, so a shared memo
    // makes the whole list cost little more than the first entry.
    DistanceSearch search(reader);

    for (unsigned int i = 0; i < count; ++i)
    {
        ScoredMove entry;
        entry.move = moves[i];
        entry.zeroing = isPawn(getNthNibble(board, moves[i] >> 4)) ||
                        !isEmpty(getNthNibble(board, moves[i] & 15u));

        const unsigned long long child = applyMoveToBoard(board, moves[i]);
        bool found = false;
        const Value childValue = reader.lookup(child, !active, &found);
        entry.legalInTable = found;
        entry.value = found ? reader.negate(childValue) : VALUE_UNRESOLVED;

        // A distance-to-mate table already stores exact distances in the value itself,
        // so the reconstruction search would be redundant work whose answer is ignored.
        if (withDistance && !reader.tracksDistance() && found && entry.value != VALUE_DRAW &&
            entry.value != VALUE_ILLEGAL && entry.value != VALUE_UNRESOLVED)
        {
            const int childDistance = search.distance(child, !active, MATE_SEARCH_PLIES);
            entry.distance = (childDistance < 0) ? -1 : childDistance + 1;
        }
        scored.push_back(entry);
    }

    std::stable_sort(scored.begin(), scored.end(), moveIsBetter);
    return scored;
}

/*
`searched` says whether the distance search was allowed to run. It matters for what an
absent distance means: with the search off it means nobody asked, and with it on it means
the mate is further away than the horizon — which is worth saying, because for a defender
that is the difference between resisting for eleven plies and resisting indefinitely.
*/
inline string describeMoveValue(const TableReader &reader, const ScoredMove &entry,
                                bool searched)
{
    if (!entry.legalInTable)
    {
        return "not in tables";
    }
    string text = reader.describe(entry.value);
    if (reader.tracksDistance())
    {
        return text;
    }
    if (entry.distance >= 0)
    {
        text += " in " + std::to_string(entry.distance);
    }
    else if (searched && (entry.value == VALUE_WIN || entry.value == VALUE_LOSS))
    {
        text += " in more than " + std::to_string(MATE_SEARCH_PLIES);
    }
    return text;
}

inline void printMoves(const TableReader &reader, const std::vector<ScoredMove> &scored,
                       const Game &game, bool searched)
{
    if (scored.empty())
    {
        std::printf("  no legal moves\n\n");
        return;
    }

    std::printf("  %-14s %-24s %s\n", "move", "table says", "");
    std::printf("  ------------------------------------------------------\n");
    for (const ScoredMove &entry : scored)
    {
        std::printf("  %-14s %-24s %s\n", moveName(game.boardValue(), entry.move).c_str(),
                    describeMoveValue(reader, entry, searched).c_str(),
                    sameQuality(entry, scored.front()) ? "<- best" : "");
    }
    std::printf("\n");
}

/*
Parse a move the way a person would type it: "5-6", "5 6", "56", "P5-6", "5x6". Returns
false if it does not name a legal move in this position.
*/
inline bool parseMove(const Game &game, const string &text, unsigned char &move)
{
    string digits;
    for (const char c : text)
    {
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            digits += c;
        }
        else if (c == '-' || c == 'x' || c == ',' || c == ' ')
        {
            digits += ' ';
        }
    }

    unsigned int from = 0, to = 0;
    bool parsed = false;
    {
        std::stringstream ss(digits);
        if (ss >> from >> to)
        {
            parsed = true;
        }
    }
    if (!parsed)
    {
        // "56" with no separator: two squares run together, only unambiguous below 10.
        string compact;
        for (const char c : digits)
        {
            if (!std::isspace(static_cast<unsigned char>(c)))
            {
                compact += c;
            }
        }
        if (compact.size() == 2)
        {
            from = static_cast<unsigned int>(compact[0] - '0');
            to = static_cast<unsigned int>(compact[1] - '0');
            parsed = true;
        }
    }
    if (!parsed || from >= BOARD_SIZE || to >= BOARD_SIZE)
    {
        return false;
    }

    const unsigned char candidate = static_cast<unsigned char>((from << 4) | to);
    unsigned char moves[MAX_MOVES];
    const unsigned int count = generateMoves(game.boardValue(), game.whiteToMove(), moves);
    for (unsigned int i = 0; i < count; ++i)
    {
        if (moves[i] == candidate)
        {
            move = candidate;
            return true;
        }
    }
    return false;
}

inline void printStatus(const TableReader &reader, const Game &game, bool withDistance)
{
    bool found = false;
    const Value value = reader.lookup(game.boardValue(), game.whiteToMove(), &found);

    std::printf("%s\n", game.fence().c_str());
    std::printf("  %s to move", game.whiteToMove() ? "white" : "black");

    if (!found)
    {
        std::printf(" — not in the loaded tables\n");
    }
    else
    {
        string verdict = reader.describe(value);
        if (withDistance && !reader.tracksDistance() && value != VALUE_DRAW &&
            value != VALUE_ILLEGAL)
        {
            DistanceSearch search(reader);
            const int distance = search.distance(game.boardValue(), game.whiteToMove(),
                                                 MATE_SEARCH_PLIES);
            if (distance >= 0)
            {
                verdict += " in " + std::to_string(distance);
            }
        }
        std::printf(" — %s\n", verdict.c_str());
    }

    std::printf("  clock %u/100", game.halfmoveClock());
    if (game.repetitions() > 1)
    {
        std::printf(", seen %u times", game.repetitions());
    }
    std::printf("\n");

    const string over = game.outcome();
    if (!over.empty())
    {
        std::printf("\n  *** %s ***\n", over.c_str());
    }
}

inline void printHelp()
{
    std::printf(
        "\n"
        "  5-6, 56, P5-6   play a move (squares are 0-15, left to right)\n"
        "  moves           list every legal move with the table's verdict\n"
        "  best            play the table's choice for the side to move\n"
        "  run [n]         let the table play both sides for n plies (default 40)\n"
        "  auto w|b|off    let the table answer for one colour after each of your moves\n"
        "  undo            take back one ply\n"
        "  new [fence]     restart from the start position, or from a position you give\n"
        "  hist            the moves played so far\n"
        "  dist on|off     work out exact distance to mate for each move (a small search)\n"
        "  board           redraw\n"
        "  help, quit\n"
        "\n");
}

inline void printHistory(const Game &game)
{
    const auto &steps = game.steps();
    if (steps.empty())
    {
        std::printf("  no moves played\n\n");
        return;
    }
    std::printf("\n");
    for (std::size_t i = 0; i < steps.size(); ++i)
    {
        if (steps[i].active)
        {
            std::printf("  %2u. %-10s", steps[i].fullmove, moveName(steps[i].board, steps[i].move).c_str());
        }
        else
        {
            std::printf("%s\n", moveName(steps[i].board, steps[i].move).c_str());
        }
    }
    if (!steps.empty() && steps.back().active)
    {
        std::printf("\n");
    }
    std::printf("\n");
}

/*
The prompt. `dist` defaults on: it makes the move list say "win in 11" rather than "win",
which is the difference between a table dump and something you can actually play against.
*/
inline int commandPlay(const string &tablePath, const string &startFence)
{
    TableReader reader;
    if (!reader.open(tablePath))
    {
        return 1;
    }

    std::printf("opened %s\n  %llu slices, %.2f GiB mapped, %s%s\n",
                tablePath.c_str(), static_cast<unsigned long long>(reader.sliceCount()),
                reader.mappedBytes() / (1024.0 * 1024.0 * 1024.0),
                reader.mode() == Mode::Wdl   ? "win/draw/loss"
                : reader.mode() == Mode::Dtm ? "distance to mate"
                                             : "win/draw/loss with the fifty-move rule",
                reader.mirrored() ? ", mirrored" : "");

    if (reader.mode() == Mode::Wdl)
    {
        std::printf("  note: this table ignores the fifty-move rule, so a win it reports may\n"
                    "  not survive the clock. A dtz table settles that.\n");
    }

    Game game;
    if (!startFence.empty() && !game.reset(startFence))
    {
        std::fprintf(stderr, "could not read \"%s\" as a position\n", startFence.c_str());
        return 1;
    }

    bool withDistance = true;
    int autoColour = -1; // -1 none, 0 black, 1 white

    printHelp();
    printBoard(game);
    printStatus(reader, game, withDistance);

    string line;
    for (;;)
    {
        std::printf("\n> ");
        std::fflush(stdout);
        if (!std::getline(std::cin, line))
        {
            std::printf("\n");
            break;
        }

        // Split off the first word.
        string command, rest;
        {
            std::stringstream ss(line);
            ss >> command;
            std::getline(ss, rest);
            while (!rest.empty() && rest.front() == ' ')
            {
                rest.erase(rest.begin());
            }
        }
        if (command.empty())
        {
            continue;
        }
        std::transform(command.begin(), command.end(), command.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (command == "quit" || command == "exit" || command == "q")
        {
            break;
        }
        if (command == "help" || command == "?")
        {
            printHelp();
            continue;
        }
        if (command == "board")
        {
            printBoard(game);
            printStatus(reader, game, withDistance);
            continue;
        }
        if (command == "hist" || command == "history")
        {
            printHistory(game);
            continue;
        }
        if (command == "dist")
        {
            withDistance = (rest != "off");
            std::printf("  distance search %s\n", withDistance ? "on" : "off");
            continue;
        }
        if (command == "auto")
        {
            if (rest == "w" || rest == "white") autoColour = 1;
            else if (rest == "b" || rest == "black") autoColour = 0;
            else autoColour = -1;
            std::printf("  table answers for %s\n",
                        autoColour == 1 ? "white" : autoColour == 0 ? "black" : "nobody");
            continue;
        }
        if (command == "new")
        {
            if (!game.reset(rest.empty() ? START_FENCE : rest))
            {
                std::printf("  could not read that as a position\n");
                continue;
            }
            printBoard(game);
            printStatus(reader, game, withDistance);
            continue;
        }
        if (command == "undo")
        {
            if (!game.undo())
            {
                std::printf("  nothing to take back\n");
                continue;
            }
            printBoard(game);
            printStatus(reader, game, withDistance);
            continue;
        }
        if (command == "moves")
        {
            printMoves(reader, scoreMoves(reader, game, withDistance), game, withDistance);
            continue;
        }
        if (command == "best" || command == "run")
        {
            int plies = 1;
            if (command == "run")
            {
                plies = rest.empty() ? 40 : std::atoi(rest.c_str());
                if (plies <= 0) plies = 40;
            }
            for (int i = 0; i < plies; ++i)
            {
                if (!game.outcome().empty())
                {
                    break;
                }
                const auto scored = scoreMoves(reader, game, withDistance);
                if (scored.empty())
                {
                    break;
                }
                std::printf("  %s plays %s (%s)\n", game.whiteToMove() ? "white" : "black",
                            moveName(game.boardValue(), scored.front().move).c_str(),
                            describeMoveValue(reader, scored.front(), withDistance).c_str());
                game.play(scored.front().move);
            }
            printBoard(game);
            printStatus(reader, game, withDistance);
            continue;
        }

        // Anything else should be a move.
        unsigned char move = 0;
        if (!parseMove(game, line, move))
        {
            std::printf("  \"%s\" is not a legal move here. Try `moves`.\n", line.c_str());
            continue;
        }

        game.play(move);
        printBoard(game, move >> 4, move & 15u);
        printStatus(reader, game, withDistance);

        const bool wantsReply = (autoColour == 1 && game.whiteToMove()) ||
                                (autoColour == 0 && !game.whiteToMove());
        if (wantsReply && game.outcome().empty())
        {
            const auto scored = scoreMoves(reader, game, withDistance);
            if (!scored.empty())
            {
                std::printf("\n  %s plays %s (%s)\n", game.whiteToMove() ? "white" : "black",
                            moveName(game.boardValue(), scored.front().move).c_str(),
                            describeMoveValue(reader, scored.front(), withDistance).c_str());
                game.play(scored.front().move);
                printBoard(game, scored.front().move >> 4, scored.front().move & 15u);
                printStatus(reader, game, withDistance);
            }
        }
    }

    return 0;
}

} // namespace retro

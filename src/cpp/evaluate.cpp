#include "position.h"
#include <functional>
#include <tuple>
#include <unordered_map>
#include <vector>

using std::function;
using std::tuple, std::make_tuple, std::tie;
using std::unordered_map;
using std::vector;

// Value of game outcomes.
const int SCORE_WIN = 100;
const int SCORE_LOSS = -100;
const int SCORE_DRAW = 0;
const int SCORE_UNFINISHED = -1;

/*
Given a board, score it for the given player using an estimate.
Piece values are from regular chess and may need tweaking.
*/
int scorePositionEstimate(unsigned long long board, bool active)
{
    int score = 0; // default to scoring white. Invert if scoring black.
    unsigned int pieceSet = getPieceSet(board);

    // Get score from bitshifting loop.
    score -= (pieceSet & 1) * 1;
    pieceSet = pieceSet >> 1;
    score -= (pieceSet & 1) * 3;
    pieceSet = pieceSet >> 1;
    score -= (pieceSet & 1) * 3;
    pieceSet = pieceSet >> 1;
    score -= (pieceSet & 1) * 5;
    pieceSet = pieceSet >> 1;
    score -= (pieceSet & 1) * 9;
    pieceSet = pieceSet >> 1;
    score -= (pieceSet & 1) * 100;
    pieceSet = pieceSet >> 1;
    score += (pieceSet & 1) * 1;
    pieceSet = pieceSet >> 1;
    score += (pieceSet & 1) * 3;
    pieceSet = pieceSet >> 1;
    score += (pieceSet & 1) * 3;
    pieceSet = pieceSet >> 1;
    score += (pieceSet & 1) * 5;
    pieceSet = pieceSet >> 1;
    score += (pieceSet & 1) * 9;
    pieceSet = pieceSet >> 1;
    score += (pieceSet & 1) * 100;

    // Return score if active, otherwise return negative score.
    return ((int(active) * 2) - 1) * score;
}

/*
Given a board, score it for the given player if the game is over.
*/
int scorePositionDefinite(unsigned long long board, bool active, unsigned int halfmove, unsigned int fullmove)
{
    int state = checkPosition(board, active, halfmove, fullmove);
    switch (state)
    {
    case 4:
        return SCORE_DRAW;
    case 9:
        return (active ? SCORE_WIN : SCORE_LOSS);
    case 8:
        return (active ? SCORE_LOSS : SCORE_WIN);
    case 5:
        return SCORE_DRAW;
    case 6:
        return SCORE_DRAW;
    case 7:
        return SCORE_DRAW;
    default:
        return SCORE_UNFINISHED;
    }
}

/*
Given a position, score it (assuming that the opponent plays optimally) and return the
score and the path to that end state. Uses breadth-first-search recursively with a depth
limit, after which it estimates the position using an estimator function.
*/
tuple<int, vector<unsigned int>> scorePosition(
    unsigned long long board,
    bool active,                // starting player to optimize score for.
    unsigned int halfmove,      // halfmove count.
    unsigned int fullmove,      // fullmove count.
    int alpha = SCORE_LOSS - 1, // minimum score that the maximizing player is assured of.
    int beta = SCORE_WIN + 1,   // maximum depth to search, in ply (a turn by a single player).
    unsigned int depth = 0,     // current depth.
    unsigned int maxDepth = -1, // maximum depth to search, in ply (a turn by a single player).
    function<int(
        unsigned long long,
        bool,
        unsigned int,
        unsigned int)>
        maxDepthHeuristic = scorePositionDefinite, // function to use to estimate score.
    function<vector<unsigned int>(
        unsigned long long,
        bool)>
        nextMoveHeuristic = getMoves,                                // function to use to order moves.
    vector<unsigned int> movelist = {},                              // list of moves made so far.
    unordered_map<unsigned long long, unsigned int> seenBoards = {}, // counter of seen boards; used for threefold repetition.
    bool findShortestLine = true)                                    // prioritize finding shortest line (longer).
{
}

int main()
{
    importLookupTables(attackLookup);
}
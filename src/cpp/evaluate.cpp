#include "position.h"
#include <functional>

using std::function;
using std::max, std::min;

// Value of game outcomes.
const int SCORE_WIN = 100;
const int SCORE_LOSS = -100;
const int SCORE_DRAW = 0;
const int SCORE_UNFINISHED = -999;

/*
Given a board, score it for the given player using an estimate.
Piece values are from regular chess and may need tweaking.
*/
int scorePositionEstimate(
    bool startingPlayer,
    unsigned long long board)
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
    return ((int(startingPlayer) * 2) - 1) * score;
}

/*
Given a board, score it for the given player if the game is over.
*/
int scorePositionDefinite(
    bool startingPlayer,
    unsigned long long board,
    bool active,
    unsigned int halfmove,
    unsigned int fullmove)
{
    int state = checkPosition(board, active, halfmove, fullmove);
    switch (state)
    {
    case 4:
        return SCORE_DRAW;
    case 9:
        return (startingPlayer ? SCORE_WIN : SCORE_LOSS);
    case 8:
        return (startingPlayer ? SCORE_LOSS : SCORE_WIN);
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
    bool startingPlayer,        // starting player to optimize score for.
    int maxDepth,               // maximum depth to search, in ply (a turn by a single player).
    unsigned long long board,   // current board state.
    bool active,                // current player to move.
    unsigned int halfmove,      // halfmove count.
    unsigned int fullmove,      // fullmove count.
    int alpha = SCORE_LOSS - 1, // minimum score that the maximizing player is assured of.
    int beta = SCORE_WIN + 1,   // maximum depth to search, in ply (a turn by a single player).
    int depth = 0,              // current depth.
    function<int(
        bool,
        unsigned long long)>
        maxDepthHeuristic = scorePositionEstimate, // function to use to estimate score.
    function<vector<unsigned int>(
        unsigned long long,
        bool)>
        nextMoveHeuristic = getMoves,                                // function to use to order moves.
    vector<unsigned int> movelist = {},                              // list of moves made so far.
    unordered_map<unsigned long long, unsigned int> seenBoards = {}, // counter of seen boards; used for threefold repetition.
    bool findShortestLine = true)                                    // prioritize finding shortest line (longer).
{
    // Check for draw via threefold repetition using the boards we've seen.
    if (seenBoards.contains(board) && seenBoards[board] >= 3)
    {
        return make_tuple(SCORE_DRAW, movelist);
    }

    // Check if game is over by other means.
    int definiteScore = scorePositionDefinite(startingPlayer, board, active, halfmove, fullmove);
    if (definiteScore != SCORE_UNFINISHED)
    {
        return make_tuple(definiteScore, movelist);
    }

    // If not, the game isn't over so we need to score the position.
    // If we are at max depth, use the estimator to score.
    if (depth == maxDepth)
    {
        return make_tuple(maxDepthHeuristic(startingPlayer, board), movelist);
    }

    // Otherwise, we are not at max depth, so we need to score the position.
    // We want the best possible score for the starting player, but we also assume that
    // the opponent plays optimally. Thus if it's the starting player's turn, pick the
    // move that gives the best score for the starting player; if it's the opponent's
    // turn, pick the move that gives the worst score for the starting player.

    // We can save time by returning SCORE_WHITE_WIN or SCORE_BLACK_WIN immediately, if
    // it's the best/worst score as above (because we know that other branches can't
    // beat it).
    vector<unsigned int> potentialMoves = nextMoveHeuristic(board, active);

    // Store best score to compare against and the moves that lead to it.
    int bestScore = (active == startingPlayer) ? SCORE_LOSS - 1 : SCORE_WIN + 1;
    vector<unsigned int> bestMovelist;

    // Iterate over moves.
    for (unsigned int potentialMove : potentialMoves)
    {
        // Declare variables to store position information.
        unsigned long long potentialBoard;
        bool potentialActive;
        unsigned int potentialHalfmove, potentialFullmove;

        tie(potentialBoard, potentialActive, potentialHalfmove, potentialFullmove) =
            applyMove(board,
                      active,
                      halfmove,
                      fullmove,
                      potentialMove);

        // Make a copy of the movelist and add the current move.
        vector<unsigned int> potentialMovelist(movelist);
        potentialMovelist.push_back(potentialMove);

        // Make a copy of seen boards and increment the current board in it.
        unordered_map<unsigned long long, unsigned int> potentialSeenBoards(seenBoards);
        potentialSeenBoards[board]++;

        // Get the score of this potential position via recursion.
        int predictedScore;
        vector<unsigned int> predictedMovelist;
        tie(predictedScore, predictedMovelist) =
            scorePosition(
                startingPlayer,
                maxDepth,
                potentialBoard,
                potentialActive,
                potentialHalfmove,
                potentialFullmove,
                alpha,
                beta,
                depth + 1,
                maxDepthHeuristic,
                nextMoveHeuristic,
                potentialMovelist,
                potentialSeenBoards,
                findShortestLine);

        // Perform alpha-beta pruning.
        if (active == startingPlayer) // maximizing player.
        {
            if (predictedScore > bestScore ||
                (findShortestLine && predictedScore == bestScore && predictedMovelist.size() < bestMovelist.size()))
            { // if we've found an even better score, or a shorter path to the best score...
                bestScore = predictedScore;
                bestMovelist = predictedMovelist;
            }
            if (bestScore >= beta &&
                (!findShortestLine || predictedMovelist.size() >= bestMovelist.size()))
            { // if we can't get any better and the line isn't shorter... then prune.
                break;
            }
            alpha = max(alpha, bestScore);
            if (!findShortestLine && bestScore == SCORE_WIN)
            { // abort early if we've found a win.
                return make_tuple(bestScore, bestMovelist);
            }
        }
        else
        { // similar (but opposite) case for the minimizing player.
            if (predictedScore < bestScore ||
                (findShortestLine && predictedScore == bestScore && predictedMovelist.size() < bestMovelist.size()))
            {
                bestScore = predictedScore;
                bestMovelist = predictedMovelist;
            }
            if (bestScore <= alpha &&
                (!findShortestLine || predictedMovelist.size() >= bestMovelist.size()))
            {
                break;
            }
            beta = min(beta, bestScore);
            if (!findShortestLine && bestScore == SCORE_LOSS)
            { // abort early if we've found a loss.
                return make_tuple(bestScore, bestMovelist);
            }
        }
    }
    return make_tuple(bestScore, bestMovelist);
}

void evaluateFence(string fence, int maxDepth)
{
    importLookupTables(attackLookup);

    // Declare variables to store position information.
    unsigned long long board;
    bool active;
    unsigned int halfmove, fullmove;
    tie(board, active, halfmove, fullmove) = fenceToVars(fence, board, active, halfmove, fullmove);

    // Get the prediction.
    int predictedScore;
    vector<unsigned int> predictedMovelist;
    tie(predictedScore, predictedMovelist) = scorePosition(active, maxDepth, board, active, halfmove, fullmove);

    std::cout << "[" << (active ? "w" : "b") << "] " << (predictedScore > 0 ? "+" : "") << predictedScore << "  (depth=" << maxDepth << ")" << std::endl;
    std::cout << varsToFence(board, active, halfmove, fullmove) << "  start" << std::endl;
    for (unsigned int m : predictedMovelist)
    {
        tie(board, active, halfmove, fullmove) = applyMove(board, active, halfmove, fullmove, m);
        string s = varsToFence(board, active, halfmove, fullmove);
        std::cout << s << "  after (" << (m >> 4) << "," << (m & 15) << ")" << std::endl;
    }
    std::cout << std::endl;
}

int main()
{
    evaluateFence("K....n.........k b 0 1", 10);
    // evaluateFence("KQRB..NP.p.nbrqk b 0 1", 10); // should be b +100
    // evaluateFence("KQRBN.P.pn..brqk w 0 1", 10); // should be w +100
    // evaluateFence(START_FENCE, 16);
    // evaluateFence(START_FENCE, 20);
    // evaluateFence(START_FENCE, 24);
}
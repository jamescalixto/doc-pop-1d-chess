#include "position.h"
#include "evaluate.cpp"
#include <functional>

/*********************************************************
Experimental, fast version of evaluation.
*********************************************************/

/*
Given an int representing a piece, return the value it's worth.
Piece values are from regular chess and may need tweaking.
*/
char bitsToValue(unsigned int i)
{
    switch (i)
    {
    case 0:
        return 0;
    case 1:
        return 1;
    case 2:
        return 3;
    case 5:
        return 3;
    case 6:
        return 5;
    case 7:
        return 9;
    case 3:
        return 99;
    case 9:
        return 1;
    case 10:
        return 3;
    case 13:
        return 3;
    case 14:
        return 5;
    case 15:
        return 9;
    case 11:
        return 99;
    default:
        return 0;
    }
}

/*
Comparator for boards, moves, and values tuple.
*/
bool compareBoardsMovesValues(tuple<unsigned long long, unsigned int, unsigned int> t1, tuple<unsigned long long, unsigned int, unsigned int> t2)
{
    return get<2>(t1) > get<2>(t2);
}

/*
Given a set of boards, return all possible next boards. Unlike
looping over getNextBoards, this attempts to do this all in bulk, so some results are
cached. This (in theory) should lead to a speedup, at the cost of not having nice and
composable functions.
*/
set<unsigned long long> getNextBoardsBulkFast(set<unsigned long long> boards, bool player)
{
    vector<tuple<unsigned long long, unsigned int, unsigned int>> nextBoardsMovesValues;

    // Iterate over each board, getting all possible (and importantly, not necessarily
    // legal) next boards.
    for (unsigned long long board : boards)
    {
        unsigned long long originalBoard = board;
        unsigned int opponentAttackedSquares = getAttackedSquares(board, !player);
        unsigned int occupancy = getOccupancy(board);                     // store occupancy.
        unsigned int playerOccupancy = getPlayerOccupancy(board, player); // store player occupancy.

        for (int start = BOARD_SIZE - 1; start >= 0; start--) // check every square for attacks.
        {
            // We are checking the [start] indexed space, from the left.
            unsigned int piece_nibble = getLastNibble(board); // get last nibble.
            if (isPieceOfPlayer(piece_nibble, player))
            {
                if (piece_nibble > 9)
                {                      // black, non-pawn piece.
                    piece_nibble %= 8; // get the white piece equivalent.
                }
                unsigned long long key = (piece_nibble << 20) | (start << 16) | (occupancy);
                unsigned int movementSquares = attackLookup[key];
                unsigned int validMovementSquares = movementSquares & (~playerOccupancy);

                // Extra pawn movement, if available.
                if (piece_nibble == 1 && start == 5 && !((occupancy >> 8) & 3))
                // is white pawn in starting position and board does not have anything in spaces 6 and 7.
                {
                    validMovementSquares |= 256; // add index 7 to possible movement.
                }
                else if (piece_nibble == 9 && start == 10 && !((occupancy >> 6) & 3))
                // is black pawn in starting position and board does not have anything in spaces 8 and 9.
                {
                    validMovementSquares |= 128; // add index 8 to possible movement.
                }

                // Loop over ending squares in validMovementSquares.
                // This way we get all the (start, end) moves.
                for (int end = BOARD_SIZE - 1; end >= 0; end--)
                {
                    if (1 & validMovementSquares)
                    {                                                                                       // check if this has been flagged as a valid end move.
                        unsigned int nextMove = (start << 4) | end;                                         // build move.
                        unsigned long long nextBoard = applyMoveToBoard(board, nextMove);                   // build new board.
                        unsigned int nextValue = bitsToValue(getNthNibble(board, getLastNibble(nextMove))); // get value of the destination square.
                        nextBoardsMovesValues.push_back(make_tuple(nextBoard, nextMove, nextValue));        // apply move.
                    }
                    validMovementSquares = validMovementSquares >> 1;
                }
            }
            board = board >> 4;
        }
    }

    // Build lambda to use and erase if new board puts player in check.
    auto isInCheckTest = [&](tuple<> tup)
    { return get<0>(tup); };
    std::erase_if(nextBoardsMovesValues, isInCheckTest);

    // Sort by projected greedy value of move.
    sort(nextBoardsMovesValues.begin(), nextBoardsMovesValues.end(), compareBoardsMovesValues);

    set<unsigned long long> nextBoards;
    for (tuple<unsigned long long, unsigned int, unsigned int> t : nextBoardsMovesValues)
    {
        nextBoards.insert(get<0>(t));
    }
    return nextBoards;
}

/*
Given a position, score it (assuming that the opponent plays optimally) and return the
score and the path to that end state. Uses breadth-first-search recursively with a depth
limit, after which it estimates the position using an estimator function.

Implements some optimizations that are not in the regular version of this function:
- ignores threefold repetition.
*/
tuple<int, vector<unsigned int>> scorePositionFast(
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
    vector<unsigned int> movelist = {},            // list of moves made so far.
    bool findShortestLine = false)                 // prioritize finding shortest line (longer).
{
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

        // Get the score of this potential position via recursion.
        int predictedScore;
        vector<unsigned int> predictedMovelist;
        tie(predictedScore, predictedMovelist) =
            scorePositionFast(
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
    tie(predictedScore, predictedMovelist) = scorePositionFast(active, maxDepth, board, active, halfmove, fullmove);

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
    // evaluateFence("K....n.........k b 0 1", 10);
    // evaluateFence("KQRB..NP.p.nbrqk b 0 1", 10); // should be b +100
    // evaluateFence("KQRBN.P.pn..brqk w 0 1", 10); // should be w +100
    evaluateFence(START_FENCE, 16);
    // evaluateFence(START_FENCE, 20);
    // evaluateFence(START_FENCE, 24);
}
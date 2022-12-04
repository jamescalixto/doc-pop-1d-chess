#include "position.h"

// Value of game outcomes.
const int SCORE_WIN = 100;
const int SCORE_LOSS = -100;
const int SCORE_DRAW = 0;

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
int scorePositionDefinite(unsigned long long board, bool active, unsigned int fullmove)
{
}

int main()
{
    importLookupTables(attackLookup);
}
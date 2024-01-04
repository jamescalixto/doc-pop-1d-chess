#include "evaluate.h"

/*
 * Script to help generate endgame tables for faster endgame resolution.
 * Essentially iterating through all possible endgame states for a given set of pieces
 * and writing the results to a file.
 */

/*
 * Generate boards with all possible legal king configurations on them (the white king
 * must be to the left of the black king).
 */
vector<unsigned long long> generateKingBoards()
{
    vector<unsigned long long> kingBoards;
    for (unsigned int wk = 0; wk < BOARD_SIZE - 2; wk++) // BOARD_SIZE - 2 as white king cannot be on the last two squares.
    {
        for (unsigned int bk = wk + 2; bk < BOARD_SIZE; bk++)
        {
            unsigned long long kingBoard = 0;
            kingBoard = insertNthNibble(kingBoard, 3, wk);  // insert white king.
            kingBoard = insertNthNibble(kingBoard, 11, bk); // insert black king.
            kingBoards.push_back(kingBoard);
        }
    }
    kingBoards.shrink_to_fit();
    return kingBoards;
}

void testKingBoards()
{
    int score;
    vector<unsigned int> path;
    for (unsigned long long kingBoard : KING_BOARDS)
    {
        print(varsToFence(kingBoard, true, 0, 0));
        tie(score, path) = scorePosition(true, 16, kingBoard, true, 0, 0);
        print(score);

        break;
    }
}

int main()
{
    importLookupTables(attackLookup);
    vector<unsigned long long> boards;
    for (unsigned int wk = 0; wk < BOARD_SIZE - 2; wk++) // BOARD_SIZE - 2 as white king cannot be on the last two squares.
    {
        for (unsigned int bk = wk + 2; bk < BOARD_SIZE; bk++)
        {
            for (unsigned int k = 0; k < BOARD_SIZE; k++)
            {
                if (k != wk && k != bk)
                {
                    unsigned long long board = 0;
                    board = insertNthNibble(board, 3, wk);  // insert white king.
                    board = insertNthNibble(board, 11, bk); // insert black king.
                    board = insertNthNibble(board, 7, k);
                    boards.push_back(board);
                }
            }
        }
    }
    boards.shrink_to_fit();

    int score;
    vector<unsigned int> path;
    for (unsigned long long board : boards)
    {
        print(varsToFence(board, true, 0, 0));
        tie(score, path) = scorePosition(false, 8, board, false, 0, 0);
        print(score);
    }
}
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

int main()
{
    vector<unsigned long long> kingBoards = generateKingBoards();
    for (unsigned long long board : kingBoards)
    {
        std::cout << board << std::endl;
    }
}
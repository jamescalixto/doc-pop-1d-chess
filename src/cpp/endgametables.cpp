#include "evaluate.h"
#include <algorithm>

using std::prev_permutation;

/*
 * Script to help generate endgame tables for faster endgame resolution.
 * Essentially iterating through all possible endgame states for a given set of pieces
 * and writing the results to a file.
 */

// Constant to store board pieces, as nibbles.
vector<unsigned int> BOARD_PIECES = {1, 2, 5, 6, 7, 9, 10, 13, 14, 15};

/*
 * Given n and k, generate a vector of all combinations of size k from the set [0..n).
 */
vector<set<unsigned int>> generateCombinations(unsigned int k, unsigned int n) {
    vector<set<unsigned int>> combinations;
    vector<bool> v(n);
    fill(v.end() - k, v.end(), true);
    do {
        set<unsigned int> combination;
        for (unsigned int i = 0; i < n; i++) {
            if (v[i]) {
                combination.insert(i);
            }
        }
        combinations.push_back(combination);
    } while (next_permutation(v.begin(), v.end()));
    combinations.shrink_to_fit();                       // save memory.
    reverse(combinations.begin(), combinations.end());  // nicer order.
    return combinations;
}


/*
 * Given n and k, generate a vector of all permutations of size k from the set [0..n).
 */
vector<vector<unsigned int>> generatePermutations(unsigned int k, unsigned int n) {
    vector<vector<unsigned int>> permutations;
    vector<set<unsigned int>> combinations = generateCombinations(k, n);
    for (set<unsigned int> combination : combinations) {
        vector<unsigned int> permutation(combination.begin(), combination.end());
        do {
            permutations.push_back(permutation);
        } while (next_permutation(permutation.begin(), permutation.end()));
    }
    permutations.shrink_to_fit(); // save memory.
    return permutations;
}


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


/*
 * Generate boards with n pieces (not including kings).
 */
vector<unsigned long long> generateNPieceBoards(unsigned int n)
{
    vector<unsigned long long> nPieceBoards;
    for (unsigned int wk = 0; wk < BOARD_SIZE - 2; wk++) // BOARD_SIZE - 2 as white king cannot be on the last two squares.
    {
        for (unsigned int bk = wk + 2; bk < BOARD_SIZE; bk++)
        {
            unsigned long long kingBoard = 0;
            kingBoard = insertNthNibble(kingBoard, 3, wk);  // insert white king.
            kingBoard = insertNthNibble(kingBoard, 11, bk); // insert black king.

            // Iterate over all possible placements of n pieces by choosing n squares from the board.
            vector<set<unsigned int>> squaresVector = generateCombinations(n, BOARD_SIZE);
            for (set<unsigned int> squares : squaresVector)
            {
                vector<unsigned int> squaresAsVector(squares.begin(), squares.end());
                if (squares.contains(wk) || squares.contains(bk))
                {
                    continue;  // skip cases where any indices are already occupied.
                }
                // Iterate over all possible placements of n pieces in these n squares, by permutating n pieces.
                vector<vector<unsigned int>> pieceIndicesVector = generatePermutations(n, BOARD_PIECES.size());
                for (vector<unsigned int> pieceIndices : pieceIndicesVector)
                {
                    unsigned long long board = 0;
                    for (unsigned int i = 0; i < n; i++)
                    {
                        board = insertNthNibble(board, BOARD_PIECES[pieceIndices[i]], squaresAsVector[i]);
                    }
                    nPieceBoards.push_back(kingBoard | board);
                }
            }
        }
    }
    nPieceBoards.shrink_to_fit();
    return nPieceBoards;
}



// Example usage
int main() {
    vector<unsigned long long> nPieceBoards = generateNPieceBoards(1);
    for (unsigned long long board : nPieceBoards) {
        std::cout << varsToFence(board, true, 0, 0) << std::endl;
        evaluateFenceVerbose(varsToFence(board, true, 0, 0), 8);
        evaluateFenceVerbose(varsToFence(board, false, 0, 0), 8);
    }

    // vector<set<unsigned int>> combinations = generateCombinations(2, 4);
    // for (set<unsigned int> combination : combinations) {
    //     // Print combination.
    //     for (unsigned int i : combination) {
    //         std::cout << i << " ";
    //     }
    //     std::cout << std::endl;
    // }
}



// void testKingBoards()
// {
//     int score;
//     vector<unsigned int> path;
//     for (unsigned long long kingBoard : KING_BOARDS)
//     {
//         print(varsToFence(kingBoard, true, 0, 0));
//         tie(score, path) = scorePosition(true, 16, kingBoard, true, 0, 0);
//         print(score);
//     }
// }


// int main()
// {
//     importLookupTables(attackLookup);
//     vector<unsigned long long> boards;
//     for (unsigned int wk = 0; wk < BOARD_SIZE - 2; wk++) // BOARD_SIZE - 2 as white king cannot be on the last two squares.
//     {
//         for (unsigned int bk = wk + 2; bk < BOARD_SIZE; bk++)
//         {
//             for (unsigned int k = 0; k < BOARD_SIZE; k++)
//             {
//                 if (k != wk && k != bk)
//                 {
//                     unsigned long long board = 0;
//                     board = insertNthNibble(board, 3, wk);  // insert white king.
//                     board = insertNthNibble(board, 11, bk); // insert black king.
//                     board = insertNthNibble(board, 7, k);
//                     boards.push_back(board);
//                 }
//             }
//         }
//     }
//     boards.shrink_to_fit();

//     int score;
//     vector<unsigned int> path;
//     for (unsigned long long board : boards)
//     {
//         print(varsToFence(board, true, 0, 0));
//         tie(score, path) = scorePosition(false, 8, board, false, 0, 0);
//         print(score);
//     }
// }
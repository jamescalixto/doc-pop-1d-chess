#include "position.h"

/*
Explore and enumerate the game tree.
Uses composites, where are just the active flag and board together.
*/
void explore(unsigned int max_level)
{
    unsigned int currentLevel = 0;
    set<unsigned long long> seenBoardsWhite;
    set<unsigned long long> seenBoardsBlack;
    set<unsigned long long> boards = {START_BOARD};
    set<unsigned long long> nextBoards;

    while (boards.size() > 0 && currentLevel < max_level)
    {
        bool active = (currentLevel % 2 == 0);
        set<unsigned long long> &seenBoards = active ? seenBoardsWhite : seenBoardsBlack;
        set<unsigned long long> &seenBoardsOpposite = active ? seenBoardsBlack : seenBoardsWhite;
        seenBoards.insert(boards.begin(), boards.end());
        for (unsigned long long board : boards)
        {
            vector<unsigned long long> possibleNextBoards = getNextBoards(board, active); // get moves.
            for (unsigned long long possibleNextBoard : possibleNextBoards)
            {
                if (!seenBoardsOpposite.count(possibleNextBoard) || !nextBoards.count(possibleNextBoard))
                {
                    nextBoards.insert(possibleNextBoard);
                }
            }
        }
        boards = nextBoards;
        nextBoards.clear();
        currentLevel += 1;
        std::cout << "# positions reachable after " << currentLevel << " halfmoves = " << boards.size() << std::endl;
    }
    std::cout << "No more traversable positions after this depth." << std::endl;
}

int main()
{
    importLookupTables(attackLookup);
    explore(5);

    // string fence = "KQRBNP....pnbrqk w 0 1";

    // // Declare variables to store position information.
    // unsigned long long board;
    // bool active;
    // unsigned int halfmove, fullmove;

    // tie(board, active, halfmove, fullmove) = fenceToVars(fence, board, active, halfmove, fullmove);

    // debugPrint(board);
    // std::cout << varsToFence(board, active, fullmove, halfmove) << std::endl;

    // getMoves(board, active);
}
#include "position.h"
#include <time.h>

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

        // Get set of all next boards in bulk.
        boards = getNextBoardsBulk(boards, active);
        currentLevel += 1;
        std::cout << "# positions reachable after " << currentLevel << " halfmoves = " << boards.size() << std::endl;
    }
    std::cout << "No more traversable positions after this depth." << std::endl;
}

int main()
{
    clock_t tStart = clock();
    importLookupTables(attackLookup);
    explore(18);
    printf("Time taken: %.2fs\n", (double)(clock() - tStart)/CLOCKS_PER_SEC);

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
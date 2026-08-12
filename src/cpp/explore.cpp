#include "position.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>

/*
Explore and enumerate the game tree breadth-first.

Two counts are reported per ply and they answer different questions:

  - "reachable" is the number of distinct positions at exactly this many halfmoves.
  - "new" is how many of those had never appeared at any earlier ply.

The original printed only the first and additionally maintained two `seen` sets that were
inserted into but never read, so a position first met at ply 4 was recounted and
re-expanded when it reappeared at ply 6. Deduplication now actually happens.

The frontier is a sorted vector rather than a std::set: a red-black tree with one
allocation per node is the wrong shape for a multi-million element frontier that is
built once and scanned in order.
*/

/*
Expand a whole frontier at once, returning the distinct positions one halfmove later.
*/
static vector<unsigned long long> expandFrontier(const vector<unsigned long long> &boards, bool player)
{
    vector<unsigned long long> next;
    next.reserve(boards.size() * 4);

    unsigned char moves[MAX_MOVES];
    for (const unsigned long long board : boards)
    {
        const unsigned int count = generateMoves(board, player, moves);
        for (unsigned int i = 0; i < count; ++i)
        {
            next.push_back(applyMoveToBoard(board, moves[i]));
        }
    }

    std::sort(next.begin(), next.end());
    next.erase(std::unique(next.begin(), next.end()), next.end());
    return next;
}

static void explore(unsigned int maxLevel)
{
    // Positions with white to move and with black to move are tracked separately: the
    // same board word means a different position depending on whose turn it is.
    std::unordered_set<unsigned long long> seen[2];
    vector<unsigned long long> boards = {START_BOARD};

    unsigned long long cumulative = 1;
    seen[1].insert(START_BOARD);

    std::printf("%6s %16s %16s %16s\n", "ply", "reachable", "new", "cumulative");
    std::printf("%6u %16zu %16d %16llu\n", 0u, boards.size(), 1, cumulative);

    for (unsigned int level = 0; level < maxLevel && !boards.empty(); ++level)
    {
        const bool player = (level % 2 == 0);
        boards = expandFrontier(boards, player);

        std::unordered_set<unsigned long long> &alreadySeen = seen[player ? 0 : 1];
        unsigned long long fresh = 0;
        for (const unsigned long long board : boards)
        {
            if (alreadySeen.insert(board).second)
            {
                ++fresh;
            }
        }
        cumulative += fresh;

        std::printf("%6u %16zu %16llu %16llu\n", level + 1, boards.size(), fresh, cumulative);
        std::fflush(stdout);
    }
}

int main(int argc, char **argv)
{
    const unsigned int maxLevel = (argc > 1) ? static_cast<unsigned int>(std::atoi(argv[1])) : 18;
    const auto started = std::chrono::steady_clock::now();
    explore(maxLevel);
    std::printf("Time taken: %.2fs\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
}

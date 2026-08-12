#include "position.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>

/*
Validates the rewritten move generator against the original one.

`reference` below is a faithful copy of the movegen that shipped before this change: the
42 MB `mapping.txt` attack table, occupancy built by a 16-iteration loop, and legality
decided by applying each pseudo-legal move and running a full-board isInCheck. It is
slow and it is the thing we trust.

Two checks run:

  1. Exhaustive tree walk from the start position. At every node the new generator's
     move list must equal the reference's, element for element and in the same order.
  2. Random playouts, so positions the opening tree does not reach in a few plies —
     lopsided endgames, checks, stalemates, blocked pawns — get covered too.

Perft counts are printed as a regression baseline: they are a property of the rules, so
any future change that alters them has changed the game.

Run from the repository root:  ./src/cpp/compiled/perft
*/
namespace reference
{

vector<unsigned int> attackLookup(10485760);

bool importLookupTables(const char *path)
{
    std::ifstream file(path);
    if (!file)
    {
        return false;
    }
    unsigned long long occupancy = 0;
    unsigned int moveset = 0;
    while (file >> occupancy >> moveset)
    {
        attackLookup[occupancy] = moveset;
    }
    return true;
}

unsigned int getOccupancy(unsigned long long board)
{
    unsigned int occupancy = 0;
    for (unsigned int i = 0; i < BOARD_SIZE; i++)
    {
        occupancy |= (!isEmpty(getLastNibble(board)) << i);
        board = board >> 4;
    }
    return occupancy;
}

unsigned int getPlayerOccupancy(unsigned long long board, bool player)
{
    unsigned int occupancy = 0;
    for (unsigned int i = 0; i < BOARD_SIZE; i++)
    {
        occupancy |= (isPieceOfPlayer(getLastNibble(board), player) << i);
        board = board >> 4;
    }
    return occupancy;
}

unsigned int getAttackedSquares(unsigned long long board, bool player)
{
    unsigned int allAttackedSquares = 0;
    unsigned int occupancy = getOccupancy(board);
    for (unsigned int i = 0; i < BOARD_SIZE; i++)
    {
        unsigned int piece_nibble = getLastNibble(board);
        if (isPieceOfPlayer(piece_nibble, player))
        {
            if (piece_nibble > 9)
            {
                piece_nibble %= 8;
            }
            unsigned long long key = (piece_nibble << 20) | ((BOARD_SIZE - i - 1) << 16) | (occupancy);
            allAttackedSquares |= attackLookup[key];
        }
        board = board >> 4;
    }
    return allAttackedSquares;
}

unsigned int findNibble(unsigned long long num, unsigned long long nibble)
{
    for (unsigned int i = 0; i < BOARD_SIZE; i++)
    {
        if ((num & LAST_NIBBLE_BITMASK) == nibble)
        {
            return BOARD_SIZE - i - 1;
        }
        num = num >> 4;
    }
    return BOARD_SIZE;
}

bool isInCheck(unsigned long long board, bool player)
{
    unsigned int kingNibble = player ? 3 : 11;
    unsigned int kingPosition = findNibble(board, kingNibble);
    unsigned int kingPositionBitflag = 1 << (BOARD_SIZE - kingPosition - 1);
    unsigned int attackedSquares = getAttackedSquares(board, !player);
    return (kingPositionBitflag & attackedSquares);
}

vector<unsigned int> getMoves(unsigned long long board, bool player)
{
    vector<unsigned int> moves;
    unsigned long long originalBoard = board;
    unsigned int occupancy = getOccupancy(board);
    unsigned int playerOccupancy = getPlayerOccupancy(board, player);

    for (int start = BOARD_SIZE - 1; start >= 0; start--)
    {
        unsigned int piece_nibble = getLastNibble(board);
        if (isPieceOfPlayer(piece_nibble, player))
        {
            if (piece_nibble > 9)
            {
                piece_nibble %= 8;
            }
            unsigned long long key = (piece_nibble << 20) | (start << 16) | (occupancy);
            unsigned int validMovementSquares = attackLookup[key] & (~playerOccupancy);

            if (piece_nibble == 1 && start == 5 && !((occupancy >> 8) & 3))
            {
                validMovementSquares |= 256;
            }
            else if (piece_nibble == 9 && start == 10 && !((occupancy >> 6) & 3))
            {
                validMovementSquares |= 128;
            }

            for (int end = BOARD_SIZE - 1; end >= 0; end--)
            {
                if (1 & validMovementSquares)
                {
                    unsigned int move = (start << 4) | end;
                    if (!isInCheck(applyMoveToBoard(originalBoard, move), player))
                    {
                        moves.push_back(move);
                    }
                }
                validMovementSquares = validMovementSquares >> 1;
            }
        }
        board = board >> 4;
    }
    return moves;
}

} // namespace reference

static unsigned long long g_nodesCompared = 0;
static unsigned long long g_mismatches = 0;

/*
Compare both generators at a single position. Returns the legal move list.
*/
static vector<unsigned int> compareAt(unsigned long long board, bool player)
{
    unsigned char buffer[MAX_MOVES];
    const unsigned int count = generateMoves(board, player, buffer);
    const vector<unsigned int> fresh(buffer, buffer + count);
    const vector<unsigned int> old = reference::getMoves(board, player);

    ++g_nodesCompared;
    if (fresh != old)
    {
        if (g_mismatches < 5)
        {
            std::printf("MISMATCH at %s\n  new:", varsToFence(board, player, 0, 1).c_str());
            for (unsigned int m : fresh)
            {
                std::printf(" %u->%u", m >> 4, m & 15);
            }
            std::printf("\n  old:");
            for (unsigned int m : old)
            {
                std::printf(" %u->%u", m >> 4, m & 15);
            }
            std::printf("\n");
        }
        ++g_mismatches;
    }

    // Also cross-check the derived helpers the rest of the engine relies on.
    if (getOccupancy(board) != reference::getOccupancy(board) ||
        getPlayerOccupancy(board, player) != reference::getPlayerOccupancy(board, player) ||
        getAttackedSquares(board, player) != reference::getAttackedSquares(board, player) ||
        isInCheck(board, player) != reference::isInCheck(board, player))
    {
        if (g_mismatches < 5)
        {
            std::printf("HELPER MISMATCH at %s\n", varsToFence(board, player, 0, 1).c_str());
        }
        ++g_mismatches;
    }

    return fresh;
}

/*
Walk the whole tree to `depth`, comparing at every node. Returns the leaf count.
*/
static unsigned long long perft(unsigned long long board, bool player, int depth)
{
    const vector<unsigned int> moves = compareAt(board, player);
    if (depth <= 1)
    {
        return moves.size();
    }
    unsigned long long nodes = 0;
    for (unsigned int move : moves)
    {
        nodes += perft(applyMoveToBoard(board, move), !player, depth - 1);
    }
    return nodes;
}

int main(int argc, char **argv)
{
    const char *mapping = (argc > 1) ? argv[1] : "mapping.txt";
    if (!reference::importLookupTables(mapping))
    {
        std::fprintf(stderr,
                     "could not open %s — the reference generator needs the old table.\n"
                     "Pass its path as the first argument.\n",
                     mapping);
        return 2;
    }

    const int maxDepth = (argc > 2) ? std::atoi(argv[2]) : 6;

    std::printf("exhaustive tree comparison from the start position\n");
    for (int depth = 1; depth <= maxDepth; ++depth)
    {
        g_nodesCompared = 0;
        const unsigned long long nodes = perft(START_BOARD, true, depth);
        std::printf("  perft(%d) = %-14llu  nodes compared %-12llu  mismatches %llu\n",
                    depth, nodes, g_nodesCompared, g_mismatches);
    }

    // Random playouts reach material and king configurations the shallow opening tree
    // never does, which is where the legality shortcuts would break if they were wrong.
    std::printf("random playouts\n");
    std::mt19937_64 rng(20260811);
    const unsigned long long beforePlayouts = g_mismatches;
    unsigned long long playoutNodes = 0, terminal = 0;
    for (int game = 0; game < 20000; ++game)
    {
        unsigned long long board = START_BOARD;
        bool player = true;
        for (int ply = 0; ply < 400; ++ply)
        {
            const vector<unsigned int> moves = compareAt(board, player);
            ++playoutNodes;
            if (moves.empty())
            {
                ++terminal;
                break;
            }
            board = applyMoveToBoard(board, moves[rng() % moves.size()]);
            player = !player;
        }
    }
    std::printf("  %llu positions over 20000 games (%llu ended in mate or stalemate), mismatches %llu\n",
                playoutNodes, terminal, g_mismatches - beforePlayouts);

    std::printf("\n%s\n", g_mismatches == 0 ? "PASS — generators agree everywhere" : "FAIL");
    return g_mismatches == 0 ? 0 : 1;
}

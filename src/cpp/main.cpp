#include "evaluate.h"

#include <cstdio>
#include <cstdlib>

/*
Alpha-beta driver.

  ./main                       search the start position to depth 12
  ./main <depth>               search the start position to <depth>
  ./main <depth> "<fence>"     search the given position

Note that a score here is a heuristic, not a proof, unless the line printed above it says
"proven". Searching the start position deeper will not change that: the game is far too
long to resolve by forward search from move one. See src/cpp/retro for the approach that
actually terminates.
*/
int main(int argc, char **argv)
{
    const int depth = (argc > 1) ? std::atoi(argv[1]) : 12;
    const string fence = (argc > 2) ? string(argv[2]) : START_FENCE;

    std::printf("Starting run...\n");
    evaluateFenceVerbose(fence, depth);
}

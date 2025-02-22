#include "evaluate.h"
#include <time.h>

int main()
{
    printf("Starting run...\n");
    clock_t tStart = clock();

    // evaluateFenceVerbose("K..............k b 0 1", 10); // should be 0
    // evaluateFenceVerbose("K....n.........k b 0 1", 10);
    // evaluateFenceVerbose("........K.n....k b 0 1", 20);
    // evaluateFenceVerbose("KQRB..NP.p.nbrqk b 0 1", 10); // should be b +100
    // evaluateFenceVerbose("KQRBN.P.pn..brqk w 0 1", 10); // should be w +100
    // evaluateFenceVerbose(START_FENCE, 12);
    // evaluateFenceVerbose(START_FENCE, 20);
    // evaluateFenceVerbose(START_FENCE, 24);

    printf("Time taken: %.2fs\n", (double)(clock() - tStart)/CLOCKS_PER_SEC);
}
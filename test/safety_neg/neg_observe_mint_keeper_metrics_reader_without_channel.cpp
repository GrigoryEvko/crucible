// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_keeper_metrics_reader has no overload without the metrics
// channel, so a call with no argument finds no function to call.

#include <crucible/observe/Metrics.h>

int main() {
    auto reader = crucible::observe::mint_keeper_metrics_reader();
    (void)reader;
    return 0;
}

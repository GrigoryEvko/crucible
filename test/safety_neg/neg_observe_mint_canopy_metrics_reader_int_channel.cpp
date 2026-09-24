// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_canopy_metrics_reader binds its argument to a metrics channel.
// An int does not bind to that reference, so the call finds no
// function to call.

#include <crucible/observe/Metrics.h>

int main() {
    int not_a_channel = 0;
    auto reader = crucible::observe::mint_canopy_metrics_reader(not_a_channel);
    (void)reader;
    return 0;
}

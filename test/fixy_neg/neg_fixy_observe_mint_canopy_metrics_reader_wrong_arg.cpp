#include <crucible/fixy/Observe.h>

// FIXY-V-214 fixture #3: the fixy::observe::mint_canopy_metrics_reader
// re-export MUST require its channel argument to be a
// RuntimeMetricsChannel&, not an arbitrary type.  Passing an int
// (wrong type entirely) must fail compilation through the fixy::
// surface with the same conversion / overload-resolution diagnostic
// as the bare substrate call.

int main() {
    int not_a_channel = 0;
    auto reader = crucible::fixy::observe::mint_canopy_metrics_reader(
        not_a_channel);
    (void)reader;
    return 0;
}

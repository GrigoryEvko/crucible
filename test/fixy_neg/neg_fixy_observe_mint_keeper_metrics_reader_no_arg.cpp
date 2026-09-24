#include <crucible/fixy/Observe.h>

// FIXY-V-214 fixture #2: the fixy::observe::mint_keeper_metrics_reader
// re-export MUST require the RuntimeMetricsChannel& argument — no
// defaulted form exists, so calling with zero arguments must fail
// compilation through the fixy:: surface with the same overload-
// resolution diagnostic as the bare substrate call.

int main() {
    auto reader = crucible::fixy::observe::mint_keeper_metrics_reader();
    (void)reader;
    return 0;
}

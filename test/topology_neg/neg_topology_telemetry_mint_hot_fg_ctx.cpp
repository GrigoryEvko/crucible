// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A telemetry history is built at startup.  The foreground row is empty,
// so the mint refuses the dispatch thread.  A foreground context comes
// only from the producer claim, so the fixture takes one as a parameter.

#include <crucible/topology/Telemetry.h>

[[maybe_unused]] static int mint_from_foreground(::fixy::HotFgCtx const& fg) {
    auto history = crucible::topology::mint_nic_telemetry_history<4>(fg);
    (void)history;
    return 0;
}

int main() { return 0; }

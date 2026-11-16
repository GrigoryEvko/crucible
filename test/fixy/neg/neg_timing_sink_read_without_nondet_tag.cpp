// A count of the counter of the core changes from run to run, so the read of
// a slot of the observation sink takes the bare tag of Nondet.  A read with
// no tag has no overload.
//
// Expected diagnostic: no candidate of read with the slot alone.

#include <fixy/Core.h>
#include <fixy/os/TimingSink.h>

int main() {
    auto sink = ::fixy::observe::mint_timing_sink<2>();
    [[maybe_unused]] auto count = sink.read(::fixy::index<0>());
    return 0;
}

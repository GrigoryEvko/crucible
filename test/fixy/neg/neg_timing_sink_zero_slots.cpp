// A sink holds at least one slot and at most 1024 slots.  A sink of zero
// slots has no slot to write, so the mint refuses it.
//
// Expected diagnostic: no candidate of mint_timing_sink, with a note on the
// count of the slots.

#include <fixy/os/TimingSink.h>

int main() {
    [[maybe_unused]] auto sink = ::fixy::observe::mint_timing_sink<0>();
    return 0;
}

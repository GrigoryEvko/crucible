// V101: a replay-deterministic payload x a pinned SIMD ISA.
//
// The payload's DetSafe band at Pure claims the same bits on every
// replay.  A body emitted for one vector ISA runs a different reduction
// order on a host without that ISA, where it falls back, so the claim
// does not survive a change of host.  Scalar and Portable pin nothing —
// the first runs anywhere and the second is one kernel for every set —
// and the rule stands down for both.
//
// The replay half is read from the payload through
// rules_of<Payload, Atoms...>, the same read V203 consumes.

#include <fixy/Bands.h>
#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>, ::fixy::atom::simd::avx2> refused{};
    return 0;
}

// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-071 HS14 fixture #1: constructing fixy::concurrent::SpinGuard<Tag>
// without a Permission<Tag>& proof rejects at construction.
//
// Violation: the wrapper's discipline is "Permission<Tag> token
// presented at every acquire" — that's the static witness that the
// caller holds the CSL region's ownership.  Bypassing it via the
// no-arg constructor would defeat the discipline; no such overload
// exists.
//
// Expected diagnostic: no matching function for call to
// SpinGuard<Tag>::SpinGuard(SpinLock<Tag>&) — only the
// (SpinLock<Tag>&, Permission<Tag>&) overload exists.

#include <crucible/fixy/concurrent/SpinLock.h>

struct GateTag {};

int main() {
    crucible::fixy::concurrent::SpinLock<GateTag> gate{};

    // ✗ Missing Permission<GateTag>& parameter; should reject.
    crucible::fixy::concurrent::SpinGuard<GateTag> guard{gate};
    (void)guard;
    return 0;
}

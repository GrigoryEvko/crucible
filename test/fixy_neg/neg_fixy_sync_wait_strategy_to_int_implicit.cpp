// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-084 HS14 fixture #1 of 4 for fixy/sync/WaitGrant.h:
// `fixy::sync::Strategy` preserves the substrate's `enum class`
// scoping through the using-decl re-export.
//
// Why this matters: V-084 surfaces the WaitStrategy enum at
// `fixy::sync::Strategy`.  A regression that replaces the using-decl
// with (a) a redefined non-scoped `enum Strategy { ... }`, or (b) a
// typedef alias `using Strategy = uint8_t`, would let
// `Strategy::SpinPause` decay to integer silently — breaking every
// downstream `switch` exhaustiveness check, admitting unrelated enum
// values into Strategy-typed slots, and defeating the lattice
// chain-ordering discipline.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// SCOPED-ENUM half — Strategy is an `enum class` and a Strategy
// value MUST NOT implicitly convert to `int`.  Sibling
// `neg_fixy_sync_wait_strict_relax_up_rejected.cpp` exercises the
// SUBSTRATE-DISCIPLINE PROPAGATION half through the same surface.
//
// Expected diagnostic: "cannot convert .* (Strategy|WaitStrategy)
// .* to .* int" / "invalid conversion from "
// 'crucible::safety::WaitStrategy'" / "narrowing conversion of".

#include <crucible/fixy/sync/WaitGrant.h>

// NB: `fsync` is taken by POSIX `int fsync(int)` in <unistd.h>; the
// canonical 5-letter alias would collide, so we spell out the longer
// form for the namespace alias used throughout the fixy/sync/ fixtures.
namespace fixy_sync = crucible::fixy::sync;

int main() {
    // Strategy::SpinPause is a scoped enum value — assigning it to a
    // plain int without explicit cast is the load-bearing error.
    int leaked_strategy = fixy_sync::Strategy::SpinPause;
    (void)leaked_strategy;
    return 0;
}

// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-084 HS14 fixture #3 of 4 for fixy/sync/MemOrderGrant.h:
// `fixy::sync::Order` preserves the substrate's `enum class` scoping
// through the using-decl re-export.
//
// Why this matters: V-084 surfaces the MemOrderTag enum at
// `fixy::sync::Order`.  A regression that replaces the using-decl
// with (a) a redefined non-scoped `enum Order { ... }`, or (b) a
// typedef alias `using Order = uint8_t`, would let
// `Order::SeqCst` decay to integer silently — breaking every
// downstream `switch` exhaustiveness check, admitting unrelated enum
// values into Order-typed slots, and defeating the lattice
// chain-ordering discipline that gates the CLAUDE.md §VI seq_cst
// ban.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// SCOPED-ENUM half — Order is an `enum class` and an Order value
// MUST NOT implicitly convert to `int`.  Sibling
// `neg_fixy_sync_memorder_strict_relax_up_rejected.cpp` exercises
// the SUBSTRATE-DISCIPLINE PROPAGATION half through the same surface.
//
// Expected diagnostic: "cannot convert .* (Order|MemOrderTag) .* to
// .* int" / "invalid conversion from "
// 'crucible::safety::MemOrderTag'" / "narrowing conversion of".

#include <crucible/fixy/sync/MemOrderGrant.h>

namespace fixy_sync = crucible::fixy::sync;

int main() {
    // Order::SeqCst is a scoped enum value — assigning it to a plain
    // int without explicit cast is the load-bearing error.
    int leaked_order = fixy_sync::Order::SeqCst;
    (void)leaked_order;
    return 0;
}

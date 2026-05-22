// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-076 HS14 fixture #1: `fixy::pipe::Tier` preserves the
// substrate's `enum class` scoping through the using-decl re-export.
//
// Why this matters: V-076 surfaces the cost-model `Tier` enum at
// `fixy::pipe::Tier`.  A regression that replaces the using-decl with
// (a) a redefined non-scoped `enum Tier { ... }`, or (b) a typedef
// alias `using Tier = uint8_t`, would let `Tier::L1Resident` decay
// to integer silently — breaking every downstream `switch` exhaust
// check and admitting unrelated enum values into Tier-typed slots.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// SCOPED-ENUM half — Tier is an `enum class` and a Tier value MUST
// NOT implicitly convert to `int`.  Sibling
// `neg_fixy_pipe_numa_to_tier_implicit.cpp` exercises the
// CROSS-ENUM-CONVERSION half through the same alias surface.
//
// Expected diagnostic: "cannot convert .* Tier .* to .* int" /
// "invalid conversion from 'crucible::concurrent::Tier'" /
// "narrowing conversion of".

#include <crucible/fixy/Pipe.h>

namespace fpipe = crucible::fixy::pipe;

int main() {
    // Tier::L1Resident is a scoped enum value — assigning it to a
    // plain int without explicit cast is the load-bearing error.
    int leaked_tier = fpipe::Tier::L1Resident;
    (void)leaked_tier;
    return 0;
}

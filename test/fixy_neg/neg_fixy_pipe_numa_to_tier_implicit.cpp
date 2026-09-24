// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-076 HS14 fixture #2: `fixy::pipe::Tier` and
// `fixy::pipe::NumaPolicy` are surfaced as DISTINCT scoped enum
// classes — assigning a `NumaPolicy` value where a `Tier` is expected
// must fail, identically to the substrate's discipline.
//
// Why this matters: V-076 re-exports BOTH `Tier` and `NumaPolicy`
// (each `enum class : uint8_t`).  A regression that conflates them
// (e.g., one underlying typedef alias `using Tier = NumaPolicy` or a
// macro mistake) would let `NumaSpread` quietly populate a Tier slot
// — every cost-model classify() emitting `DRAMBound` would suddenly
// also be `NumaIgnore` etc., shattering DetSafe.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// CROSS-ENUM-CONVERSION half — two distinct `enum class` types must
// not interconvert.  Sibling `neg_fixy_pipe_tier_to_int_implicit.cpp`
// exercises the SCOPED-ENUM half (enum class → int rejection).
//
// Expected diagnostic: "cannot convert .* NumaPolicy .* to .* Tier" /
// "invalid conversion from 'crucible::concurrent::NumaPolicy'" /
// "no known conversion".

#include <crucible/fixy/Pipe.h>

namespace fpipe = crucible::fixy::pipe;

int main() {
    // NumaPolicy and Tier are distinct scoped enum classes.  Their
    // values must NOT be interchangeable through the alias surface.
    fpipe::Tier confused = fpipe::NumaPolicy::NumaSpread;
    (void)confused;
    return 0;
}

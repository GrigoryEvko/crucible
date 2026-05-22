// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fsrow::NumericalPayloadRow<99, Row<>>` — `99` is an
// `int` literal that does NOT convert to the strong enum-class
// `safety::Tolerance`.  The carrier template's first parameter is
// a non-type template parameter of type `safety::Tolerance`; an int
// literal cannot bind to it without an explicit cast.  This pins
// the substrate's TypeSafe discipline at the fixy:: re-export
// boundary: a future regression that loosens NumericalPayloadRow's
// signature (e.g., `class Tier` instead of `Tolerance Tier`) would
// silently admit arbitrary Tier types, weakening MIMIC §41's
// numerical-recipe pinning guarantee on the wire.
//
// FIXY-V-062 HS14 floor — fixture 2 of 3.  Pairs with:
//   1. neg_fixy_sess_row_non_protocol_input.cpp
//      (protocol_effect_row<int> — non-protocol guard at walker)
//   3. neg_fixy_sess_row_tagged_unwrap_drift.cpp
//      (Tagged transparent-unwrap discipline gate)
//
// This fixture's role: pin the Tolerance NTTP discipline.  Without
// the strong-enum constraint, a NumericalRecipe whose Tier was
// downgraded mid-flight (e.g. BITEXACT → ULP_FP16 via a stray int
// arithmetic at a call site) would compile fine but compute under
// the WRONG numerical tolerance — Forge phases would admit IR that
// later violated the cross-vendor pairwise-equivalence CI test.

#include <crucible/fixy/SessRowExtraction.h>
#include <crucible/effects/EffectRow.h>

namespace fsrow = ::crucible::fixy::sess::row;
namespace eff   = ::crucible::effects;

int main() {
    // Should FAIL: `99` is an int literal that cannot convert to
    // `safety::Tolerance` (an enum class : uint8_t).  GCC fires
    // "could not convert template argument '99' to ...Tolerance".
    using BadTier = fsrow::NumericalPayloadRow<99, eff::Row<>>;
    (void) sizeof(BadTier);  // force instantiation
    return 0;
}

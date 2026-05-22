// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fsrow::protocol_effect_row_t<int>` — `int` is NOT a
// session protocol shape.  The substrate's `protocol_effect_row<P>`
// primary template is forward-declared WITHOUT a definition (only
// per-combinator specialisations are defined); instantiating with a
// non-protocol type triggers "incomplete type" / "no definition"
// at the substrate boundary.
//
// FIXY-V-062 HS14 floor — fixture 1 of 3.  Pairs with:
//   2. neg_fixy_sess_row_numerical_tier_non_tolerance.cpp
//      (NumericalPayloadRow rejects non-Tolerance Tier)
//   3. neg_fixy_sess_row_tagged_unwrap_drift.cpp
//      (Tagged transparent-unwrap discipline gate)
//
// This fixture's role: pin the "non-protocol guard" at the
// protocol-walker boundary.  Without the forward-only primary
// template, a future regression that adds a fallback (e.g., "primary
// returns Row<>") would SILENTLY admit nonsense protocol inputs —
// the call site would compile but compute a structurally-incorrect
// empty row, causing Forge phases to admit IR they shouldn't.
//
// The substrate-side discipline IS the forward-only primary; this
// fixture witnesses it from the fixy:: surface, drift-protecting
// the using-decl + the substrate's discipline together.

#include <crucible/fixy/SessRowExtraction.h>

namespace fsrow = ::crucible::fixy::sess::row;

int main() {
    // Should FAIL: `int` is NOT a protocol shape.  The substrate's
    // protocol_effect_row<int> has no definition (primary is
    // forward-declared only).  GCC fires an "incomplete type" or
    // "use of undefined template" diagnostic.
    using NoSuchRow = fsrow::protocol_effect_row_t<int>;
    (void) sizeof(NoSuchRow);  // force instantiation
    return 0;
}

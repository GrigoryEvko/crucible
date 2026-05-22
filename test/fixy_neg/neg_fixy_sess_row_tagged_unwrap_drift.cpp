// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: a static_assert deliberately CLAIMS that
// `payload_row_t<Tagged<IoComp, ProvTag>>` is NOT equal to
// `payload_row_t<IoComp>`.  The current substrate ships the
// transparent-unwrap specialisation
//
//     template <class T, class Tag>
//     struct payload_row<Tagged<T, Tag>> : payload_row<T> {};
//
// so the two ARE the same row (the Tagged layer is transparent for
// row extraction — provenance tags carry zero effect contribution).
// The wrong-direction static_assert fires, and the file fails to
// compile — exactly what a neg-compile fixture wants.
//
// FIXY-V-062 HS14 floor — fixture 3 of 3.  Pairs with:
//   1. neg_fixy_sess_row_non_protocol_input.cpp
//      (protocol_effect_row<int> — non-protocol walker guard)
//   2. neg_fixy_sess_row_numerical_tier_non_tolerance.cpp
//      (NumericalPayloadRow rejects non-Tolerance Tier)
//
// This fixture's role: pin the Tagged transparent-unwrap discipline.
// If a future regression removes the `payload_row<Tagged<...>>`
// specialisation, `payload_row_t<Tagged<IoComp, ProvTag>>` would
// fall through to the primary template returning `Row<>`, the two
// rows would differ, the static_assert would PASS, and this fixture
// would compile successfully — which the neg-compile driver flags
// as a regression.  The fixture is therefore a drift-protector for
// every transparent-unwrap wrapper specialisation (Tagged / Stale /
// Linear / Secret / SealedRefined / Refined / ContentAddressed /
// Transferable / Borrowed / Returned plus all canonical Graded
// wrappers — ~28 specialisations collectively).
//
// The same wrong-claim shape would work for any of those wrappers;
// Tagged is the canonical witness because it carries information
// (provenance) that a naive author might believe affects row
// extraction — but transparent unwrap is the load-bearing
// discipline and this fixture pins it.

#include <crucible/fixy/SessRowExtraction.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Tagged.h>

#include <type_traits>

namespace fsrow = ::crucible::fixy::sess::row;
namespace eff   = ::crucible::effects;
namespace saf   = ::crucible::safety;

namespace v062_neg_gamma {
using IoComp  = eff::Computation<eff::Row<eff::Effect::IO>, int>;
struct ProvTag {};
using Wrapped = saf::Tagged<IoComp, ProvTag>;
}  // namespace v062_neg_gamma

// Should FAIL: the substrate's transparent-unwrap specialisation
// makes these two row types IDENTICAL.  This static_assert claims
// the opposite — it fires at compile time.  If a regression deletes
// the Tagged specialisation, the unwrapped form would be
// `Row<>` while the inner is `Row<IO>`, the static_assert would
// pass, and this fixture would compile successfully (neg-compile
// driver flags that as the regression).
static_assert(
    !std::is_same_v<
        fsrow::payload_row_t<v062_neg_gamma::Wrapped>,
        fsrow::payload_row_t<v062_neg_gamma::IoComp>>,
    "Tagged should NOT be transparent for payload_row "
    "(intentionally-wrong claim — fires as long as substrate keeps "
    "the transparent-unwrap specialisation that says it IS).");

int main() { return 0; }

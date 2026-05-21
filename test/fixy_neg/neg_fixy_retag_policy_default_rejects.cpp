// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-025 fixture #1: `fixy::tags::RetagAllowed<From, To>` must
// reject unspecialized phantom-tag transitions when consumed at the
// fixy-band alias.
//
// Violation: requesting a phantom-tag transition through the
// fixy::tags re-export that has NOT been opted into via an explicit
// `retag_policy<From, To>` specialization.  V-022 reserves the
// sentinel pair `retag_policy_test::{NeverFrom, NeverTo}` to remain
// unspecialized forever; the fail-closed primary template reaches
// through the alias and rejects the call.
//
// Sister fixture to the V-022 safety_neg fixtures
// (neg_retag_policy_default_rejects.cpp +
//  neg_retag_policy_cross_axis_rejects.cpp) and the V-024 consumer
// fixtures (neg_tagged_retag_*.cpp): together they witness that the
// gate fires at the GATE level (V-022), the CONSUMER level (V-024,
// Tagged::retag()'s wired requires-clause), AND through the fixy
// band re-export (V-025, this file).
//
// Uses the V-022 sentinel pair re-exported at
// `fixy::tags::retag_policy_test::` to stay decoupled from V-023's
// catalog as it grows.

#include <crucible/fixy/Source.h>

namespace ft = crucible::fixy::tags;

// Function-template gate constrained on the fixy alias — mirrors
// V-022's `demand_retag_allowed` but consumes the concept via the
// fixy::tags re-export instead of safety::.
template <typename From, typename To>
    requires ft::RetagAllowed<From, To>
constexpr void demand_retag_allowed_fixy() noexcept {}

int main() {
    // Sentinel pair via the fixy::tags re-export — V-022 guarantees
    // it stays unspecialized forever, so the fail-closed primary
    // template reaches through the alias and rejects the call.
    demand_retag_allowed_fixy<ft::retag_policy_test::NeverFrom,
                               ft::retag_policy_test::NeverTo>();
    return 0;
}

// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: requesting a phantom-tag transition that was not opted
// into via an explicit `retag_policy<From, To>` specialization.
//
// FIXY-V-022 ships `safety::retag_policy<From, To>` as a primary
// template with `allowed = false` — fail-closed by default.  V-023
// admits production transitions (External → Sanitized, FromPytorch →
// Validated, ...) by specialization; this fixture uses the V-022
// sentinel pair `detail::retag_policy_test::{NeverFrom, NeverTo}`
// which is reserved to remain unspecialized forever, so the
// fail-closed witness stays correct as the V-023 catalog grows.
//
// This fixture exercises the FUNCTION-TEMPLATE consumer shape — the
// concept constrains a free function template's call.  Sister
// fixture `neg_retag_policy_cross_axis_rejects.cpp` covers the
// CLASS-TEMPLATE consumer shape; together they witness the gate
// fires across the two consumer surfaces V-024 will pin onto
// `Tagged::retag()`.

#include <crucible/safety/Tagged.h>

namespace ns = crucible::safety;

// Function-template gate — the shape V-024 will pin onto
// Tagged<T, From>::retag<To>() requires RetagAllowed<From, To>.
template <typename From, typename To>
    requires ns::RetagAllowed<From, To>
constexpr void demand_retag_allowed() noexcept {}

int main() {
    // Sentinel pair — guaranteed unspecialized forever per V-022.
    demand_retag_allowed<ns::detail::retag_policy_test::NeverFrom,
                          ns::detail::retag_policy_test::NeverTo>();
    return 0;
}

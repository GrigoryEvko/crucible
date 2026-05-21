// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: requesting a phantom-tag transition that was not opted
// into via an explicit `retag_policy<From, To>` specialization.
//
// FIXY-V-022 ships `safety::retag_policy<From, To>` as a primary
// template with `allowed = false` — the discipline is fail-closed.
// V-023 will add per-transition specializations admitting the safe
// directions (External → Sanitized, FromPytorch → Validated, etc.).
// V-024 will wire `Tagged::retag()` itself to require the gate.
//
// This fixture demonstrates the concept-form gate fires on an
// unspecialized cross-axis transition (`source::External` →
// `trust::Verified`).  Without the V-023 catalog there is no
// specialization; the primary template's `allowed = false` blocks
// any generic code constrained on `RetagAllowed`.

#include <crucible/safety/Tagged.h>

namespace ns = crucible::safety;

// Function template constrained on RetagAllowed — V-022's load-bearing
// soundness witness.  This is the same shape V-024 will pin onto
// Tagged::retag<NewTag>().
template <typename From, typename To>
    requires ns::RetagAllowed<From, To>
constexpr void demand_retag_allowed() noexcept {}

int main() {
    // Cross-axis transition (provenance → trust) — must be rejected
    // until an explicit specialization admits it.
    demand_retag_allowed<ns::source::External, ns::trust::Verified>();
    return 0;
}

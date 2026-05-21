// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: cross-axis phantom transition (provenance → access) on
// `retag_policy<>` primary template.  No specialization exists for
// (source::FromUser → access::WriteOnce); the primary's fail-closed
// `allowed = false` must reject.
//
// Sister fixture to neg_retag_policy_default_rejects.cpp.  That one
// hits source::* → trust::*; this one hits source::* → access::*.
// Together they witness the gate fires across BOTH cross-axis
// families the existing tag tree exposes (provenance, trust, access,
// version, vessel_trust, secret_policy).
//
// FIXY-V-022 ships the primary template only.  V-023 will add
// per-direction specializations.  V-024 wires `Tagged::retag()`.

#include <crucible/safety/Tagged.h>

namespace ns = crucible::safety;

template <typename From, typename To>
    requires ns::RetagAllowed<From, To>
constexpr void demand_retag_allowed() noexcept {}

int main() {
    // Cross-axis transition (provenance → access-mode) — fail-closed.
    demand_retag_allowed<ns::source::FromUser, ns::access::WriteOnce>();
    return 0;
}

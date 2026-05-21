// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating a class template constrained on
// `RetagAllowed<From, To>` for an unspecialized phantom-tag
// transition.
//
// Sister fixture to neg_retag_policy_default_rejects.cpp.  That one
// hits the FUNCTION-TEMPLATE consumer shape; this one hits the
// CLASS-TEMPLATE consumer shape.  Both V-022 consumers route through
// the same `RetagAllowed` concept gate, so the fail-closed primary
// template's `allowed = false` must reject both.  V-024 may use
// either consumer shape (the requires-clause on Tagged::retag() is
// the function form; metaprogramming around retag_policy may use
// the class form) — both must reject unspecialized transitions.
//
// Uses the V-022 sentinel pair to stay decoupled from V-023's
// catalog (see neg_retag_policy_default_rejects.cpp doc-block).

#include <crucible/safety/Tagged.h>

namespace ns = crucible::safety;

// Class-template gate — the shape used at the type level for
// retag-policy metaprogramming (e.g., conditional members,
// constrained type aliases).
template <typename From, typename To>
    requires ns::RetagAllowed<From, To>
struct retag_witness {
    static constexpr bool ok = true;
};

int main() {
    // Sentinel pair — guaranteed unspecialized forever per V-022.
    retag_witness<ns::detail::retag_policy_test::NeverFrom,
                   ns::detail::retag_policy_test::NeverTo> w{};
    (void)w;
    return 0;
}

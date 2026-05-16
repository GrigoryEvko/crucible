// ── neg_fixy_arg_promote_violates_refinement (FIXY-G5 HS14) ───────────
//
// Promoting a raw value to a typed argument that requires a non-trivial
// construction (e.g., from a non-convertible class type) fires
// PromoteImpossible.  Distinct from the void*->int fixture; this one
// exercises an unrelated class type.

#include <crucible/fixy/Fixy.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

namespace {

using AdderPtr = int(*)(int, int);

int adder(int a, int b) noexcept { return a + b; }

using AdderFn = cf::fn<AdderPtr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// A class type that has NO conversion to int.  Promoting it to arg 0
// of AdderFn (which is int) fails the can_promote_v concept.
struct UnrelatedClass {
    int internal_state{0};
};

int main() {
    UnrelatedClass u{};
    // PromoteImpossible — UnrelatedClass is neither identical to int
    // nor constructible/convertible to int.
    auto r = cf::mint_arg<AdderFn, 0>(u);
    (void)r;
    return 0;
}

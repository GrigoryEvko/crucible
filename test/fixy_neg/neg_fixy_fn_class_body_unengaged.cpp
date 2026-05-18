// fixy_neg: fixy::fn class-body static_assert rejects unengaged pack.
//
// HS14 floor for FIXY-AUDIT-D3.  The wrapper's class-body
// static_assert(IsAcceptedFn<Type, Grants...>, ...) fires when fixy::fn
// is INSTANTIATED directly (not through mint_fn or a stance::*) with an
// unengaged-axis pack.  This pathway is distinct from mint_fn's
// requires-clause: the class-body assert lives inside class-template
// instantiation rather than overload resolution.  Both must reject;
// previous fixtures pin the requires-clause path
// (neg_fixy_mint_fn_unengaged_axis exercises mint_fn directly), this
// fixture pins the class-body path.
//
// Expected diagnostic: "FixyNotEngaged_Usage" — the class-body
// static_assert message names the per-axis diagnostic tag prefix
// AND triggers the per-axis FIXY_NEG_FIXTURE machinery indirectly via
// the IsAcceptedFn → AllDimsEngaged → first_missing_axis chain.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// Direct class-template instantiation with Usage axis omitted.  This
// route — `using BadFn = fixy::fn<int, ...>` — does NOT go through
// `mint_fn`'s requires-clause; the class-body static_assert is the
// load-bearing rejection mechanism.  The driver greps for
// "FixyNotEngaged_" in the diagnostic chain.
using BadFn = fixy::fn<int,
    strict<D::Refinement>, /* strict<D::Usage> omitted */
    strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
    strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>;

// Force class-body completion: a member-of-incomplete-type check would
// not be enough; the static_assert in the class body fires only on
// completion, which is forced by sizeof or by deriving from the type.
static_assert(sizeof(BadFn) > 0,
    "instantiate fixy::fn class body to force its static_assert");

int main() { return 0; }

// ── neg_fixy_federation_recipe_tier_disjoint (FIXY-G7 HS14) ───────────
//
// Role A is vendor_nv; Role B is vendor_am.  Recipe tiers differ →
// federation intersection is empty.

#include <crucible/fixy/Fixy.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

namespace {

using FnPtr = int(*)(int);

int noop(int x) noexcept { return x; }

template <typename V>
using VendorBind = cf::fn<FnPtr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    V,
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

using NvRole = VendorBind<cg::vendor_nv>;
using AmRole = VendorBind<cg::vendor_am>;

static_assert(!cf::FederationCompatible<NvRole, AmRole>,
    "FederationEmpty: NV ⊓ AM — recipe_tier vendors are disjoint.");

int main() {
    NvRole a{&noop};
    AmRole b{&noop};
    auto channel = cf::mint_federation_channel(a, b);
    (void)channel;
    return 0;
}

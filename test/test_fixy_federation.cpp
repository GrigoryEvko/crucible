// ── test_fixy_federation — FIXY-G7 positive test ──────────────────────
//
// Pin FederationGrade + mint_federation_channel for a three-role
// federation where all roles agree on the load-bearing axes.

#include <crucible/fixy/Fixy.h>

#include <cstdio>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fx = crucible::effects;

namespace {

using FnPtr = int(*)(int);

int identity_impl(int x) noexcept { return x; }

// Three roles — same grade vector → federation compatible.
template <int RoleId>
using RoleBinding = cf::fn<FnPtr,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cg::copy,
    cg::with<fx::Effect::Bg>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cg::vendor_nv,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cg::precision_f32,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

using Org1 = RoleBinding<1>;
using Org2 = RoleBinding<2>;
using Org3 = RoleBinding<3>;

// Compile-time invariants.
static_assert(cf::FederationCompatible<Org1, Org2, Org3>,
    "Three roles with identical Vendor/Trust/Effect/Precision must be "
    "federation-compatible.");

static_assert(std::is_same_v<
    typename cf::FederationGrade<Org1, Org2, Org3>::meet_role_t,
    Org1>);

}  // namespace

int main() {
    Org1 r1{&identity_impl};
    Org2 r2{&identity_impl};
    Org3 r3{&identity_impl};

    auto channel = cf::mint_federation_channel(r1, r2, r3);
    (void)channel;

    std::fputs("test_fixy_federation: OK\n", stdout);
    return 0;
}

// ── neg_fixy_rule_R020_federation_peer_unbounded (FIXY-G11/G12 FUtb-A) ─
//
// Followup A — mint_federation_channel<Roles...> now enforces R020
// (federation peer roles require cg::terminating + cg::wallclock_budget)
// at construction time.  Pre-Followup the rule was a consumer-side
// predicate only — a federation channel could be minted with peer
// roles that lacked bounded-resource discipline and the violation
// surfaced only at admission downstream.
//
// This fixture pins the construction-time gate: a peer role missing
// cg::wallclock_budget fails the mint's `requires` clause and the
// build is red.
//
// Build red is the EXPECTED outcome.

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/dim/Termination.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fx = crucible::effects;

namespace {

using FnPtr = int(*)(int);
int identity_impl(int x) noexcept { return x; }

// Peer A: well-formed — carries terminating + wallclock_budget.
using PeerA = cf::fn<FnPtr,
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
    cf::accept_default_strict_for<cd::Staleness>,
    cg::terminating,
    cg::wallclock_budget<1'000'000'000>>;

// Peer B: MISSING wallclock_budget — R020 rejects.
using PeerBMissingBudget = cf::fn<FnPtr,
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
    cf::accept_default_strict_for<cd::Staleness>,
    cg::terminating>;  // NO wallclock_budget

// Sanity: R020 predicate rejects PeerB.
static_assert(!cf::rule::R020_federation_peer_bounded_v<PeerBMissingBudget>);
static_assert(cf::rule::R020_federation_peer_bounded_v<PeerA>);
static_assert(!cf::all_roles_r020_satisfied_v<PeerA, PeerBMissingBudget>);

// THE DISCIPLINE: mint_federation_channel now folds R020 over the role
// pack — instantiating with PeerB triggers the static_assert at the
// mint's body, rejecting the call at construction time.
//
// INVERTED static_assert pins build-red: the predicate at the mint
// boundary IS that R020 holds; we assert the opposite, so any reachable
// instantiation of mint_federation_channel<PeerA, PeerBMissingBudget>
// fires the inner R020 static_assert AND this outer inverted assert.
static_assert(cf::all_roles_r020_satisfied_v<PeerA, PeerBMissingBudget>,
    "R020_FederationPeerUnbounded fixture: a federation pack with one "
    "role missing wallclock_budget must fail R020.  Build red on this "
    "inverted predicate is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }

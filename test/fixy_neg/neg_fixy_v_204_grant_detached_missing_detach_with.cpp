// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-204 HS14 fixture #2 of 3 for fixy/spawn/SpawnGrant.h:
// `JoinPolicyGrantsCoherent<join::Detached, ...>` rejects a grant
// pack that does NOT contain a `grant::detach_with<>` entry.
//
// Mismatch axis: DETACH-SPAWN missing audit grant.
//   join::Detached (V-203 phantom tag) marks jthread.detach() —
//   banned-by-default per CLAUDE.md §IX.  The V-204 coherence
//   concept requires `detach_with<Rationale>` to opt in with an
//   audit-trail rationale.  Submitting Detached without the
//   grant fires the concept gate.
//
// Distinct from fixture #1 (empty rationale → in-class
// static_assert) and fixture #3 (Forked missing subprocess →
// different syscall family).
//
// Expected diagnostic: JoinPolicyGrantsCoherent / constraints not
//                      satisfied / detach_with.

#include <crucible/fixy/spawn/SpawnGrant.h>

namespace neg_fixy_v_204_grant_detached_missing {

namespace gr   = ::crucible::fixy::spawn::grant;
namespace join = ::crucible::fixy::spawn::join;
using ::crucible::fixy::grant::ctrl::rationale;

// Consumer template constrained on JoinPolicyGrantsCoherent.
// Instantiating with Detached and a grant pack that LACKS
// detach_with<> MUST red the requires-clause.
template <typename Mechanism, typename... Grants>
    requires ::crucible::fixy::spawn::JoinPolicyGrantsCoherent<Mechanism, Grants...>
constexpr int gate_check() noexcept { return 1; }

// Wrong-family bystanders only — no detach_with<> anywhere.
struct dummy_parent_tag {};
struct dummy_ctx {};

constexpr int bad_dispatch = gate_check<
    join::Detached,
    gr::fork_parent<dummy_parent_tag>,
    gr::syscall_only<rationale{"wrong family — this is for Cloned"}>,
    gr::exec_ctx<dummy_ctx>
>();

}  // namespace neg_fixy_v_204_grant_detached_missing

int main() {
    return 0;
}

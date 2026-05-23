// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-204 HS14 fixture #3 of 3 for fixy/spawn/SpawnGrant.h:
// `JoinPolicyGrantsCoherent<join::Forked, ...>` rejects a grant
// pack that does NOT contain a `grant::subprocess<>` entry.
//
// Mismatch axis: PROCESS-SPAWN missing audit grant.
//   join::Forked (V-203 phantom tag) marks fork(2) — banned
//   everywhere except CLI launcher tools.  The V-204 coherence
//   concept requires `subprocess<Rationale>` to opt in with an
//   audit-trail rationale; the script-side CRUCIBLE_SPAWN_ALLOW_PROCESS
//   guard (V-210 already shipped) is the orthogonal opt-in for
//   the directory-level guard.
//
// Distinct from fixture #2 (Detached missing detach_with) because
// (a) the syscall family differs (fork(2) vs jthread.detach()),
// (b) the audit-trail grant family differs (subprocess<> vs
// detach_with<>), and (c) the production-allowlist mechanism
// differs (CMake CRUCIBLE_SPAWN_ALLOW_PROCESS vs detach_with<>
// alone).
//
// Expected diagnostic: JoinPolicyGrantsCoherent / constraints not
//                      satisfied / subprocess.

#include <crucible/fixy/spawn/SpawnGrant.h>

namespace neg_fixy_v_204_grant_forked_missing {

namespace gr   = ::crucible::fixy::spawn::grant;
namespace join = ::crucible::fixy::spawn::join;
using ::crucible::fixy::grant::ctrl::rationale;

// Consumer template constrained on JoinPolicyGrantsCoherent.
// Instantiating with Forked and a grant pack that LACKS
// subprocess<> MUST red the requires-clause.
template <typename Mechanism, typename... Grants>
    requires ::crucible::fixy::spawn::JoinPolicyGrantsCoherent<Mechanism, Grants...>
constexpr int gate_check() noexcept { return 1; }

// Wrong-family bystanders only — no subprocess<> anywhere.
struct dummy_parent_tag {};
struct dummy_ctx {};

constexpr int bad_dispatch = gate_check<
    join::Forked,
    gr::fork_parent<dummy_parent_tag>,
    gr::detach_with<rationale{"wrong family — this is for Detached"}>,
    gr::exec_ctx<dummy_ctx>
>();

}  // namespace neg_fixy_v_204_grant_forked_missing

int main() {
    return 0;
}

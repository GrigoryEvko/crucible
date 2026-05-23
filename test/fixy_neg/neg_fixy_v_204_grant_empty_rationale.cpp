// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-204 HS14 fixture #1 of 3 for fixy/spawn/SpawnGrant.h:
// `grant::detach_with<"">` reds at GRANT INSTANTIATION via the
// in-class `static_assert(rationale_nonempty_v<Rationale>)`.
//
// Mismatch axis: EMPTY RATIONALE STRING.
//   The grant's in-class static_assert fires BEFORE any consumer
//   sees the grant — empty audit-trail rationale is rejected at
//   the point of grant construction, mirroring the secret_policy::*
//   declassification policy discipline.
//
// Distinct from fixtures #2 (Detached without detach_with) and
// #3 (Forked without subprocess), which fire on the
// JoinPolicyGrantsCoherent CONCEPT axis rather than the in-class
// static_assert axis.  Three orthogonal rejection paths ⇒ HS14
// floor satisfied.
//
// Expected diagnostic: rationale_nonempty_v / static assertion
//                      failed / Rationale must be non-empty.

#include <crucible/fixy/spawn/SpawnGrant.h>

namespace neg_fixy_v_204_grant_empty_rationale {

namespace gr = ::crucible::fixy::spawn::grant;
using ::crucible::fixy::grant::ctrl::rationale;

// Empty rationale literal — the grant's in-class static_assert
// MUST fire on instantiation, BEFORE any consumer sees the grant.
using BadDetach = gr::detach_with<rationale{""}>;

[[maybe_unused]] BadDetach bad_instance{};

}  // namespace neg_fixy_v_204_grant_empty_rationale

int main() {
    return 0;
}

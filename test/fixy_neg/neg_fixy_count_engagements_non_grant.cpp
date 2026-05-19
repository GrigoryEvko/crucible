// fixy_neg: count_engagements_for / UniqueEngagementPerAxis tolerates
// a non-grant type in the pack via the same `engages_dim_one<D, G>`
// gating fix.
//
// HS14 floor for fixy-CR-08 (sibling fixture to
// neg_fixy_engaged_for_non_grant).  The pre-CR-08 fold
//
//     ((grant::IsGrantTag_v<G>
//       && grant::which_dim_v<G> == D ? 1u : 0u) + ...)
//
// inside `count_engagements_for` has the same eager-substitution
// hazard as `engaged_for`: the compiler substitutes `which_dim_v<G>`
// for every G in the pack BEFORE the consteval `&&` short-circuit
// runs, and `which_dim`'s primary template is undefined.  A non-grant
// G (here: a user-defined empty struct `NotAGrant` that does not
// inherit `grant_base`) thus produced a substitution failure inside
// `every_axis_engaged_at_most_once` rather than a clean rejection at
// the IsAcceptedGrants level.
//
// After CR-08, the per-grant probe in `detail::engagement::
// engages_dim_one<D, G>()` gates `which_dim_v<G>` substitution behind
// `if constexpr (grant::IsGrantTag_v<G>)`.  Non-grant Gs return
// `false` cleanly, `count_engagements_for` returns `0` for them, and
// the failure surfaces at the `AllGrantsWellFormed<...>` concept
// ahead of `UniqueEngagementPerAxis<...>` ever being evaluated.
//
// Expected diagnostic: constraint-satisfaction failure naming
// `IsAcceptedGrants` / `AllGrantsWellFormed` / `IsGrantTag` — NOT a
// "no member named 'value'" / "incomplete type which_dim<...>" error.
// The fixture must FAIL to compile because the pack is malformed
// regardless of the gating fix; CR-08's contribution is the SHAPE of
// the diagnostic (clean concept-rejection chain instead of deep
// substitution failure).

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

// User-defined empty struct that does NOT inherit `grant_base` and is
// therefore not a Grant tag per `IsGrantTag_v`.  Distinct from the
// FIXY-AUDIT-A3 `evil_grant` fixture (which DOES inherit grant_base
// and exercises the missing-which_dim path) — this fixture exercises
// the upstream "type is not a Grant at all" path that CR-08 unbreaks.
namespace neg_count_engagements_non_grant {
struct NotAGrant {};
}  // namespace neg_count_engagements_non_grant

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // Inject the non-grant alongside the full 19-axis strict cover
    // so every axis is engaged once; the rejection cause is purely
    // the non-grant pack member, which cleanly fails
    // AllGrantsWellFormed → IsAcceptedGrants → IsAccepted → the
    // class-body static_assert in fixy::fn.
    using NG = neg_count_engagements_non_grant::NotAGrant;
    auto bad = fixy::mint_fn<double,
        strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
        strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
        strict<D::Provenance>, strict<D::Trust>,
        strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>,
        strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
        strict<D::Staleness>,
        NG>(2.71);
    (void)bad;
    return 0;
}

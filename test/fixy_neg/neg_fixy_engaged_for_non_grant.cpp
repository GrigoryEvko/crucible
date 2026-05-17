// fixy_neg: IsAcceptedGrants rejects a pack containing a non-grant
// type via the AllGrantsWellFormed gate — and crucially does NOT
// short-cut to a `which_dim_v<NonGrant>` substitution failure inside
// the `engaged_for` / `first_missing_axis` fold.
//
// HS14 floor for fixy-CR-08.  The pre-CR-08 fold
//
//     ((grant::IsGrantTag_v<G>
//       && grant::which_dim_v<G> == D) || ...)
//
// substitutes `which_dim_v<G>` for every G in the pack BEFORE the
// consteval `&&` short-circuit runs.  `which_dim`'s primary template
// is intentionally left undefined (per-tag specialization only), so a
// non-grant G (here: a plain `int`) reaching `engaged_for` produced a
// hard substitution error inside the fold rather than a clean
// rejection at the IsAccepted boundary.
//
// The CR-08 fix routes the lookup through the
// `detail::engagement::engages_dim_one<D, G>()` helper, which uses
// `if constexpr (grant::IsGrantTag_v<G>)` to gate `which_dim_v<G>`
// substitution.  Non-grant types now return `false` cleanly and the
// rejection surfaces at the `AllGrantsWellFormed<...>` concept ahead
// of any `AllDimsEngaged<...>` evaluation — exactly mirroring the
// FIXY-AUDIT-A1 fix that Fn.h:338-347 applies to `find_grant_impl`
// (substitution gating, not substitution failure, as the rejection
// mechanism).
//
// Expected diagnostic: a constraint-satisfaction failure naming
// `IsAcceptedGrants` / `AllGrantsWellFormed` / `IsGrantTag` — NOT a
// "no member named 'value'" / "incomplete type which_dim<int>" error.
// The regex accepts either the concept-name chain (post-CR-08) OR
// (defensively) the substrate substitution diagnostic, so a
// hypothetical regression that re-broke the gating would still
// surface as a compile failure (the fixture stays "this must fail to
// compile" — the regex just covers both rejection modes).

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

int main() {
    // 19 strict markers cover all non-Type axes (Type is injected
    // implicitly by mint_fn).  Mixing a plain `int` into the pack
    // would have triggered `which_dim_v<int>` substitution inside
    // engaged_for / count_engagements_for / first_missing_axis_v;
    // after CR-08 it triggers `IsGrantTag_v<int> == false` first,
    // and the rejection comes from AllGrantsWellFormed.
    auto bad = fixy::mint_fn<float,
        strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
        strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
        strict<D::Provenance>, strict<D::Trust>,
        strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>,
        strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
        strict<D::Staleness>,
        int>(3.14f);
    (void)bad;
    return 0;
}

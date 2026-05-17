// fixy_neg: fixy::fn class-body tier-2 branch rejects malformed grant.
//
// HS14 floor for fixy-H-02.  The wrapper's class-body static_assert
// chain now has FIVE tiers (replacing a single misleading message).
// Tier 2 fires when the Grants pack contains a type that does NOT
// satisfy fixy::grant::IsGrantTag (not final-class, doesn't inherit
// grant_base, or is a non-grant type entirely — here: a raw `int`).
// Before H-02 this would produce a misleading "axis not engaged"
// diagnostic; H-02 surfaces the FixyMalformedGrant tag instead so
// the author knows the issue is a malformed entry, not a missing
// axis.
//
// Distinct from neg_fixy_grant_missing_which_dim.cpp (which targets
// the per-grant trait-spec injection defense at the grant-template
// level): this fixture pins the PACK-LEVEL AllGrantsWellFormed check
// inside fn<>'s class-body static_assert chain.
//
// Expected diagnostic: "FixyMalformedGrant" — proves tier 2 fired
// and that the diagnostic correctly names the malformed-pack tag
// rather than the missing-axis or duplicate-axis path.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// Direct class-template instantiation with a raw `int` in the Grants
// pack — NOT a grant tag.  Tier 1 (Type) passes (the OUTER int is
// fine as the wrapped value Type), tier 2 (AllGrantsWellFormed)
// FAILS because IsGrantTag<int> is false.  The remaining tiers are
// skipped via the chained-ternary guard.
//
// We use an otherwise-complete 19-axis strict pack so the failure
// CANNOT be attributed to a missing axis — only the malformed `int`
// entry breaks well-formedness.
using BadFn = fixy::fn<int,
    strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
    strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
    strict<D::Staleness>,
    int                              // ← malformed grant: raw int
>;

// Force class-body completion via sizeof.
static_assert(sizeof(BadFn) > 0,
    "instantiate fixy::fn class body to force its static_assert chain");

int main() { return 0; }

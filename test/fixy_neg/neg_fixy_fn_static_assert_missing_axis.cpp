// fixy_neg: fixy::fn class-body tier-3 branch rejects missing axis.
//
// HS14 floor for fixy-H-02.  The wrapper's class-body static_assert
// chain now has FIVE tiers (replacing a single misleading message).
// Tier 3 fires when at least one DimensionAxis is NOT engaged by any
// grant — its diagnostic names BOTH `first_missing_axis_v<Grants...>`
// (the inspection helper) AND the FixyNotEngaged_<Axis> diagnostic
// tag family.  Sibling of neg_fixy_fn_class_body_unengaged.cpp; the
// older fixture pins the surviving "FixyNotEngaged_<Axis>" substring,
// this one pins the NEW "first_missing_axis_v" cite added by H-02.
//
// Expected diagnostic: "first_missing_axis_v" — proves the branched
// message points at the correct inspection helper for the missing-
// axis failure mode (distinct from tier 4's first_duplicate_axis_v
// and tier 2's FixyMalformedGrant).

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// Direct class-template instantiation with Effect axis omitted —
// goes through the class-body static_assert chain (NOT mint_fn's
// requires-clause path).  Tier 1 (Type) passes (int is fine), tier 2
// (well-formed grants) passes (all entries are accept_default_strict_
// for), tier 3 (AllDimsEngaged) FAILS at the Effect axis.
using BadFn = fixy::fn<int,
    strict<D::Refinement>, strict<D::Usage>,
    /* strict<D::Effect> omitted */
    strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
    strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
    strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
    strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
    strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>>;

// Force class-body completion via sizeof.
static_assert(sizeof(BadFn) > 0,
    "instantiate fixy::fn class body to force its static_assert chain");

int main() { return 0; }

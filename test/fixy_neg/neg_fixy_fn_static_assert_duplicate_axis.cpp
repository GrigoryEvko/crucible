// fixy_neg: fixy::fn class-body tier-4 branch rejects duplicate axis.
//
// HS14 floor for fixy-H-02.  The wrapper's class-body static_assert
// chain now has FIVE tiers (replacing a single misleading message).
// Tier 4 fires when at least one DimensionAxis is engaged MORE THAN
// ONCE.  Before H-02 a duplicate engagement produced the SAME
// misleading "axis not engaged" message tier 3 emits — H-02 splits
// the cases so the diagnostic now names BOTH
// `first_duplicate_axis_v<Grants...>` (the inspection helper) AND
// the FixyDuplicate_<Axis> diagnostic tag family.
//
// Distinct from neg_fixy_duplicate_engagement.cpp (which targets
// mint_fn's requires-clause path and surfaces "UniqueEngagementPer-
// Axis" as a concept-name match): this fixture instantiates fixy::fn
// directly, so the diagnostic is the tier-4 static_assert message
// inside the class body — proving the H-02 branched-message fix
// actually surfaces the duplicate-axis cite, not the missing-axis
// one.
//
// Expected diagnostic: "first_duplicate_axis_v" — confirms tier 4
// fired (not tier 3) and that the duplicate-axis inspection helper
// is correctly cited.

#include <crucible/fixy/Fn.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// Direct class-template instantiation with a 20-entry pack that
// engages all 19 non-Type axes — but Usage TWICE.  Tier 1 (Type)
// passes (int is fine), tier 2 (well-formed grants) passes (every
// entry is accept_default_strict_for), tier 3 (AllDimsEngaged)
// PASSES (Usage is engaged), tier 4 (UniqueEngagementPerAxis) FAILS
// at the Usage axis.
using BadFn = fixy::fn<int,
    strict<D::Refinement>,
    strict<D::Usage>,                 // first engagement of Usage
    strict<D::Usage>,                 // DUPLICATE engagement of Usage
    strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
    strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>;

// Force class-body completion via sizeof.
static_assert(sizeof(BadFn) > 0,
    "instantiate fixy::fn class body to force its static_assert chain");

int main() { return 0; }

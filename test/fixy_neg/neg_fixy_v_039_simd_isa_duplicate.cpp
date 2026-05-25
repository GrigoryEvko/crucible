// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-039 — SimdIsa duplicate-engagement rejection.
//
// `grant::hw::simd_width<uint16_t WidthBits>` (V-257, Hw.h:322) and
// `grant::simd::width<WidthBits W>` (V-259, Simd.h:124) ARE structurally
// distinct grant types but BOTH route to `DimensionAxis::SimdIsa`
// (proven structurally by the FOUND-039 witness block in Simd.h's
// self-test).  A `fixy::fn` Grants pack that contains one of each
// therefore engages SimdIsa twice and MUST be rejected by tier-4
// `UniqueEngagementPerAxis` with the `FixyDuplicate_SimdIsa` diagnostic.
//
// Without this rejection a user could silently double-pin SimdIsa from
// two independent grant families — the federation cache would allocate
// distinct slots for the structurally-different but axis-identical
// engagements, ISA-dispatch behavior would depend on which grant the
// resolver picked first, and the audit trail would show two
// "SimdIsa-engaged" rows for one binding.
//
// Mismatch class: per-axis cardinality (UniqueEngagementPerAxis).
// Expected diagnostic: "UniqueEngagementPerAxis" OR "FixyDuplicate_"
// OR "first_duplicate_axis_v" — confirms the tier-4 gate fired on
// SimdIsa (not a missing-axis error from tier 3).

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Hw.h>
#include <crucible/fixy/Simd.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
namespace ghw  = crucible::fixy::grant::hw;
namespace gsi  = crucible::fixy::grant::simd;
using D        = crucible::fixy::dim::DimensionAxis;
using W        = crucible::fixy::simd::WidthBits;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// Engage all 33 non-Type axes via strict defaults, then ADD BOTH SimdIsa
// grants — tier 3 (AllDimsEngaged) PASSES because SimdIsa is engaged at
// least once; tier 4 (UniqueEngagementPerAxis) FAILS on SimdIsa because
// the pack engages it THREE times (strict-default + hw::simd_width<256>
// + simd::width<Bits256>).
//
// (Note: we keep the strict-default SimdIsa grant in the pack so tier 3
// passes cleanly — we want tier 4's duplicate-axis diagnostic, not a
// missing-axis red.  The duplicate gate fires whenever cardinality > 1,
// so 3 is sufficient to witness FOUND-039.)
using BadFn = fixy::fn<int,
    strict<D::Refinement>,
    strict<D::Usage>,
    strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
    strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
    strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
    strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>,
    strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>,
    strict<D::SimdIsa>,                       // first SimdIsa engagement
    strict<D::MemoryScope>,
    ghw::simd_width<256>,                     // SECOND SimdIsa engagement
    gsi::width<W::Bits256>>;                  // THIRD SimdIsa engagement

// Force class-body completion via sizeof so the tier-4 static_assert
// chain fires.
static_assert(sizeof(BadFn) > 0,
    "instantiate fixy::fn class body to force its static_assert chain");

int main() { return 0; }

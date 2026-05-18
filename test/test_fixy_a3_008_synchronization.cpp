// fixy-A3-008 sentinel TU: Wait + MemOrder dimensional reclassification.
//
// Pre-fix: wrapper_dimension<Wait<...>>     == DimensionAxis::Reentrancy
//          wrapper_dimension<MemOrder<...>> == DimensionAxis::Reentrancy
// Post-fix: both map to DimensionAxis::Synchronization (new axis 20).
//
// Reentrancy (fixy.md §24.1 dim 19) tracks call-graph self-call
// permission (`rec` / `with Reentrant`).  Wait tracks waiting-strategy
// choice (SpinPause / Sleep / Futex) and MemOrder tracks C++ memory-
// order discipline (Relaxed / Acquire / Release / SeqCst).  Both are
// concurrency-coordination axes; neither touches reentrancy semantics.
//
// Tier preservation: both wrappers stay at TierKind::Semiring under
// the new Synchronization axis (same Tier S surface, just a clean
// per-axis identity).  GAPS-091's verify_quadruple<W>() continues to
// hold because lattice/modality/tier remain the same.

#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/Wait.h>

namespace cs = ::crucible::safety;

namespace {

using WaitSpin   = cs::Wait<cs::WaitStrategy_v::SpinPause, int>;
using WaitPark   = cs::Wait<cs::WaitStrategy_v::Park,    int>;
using WaitBlock  = cs::Wait<cs::WaitStrategy_v::Block,   int>;

using MoRelaxed  = cs::MemOrder<cs::MemOrderTag_v::Relaxed, int>;
using MoAcquire  = cs::MemOrder<cs::MemOrderTag_v::Acquire, int>;
using MoRelease  = cs::MemOrder<cs::MemOrderTag_v::Release, int>;
using MoSeqCst   = cs::MemOrder<cs::MemOrderTag_v::SeqCst,  int>;

// ── Post-fix dimensional identity — Synchronization, not Reentrancy.
static_assert(cs::wrapper_dimension_v<WaitSpin>
              == cs::DimensionAxis::Synchronization,
    "fixy-A3-008: Wait<SpinPause> must classify on Synchronization, "
    "not Reentrancy.  Wait wraps a waiting-strategy choice; Reentrancy "
    "tracks call-graph self-call permission.");
static_assert(cs::wrapper_dimension_v<WaitPark>
              == cs::DimensionAxis::Synchronization,
    "fixy-A3-008: Wait<Park> must classify on Synchronization.");
static_assert(cs::wrapper_dimension_v<WaitBlock>
              == cs::DimensionAxis::Synchronization,
    "fixy-A3-008: Wait<Block> must classify on Synchronization.");

static_assert(cs::wrapper_dimension_v<MoRelaxed>
              == cs::DimensionAxis::Synchronization,
    "fixy-A3-008: MemOrder<Relaxed> must classify on Synchronization, "
    "not Reentrancy.  MemOrder wraps C++ memory-order discipline; "
    "Reentrancy tracks call-graph self-call permission.");
static_assert(cs::wrapper_dimension_v<MoAcquire>
              == cs::DimensionAxis::Synchronization,
    "fixy-A3-008: MemOrder<Acquire> must classify on Synchronization.");
static_assert(cs::wrapper_dimension_v<MoRelease>
              == cs::DimensionAxis::Synchronization,
    "fixy-A3-008: MemOrder<Release> must classify on Synchronization.");
static_assert(cs::wrapper_dimension_v<MoSeqCst>
              == cs::DimensionAxis::Synchronization,
    "fixy-A3-008: MemOrder<SeqCst> must classify on Synchronization.");

// ── Negative-direction guards — Wait / MemOrder are NOT on Reentrancy.
static_assert(cs::wrapper_dimension_v<WaitSpin>
              != cs::DimensionAxis::Reentrancy,
    "fixy-A3-008 regression: Wait wrappers leaked back onto Reentrancy.");
static_assert(cs::wrapper_dimension_v<MoSeqCst>
              != cs::DimensionAxis::Reentrancy,
    "fixy-A3-008 regression: MemOrder wrappers leaked back onto Reentrancy.");

// ── Tier preservation — Synchronization is Tier S (Semiring).
static_assert(cs::tier_of_axis(cs::DimensionAxis::Synchronization)
              == cs::TierKind::Semiring,
    "fixy-A3-008: Synchronization classifies on Tier S (par=+, seq=*).");
static_assert(cs::wrapper_tier_v<WaitSpin>  == cs::TierKind::Semiring,
    "fixy-A3-008: Wait wrappers preserve Tier-S surface post-reclassify.");
static_assert(cs::wrapper_tier_v<MoSeqCst>  == cs::TierKind::Semiring,
    "fixy-A3-008: MemOrder wrappers preserve Tier-S surface post-reclassify.");

// ── GAPS-091 cross-product verifier — still passes after reclassify.
static_assert(cs::verify_quadruple<WaitSpin>(),
    "fixy-A3-008: GAPS-091 verify_quadruple<Wait> must hold under the "
    "new Synchronization classification.");
static_assert(cs::verify_quadruple<WaitBlock>(),
    "fixy-A3-008: GAPS-091 verify_quadruple<Wait<Block>> must hold.");
static_assert(cs::verify_quadruple<MoRelaxed>(),
    "fixy-A3-008: GAPS-091 verify_quadruple<MemOrder<Relaxed>> must hold.");
static_assert(cs::verify_quadruple<MoSeqCst>(),
    "fixy-A3-008: GAPS-091 verify_quadruple<MemOrder<SeqCst>> must hold.");

// ── Catalog cardinality — Synchronization grew the axis count to 21.
static_assert(cs::DIMENSION_AXIS_COUNT == 21,
    "fixy-A3-008: DimensionAxis catalog must equal 21 (20 FX-derived + "
    "Synchronization extension added 2026-05-18).");

// ── Synchronization carries a non-empty, non-sentinel name.
static_assert(cs::dimension_axis_name(cs::DimensionAxis::Synchronization)
              == std::string_view{"Synchronization"},
    "fixy-A3-008: dimension_axis_name must return \"Synchronization\" "
    "for the new axis; sentinel leak indicates a missing switch arm.");

}  // namespace

int main() { return 0; }

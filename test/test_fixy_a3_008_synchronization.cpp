#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/Wait.h>

namespace cs = ::crucible::safety;

namespace {

using WaitSpin = cs::Wait<cs::WaitStrategy_v::SpinPause, int>;
using WaitPark = cs::Wait<cs::WaitStrategy_v::Park, int>;
using WaitBlock = cs::Wait<cs::WaitStrategy_v::Block, int>;

using MoRelaxed = cs::MemOrder<cs::MemOrderTag_v::Relaxed, int>;
using MoAcquire = cs::MemOrder<cs::MemOrderTag_v::Acquire, int>;
using MoRelease = cs::MemOrder<cs::MemOrderTag_v::Release, int>;
using MoSeqCst = cs::MemOrder<cs::MemOrderTag_v::SeqCst, int>;

static_assert(cs::wrapper_dimension_v<WaitSpin> == cs::DimensionAxis::Synchronization,
              "Wait<SpinPause> must classify on Synchronization, not Reentrancy. "
              "Wait wraps a waiting-strategy choice. Reentrancy tracks call-graph "
              "self-call permission.");
static_assert(cs::wrapper_dimension_v<WaitPark> == cs::DimensionAxis::Synchronization,
              "Wait<Park> must classify on Synchronization.");
static_assert(cs::wrapper_dimension_v<WaitBlock> == cs::DimensionAxis::Synchronization,
              "Wait<Block> must classify on Synchronization.");

static_assert(cs::wrapper_dimension_v<MoRelaxed> == cs::DimensionAxis::Synchronization,
              "MemOrder<Relaxed> must classify on Synchronization, not Reentrancy. "
              "MemOrder wraps memory-order discipline. Reentrancy tracks call-graph "
              "self-call permission.");
static_assert(cs::wrapper_dimension_v<MoAcquire> == cs::DimensionAxis::Synchronization,
              "MemOrder<Acquire> must classify on Synchronization.");
static_assert(cs::wrapper_dimension_v<MoRelease> == cs::DimensionAxis::Synchronization,
              "MemOrder<Release> must classify on Synchronization.");
static_assert(cs::wrapper_dimension_v<MoSeqCst> == cs::DimensionAxis::Synchronization,
              "MemOrder<SeqCst> must classify on Synchronization.");

static_assert(cs::wrapper_dimension_v<WaitSpin> != cs::DimensionAxis::Reentrancy,
              "Wait wrappers leaked back onto Reentrancy.");
static_assert(cs::wrapper_dimension_v<MoSeqCst> != cs::DimensionAxis::Reentrancy,
              "MemOrder wrappers leaked back onto Reentrancy.");

static_assert(cs::tier_of_axis(cs::DimensionAxis::Synchronization) == cs::TierKind::Semiring,
              "Synchronization classifies on Tier S, with par=+ and seq=*.");
static_assert(cs::wrapper_tier_v<WaitSpin> == cs::TierKind::Semiring, "Wait wrappers carry a Tier-S surface.");
static_assert(cs::wrapper_tier_v<MoSeqCst> == cs::TierKind::Semiring, "MemOrder wrappers carry a Tier-S surface.");

static_assert(cs::verify_quadruple<WaitSpin>(), "verify_quadruple<Wait> must hold under the Synchronization "
                                                "classification.");
static_assert(cs::verify_quadruple<WaitBlock>(), "verify_quadruple<Wait<Block>> must hold.");
static_assert(cs::verify_quadruple<MoRelaxed>(), "verify_quadruple<MemOrder<Relaxed>> must hold.");
static_assert(cs::verify_quadruple<MoSeqCst>(), "verify_quadruple<MemOrder<SeqCst>> must hold.");

// A floor, not an exact count. The exact count is pinned beside the
// enumerator definition, so this witness catches only the removal of an
// arm, which that pin cannot see.
static_assert(cs::DIMENSION_AXIS_COUNT >= 21, "the DimensionAxis catalog must include Synchronization, so at least "
                                              "21 axes. Further extensions are additive.");

static_assert(cs::dimension_axis_name(cs::DimensionAxis::Synchronization) == std::string_view{"Synchronization"},
              "dimension_axis_name must return \"Synchronization\" for this axis. "
              "A sentinel leak means a missing switch arm.");

}  // namespace

int main() { return 0; }

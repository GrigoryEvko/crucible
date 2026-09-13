// Two collision rules are under test.
//
//   V401 rejects a publication visible at device scope or wider that is
//   fenced weaker than acquire-release.  It reads the scope tier and
//   the barrier tier out of one composed carrier type, so the two
//   detectors have to see through each other.
//   V402 rejects a scope drawn from one hardware trunk pinned to a host
//   architecture from another: an accelerator scope on a CPU host, or
//   an ARM shareability scope on a host that is not ARM.
//
// Each rule is wired twice, as a concept folded into the all-rules
// check and as an independent assertion inside the validator.  The
// concept alone does not gate instantiation of a composed carrier.

#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Fn.h>  // pulls in the collision catalog
#include <crucible/safety/Hw.h>
#include <crucible/safety/ScopedFence.h>
#include <crucible/safety/SimdWidthPinned.h>
#include <crucible/safety/source/Arch.h>

#include <string_view>
#include <type_traits>

namespace cs = ::crucible::safety;
namespace csfn = ::crucible::safety::fn;
namespace csc = ::crucible::safety::fn::collision;
namespace eff = ::crucible::effects;
namespace src = ::crucible::safety::source;
using MS = ::crucible::algebra::lattices::MemoryScope;
using BS = ::crucible::algebra::lattices::BarrierStrength;
using HW = ::crucible::algebra::lattices::HwInstruction;
using SI = ::crucible::algebra::lattices::SimdIsa;
using AT = ::crucible::safety::source::ArchTag;

namespace {

static_assert(csc::catalog_size >= 47, "The catalog must still contain both memory-scope rules.");
static_assert(csc::rule_bijection_v<csc::RuleCode::V401>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V402>);

using DefaultFn = csfn::Fn<int>;
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::V401>::rule_code() == std::string_view{"V401"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::V402>::rule_code() == std::string_view{"V402"});

using FenceGpu = cs::ScopedFence<MS::Gpu, int>;
using FenceCta = cs::ScopedFence<MS::Cta, int>;
using FenceSystem = cs::ScopedFence<MS::System, int>;
using FenceInner = cs::ScopedFence<MS::Inner, int>;
using FenceThread = cs::ScopedFence<MS::Thread, int>;
using BarNone = cs::BarrierGuarded<BS::None, int>;
using BarAcqRel = cs::BarrierGuarded<BS::AcqRel, int>;

static_assert(csc::scope_tier_of<FenceGpu>::has_scope);
static_assert(csc::scope_tier_of<FenceGpu>::value == MS::Gpu);
static_assert(csc::scope_tier_of<FenceInner>::value == MS::Inner);
static_assert(!csc::scope_tier_of<int>::has_scope);
static_assert(!csc::scope_tier_of<BarNone>::has_scope);  // a barrier carries no scope

static_assert(csc::scope_tier_of<FenceGpu const&>::has_scope);
static_assert(csc::scope_tier_of<FenceGpu const>::value == MS::Gpu);

// The scope detector sees through the sibling hardware-band wrappers,
// so the rule still finds a scope nested inside one of them.
static_assert(csc::scope_tier_of<cs::BarrierGuarded<BS::None, FenceGpu>>::has_scope);
static_assert(csc::scope_tier_of<cs::BarrierGuarded<BS::None, FenceGpu>>::value == MS::Gpu);
static_assert(csc::scope_tier_of<cs::Hw<HW::Scalar, FenceCta>>::value == MS::Cta);
static_assert(csc::scope_tier_of<cs::SimdWidthPinned<SI::Avx2, FenceInner>>::value == MS::Inner);

// The barrier detector sees through a fence wrapper in turn.  The two
// detectors pierce each other, which is what makes the rule
// independent of the order the wrappers are nested in.
static_assert(csc::barrier_tier_of<cs::ScopedFence<MS::Gpu, BarAcqRel>>::has_barrier);
static_assert(csc::barrier_tier_of<cs::ScopedFence<MS::Gpu, BarAcqRel>>::value == BS::AcqRel);
static_assert(csc::barrier_tier_of<BarAcqRel>::value == BS::AcqRel);

// Only device scope and wider clear the floor.  A block-level scope
// sits below it, and an ARM-trunk scope fails for a different reason:
// ordering across trunks is never true.
static_assert(csc::scope_at_or_above_v<MS::Gpu, FenceGpu>);  // at the floor
static_assert(csc::scope_at_or_above_v<MS::Gpu, FenceSystem>);  // top, above the floor
static_assert(!csc::scope_at_or_above_v<MS::Gpu, FenceCta>);  // block scope, below
static_assert(!csc::scope_at_or_above_v<MS::Gpu, FenceInner>);  // ARM trunk
static_assert(!csc::scope_at_or_above_v<MS::Gpu, FenceThread>);  // bottom
static_assert(!csc::scope_at_or_above_v<MS::Gpu, int>);  // no wrapper
static_assert(csc::scope_at_or_above_v<MS::Gpu, cs::BarrierGuarded<BS::None, FenceGpu>>);

// A portable pin names no concrete host, so it contradicts nothing.
// The bottom and top scopes are realizable on every host, so they
// contradict nothing either.
static_assert(csc::scope_contradicts_host_arch(MS::Gpu, AT::Arm));
static_assert(csc::scope_contradicts_host_arch(MS::Gpu, AT::X86));
static_assert(!csc::scope_contradicts_host_arch(MS::Gpu, AT::Portable));
static_assert(csc::scope_contradicts_host_arch(MS::Cta, AT::Arm));
static_assert(csc::scope_contradicts_host_arch(MS::Inner, AT::X86));
static_assert(!csc::scope_contradicts_host_arch(MS::Inner, AT::Arm));
static_assert(!csc::scope_contradicts_host_arch(MS::Inner, AT::Portable));
static_assert(!csc::scope_contradicts_host_arch(MS::Thread, AT::X86));
static_assert(!csc::scope_contradicts_host_arch(MS::System, AT::X86));
static_assert(!csc::scope_contradicts_host_arch(MS::System, AT::Arm));

// The probes below are deliberately not composed carriers.  A composed
// carrier asserts its own validity at instantiation, which would fire
// before a concept could be read, so the probe would prove nothing
// about the concept.  Each probe carries the three member types the
// detectors read.
struct MockV401Under {  // device scope, unfenced
    using type_t = cs::BarrierGuarded<BS::None, FenceGpu>;
    using source_t = src::PortablePinned;
    using effect_row_t = eff::Row<>;
};
struct MockV402Accel {  // device scope on an ARM host
    using type_t = cs::BarrierGuarded<BS::AcqRel, FenceGpu>;  // fenced, so only V402 can fire
    using source_t = src::ArmPinned;
    using effect_row_t = eff::Row<>;
};
struct MockV402Arm {  // ARM scope on an x86 host
    using type_t = FenceInner;
    using source_t = src::X86Pinned;
    using effect_row_t = eff::Row<>;
};
struct MockV402Marker {  // cross-trunk declared by marker
    using type_t = int;
    using source_t = src::PortablePinned;
    using effect_row_t = eff::Row<>;
};
struct MockScopeOk {  // fenced device scope, no host pin
    using type_t = cs::BarrierGuarded<BS::AcqRel, FenceGpu>;
    using source_t = src::PortablePinned;
    using effect_row_t = eff::Row<>;
};
struct MockArmOk {  // ARM scope on an ARM host
    using type_t = FenceInner;
    using source_t = src::ArmPinned;
    using effect_row_t = eff::Row<>;
};

}  // namespace

namespace crucible::safety::fn::collision {
template <>
struct marks_scope_arch_cross_trunk<::MockV402Marker> : std::true_type {};
}  // namespace crucible::safety::fn::collision

namespace {

// Each probe must trip exactly the rule it was built for and leave the
// other clear, or a passing assertion would not tell the two rules
// apart.
static_assert(!csc::V401_OK<MockV401Under>);
static_assert(csc::V401_OK<MockScopeOk>);
static_assert(csc::V401_OK<MockV402Accel>);
static_assert(csc::V401_OK<MockV402Arm>);

static_assert(!csc::V402_OK<MockV402Accel>);
static_assert(!csc::V402_OK<MockV402Arm>);
static_assert(!csc::V402_OK<MockV402Marker>);
static_assert(csc::V402_OK<MockScopeOk>);
static_assert(csc::V402_OK<MockArmOk>);
static_assert(csc::V402_OK<MockV401Under>);

// The leg of the second rule that reads the carrier type, as opposed
// to the one driven by the marker.
static_assert(csc::scope_arch_cross_trunk_v<MockV402Accel>);
static_assert(csc::scope_arch_cross_trunk_v<MockV402Arm>);
static_assert(!csc::scope_arch_cross_trunk_v<MockScopeOk>);
static_assert(!csc::scope_arch_cross_trunk_v<MockArmOk>);

// Compositions that must trip neither rule.  Only the two unsound
// shapes are rejected, and a rule that over-fires would show up here.

// Block-scope visibility does not require acquire-release, so an
// unfenced block scope is sound.
using NeutralCtaNone = csfn::Fn<cs::BarrierGuarded<BS::None, FenceCta>>;
static_assert(csfn::ValidComposition<NeutralCtaNone>);
static_assert(csc::first_failure_v<NeutralCtaNone> == csc::RuleCode::None);

using NeutralGpuAcqRel = csfn::Fn<cs::BarrierGuarded<BS::AcqRel, FenceGpu>>;
static_assert(csfn::ValidComposition<NeutralGpuAcqRel>);
static_assert(csc::first_failure_v<NeutralGpuAcqRel> == csc::RuleCode::None);

using NeutralGpuFenced = csfn::Fn<cs::BarrierGuarded<BS::AcqRel, FenceGpu>>;
static_assert(csfn::ValidComposition<NeutralGpuFenced>);

// The default source pin names no concrete host, so no scope can cross
// a trunk against it.
using NeutralInner = csfn::Fn<FenceInner>;
static_assert(csfn::ValidComposition<NeutralInner>);
static_assert(csc::first_failure_v<NeutralInner> == csc::RuleCode::None);

// The top scope is realizable on any host.
using NeutralSystem = csfn::Fn<cs::BarrierGuarded<BS::AcqRel, FenceSystem>>;
static_assert(csfn::ValidComposition<NeutralSystem>);

// A carrier with no fence at all is outside both rules.
static_assert(csfn::ValidComposition<DefaultFn>);
static_assert(csc::first_failure_v<DefaultFn> == csc::RuleCode::None);

// A composed carrier that genuinely trips either rule cannot be
// asserted here at all: instantiating it runs the carrier's own
// validity assertion, which fires before the first-failure code can be
// read.  Those two cases are covered by negative-compile fixtures
// instead, one per rule, each instantiating a real carrier so that the
// assertion leg is the thing exercised.

}  // namespace

int main() { return 0; }

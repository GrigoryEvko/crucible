// Eight collision rules are under test.  Each gates a composition in
// which a hardware declaration is unsound against another axis.  The
// digits after the leading letter encode which hardware sub-axis a
// rule belongs to: the hundreds place runs vendor, SIMD instruction
// set, hardware instruction, then barrier strength.
//
// Five of the eight read a wrapper tier out of the carrier type, so
// they can fire on a type alone.  The other three read an opt-in
// marker that grant-pack analysis specializes, so they stay silent
// until something opts in.

#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Fn.h>  // pulls in the collision catalog
#include <crucible/safety/Hw.h>
#include <crucible/safety/SimdWidthPinned.h>

#include <string_view>
#include <type_traits>

namespace cs = ::crucible::safety;
namespace csfn = ::crucible::safety::fn;
namespace csc = ::crucible::safety::fn::collision;
namespace eff = ::crucible::effects;
using HW = ::crucible::algebra::lattices::HwInstruction;
using BS = ::crucible::algebra::lattices::BarrierStrength;
using SI = ::crucible::algebra::lattices::SimdIsa;

namespace {

static_assert(csc::catalog_size >= 44, "The catalog must still contain all eight hardware rules.");
static_assert(csc::rule_bijection_v<csc::RuleCode::V001>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V002>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V101>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V102>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V201>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V202>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V203>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V301>);

using DefaultFn = csfn::Fn<int>;
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::V001>::rule_code() == std::string_view{"V001"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::V101>::rule_code() == std::string_view{"V101"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::V201>::rule_code() == std::string_view{"V201"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::V202>::rule_code() == std::string_view{"V202"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::V301>::rule_code() == std::string_view{"V301"});

using HwScalar = cs::Hw<HW::Scalar, int>;
using HwVec = cs::Hw<HW::Vectorizable, int>;
using HwTsc = cs::Hw<HW::NonDeterministicTsc, int>;
using HwMsr = cs::Hw<HW::PrivilegedMsr, int>;
using BarNone = cs::BarrierGuarded<BS::None, int>;
using BarAcqRel = cs::BarrierGuarded<BS::AcqRel, int>;
using BarSeqCst = cs::BarrierGuarded<BS::SeqCst, int>;
using BarFence = cs::BarrierGuarded<BS::FullFence, int>;
using SimdScalar = cs::SimdWidthPinned<SI::Scalar, int>;
using SimdAvx2 = cs::SimdWidthPinned<SI::Avx2, int>;
using SimdPort = cs::SimdWidthPinned<SI::Portable, int>;

static_assert(csc::hw_tier_of<HwTsc>::has_hw);
static_assert(csc::hw_tier_of<HwTsc>::value == HW::NonDeterministicTsc);
static_assert(!csc::hw_tier_of<int>::has_hw);
static_assert(!csc::hw_tier_of<BarSeqCst>::has_hw);  // a sibling wrapper family
static_assert(csc::barrier_tier_of<BarSeqCst>::has_barrier);
static_assert(csc::barrier_tier_of<BarSeqCst>::value == BS::SeqCst);
static_assert(!csc::barrier_tier_of<int>::has_barrier);
static_assert(csc::simd_isa_of<SimdAvx2>::has_simd);
static_assert(csc::simd_isa_of<SimdAvx2>::value == SI::Avx2);
static_assert(!csc::simd_isa_of<int>::has_simd);

// A detector reads through const and reference, because a wrapper on a
// returned value carries the same declaration as one on a stored value.
static_assert(csc::hw_tier_of<HwTsc const&>::has_hw);
static_assert(csc::barrier_tier_of<BarSeqCst&>::value == BS::SeqCst);
static_assert(csc::simd_isa_of<SimdAvx2 const>::value == SI::Avx2);

// The floor of the hardware-instruction rules is the timestamp tier.
static_assert(!csc::hw_at_or_above_v<HW::NonDeterministicTsc, HwScalar>);  // below
static_assert(!csc::hw_at_or_above_v<HW::NonDeterministicTsc, HwVec>);  // below
static_assert(csc::hw_at_or_above_v<HW::NonDeterministicTsc, HwTsc>);  // at the floor
static_assert(csc::hw_at_or_above_v<HW::NonDeterministicTsc, HwMsr>);  // above
static_assert(!csc::hw_at_or_above_v<HW::NonDeterministicTsc, int>);  // no wrapper

// The floor of the barrier rule is sequential consistency.
static_assert(!csc::barrier_at_or_above_v<BS::SeqCst, BarNone>);
static_assert(!csc::barrier_at_or_above_v<BS::SeqCst, BarAcqRel>);
static_assert(csc::barrier_at_or_above_v<BS::SeqCst, BarSeqCst>);  // at the floor
static_assert(csc::barrier_at_or_above_v<BS::SeqCst, BarFence>);  // above
static_assert(!csc::barrier_at_or_above_v<BS::SeqCst, int>);  // no wrapper

// Scalar and portable are the two poles that name no concrete vector
// width, so neither one can make a replay diverge.
static_assert(!csc::simd_isa_pins_specific_vector_v<SimdScalar>);
static_assert(!csc::simd_isa_pins_specific_vector_v<SimdPort>);
static_assert(csc::simd_isa_pins_specific_vector_v<SimdAvx2>);
static_assert(!csc::simd_isa_pins_specific_vector_v<int>);

// The probes below are deliberately not composed carriers.  A composed
// carrier asserts its own validity at instantiation, which would fire
// before a concept could be read, so the probe would prove nothing
// about the concept.
struct MockReplaySimd {
    using type_t = SimdAvx2;
    using effect_row_t = eff::Row<>;
};
struct MockHotTsc {
    using type_t = HwTsc;
    using effect_row_t = eff::Row<>;
};
struct MockMsrNoInit {
    using type_t = HwMsr;
    using effect_row_t = eff::Row<>;
};
struct MockReplayTsc {
    using type_t = HwTsc;
    using effect_row_t = eff::Row<>;
};
struct MockHotFence {
    using type_t = BarFence;
    using effect_row_t = eff::Row<>;
};
struct MockVendorMix {
    using type_t = int;
    using effect_row_t = eff::Row<>;
};
struct MockCrossArch {
    using type_t = int;
    using effect_row_t = eff::Row<>;
};
struct MockWidthOver {
    using type_t = int;
    using effect_row_t = eff::Row<>;
};

}  // namespace

namespace crucible::safety::fn::collision {
template <>
struct marks_replay_required<::MockReplaySimd> : std::true_type {};
template <>
struct marks_hot_path<::MockHotTsc> : std::true_type {};
template <>
struct marks_replay_required<::MockReplayTsc> : std::true_type {};
template <>
struct marks_hot_path<::MockHotFence> : std::true_type {};
template <>
struct marks_vendor_isa_inconsistent<::MockVendorMix> : std::true_type {};
template <>
struct marks_vendor_cross_arch<::MockCrossArch> : std::true_type {};
template <>
struct marks_simd_width_exceeds_isa<::MockWidthOver> : std::true_type {};
}  // namespace crucible::safety::fn::collision

namespace {

// The five rules that read the carrier type.
static_assert(!csc::V101_OK<MockReplaySimd>);  // replay against a specific vector width
static_assert(!csc::V201_OK<MockHotTsc>);  // hot path against a non-deterministic timer
static_assert(!csc::V202_OK<MockMsrNoInit>);  // privileged register outside setup
static_assert(!csc::V203_OK<MockReplayTsc>);  // replay against a non-deterministic timer
static_assert(!csc::V301_OK<MockHotFence>);  // hot path against a full fence
// The three rules that read a marker.
static_assert(!csc::V001_OK<MockVendorMix>);  // inconsistent vendor pack
static_assert(!csc::V002_OK<MockCrossArch>);  // two architectures in one binding
static_assert(!csc::V102_OK<MockWidthOver>);  // width beyond what the instruction set has

// The privileged-register rule stands down inside a setup context,
// which is where touching such a register is legitimate.
struct MockMsrWithInit {
    using type_t = HwMsr;
    using effect_row_t = eff::Row<eff::Effect::Init>;
};
static_assert(csc::V202_OK<MockMsrWithInit>);

// Compositions that must trip no rule.  Exactly eight combinations are
// rejected, and a rule that over-fires would show up here.

// Below the floor the hardware-instruction rules do not apply, marker
// or no marker.
using NeutralHwVec = csfn::Fn<HwVec>;
static_assert(csfn::ValidComposition<NeutralHwVec>);
static_assert(csc::first_failure_v<NeutralHwVec> == csc::RuleCode::None);

using NeutralBarAcqRel = csfn::Fn<BarAcqRel>;
static_assert(csfn::ValidComposition<NeutralBarAcqRel>);
static_assert(csc::first_failure_v<NeutralBarAcqRel> == csc::RuleCode::None);

// Neither pole names a concrete vector width, so neither can trip the
// replay rule.
using NeutralSimdScalar = csfn::Fn<SimdScalar>;
using NeutralSimdPort = csfn::Fn<SimdPort>;
static_assert(csfn::ValidComposition<NeutralSimdScalar>);
static_assert(csfn::ValidComposition<NeutralSimdPort>);

// A specific vector width is sound on its own.  Only a binding that
// also demands replay trips the rule, and a default carrier carries no
// such marker.
using NeutralSimdAvx2 = csfn::Fn<SimdAvx2>;
static_assert(csfn::ValidComposition<NeutralSimdAvx2>);
static_assert(csc::first_failure_v<NeutralSimdAvx2> == csc::RuleCode::None);

// The same argument for the timer tier: above the floor, but with no
// hot-path or replay marker to collide with.
using NeutralHwTsc = csfn::Fn<HwTsc>;
static_assert(csfn::ValidComposition<NeutralHwTsc>);

// A carrier with no hardware wrapper and no marker is outside all
// eight rules.
static_assert(csfn::ValidComposition<DefaultFn>);
static_assert(csc::first_failure_v<DefaultFn> == csc::RuleCode::None);

// A composed carrier that genuinely trips a rule cannot be asserted
// here at all: instantiating it runs the carrier's own validity
// assertion, which fires before the first-failure code can be read.
// Those eight cases are covered by negative-compile fixtures instead,
// one per rule.  Each fixture matches on that rule's own diagnostic
// text, so relaxing a rule makes its fixture compile and the test
// report a failure.

}  // namespace

int main() { return 0; }

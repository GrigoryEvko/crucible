// FIXY-V-004 sentinel TU: WrapperKind + wrapper_for reverse-lookup
// metafunction.  Closes the inverse of wrapper_dimension<W>::value
// (many-to-one wrapper→axis):
//
//   wrapper_for<DimensionAxis::X>() → std::array<WrapperKind, N>
//
// Forces every header-embedded static_assert through project
// warnings-as-errors per
// feedback_header_only_static_assert_blind_spot.

#include <crucible/fixy/Dim.h>
#include <crucible/safety/DimensionTraits.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace {

namespace sd = ::crucible::safety;
namespace fd = ::crucible::fixy::dim;

// ── Cardinality pin — 33 wrappers shipped ───────────────────────────
//
// Tracks the 33 wrapper_dimension specializations declared at
// safety/DimensionTraits.h lines 566..746.  Reflection-derived count
// matches the substrate constant.
static_assert(sd::WRAPPER_KIND_COUNT == 33);
static_assert(fd::WRAPPER_KIND_COUNT == 33);

// ── Forward projection — kind → axis ────────────────────────────────
//
// Spot-check coverage across every DimensionAxis that has at least
// one shipped wrapper (21 axes).  Verifies the wrapper_kind_to_axis
// switch routes each enumerator to its declared axis.

static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::Linear)
              == sd::DimensionAxis::Usage);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::Refined)
              == sd::DimensionAxis::Refinement);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::SealedRefined)
              == sd::DimensionAxis::Refinement);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::Tagged)
              == sd::DimensionAxis::Provenance);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::Secret)
              == sd::DimensionAxis::Security);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::CipherTier)
              == sd::DimensionAxis::Security);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::Stale)
              == sd::DimensionAxis::Staleness);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::TimeOrdered)
              == sd::DimensionAxis::Representation);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::Vendor)
              == sd::DimensionAxis::Representation);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::NumaPlacement)
              == sd::DimensionAxis::Representation);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::Monotonic)
              == sd::DimensionAxis::Mutation);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::AppendOnly)
              == sd::DimensionAxis::Mutation);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::HotPath)
              == sd::DimensionAxis::Regime);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::DetSafe)
              == sd::DimensionAxis::Effect);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::Crash)
              == sd::DimensionAxis::Effect);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::NumericalTier)
              == sd::DimensionAxis::Precision);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::RecipeSpec)
              == sd::DimensionAxis::Precision);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::Hw)
              == sd::DimensionAxis::HwInstruction);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::BarrierGuarded)
              == sd::DimensionAxis::BarrierStrength);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::SimdWidthPinned)
              == sd::DimensionAxis::SimdIsa);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::ScopedFence)
              == sd::DimensionAxis::MemoryScope);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::ResidencyHeat)
              == sd::DimensionAxis::Space);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::AllocClass)
              == sd::DimensionAxis::Space);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::Budgeted)
              == sd::DimensionAxis::Space);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::Wait)
              == sd::DimensionAxis::Synchronization);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::MemOrder)
              == sd::DimensionAxis::Synchronization);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::JoinPolicy)
              == sd::DimensionAxis::Synchronization);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::Progress)
              == sd::DimensionAxis::Complexity);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::Consistency)
              == sd::DimensionAxis::Version);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::EpochVersioned)
              == sd::DimensionAxis::Version);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::Witness)
              == sd::DimensionAxis::Observability);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::FpModePinned)
              == sd::DimensionAxis::FpMode);
static_assert(sd::wrapper_kind_to_axis(sd::WrapperKind::OpaqueLifetime)
              == sd::DimensionAxis::Lifetime);

// ── Name projection — every kind has a non-sentinel name ────────────
//
// Reflects the substrate's every_wrapper_kind_has_name() coverage
// assertion at the consumer side.
static_assert(sd::wrapper_kind_name(sd::WrapperKind::Linear)
              == std::string_view{"Linear"});
static_assert(sd::wrapper_kind_name(sd::WrapperKind::Witness)
              == std::string_view{"Witness"});
static_assert(sd::wrapper_kind_name(sd::WrapperKind::RecipeSpec)
              == std::string_view{"RecipeSpec"});

// ── Reverse map — count + array per axis ────────────────────────────
//
// Each per-axis count is hand-derived from the wrapper_dimension
// specializations above.  Adding a new wrapper on an existing axis
// requires bumping the count below (and is caught structurally by
// the cardinality pin); adding a new wrapper on a new axis requires
// no change here (just a new per-axis count assertion).

static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Usage)            == 1);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Refinement)       == 2);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Provenance)       == 1);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Security)         == 2);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Staleness)        == 1);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Representation)   == 3);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Mutation)         == 2);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Regime)           == 1);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Effect)           == 2);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Precision)        == 2);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::HwInstruction)    == 1);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::BarrierStrength)  == 1);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::SimdIsa)          == 1);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::MemoryScope)      == 1);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Space)            == 3);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Synchronization)  == 3);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Complexity)       == 1);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Version)          == 2);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Observability)    == 1);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::FpMode)           == 1);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Lifetime)         == 1);

// Axes with NO wrappers (12 axes) — reverse map returns empty.
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Type)             == 0);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Usage)            != 0); // sanity
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Protocol)         == 0);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Trust)            == 0);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Overflow)         == 0);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Reentrancy)       == 0);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Size)             == 0);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::SyscallSurface)   == 0);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::ControlFlow)      == 0);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::CallShape)        == 0);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::StackUse)         == 0);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::GlobalState)      == 0);
static_assert(sd::count_wrappers_on_axis(sd::DimensionAxis::Stdio)            == 0);

// Sum of per-axis counts == total wrapper count (33).
static_assert(
    sd::count_wrappers_on_axis(sd::DimensionAxis::Usage) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Refinement) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Provenance) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Security) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Staleness) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Representation) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Mutation) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Regime) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Effect) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Precision) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::HwInstruction) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::BarrierStrength) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::SimdIsa) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::MemoryScope) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Space) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Synchronization) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Complexity) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Version) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Observability) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::FpMode) +
    sd::count_wrappers_on_axis(sd::DimensionAxis::Lifetime)
    == 33,
    "Sum of per-axis wrapper counts must equal WRAPPER_KIND_COUNT.");

// ── wrapper_for<D>() — content + ordering checks ────────────────────
//
// Mutation axis: {Monotonic, AppendOnly} in enumerator declaration
// order.
constexpr auto kMutationWrappers = sd::wrapper_for<sd::DimensionAxis::Mutation>();
static_assert(kMutationWrappers.size()  == 2);
static_assert(kMutationWrappers[0] == sd::WrapperKind::Monotonic);
static_assert(kMutationWrappers[1] == sd::WrapperKind::AppendOnly);

// Synchronization axis: {Wait, MemOrder, JoinPolicy} in enumerator
// declaration order.
constexpr auto kSyncWrappers = sd::wrapper_for<sd::DimensionAxis::Synchronization>();
static_assert(kSyncWrappers.size()  == 3);
static_assert(kSyncWrappers[0] == sd::WrapperKind::Wait);
static_assert(kSyncWrappers[1] == sd::WrapperKind::MemOrder);
static_assert(kSyncWrappers[2] == sd::WrapperKind::JoinPolicy);

// Space axis: {ResidencyHeat, AllocClass, Budgeted} in declaration order.
constexpr auto kSpaceWrappers = sd::wrapper_for<sd::DimensionAxis::Space>();
static_assert(kSpaceWrappers.size()  == 3);
static_assert(kSpaceWrappers[0] == sd::WrapperKind::ResidencyHeat);
static_assert(kSpaceWrappers[1] == sd::WrapperKind::AllocClass);
static_assert(kSpaceWrappers[2] == sd::WrapperKind::Budgeted);

// Representation axis: {TimeOrdered, Vendor, NumaPlacement}.
constexpr auto kReprWrappers = sd::wrapper_for<sd::DimensionAxis::Representation>();
static_assert(kReprWrappers.size()  == 3);
static_assert(kReprWrappers[0] == sd::WrapperKind::TimeOrdered);
static_assert(kReprWrappers[1] == sd::WrapperKind::Vendor);
static_assert(kReprWrappers[2] == sd::WrapperKind::NumaPlacement);

// Refinement axis: {Refined, SealedRefined}.
constexpr auto kRefinementWrappers = sd::wrapper_for<sd::DimensionAxis::Refinement>();
static_assert(kRefinementWrappers.size()  == 2);
static_assert(kRefinementWrappers[0] == sd::WrapperKind::Refined);
static_assert(kRefinementWrappers[1] == sd::WrapperKind::SealedRefined);

// Empty axis: Type has no Graded-backed wrappers.
constexpr auto kTypeWrappers = sd::wrapper_for<sd::DimensionAxis::Type>();
static_assert(kTypeWrappers.size() == 0);
static_assert(std::is_same_v<decltype(kTypeWrappers),
                             const std::array<sd::WrapperKind, 0>>);

// Singleton axis: Usage has exactly one wrapper (Linear).
constexpr auto kUsageWrappers = sd::wrapper_for<sd::DimensionAxis::Usage>();
static_assert(kUsageWrappers.size() == 1);
static_assert(kUsageWrappers[0] == sd::WrapperKind::Linear);

// ── wrapper_for_v variable template form ────────────────────────────
//
// Verifies the inline constexpr variable form composes correctly
// with structured bindings + array indexing.
static_assert(sd::wrapper_for_v<sd::DimensionAxis::Mutation>.size() == 2);
static_assert(sd::wrapper_for_v<sd::DimensionAxis::Mutation>[0]
              == sd::WrapperKind::Monotonic);
static_assert(sd::wrapper_for_v<sd::DimensionAxis::Type>.size()     == 0);

// ── fixy::dim namespace re-export ───────────────────────────────────
//
// Verifies the fixy::dim aliases name the same substrate entities.
static_assert(std::is_same_v<fd::WrapperKind, sd::WrapperKind>);
static_assert(fd::WrapperKind::Linear == sd::WrapperKind::Linear);
static_assert(fd::wrapper_kind_to_axis(fd::WrapperKind::Witness)
              == fd::DimensionAxis::Observability);
static_assert(fd::wrapper_kind_name(fd::WrapperKind::JoinPolicy)
              == std::string_view{"JoinPolicy"});
static_assert(fd::count_wrappers_on_axis(fd::DimensionAxis::Synchronization)
              == 3);

constexpr auto kFixyMutation = fd::wrapper_for<fd::DimensionAxis::Mutation>();
static_assert(kFixyMutation.size()  == 2);
static_assert(kFixyMutation[0] == fd::WrapperKind::Monotonic);
static_assert(kFixyMutation[1] == fd::WrapperKind::AppendOnly);
static_assert(fd::wrapper_for_v<fd::DimensionAxis::Mutation>[1]
              == fd::WrapperKind::AppendOnly);

}  // namespace

int main() {
    // Compile-time-only surface — the sentinel TU forces every
    // header-embedded static_assert through project warnings-as-errors
    // per feedback_header_only_static_assert_blind_spot.
    return 0;
}

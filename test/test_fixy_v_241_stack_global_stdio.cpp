#include <crucible/algebra/lattices/GlobalStateLattice.h>
#include <crucible/algebra/lattices/StackUseLattice.h>
#include <crucible/algebra/lattices/StdioLattice.h>
#include <crucible/safety/DimensionTraits.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cs = ::crucible::safety;
namespace cal = ::crucible::algebra::lattices;

namespace {

static_assert(std::to_underlying(cs::DimensionAxis::StackUse) == 26,
              "DimensionAxis::StackUse must be ordinal 26. The axis enumeration is "
              "append-only and ordinals are never reused.");
static_assert(std::to_underlying(cs::DimensionAxis::GlobalState) == 27,
              "DimensionAxis::GlobalState must be ordinal 27.");
static_assert(std::to_underlying(cs::DimensionAxis::Stdio) == 28,
              "DimensionAxis::Stdio must be ordinal 28, the topmost axis.");

static_assert(cs::tier_of_axis(cs::DimensionAxis::StackUse) == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::GlobalState) == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Stdio) == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis_v<cs::DimensionAxis::StackUse> == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis_v<cs::DimensionAxis::GlobalState> == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis_v<cs::DimensionAxis::Stdio> == cs::TierKind::Semiring);

static_assert(cs::dimension_axis_name(cs::DimensionAxis::StackUse) == std::string_view{"StackUse"});
static_assert(cs::dimension_axis_name(cs::DimensionAxis::GlobalState) == std::string_view{"GlobalState"});
static_assert(cs::dimension_axis_name(cs::DimensionAxis::Stdio) == std::string_view{"Stdio"});

// A floor, not an equality: appending a further axis must not redden this
// file. Stdio sits at ordinal 28, so removing any axis at or below it drops
// the count under 29 and is caught.
static_assert(cs::DIMENSION_AXIS_COUNT >= 29, "the DimensionAxis cardinality regressed below 29, so an axis was "
                                              "removed.");

static_assert(cal::detail::stack_use_lattice_self_test::stack_use_count == 4,
              "StackUse must have exactly 4 enumerators — ConstantFrame, "
              "BoundedByParam, BoundedDynamic, Unbounded.");
static_assert(std::is_same_v<std::underlying_type_t<cal::StackUse>, std::uint8_t>);
static_assert(std::to_underlying(cal::StackUse::ConstantFrame) == 0,
              "StackUse::ConstantFrame must be ordinal 0, the bottom of the chain "
              "and the strongest bound: a compile-time-constant frame.");
static_assert(std::to_underlying(cal::StackUse::BoundedByParam) == 1);
static_assert(std::to_underlying(cal::StackUse::BoundedDynamic) == 2);
static_assert(std::to_underlying(cal::StackUse::Unbounded) == 3,
              "StackUse::Unbounded must be ordinal 3, the top of the chain and the "
              "absence of any bound.");
static_assert(cal::StackUseLattice::bottom() == cal::StackUse::ConstantFrame);
static_assert(cal::StackUseLattice::top() == cal::StackUse::Unbounded);
static_assert(cal::StackUseLattice::leq(cal::StackUse::ConstantFrame, cal::StackUse::Unbounded));
static_assert(!cal::StackUseLattice::leq(cal::StackUse::BoundedDynamic, cal::StackUse::BoundedByParam),
              "the StackUse chain is a strict total order, so a weaker bound is not "
              "leq a stronger one.");
// Parallel composition is the join, so the weaker bound dominates.
static_assert(cal::StackUseLattice::join(cal::StackUse::BoundedByParam, cal::StackUse::Unbounded)
              == cal::StackUse::Unbounded);
static_assert(cal::StackUseLattice::meet(cal::StackUse::Unbounded, cal::StackUse::ConstantFrame)
              == cal::StackUse::ConstantFrame);
static_assert(std::is_empty_v<cal::StackUseLattice::At<cal::StackUse::ConstantFrame>::element_type>,
              "At<ConstantFrame>::element_type must be empty so the grade collapses "
              "under EBO.");
static_assert(std::is_empty_v<cal::StackUseLattice::At<cal::StackUse::Unbounded>::element_type>);
static_assert(cal::StackUseLattice::At<cal::StackUse::BoundedByParam>::tier == cal::StackUse::BoundedByParam);

static_assert(cal::detail::global_state_lattice_self_test::global_state_count == 4,
              "GlobalState must have exactly 4 enumerators — Stateless, "
              "ConstGlobal, MutableGlobal, InitOrderHazard.");
static_assert(std::is_same_v<std::underlying_type_t<cal::GlobalState>, std::uint8_t>);
static_assert(std::to_underlying(cal::GlobalState::Stateless) == 0,
              "GlobalState::Stateless must be ordinal 0, the bottom of the chain "
              "and the absence of any global interaction.");
static_assert(std::to_underlying(cal::GlobalState::ConstGlobal) == 1);
static_assert(std::to_underlying(cal::GlobalState::MutableGlobal) == 2);
static_assert(std::to_underlying(cal::GlobalState::InitOrderHazard) == 3,
              "GlobalState::InitOrderHazard must be ordinal 3, the top of the "
              "chain.");
static_assert(cal::GlobalStateLattice::bottom() == cal::GlobalState::Stateless);
static_assert(cal::GlobalStateLattice::top() == cal::GlobalState::InitOrderHazard);
static_assert(cal::GlobalStateLattice::leq(cal::GlobalState::Stateless, cal::GlobalState::InitOrderHazard));
static_assert(!cal::GlobalStateLattice::leq(cal::GlobalState::MutableGlobal, cal::GlobalState::ConstGlobal),
              "the GlobalState chain is a strict total order, so a higher-hazard "
              "tier is not leq a lower-hazard one.");
// Parallel composition is the join, so composing a const reader with a
// mutable writer yields the mutable-hazard tier.
static_assert(cal::GlobalStateLattice::join(cal::GlobalState::ConstGlobal, cal::GlobalState::InitOrderHazard)
              == cal::GlobalState::InitOrderHazard);
static_assert(cal::GlobalStateLattice::meet(cal::GlobalState::InitOrderHazard, cal::GlobalState::Stateless)
              == cal::GlobalState::Stateless);
static_assert(std::is_empty_v<cal::GlobalStateLattice::At<cal::GlobalState::Stateless>::element_type>);
static_assert(std::is_empty_v<cal::GlobalStateLattice::At<cal::GlobalState::InitOrderHazard>::element_type>);
static_assert(cal::GlobalStateLattice::At<cal::GlobalState::MutableGlobal>::tier == cal::GlobalState::MutableGlobal);

static_assert(cal::detail::stdio_lattice_self_test::stdio_count == 4,
              "Stdio must have exactly 4 enumerators — NoStdio, BufferedWrite, "
              "UnbufferedWrite, InteractiveRead.");
static_assert(std::is_same_v<std::underlying_type_t<cal::Stdio>, std::uint8_t>);
static_assert(std::to_underlying(cal::Stdio::NoStdio) == 0,
              "Stdio::NoStdio must be ordinal 0, the bottom of the chain and the "
              "only tier admissible on the hot path.");
static_assert(std::to_underlying(cal::Stdio::BufferedWrite) == 1);
static_assert(std::to_underlying(cal::Stdio::UnbufferedWrite) == 2);
static_assert(std::to_underlying(cal::Stdio::InteractiveRead) == 3,
              "Stdio::InteractiveRead must be ordinal 3, the top of the chain, "
              "where blocking on interactive input is unbounded.");
static_assert(cal::StdioLattice::bottom() == cal::Stdio::NoStdio);
static_assert(cal::StdioLattice::top() == cal::Stdio::InteractiveRead);
static_assert(cal::StdioLattice::leq(cal::Stdio::NoStdio, cal::Stdio::InteractiveRead));
static_assert(!cal::StdioLattice::leq(cal::Stdio::UnbufferedWrite, cal::Stdio::BufferedWrite),
              "the Stdio chain is a strict total order, so a more disruptive "
              "surface is not leq a less disruptive one.");
// Parallel composition is the join, so the more disruptive surface dominates.
static_assert(cal::StdioLattice::join(cal::Stdio::BufferedWrite, cal::Stdio::InteractiveRead)
              == cal::Stdio::InteractiveRead);
static_assert(cal::StdioLattice::meet(cal::Stdio::InteractiveRead, cal::Stdio::NoStdio) == cal::Stdio::NoStdio);
static_assert(std::is_empty_v<cal::StdioLattice::At<cal::Stdio::NoStdio>::element_type>);
static_assert(std::is_empty_v<cal::StdioLattice::At<cal::Stdio::InteractiveRead>::element_type>);
static_assert(cal::StdioLattice::At<cal::Stdio::UnbufferedWrite>::tier == cal::Stdio::UnbufferedWrite);

// Three distinct enum types, so a value of one axis cannot be passed into
// another axis's NTTP slot.
static_assert(!std::is_same_v<cal::StackUse, cal::GlobalState>);
static_assert(!std::is_same_v<cal::StackUse, cal::Stdio>);
static_assert(!std::is_same_v<cal::GlobalState, cal::Stdio>);

}  // namespace

// Each header's own smoke body drives leq, join and meet with non-constant
// operands, which nothing above this point does.
int main() {
    cal::detail::stack_use_lattice_self_test::stack_use_lattice_runtime_smoke_test();
    cal::detail::global_state_lattice_self_test::global_state_lattice_runtime_smoke_test();
    cal::detail::stdio_lattice_self_test::stdio_lattice_runtime_smoke_test();
    return 0;
}

// FIXY-V-241 sentinel TU: the three remaining V-238 hazard-axis
// lattices — StackUse (dim 26), GlobalState (dim 27), Stdio (dim 28).
//
// V-238 shipped the three enumerators (all Tier-S Semiring); V-241
// ships their lattices:
//   1. algebra/lattices/StackUseLattice.h    — ConstantFrame ⊏
//      BoundedByParam ⊏ BoundedDynamic ⊏ Unbounded.
//   2. algebra/lattices/GlobalStateLattice.h — Stateless ⊏ ConstGlobal
//      ⊏ MutableGlobal ⊏ InitOrderHazard.
//   3. algebra/lattices/StdioLattice.h       — NoStdio ⊏ BufferedWrite
//      ⊏ UnbufferedWrite ⊏ InteractiveRead.
//   plus AllLattices.h umbrella registration (3 includes + 3 name-pack
//   entries), so the headers are compiled and HasLatticeName-checked.
//
// V-241 ships NO value-level machinery: V-242 ships the safety/* Graded
// carriers, V-243 the §6.8 collision rules (incl. S004 init-cycle),
// V-246 the fixy/grant/{Stack,Global,Stdio}.h grants.  This sentinel TU
// witnesses that all three VOCABULARY changes are structurally
// consistent and that each axis enumerator agrees with its lattice.

#include <crucible/algebra/lattices/GlobalStateLattice.h>
#include <crucible/algebra/lattices/StackUseLattice.h>
#include <crucible/algebra/lattices/StdioLattice.h>
#include <crucible/safety/DimensionTraits.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cs   = ::crucible::safety;
namespace cal  = ::crucible::algebra::lattices;

namespace {

// ════════════════════════════════════════════════════════════════════
// Axis-enumerator pins — ordinal / tier / name for all three axes.
// ════════════════════════════════════════════════════════════════════
static_assert(std::to_underlying(cs::DimensionAxis::StackUse)    == 26,
    "FIXY-V-241: DimensionAxis::StackUse must be ordinal 26 (third V-238 "
    "hazard axis).  Append-only discipline forbids reusing ordinals.");
static_assert(std::to_underlying(cs::DimensionAxis::GlobalState) == 27,
    "FIXY-V-241: DimensionAxis::GlobalState must be ordinal 27.");
static_assert(std::to_underlying(cs::DimensionAxis::Stdio)       == 28,
    "FIXY-V-241: DimensionAxis::Stdio must be ordinal 28 (topmost axis).");

static_assert(cs::tier_of_axis(cs::DimensionAxis::StackUse)    == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::GlobalState) == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Stdio)       == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis_v<cs::DimensionAxis::StackUse>    == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis_v<cs::DimensionAxis::GlobalState> == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis_v<cs::DimensionAxis::Stdio>       == cs::TierKind::Semiring);

static_assert(cs::dimension_axis_name(cs::DimensionAxis::StackUse)    == std::string_view{"StackUse"});
static_assert(cs::dimension_axis_name(cs::DimensionAxis::GlobalState) == std::string_view{"GlobalState"});
static_assert(cs::dimension_axis_name(cs::DimensionAxis::Stdio)       == std::string_view{"Stdio"});

// Cardinality FLOOR (exact ceiling lives in DimensionTraits.h) — catches
// removal of any V-238 hazard axis.  Stdio is the topmost (ordinal 28),
// so the count must be ≥ 29.
static_assert(cs::DIMENSION_AXIS_COUNT >= 29,
    "FIXY-V-241 floor: DimensionAxis cardinality regressed below 29 — a "
    "V-238 hazard axis was removed.");

// ════════════════════════════════════════════════════════════════════
// StackUseLattice — 4-tier stack-bound chain.
// ════════════════════════════════════════════════════════════════════
static_assert(cal::detail::stack_use_lattice_self_test::stack_use_count == 4,
    "FIXY-V-241: StackUse must have exactly 4 enumerators — ConstantFrame, "
    "BoundedByParam, BoundedDynamic, Unbounded.");
static_assert(std::is_same_v<std::underlying_type_t<cal::StackUse>, std::uint8_t>);
static_assert(std::to_underlying(cal::StackUse::ConstantFrame)  == 0,
    "FIXY-V-241: StackUse::ConstantFrame must be ordinal 0 (bottom, "
    "strongest bound = compile-time-constant frame).");
static_assert(std::to_underlying(cal::StackUse::BoundedByParam) == 1);
static_assert(std::to_underlying(cal::StackUse::BoundedDynamic) == 2);
static_assert(std::to_underlying(cal::StackUse::Unbounded)      == 3,
    "FIXY-V-241: StackUse::Unbounded must be ordinal 3 (top, no bound).");
static_assert(cal::StackUseLattice::bottom() == cal::StackUse::ConstantFrame);
static_assert(cal::StackUseLattice::top()    == cal::StackUse::Unbounded);
// Chain monotonicity + the load-bearing reverse-direction NEGATIVE.
static_assert(cal::StackUseLattice::leq(cal::StackUse::ConstantFrame, cal::StackUse::Unbounded));
static_assert(!cal::StackUseLattice::leq(cal::StackUse::BoundedDynamic, cal::StackUse::BoundedByParam),
    "FIXY-V-241: StackUse chain is a strict total order — a weaker bound "
    "is NOT leq a stronger one.");
// par=join (weaker bound dominates) — Forge derives the LUB stack bound.
static_assert(cal::StackUseLattice::join(cal::StackUse::BoundedByParam, cal::StackUse::Unbounded)
              == cal::StackUse::Unbounded);
static_assert(cal::StackUseLattice::meet(cal::StackUse::Unbounded, cal::StackUse::ConstantFrame)
              == cal::StackUse::ConstantFrame);
static_assert(std::is_empty_v<cal::StackUseLattice::At<cal::StackUse::ConstantFrame>::element_type>,
    "FIXY-V-241: At<ConstantFrame>::element_type must be empty for EBO collapse.");
static_assert(std::is_empty_v<cal::StackUseLattice::At<cal::StackUse::Unbounded>::element_type>);
static_assert(cal::StackUseLattice::At<cal::StackUse::BoundedByParam>::tier == cal::StackUse::BoundedByParam);

// ════════════════════════════════════════════════════════════════════
// GlobalStateLattice — 4-tier global-state-hazard chain.
// ════════════════════════════════════════════════════════════════════
static_assert(cal::detail::global_state_lattice_self_test::global_state_count == 4,
    "FIXY-V-241: GlobalState must have exactly 4 enumerators — Stateless, "
    "ConstGlobal, MutableGlobal, InitOrderHazard.");
static_assert(std::is_same_v<std::underlying_type_t<cal::GlobalState>, std::uint8_t>);
static_assert(std::to_underlying(cal::GlobalState::Stateless)       == 0,
    "FIXY-V-241: GlobalState::Stateless must be ordinal 0 (bottom, no "
    "global interaction).");
static_assert(std::to_underlying(cal::GlobalState::ConstGlobal)     == 1);
static_assert(std::to_underlying(cal::GlobalState::MutableGlobal)   == 2);
static_assert(std::to_underlying(cal::GlobalState::InitOrderHazard) == 3,
    "FIXY-V-241: GlobalState::InitOrderHazard must be ordinal 3 (top, "
    "what V-248's S004 Meyers-singleton init-cycle detector keys on).");
static_assert(cal::GlobalStateLattice::bottom() == cal::GlobalState::Stateless);
static_assert(cal::GlobalStateLattice::top()    == cal::GlobalState::InitOrderHazard);
static_assert(cal::GlobalStateLattice::leq(cal::GlobalState::Stateless, cal::GlobalState::InitOrderHazard));
static_assert(!cal::GlobalStateLattice::leq(cal::GlobalState::MutableGlobal, cal::GlobalState::ConstGlobal),
    "FIXY-V-241: GlobalState chain is a strict total order — a higher-"
    "hazard tier is NOT leq a lower-hazard one.");
// par=join (higher hazard dominates) — composing a const-reader with a
// mutable-writer yields the mutable-hazard tier.
static_assert(cal::GlobalStateLattice::join(cal::GlobalState::ConstGlobal, cal::GlobalState::InitOrderHazard)
              == cal::GlobalState::InitOrderHazard);
static_assert(cal::GlobalStateLattice::meet(cal::GlobalState::InitOrderHazard, cal::GlobalState::Stateless)
              == cal::GlobalState::Stateless);
static_assert(std::is_empty_v<cal::GlobalStateLattice::At<cal::GlobalState::Stateless>::element_type>);
static_assert(std::is_empty_v<cal::GlobalStateLattice::At<cal::GlobalState::InitOrderHazard>::element_type>);
static_assert(cal::GlobalStateLattice::At<cal::GlobalState::MutableGlobal>::tier == cal::GlobalState::MutableGlobal);

// ════════════════════════════════════════════════════════════════════
// StdioLattice — 4-tier stdio-surface chain.
// ════════════════════════════════════════════════════════════════════
static_assert(cal::detail::stdio_lattice_self_test::stdio_count == 4,
    "FIXY-V-241: Stdio must have exactly 4 enumerators — NoStdio, "
    "BufferedWrite, UnbufferedWrite, InteractiveRead.");
static_assert(std::is_same_v<std::underlying_type_t<cal::Stdio>, std::uint8_t>);
static_assert(std::to_underlying(cal::Stdio::NoStdio)         == 0,
    "FIXY-V-241: Stdio::NoStdio must be ordinal 0 (bottom) — the only "
    "tier admissible on the hot path (CLAUDE.md §XII).");
static_assert(std::to_underlying(cal::Stdio::BufferedWrite)   == 1);
static_assert(std::to_underlying(cal::Stdio::UnbufferedWrite) == 2);
static_assert(std::to_underlying(cal::Stdio::InteractiveRead) == 3,
    "FIXY-V-241: Stdio::InteractiveRead must be ordinal 3 (top, unbounded "
    "blocking on interactive input).");
static_assert(cal::StdioLattice::bottom() == cal::Stdio::NoStdio);
static_assert(cal::StdioLattice::top()    == cal::Stdio::InteractiveRead);
static_assert(cal::StdioLattice::leq(cal::Stdio::NoStdio, cal::Stdio::InteractiveRead));
static_assert(!cal::StdioLattice::leq(cal::Stdio::UnbufferedWrite, cal::Stdio::BufferedWrite),
    "FIXY-V-241: Stdio chain is a strict total order — a more-disruptive "
    "surface is NOT leq a less-disruptive one.");
// par=join (more-disruptive dominates).
static_assert(cal::StdioLattice::join(cal::Stdio::BufferedWrite, cal::Stdio::InteractiveRead)
              == cal::Stdio::InteractiveRead);
static_assert(cal::StdioLattice::meet(cal::Stdio::InteractiveRead, cal::Stdio::NoStdio)
              == cal::Stdio::NoStdio);
static_assert(std::is_empty_v<cal::StdioLattice::At<cal::Stdio::NoStdio>::element_type>);
static_assert(std::is_empty_v<cal::StdioLattice::At<cal::Stdio::InteractiveRead>::element_type>);
static_assert(cal::StdioLattice::At<cal::Stdio::UnbufferedWrite>::tier == cal::Stdio::UnbufferedWrite);

// ── Cross-axis distinctness — the three enums are independent types ──
//
// A binding that engages StackUse must NOT accidentally satisfy a
// GlobalState or Stdio NTTP slot (strong-enum discipline).
static_assert(!std::is_same_v<cal::StackUse, cal::GlobalState>);
static_assert(!std::is_same_v<cal::StackUse, cal::Stdio>);
static_assert(!std::is_same_v<cal::GlobalState, cal::Stdio>);

// ── Runtime smoke — exercises non-constant operands for all three. ──
// (Each header's runtime smoke fires leq/join/meet on non-constexpr
// operands + the At<T>::element_type round-trip; invoked from main().)

}  // namespace

int main() {
    cal::detail::stack_use_lattice_self_test::stack_use_lattice_runtime_smoke_test();
    cal::detail::global_state_lattice_self_test::global_state_lattice_runtime_smoke_test();
    cal::detail::stdio_lattice_self_test::stdio_lattice_runtime_smoke_test();
    return 0;
}

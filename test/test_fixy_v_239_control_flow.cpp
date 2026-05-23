// FIXY-V-239 sentinel TU: DimensionAxis::ControlFlow (V-238) +
// ControlFlowLattice.h scaffolding (V-239).
//
// V-238 shipped the enumerator (ControlFlow = 24, Tier-S Semiring);
// V-239 ships:
//   1. algebra/lattices/ControlFlowLattice.h — 5-element control-flow-
//      escape chain (Pure ⊏ AbortOnly ⊏ ThrowOnly ⊏ MayLongjmp ⊏
//      MaySignal) + ChainLatticeOps + At<T> singleton + reflection-
//      driven cardinality/name-coverage self-test + exhaustive
//      lattice-axiom verification.
//   2. AllLattices.h umbrella registration (include + name-coverage
//      pack), so the header is compiled and HasLatticeName-checked.
//
// V-239 ships NO value-level machinery: V-242 ships safety/ControlFlow.h
// (the Graded<Absolute, At<T>, P> carrier), V-243 ships the
// CollisionCatalog cross-axis rules, V-244 (fixy/grant/Ctrl.h) ships
// the throws / abort / longjmp / exit / coroutine grants.  This sentinel
// TU witnesses that the VOCABULARY change is structurally consistent and
// that the axis enumerator and lattice agree.
//
// Why a dedicated ControlFlow axis (not folded onto Effect or the
// -fno-exceptions build posture):
//   The Met(X) effect row + "exceptions are off" collapse five
//   structurally distinct non-local escapes (normal-return, abort,
//   throw-with-unwind, longjmp-without-unwind, async-signal) into one
//   bit.  Forge phase E.RecipeSelect admits to the hot path only if the
//   escape capability ⊑ Pure; Warden's signal-handler audit needs
//   ⊑ MaySignal + async-signal-safety; permission_fork must reject a
//   may-throw / may-longjmp child body; Cipher's emergency_flush on the
//   abort path must be ⊑ AbortOnly.  None of those gates can be
//   expressed on a single effect bit.

#include <crucible/algebra/lattices/ControlFlowLattice.h>
#include <crucible/safety/DimensionTraits.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cs   = ::crucible::safety;
namespace cal  = ::crucible::algebra::lattices;

namespace {

// ── DimensionAxis::ControlFlow is the 25th axis (ordinal 24) ─────────
static_assert(std::to_underlying(cs::DimensionAxis::ControlFlow) == 24,
    "FIXY-V-239: DimensionAxis::ControlFlow must be ordinal 24 (the "
    "first of the five V-238 hazard axes).  Append-only discipline "
    "forbids reusing earlier ordinals — every Universe extension goes "
    "at the next free slot.");

// ── Tier classification — ControlFlow is Tier-S (Semiring) ──────────
static_assert(cs::tier_of_axis(cs::DimensionAxis::ControlFlow)
              == cs::TierKind::Semiring,
    "FIXY-V-239: ControlFlow lives on Tier-S (Semiring) — par=join "
    "(strictest-wins / wider-escape-dominates), peer to SyscallSurface "
    "(V-097) and FpMode (V-088).  Misclassification breaks Forge phase "
    "E.RecipeSelect's hot-path escape-capability admission gate.");
static_assert(cs::tier_of_axis_v<cs::DimensionAxis::ControlFlow>
              == cs::TierKind::Semiring,
    "FIXY-V-239: variable-template form of tier_of_axis must agree.");

// ── Name surface — non-sentinel, non-empty name ─────────────────────
static_assert(cs::dimension_axis_name(cs::DimensionAxis::ControlFlow)
              == std::string_view{"ControlFlow"},
    "FIXY-V-239: dimension_axis_name must return \"ControlFlow\" for the "
    "axis; a sentinel leak indicates a missing switch arm.");

// ── Catalog cardinality floor — V-238 grew the dim count to 29 ──────
//
// The EXACT ceiling pin lives in safety/DimensionTraits.h colocated
// with the source-of-truth enum; THIS TU holds the FLOOR pin which
// catches the inverse direction — an accidental REMOVAL of a
// DimensionAxis enumerator at or after ControlFlow.
static_assert(cs::DIMENSION_AXIS_COUNT >= 29,
    "FIXY-V-239 floor: DimensionAxis cardinality regressed below 29 — a "
    "V-238 hazard axis (ControlFlow / CallShape / StackUse / GlobalState "
    "/ Stdio) was removed without updating both DimensionTraits.h's "
    "colocated ceiling pin AND this floor witness.");

// ── Tier preservation — adding ControlFlow did not perturb peers ────
//
// Bug-class catch: an accidental tier re-classification of a neighbor
// would survive a cardinality-only check.  Re-witness representative
// axes across every tier.
static_assert(cs::tier_of_axis(cs::DimensionAxis::Type)
              == cs::TierKind::Foundational);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Protocol)
              == cs::TierKind::Typestate);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Representation)
              == cs::TierKind::Lattice);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Version)
              == cs::TierKind::Versioned);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Synchronization)
              == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::FpMode)
              == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::SyscallSurface)
              == cs::TierKind::Semiring);

// ── ControlFlowLattice.h — 5-element escape chain ───────────────────
//
// V-239 ships the enum + ChainLatticeOps + At<T> singletons +
// cardinality + name-coverage + lattice-axiom self-tests.  V-242+
// wrap it as a value-level Graded carrier and route grants onto it.
static_assert(cal::detail::control_flow_lattice_self_test::control_flow_count
              == 5,
    "FIXY-V-239: ControlFlow must have exactly 5 enumerators — Pure, "
    "AbortOnly, ThrowOnly, MayLongjmp, MaySignal.  Adding a new tier "
    "requires bumping control_flow_count + appending an At<T> "
    "specialization at the END of the lattice.");

// ── Underlying type is uint8_t ──────────────────────────────────────
static_assert(std::is_same_v<std::underlying_type_t<cal::ControlFlow>,
                             std::uint8_t>,
    "FIXY-V-239: ControlFlow must use uint8_t underlying type — "
    "ChainLatticeOps<E>::leq derives via std::to_underlying, and "
    "bit-width pinning lets a future effect-row bridge derive indices "
    "without zero-extending.");

// ── Subset-inclusion ordinal convention ─────────────────────────────
//
// ControlFlow uses capability-superset order: Pure has the smallest
// escape set (always returns normally), MaySignal the largest (also
// raises async signals).  bottom() = Pure = 0; top() = MaySignal = 4.
static_assert(std::to_underlying(cal::ControlFlow::Pure)       == 0,
    "FIXY-V-239: ControlFlow::Pure must be ordinal 0 (bottom). "
    "ChainLatticeOps derives bottom() mechanically from the lowest "
    "enumerator; reordering would break every binding that defaults to "
    "Pure through strict_default_for<ControlFlow> (V-242+).");
static_assert(std::to_underlying(cal::ControlFlow::AbortOnly)  == 1);
static_assert(std::to_underlying(cal::ControlFlow::ThrowOnly)  == 2);
static_assert(std::to_underlying(cal::ControlFlow::MayLongjmp) == 3);
static_assert(std::to_underlying(cal::ControlFlow::MaySignal)  == 4,
    "FIXY-V-239: ControlFlow::MaySignal must be ordinal 4 (top). "
    "ChainLatticeOps derives top() from the highest enumerator; "
    "MaySignal subsumes every synchronous escape (abort, throw, "
    "longjmp) and adds asynchronous signal delivery.");

// ── Lattice axioms — bottom, top, ordering ──────────────────────────
//
// ControlFlowLattice is a finite chain (a fortiori a distributive
// lattice).  Pin the identity axioms; the exhaustive lattice-axiom
// self-test inside ControlFlowLattice.h verifies meet/join/leq for
// every triple.
static_assert(cal::ControlFlowLattice::bottom() == cal::ControlFlow::Pure);
static_assert(cal::ControlFlowLattice::top()    == cal::ControlFlow::MaySignal);
static_assert(cal::ControlFlowLattice::leq(cal::ControlFlow::Pure,
                                           cal::ControlFlow::MaySignal),
    "FIXY-V-239: bottom must be leq top — the chain ordering is "
    "structurally violated if this fails.");

// ── Per-element At<T> singleton pattern witness ─────────────────────
//
// The At<T> alias lets per-binding sites pin a ControlFlow tier at the
// type level (e.g. `Graded<Absolute, ControlFlowLattice::At<ControlFlow
// ::Pure>, T>` for a hot-path-admissible binding).  V-239 pins the
// contract that every At<T>::element_type is EMPTY so Graded<..., At<T>,
// P> EBO-collapses to sizeof(P) downstream.  Witness bottom, mid, top.
static_assert(std::is_empty_v<
    cal::ControlFlowLattice::At<cal::ControlFlow::Pure>::element_type>,
    "FIXY-V-239: At<Pure>::element_type must be an empty struct so "
    "Graded<Absolute, At<Pure>, P> EBO-collapses to sizeof(P) at every "
    "binding site — zero-byte control-flow annotation.");
static_assert(std::is_empty_v<
    cal::ControlFlowLattice::At<cal::ControlFlow::ThrowOnly>::element_type>);
static_assert(std::is_empty_v<
    cal::ControlFlowLattice::At<cal::ControlFlow::MaySignal>::element_type>);

// Singleton `tier` constant pins the enum value at the type level —
// what V-242+ wrappers key on for compile-time admission decisions.
static_assert(cal::ControlFlowLattice::At<cal::ControlFlow::AbortOnly>::tier
              == cal::ControlFlow::AbortOnly,
    "FIXY-V-239: At<T>::tier must equal T at the type level so "
    "downstream wrappers can read the escape tier without runtime data.");

// ── Chain monotonicity spot-checks ──────────────────────────────────
//
// Verify the canonical "no-escape < abort < throw < longjmp < signal"
// ordering matches capability-superset intuition.  These pins protect
// against accidental enumerator reordering during merges.
static_assert(cal::ControlFlowLattice::leq(cal::ControlFlow::Pure,
                                           cal::ControlFlow::AbortOnly));
static_assert(cal::ControlFlowLattice::leq(cal::ControlFlow::AbortOnly,
                                           cal::ControlFlow::ThrowOnly));
static_assert(cal::ControlFlowLattice::leq(cal::ControlFlow::ThrowOnly,
                                           cal::ControlFlow::MayLongjmp));
static_assert(cal::ControlFlowLattice::leq(cal::ControlFlow::MayLongjmp,
                                           cal::ControlFlow::MaySignal));

// Inverse direction: ThrowOnly is NOT leq AbortOnly (chain is strictly
// ordered, not a partial order with incomparable peers) — the
// load-bearing NEGATIVE assertion for a total-order chain.
static_assert(!cal::ControlFlowLattice::leq(cal::ControlFlow::ThrowOnly,
                                            cal::ControlFlow::AbortOnly));
static_assert(!cal::ControlFlowLattice::leq(cal::ControlFlow::MaySignal,
                                            cal::ControlFlow::Pure));

// ── Join semantics — par=join (wider-escape-dominates) ──────────────
//
// Two composed sites take the LUB of their escape capability: a binding
// that may throw + a binding that may longjmp composes to MayLongjmp
// (the wider escape).  This pins the Tier-S par=join discipline that
// permission_fork's body gate and Forge's hot-path admission depend on.
static_assert(cal::ControlFlowLattice::join(cal::ControlFlow::ThrowOnly,
                                            cal::ControlFlow::MayLongjmp)
              == cal::ControlFlow::MayLongjmp,
    "FIXY-V-239: par=join (wider-escape-dominates) — composing a "
    "narrower + wider escape at a site yields the wider.  permission_"
    "fork's body gate depends on this so a forked region's declared "
    "ControlFlow is the LUB of its constituents.");
// Pure is the join identity (composing with a pure site never widens).
static_assert(cal::ControlFlowLattice::join(cal::ControlFlow::Pure,
                                            cal::ControlFlow::AbortOnly)
              == cal::ControlFlow::AbortOnly);

// ── Meet semantics — and=meet (tighter-floor) ───────────────────────
//
// At an admission gate (e.g. Forge hot-path RecipeSelect), the
// admitted-tier meet asks "what's the tightest both sides will admit"
// — and=meet returns the floor.
static_assert(cal::ControlFlowLattice::meet(cal::ControlFlow::MaySignal,
                                            cal::ControlFlow::AbortOnly)
              == cal::ControlFlow::AbortOnly,
    "FIXY-V-239: and=meet (tighter-floor) — meeting a tight admission "
    "policy with a loose binding yields the tight floor.");

// ── Runtime smoke test — non-constant lattice ops ───────────────────
//
// ControlFlowLattice.h's runtime smoke fires leq/join/meet on
// non-constexpr operands + the At<T>::element_type round-trip.  The
// static_assert chain above pins the inductive base; runtime closes the
// loop for operands the optimizer cannot fold.
//
// (Header's `inline void control_flow_lattice_runtime_smoke_test()` is
// invoked from main(); a regression there fails the TU at runtime.)

}  // namespace

int main() {
    cal::detail::control_flow_lattice_self_test
        ::control_flow_lattice_runtime_smoke_test();
    return 0;
}

// FIXY-V-240 sentinel TU: DimensionAxis::CallShape (V-238) +
// CallShapeLattice.h scaffolding (V-240).
//
// V-238 shipped the enumerator (CallShape = 25, Tier-S Semiring);
// V-240 ships:
//   1. algebra/lattices/CallShapeLattice.h — 5-element dispatch-shape
//      chain (Direct ⊏ BoundedRecurses ⊏ Indirect ⊏ Virtual ⊏
//      Unbounded) + ChainLatticeOps + At<T> singleton + reflection-
//      driven cardinality/name-coverage self-test + exhaustive
//      lattice-axiom verification.
//   2. AllLattices.h umbrella registration (include + name-coverage
//      pack), so the header is compiled and HasLatticeName-checked.
//
// V-240 ships NO value-level machinery: V-242 ships safety/CallShape.h
// (the Graded<Absolute, At<T>, P> carrier that also carries the
// BoundedRecurses bound N as orthogonal metadata), V-243 ships the
// CollisionCatalog D001/D002 dispatch rules, V-245 (fixy/grant/
// Dispatch.h) ships indirect_call / virtual_call / recurses<N> /
// tail_call grants.  This sentinel TU witnesses that the VOCABULARY
// change is structurally consistent and that the axis enumerator and
// lattice agree.
//
// Why a dedicated CallShape axis (not folded onto Effect):
//   The Met(X) effect row records memory effects {Alloc, IO, Block,
//   Bg, Init, Test} — it has NO notion of dispatch shape.  Whether a
//   function resolves every call statically (inlinable, hot-path-safe)
//   or jumps through a vtable (two dependent loads, no devirt) is
//   invisible to the effect row.  Forge phase E.RecipeSelect admits to
//   the hot path only if call_shape ⊑ Direct; the StackUse axis
//   (V-241) derives a finite stack bound only when the shape is
//   BoundedRecurses; Warden rejects an Unbounded shape on a deadline
//   path.  None of those gates can be expressed without a dedicated
//   dispatch-shape axis.
//
// On `<N>`: the BoundedRecurses recursion bound is orthogonal metadata
// (V-242 wrapper / V-245 recurses<N> grant), NOT a chain enum tier —
// ChainLatticeOps requires a plain scoped enum, and at call-shape
// granularity every bounded recursion is categorically more analyzable
// than any indirect call regardless of N.

#include <crucible/algebra/lattices/CallShapeLattice.h>
#include <crucible/safety/DimensionTraits.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cs   = ::crucible::safety;
namespace cal  = ::crucible::algebra::lattices;

namespace {

// ── DimensionAxis::CallShape is the 26th axis (ordinal 25) ──────────
static_assert(std::to_underlying(cs::DimensionAxis::CallShape) == 25,
    "FIXY-V-240: DimensionAxis::CallShape must be ordinal 25 (the "
    "second of the five V-238 hazard axes, following ControlFlow=24). "
    "Append-only discipline forbids reusing earlier ordinals — every "
    "Universe extension goes at the next free slot.");

// ── Tier classification — CallShape is Tier-S (Semiring) ────────────
static_assert(cs::tier_of_axis(cs::DimensionAxis::CallShape)
              == cs::TierKind::Semiring,
    "FIXY-V-240: CallShape lives on Tier-S (Semiring) — par=join "
    "(strictest-wins / less-analyzable-shape-dominates), peer to "
    "ControlFlow (V-239), SyscallSurface (V-097) and FpMode (V-088). "
    "Misclassification breaks Forge phase E.RecipeSelect's hot-path "
    "call-shape admission gate.");
static_assert(cs::tier_of_axis_v<cs::DimensionAxis::CallShape>
              == cs::TierKind::Semiring,
    "FIXY-V-240: variable-template form of tier_of_axis must agree.");

// ── Name surface — non-sentinel, non-empty name ─────────────────────
static_assert(cs::dimension_axis_name(cs::DimensionAxis::CallShape)
              == std::string_view{"CallShape"},
    "FIXY-V-240: dimension_axis_name must return \"CallShape\" for the "
    "axis; a sentinel leak indicates a missing switch arm.");

// ── Catalog cardinality floor — V-238 grew the dim count to 29 ──────
//
// The EXACT ceiling pin lives in safety/DimensionTraits.h colocated
// with the source-of-truth enum; THIS TU holds the FLOOR pin which
// catches the inverse direction — an accidental REMOVAL of a
// DimensionAxis enumerator at or after CallShape.
static_assert(cs::DIMENSION_AXIS_COUNT >= 29,
    "FIXY-V-240 floor: DimensionAxis cardinality regressed below 29 — a "
    "V-238 hazard axis (ControlFlow / CallShape / StackUse / GlobalState "
    "/ Stdio) was removed without updating both DimensionTraits.h's "
    "colocated ceiling pin AND this floor witness.");

// ── Tier preservation — adding CallShape did not perturb peers ──────
//
// Bug-class catch: an accidental tier re-classification of a neighbor
// would survive a cardinality-only check.  Re-witness representative
// axes across every tier, including the sibling ControlFlow axis.
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
static_assert(cs::tier_of_axis(cs::DimensionAxis::SyscallSurface)
              == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::ControlFlow)
              == cs::TierKind::Semiring);

// ── CallShapeLattice.h — 5-element dispatch chain ───────────────────
//
// V-240 ships the enum + ChainLatticeOps + At<T> singletons +
// cardinality + name-coverage + lattice-axiom self-tests.  V-242+
// wrap it as a value-level Graded carrier and route grants onto it.
static_assert(cal::detail::call_shape_lattice_self_test::call_shape_count
              == 5,
    "FIXY-V-240: CallShape must have exactly 5 enumerators — Direct, "
    "BoundedRecurses, Indirect, Virtual, Unbounded.  Adding a new tier "
    "requires bumping call_shape_count + appending an At<T> "
    "specialization at the END of the lattice.");

// ── Underlying type is uint8_t ──────────────────────────────────────
static_assert(std::is_same_v<std::underlying_type_t<cal::CallShape>,
                             std::uint8_t>,
    "FIXY-V-240: CallShape must use uint8_t underlying type — "
    "ChainLatticeOps<E>::leq derives via std::to_underlying, and "
    "bit-width pinning lets a future effect-row bridge derive indices "
    "without zero-extending.");

// ── Subset-inclusion ordinal convention ─────────────────────────────
//
// CallShape uses analyzability-decreasing order: Direct has the
// smallest shape set (every call statically resolved), Unbounded the
// largest (neither depth nor target statically bounded).  bottom() =
// Direct = 0; top() = Unbounded = 4.
static_assert(std::to_underlying(cal::CallShape::Direct)          == 0,
    "FIXY-V-240: CallShape::Direct must be ordinal 0 (bottom). "
    "ChainLatticeOps derives bottom() mechanically from the lowest "
    "enumerator; reordering would break every binding that defaults to "
    "Direct through strict_default_for<CallShape> (V-242+).");
static_assert(std::to_underlying(cal::CallShape::BoundedRecurses) == 1);
static_assert(std::to_underlying(cal::CallShape::Indirect)        == 2);
static_assert(std::to_underlying(cal::CallShape::Virtual)         == 3);
static_assert(std::to_underlying(cal::CallShape::Unbounded)       == 4,
    "FIXY-V-240: CallShape::Unbounded must be ordinal 4 (top). "
    "ChainLatticeOps derives top() from the highest enumerator; "
    "Unbounded subsumes every analyzable shape (direct, bounded "
    "recursion, indirect, virtual) and adds unbounded depth/target.");

// ── Lattice axioms — bottom, top, ordering ──────────────────────────
//
// CallShapeLattice is a finite chain (a fortiori a distributive
// lattice).  Pin the identity axioms; the exhaustive lattice-axiom
// self-test inside CallShapeLattice.h verifies meet/join/leq for every
// triple.
static_assert(cal::CallShapeLattice::bottom() == cal::CallShape::Direct);
static_assert(cal::CallShapeLattice::top()    == cal::CallShape::Unbounded);
static_assert(cal::CallShapeLattice::leq(cal::CallShape::Direct,
                                         cal::CallShape::Unbounded),
    "FIXY-V-240: bottom must be leq top — the chain ordering is "
    "structurally violated if this fails.");

// ── Per-element At<T> singleton pattern witness ─────────────────────
//
// The At<T> alias lets per-binding sites pin a CallShape tier at the
// type level (e.g. `Graded<Absolute, CallShapeLattice::At<CallShape::
// Direct>, T>` for a hot-path-admissible binding).  V-240 pins the
// contract that every At<T>::element_type is EMPTY so Graded<..., At<T>,
// P> EBO-collapses to sizeof(P) downstream.  Witness bottom, mid, top.
static_assert(std::is_empty_v<
    cal::CallShapeLattice::At<cal::CallShape::Direct>::element_type>,
    "FIXY-V-240: At<Direct>::element_type must be an empty struct so "
    "Graded<Absolute, At<Direct>, P> EBO-collapses to sizeof(P) at every "
    "binding site — zero-byte call-shape annotation.");
static_assert(std::is_empty_v<
    cal::CallShapeLattice::At<cal::CallShape::Indirect>::element_type>);
static_assert(std::is_empty_v<
    cal::CallShapeLattice::At<cal::CallShape::Unbounded>::element_type>);

// Singleton `tier` constant pins the enum value at the type level —
// what V-242+ wrappers key on for compile-time admission decisions.
static_assert(cal::CallShapeLattice::At<cal::CallShape::Virtual>::tier
              == cal::CallShape::Virtual,
    "FIXY-V-240: At<T>::tier must equal T at the type level so "
    "downstream wrappers can read the dispatch shape without runtime "
    "data.");

// ── Chain monotonicity spot-checks ──────────────────────────────────
//
// Verify the canonical "direct < bounded-recursion < indirect < virtual
// < unbounded" ordering matches analyzability-decreasing intuition.
// These pins protect against accidental enumerator reordering during
// merges.
static_assert(cal::CallShapeLattice::leq(cal::CallShape::Direct,
                                         cal::CallShape::BoundedRecurses));
static_assert(cal::CallShapeLattice::leq(cal::CallShape::BoundedRecurses,
                                         cal::CallShape::Indirect));
static_assert(cal::CallShapeLattice::leq(cal::CallShape::Indirect,
                                         cal::CallShape::Virtual));
static_assert(cal::CallShapeLattice::leq(cal::CallShape::Virtual,
                                         cal::CallShape::Unbounded));

// Inverse direction: Indirect is NOT leq BoundedRecurses (chain is
// strictly ordered, not a partial order with incomparable peers) — the
// load-bearing NEGATIVE assertion for a total-order chain.
static_assert(!cal::CallShapeLattice::leq(cal::CallShape::Indirect,
                                          cal::CallShape::BoundedRecurses));
static_assert(!cal::CallShapeLattice::leq(cal::CallShape::Unbounded,
                                          cal::CallShape::Direct));

// ── Join semantics — par=join (less-analyzable shape dominates) ─────
//
// Two composed sites take the LUB of their call shapes: an indirect
// site + a virtual site composes to Virtual (the wider / less-
// analyzable shape).  This pins the Tier-S par=join discipline that
// Forge's hot-path admission and permission_fork's body gate depend on.
static_assert(cal::CallShapeLattice::join(cal::CallShape::Indirect,
                                          cal::CallShape::Virtual)
              == cal::CallShape::Virtual,
    "FIXY-V-240: par=join (less-analyzable-shape-dominates) — composing "
    "a more-analyzable + less-analyzable shape at a site yields the "
    "less-analyzable.  Forge hot-path admission depends on this so a "
    "region's declared CallShape is the LUB of its constituents.");
// Direct is the join identity (composing with a fully-static site never
// widens the shape).
static_assert(cal::CallShapeLattice::join(cal::CallShape::Direct,
                                          cal::CallShape::BoundedRecurses)
              == cal::CallShape::BoundedRecurses);

// ── Meet semantics — and=meet (more-analyzable floor) ───────────────
//
// At an admission gate (e.g. Forge hot-path RecipeSelect), the
// admitted-shape meet asks "what's the most-static shape both sides will
// admit" — and=meet returns the floor.
static_assert(cal::CallShapeLattice::meet(cal::CallShape::Unbounded,
                                          cal::CallShape::BoundedRecurses)
              == cal::CallShape::BoundedRecurses,
    "FIXY-V-240: and=meet (more-analyzable-floor) — meeting a tight "
    "admission policy with a loose binding yields the tight floor.");

// ── Runtime smoke test — non-constant lattice ops ───────────────────
//
// CallShapeLattice.h's runtime smoke fires leq/join/meet on
// non-constexpr operands + the At<T>::element_type round-trip.  The
// static_assert chain above pins the inductive base; runtime closes the
// loop for operands the optimizer cannot fold.
//
// (Header's `inline void call_shape_lattice_runtime_smoke_test()` is
// invoked from main(); a regression there fails the TU at runtime.)

}  // namespace

int main() {
    cal::detail::call_shape_lattice_self_test
        ::call_shape_lattice_runtime_smoke_test();
    return 0;
}

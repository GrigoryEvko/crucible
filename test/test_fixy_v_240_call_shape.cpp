// The dispatch shape is a separate axis rather than a refinement of
// the effect row.  An effect row records memory effects and has no
// notion of dispatch at all.  Whether a function resolves every call
// statically or jumps through a vtable is invisible to it, yet that is
// exactly what a hot-path admission gate, a finite stack bound and a
// deadline check each need to ask.
//
// The recursion bound that goes with BoundedRecurses is carried as
// separate metadata rather than as further chain tiers.  A chain needs
// a plain scoped enum, and at this granularity a bounded recursion is
// more analyzable than any indirect call whatever its bound, so tiers
// per bound would add no ordering.

#include <crucible/algebra/lattices/CallShapeLattice.h>
#include <crucible/safety/DimensionTraits.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cs = ::crucible::safety;
namespace cal = ::crucible::algebra::lattices;

namespace {

// Axis ordinals are append-only: an extension takes the next free
// slot, never a vacated earlier one.
static_assert(std::to_underlying(cs::DimensionAxis::CallShape) == 25,
              "CallShape must be at ordinal 25, immediately after ControlFlow.");

static_assert(cs::tier_of_axis(cs::DimensionAxis::CallShape) == cs::TierKind::Semiring,
              "CallShape must classify as Semiring, where parallel composition "
              "is join and the less analyzable shape dominates.");
static_assert(cs::tier_of_axis_v<cs::DimensionAxis::CallShape> == cs::TierKind::Semiring,
              "The variable-template form of tier_of_axis must agree with the "
              "function form.");

static_assert(cs::dimension_axis_name(cs::DimensionAxis::CallShape) == std::string_view{"CallShape"},
              "dimension_axis_name must return \"CallShape\"; a sentinel name "
              "indicates a missing switch arm.");

// This is the floor half of a floor-and-ceiling pair.  The exact
// ceiling sits beside the enum itself, so this witness only has to
// catch the inverse direction: removal of an enumerator.
static_assert(cs::DIMENSION_AXIS_COUNT >= 29, "DimensionAxis cardinality regressed below 29: one of the hazard "
                                              "axes was removed without updating both the ceiling pin and this "
                                              "floor.");

// A wrong tier arm for a neighbouring axis re-classifies it silently,
// and a cardinality-only check still passes.  Re-witnessing one axis
// per tier rules that out.
static_assert(cs::tier_of_axis(cs::DimensionAxis::Type) == cs::TierKind::Foundational);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Protocol) == cs::TierKind::Typestate);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Representation) == cs::TierKind::Lattice);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Version) == cs::TierKind::Versioned);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Synchronization) == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::SyscallSurface) == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::ControlFlow) == cs::TierKind::Semiring);

static_assert(cal::detail::call_shape_lattice_self_test::call_shape_count == 5,
              "CallShape must have exactly 5 enumerators.  A new tier raises "
              "this count and appends its At<T> specialization at the end of the "
              "lattice.");

static_assert(std::is_same_v<std::underlying_type_t<cal::CallShape>, std::uint8_t>,
              "CallShape must have uint8_t as its underlying type, so that an "
              "ordinal indexes a bridge table without zero-extension.");

// The chain is ordered by decreasing analyzability.  Direct resolves
// every call statically, and Unbounded bounds neither depth nor
// target, so Direct is the bottom and Unbounded the top.
static_assert(std::to_underlying(cal::CallShape::Direct) == 0,
              "Direct must be ordinal 0.  The lattice derives bottom() from the "
              "lowest enumerator, and a binding that declares no dispatch hazard "
              "defaults to it.");
static_assert(std::to_underlying(cal::CallShape::BoundedRecurses) == 1);
static_assert(std::to_underlying(cal::CallShape::Indirect) == 2);
static_assert(std::to_underlying(cal::CallShape::Virtual) == 3);
static_assert(std::to_underlying(cal::CallShape::Unbounded) == 4,
              "Unbounded must be ordinal 4.  The lattice derives top() from the "
              "highest enumerator, and an unbounded shape subsumes every "
              "analyzable one.");

// Only the identity axioms are pinned here.  Meet, join and leq are
// checked over every triple by the runtime smoke test below.
static_assert(cal::CallShapeLattice::bottom() == cal::CallShape::Direct);
static_assert(cal::CallShapeLattice::top() == cal::CallShape::Unbounded);
static_assert(cal::CallShapeLattice::leq(cal::CallShape::Direct, cal::CallShape::Unbounded),
              "Bottom must be leq top, or the chain ordering is broken.");

// At<T> pins a tier at the type level so a binding site carries its
// dispatch shape with no runtime data.  Emptiness is the load-bearing
// part: it is what lets the graded wrapper collapse to the size of its
// payload.  Bottom, top and one mid-chain element are witnessed.
static_assert(std::is_empty_v<cal::CallShapeLattice::At<cal::CallShape::Direct>::element_type>,
              "At<T>::element_type must be an empty struct, so that a graded "
              "wrapper over it collapses to the size of its payload.");
static_assert(std::is_empty_v<cal::CallShapeLattice::At<cal::CallShape::Indirect>::element_type>);
static_assert(std::is_empty_v<cal::CallShapeLattice::At<cal::CallShape::Unbounded>::element_type>);

static_assert(cal::CallShapeLattice::At<cal::CallShape::Virtual>::tier == cal::CallShape::Virtual,
              "At<T>::tier must equal T, so a wrapper can read the dispatch "
              "shape without runtime data.");

// The direct, bounded-recursion, indirect, virtual and unbounded
// ordering is the one a reader would expect from decreasing
// analyzability.  Pinning it catches an enumerator reordering during a
// merge.
static_assert(cal::CallShapeLattice::leq(cal::CallShape::Direct, cal::CallShape::BoundedRecurses));
static_assert(cal::CallShapeLattice::leq(cal::CallShape::BoundedRecurses, cal::CallShape::Indirect));
static_assert(cal::CallShapeLattice::leq(cal::CallShape::Indirect, cal::CallShape::Virtual));
static_assert(cal::CallShapeLattice::leq(cal::CallShape::Virtual, cal::CallShape::Unbounded));

// The chain is totally ordered, so no two shapes are incomparable.
static_assert(!cal::CallShapeLattice::leq(cal::CallShape::Indirect, cal::CallShape::BoundedRecurses));
static_assert(!cal::CallShapeLattice::leq(cal::CallShape::Unbounded, cal::CallShape::Direct));

// Composing two sites takes the least upper bound of their shapes, so
// the less analyzable shape dominates.
static_assert(cal::CallShapeLattice::join(cal::CallShape::Indirect, cal::CallShape::Virtual) == cal::CallShape::Virtual,
              "Composing a more analyzable shape with a less analyzable one must "
              "yield the less analyzable, so that a region's declared shape is "
              "the least upper bound of its constituents.");
// Direct is the join identity: a fully static site never widens.
static_assert(cal::CallShapeLattice::join(cal::CallShape::Direct, cal::CallShape::BoundedRecurses)
              == cal::CallShape::BoundedRecurses);

// An admission gate asks what both sides will admit, which is the
// greatest lower bound, not the upper one.
static_assert(cal::CallShapeLattice::meet(cal::CallShape::Unbounded, cal::CallShape::BoundedRecurses)
                  == cal::CallShape::BoundedRecurses,
              "Meeting a tight admission policy with a loose binding must yield "
              "the tight floor.");

// The static assertions above pin the inductive base.  The runtime
// smoke test closes the loop over operands the optimizer cannot fold.

}  // namespace

int main() {
    cal::detail::call_shape_lattice_self_test::call_shape_lattice_runtime_smoke_test();
    return 0;
}

// The escape capability is a separate axis rather than a refinement of
// the effect row or of the build's exception posture.  Together those
// two collapse five structurally distinct non-local escapes into a
// single bit: normal return, abort, throw with unwind, longjmp without
// unwind, and asynchronous signal.  A hot-path admission gate, a
// signal-handler audit, a forked-body check and an abort-path flush
// each need to distinguish among them, and none of those questions can
// be asked of one bit.

#include <crucible/algebra/lattices/ControlFlowLattice.h>
#include <crucible/safety/DimensionTraits.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cs = ::crucible::safety;
namespace cal = ::crucible::algebra::lattices;

namespace {

// Axis ordinals are append-only: an extension takes the next free
// slot, never a vacated earlier one.
static_assert(std::to_underlying(cs::DimensionAxis::ControlFlow) == 24,
              "ControlFlow must be at ordinal 24, the first of the hazard axes.");

static_assert(cs::tier_of_axis(cs::DimensionAxis::ControlFlow) == cs::TierKind::Semiring,
              "ControlFlow must classify as Semiring, where parallel composition "
              "is join and the wider escape dominates.");
static_assert(cs::tier_of_axis_v<cs::DimensionAxis::ControlFlow> == cs::TierKind::Semiring,
              "The variable-template form of tier_of_axis must agree with the "
              "function form.");

static_assert(cs::dimension_axis_name(cs::DimensionAxis::ControlFlow) == std::string_view{"ControlFlow"},
              "dimension_axis_name must return \"ControlFlow\"; a sentinel name "
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
static_assert(cs::tier_of_axis(cs::DimensionAxis::FpMode) == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::SyscallSurface) == cs::TierKind::Semiring);

static_assert(cal::detail::control_flow_lattice_self_test::control_flow_count == 5,
              "ControlFlow must have exactly 5 enumerators.  A new tier raises "
              "this count and appends its At<T> specialization at the end of the "
              "lattice.");

static_assert(std::is_same_v<std::underlying_type_t<cal::ControlFlow>, std::uint8_t>,
              "ControlFlow must have uint8_t as its underlying type, so that an "
              "ordinal indexes a bridge table without zero-extension.");

// The chain is ordered by capability superset.  Pure always returns
// normally, and MaySignal adds asynchronous delivery on top of every
// synchronous escape, so Pure is the bottom and MaySignal the top.
static_assert(std::to_underlying(cal::ControlFlow::Pure) == 0,
              "Pure must be ordinal 0.  The lattice derives bottom() from the "
              "lowest enumerator, and a binding that declares no escape defaults "
              "to it.");
static_assert(std::to_underlying(cal::ControlFlow::AbortOnly) == 1);
static_assert(std::to_underlying(cal::ControlFlow::ThrowOnly) == 2);
static_assert(std::to_underlying(cal::ControlFlow::MayLongjmp) == 3);
static_assert(std::to_underlying(cal::ControlFlow::MaySignal) == 4,
              "MaySignal must be ordinal 4.  The lattice derives top() from the "
              "highest enumerator, and asynchronous signal delivery subsumes "
              "every synchronous escape.");

// Only the identity axioms are pinned here.  Meet, join and leq are
// checked over every triple by the runtime smoke test below.
static_assert(cal::ControlFlowLattice::bottom() == cal::ControlFlow::Pure);
static_assert(cal::ControlFlowLattice::top() == cal::ControlFlow::MaySignal);
static_assert(cal::ControlFlowLattice::leq(cal::ControlFlow::Pure, cal::ControlFlow::MaySignal),
              "Bottom must be leq top, or the chain ordering is broken.");

// At<T> pins a tier at the type level so a binding site carries its
// escape capability with no runtime data.  Emptiness is the
// load-bearing part: it is what lets the graded wrapper collapse to
// the size of its payload.  Bottom, top and one mid-chain element are
// witnessed.
static_assert(std::is_empty_v<cal::ControlFlowLattice::At<cal::ControlFlow::Pure>::element_type>,
              "At<T>::element_type must be an empty struct, so that a graded "
              "wrapper over it collapses to the size of its payload.");
static_assert(std::is_empty_v<cal::ControlFlowLattice::At<cal::ControlFlow::ThrowOnly>::element_type>);
static_assert(std::is_empty_v<cal::ControlFlowLattice::At<cal::ControlFlow::MaySignal>::element_type>);

static_assert(cal::ControlFlowLattice::At<cal::ControlFlow::AbortOnly>::tier == cal::ControlFlow::AbortOnly,
              "At<T>::tier must equal T, so a wrapper can read the escape tier "
              "without runtime data.");

// The no-escape, abort, throw, longjmp and signal ordering is the one
// a reader would expect from capability superset.  Pinning it catches
// an enumerator reordering during a merge.
static_assert(cal::ControlFlowLattice::leq(cal::ControlFlow::Pure, cal::ControlFlow::AbortOnly));
static_assert(cal::ControlFlowLattice::leq(cal::ControlFlow::AbortOnly, cal::ControlFlow::ThrowOnly));
static_assert(cal::ControlFlowLattice::leq(cal::ControlFlow::ThrowOnly, cal::ControlFlow::MayLongjmp));
static_assert(cal::ControlFlowLattice::leq(cal::ControlFlow::MayLongjmp, cal::ControlFlow::MaySignal));

// The chain is totally ordered, so no two escapes are incomparable.
static_assert(!cal::ControlFlowLattice::leq(cal::ControlFlow::ThrowOnly, cal::ControlFlow::AbortOnly));
static_assert(!cal::ControlFlowLattice::leq(cal::ControlFlow::MaySignal, cal::ControlFlow::Pure));

// Composing two sites takes the least upper bound of their escape
// capabilities, so the wider escape dominates.
static_assert(cal::ControlFlowLattice::join(cal::ControlFlow::ThrowOnly, cal::ControlFlow::MayLongjmp)
                  == cal::ControlFlow::MayLongjmp,
              "Composing a narrower escape with a wider one must yield the "
              "wider, so that a forked region's declared escape is the least "
              "upper bound of its constituents.");
// Pure is the join identity: a pure site never widens.
static_assert(cal::ControlFlowLattice::join(cal::ControlFlow::Pure, cal::ControlFlow::AbortOnly)
              == cal::ControlFlow::AbortOnly);

// An admission gate asks what both sides will admit, which is the
// greatest lower bound, not the upper one.
static_assert(cal::ControlFlowLattice::meet(cal::ControlFlow::MaySignal, cal::ControlFlow::AbortOnly)
                  == cal::ControlFlow::AbortOnly,
              "Meeting a tight admission policy with a loose binding must yield "
              "the tight floor.");

// The static assertions above pin the inductive base.  The runtime
// smoke test closes the loop over operands the optimizer cannot fold.

}  // namespace

int main() {
    cal::detail::control_flow_lattice_self_test::control_flow_lattice_runtime_smoke_test();
    return 0;
}

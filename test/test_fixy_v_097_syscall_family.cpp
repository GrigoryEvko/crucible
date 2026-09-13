// The syscall surface is a separate axis rather than a refinement of
// the effect row.  An effect row is a coarse semantic class; two
// operations sharing one row can differ completely in kernel attack
// surface.  A vDSO-only clock read and a privilege-raising call both
// bind under no effect at all, yet only one of them is admissible on a
// hot path.  Folding the surface onto the effect axis loses that
// granularity and forces every consumer to re-derive it from the
// binding's call-site specification.

#include <crucible/algebra/lattices/SyscallFamilyLattice.h>
#include <crucible/safety/DimensionTraits.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cs = ::crucible::safety;
namespace cal = ::crucible::algebra::lattices;

namespace {

// Axis ordinals are append-only: an extension takes the next free
// slot, never a vacated earlier one.
static_assert(std::to_underlying(cs::DimensionAxis::SyscallSurface) == 23,
              "SyscallSurface must be the topmost axis, at ordinal 23.");

static_assert(cs::tier_of_axis(cs::DimensionAxis::SyscallSurface) == cs::TierKind::Semiring,
              "SyscallSurface must classify as Semiring, where parallel "
              "composition is join and the strictest surface wins.");
static_assert(cs::tier_of_axis_v<cs::DimensionAxis::SyscallSurface> == cs::TierKind::Semiring,
              "The variable-template form of tier_of_axis must agree with the "
              "function form.");

static_assert(cs::dimension_axis_name(cs::DimensionAxis::SyscallSurface) == std::string_view{"SyscallSurface"},
              "dimension_axis_name must return \"SyscallSurface\"; a sentinel "
              "name indicates a missing switch arm.");

// This is the floor half of a floor-and-ceiling pair.  The exact
// ceiling sits beside the enum itself, so this witness only has to
// catch the inverse direction: removal of an enumerator.
static_assert(cs::DIMENSION_AXIS_COUNT >= 24, "DimensionAxis cardinality regressed below 24: an enumerator was "
                                              "removed without updating both the ceiling pin and this floor.");

// A wrong tier arm for the new axis re-classifies it silently, and a
// cardinality-only check still passes.  Re-witnessing one axis per
// tier rules that out.
static_assert(cs::tier_of_axis(cs::DimensionAxis::Type) == cs::TierKind::Foundational);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Refinement) == cs::TierKind::Foundational);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Protocol) == cs::TierKind::Typestate);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Representation) == cs::TierKind::Lattice);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Version) == cs::TierKind::Versioned);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Synchronization) == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Regime) == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::FpMode) == cs::TierKind::Semiring);

static_assert(cal::detail::syscall_family_lattice_self_test::family_count == 9,
              "SyscallFamily must have exactly 9 enumerators.  A new family "
              "raises this count and appends its At<T> specialization at the end "
              "of the lattice.");

static_assert(std::is_same_v<std::underlying_type_t<cal::SyscallFamily>, std::uint8_t>,
              "SyscallFamily must have uint8_t as its underlying type, so that "
              "an ordinal indexes a bridge table without zero-extension.");

// The chain is ordered by subset inclusion.  NoSyscall is the empty
// surface, and a capability-bearing call subsumes every weaker family,
// so Privilege is the top.
static_assert(std::to_underlying(cal::SyscallFamily::NoSyscall) == 0,
              "NoSyscall must be ordinal 0.  The lattice derives bottom() from "
              "the lowest enumerator, and every binding that declares no syscall "
              "surface defaults to it.");
static_assert(std::to_underlying(cal::SyscallFamily::VdsoOnly) == 1);
static_assert(std::to_underlying(cal::SyscallFamily::ReadOnlyState) == 2);
static_assert(std::to_underlying(cal::SyscallFamily::FileMutation) == 3);
static_assert(std::to_underlying(cal::SyscallFamily::MemoryMapping) == 4);
static_assert(std::to_underlying(cal::SyscallFamily::ThreadSync) == 5);
static_assert(std::to_underlying(cal::SyscallFamily::NetworkIo) == 6);
static_assert(std::to_underlying(cal::SyscallFamily::ProcessControl) == 7);
static_assert(std::to_underlying(cal::SyscallFamily::Privilege) == 8,
              "Privilege must be ordinal 8.  The lattice derives top() from the "
              "highest enumerator, and a capability-bearing syscall also mutates "
              "files, synchronizes and performs IO.");

// Only the three identity axioms are pinned here.  Meet, join and leq
// are checked over every pair by the runtime smoke test below.
static_assert(cal::SyscallFamilyLattice::bottom() == cal::SyscallFamily::NoSyscall);
static_assert(cal::SyscallFamilyLattice::top() == cal::SyscallFamily::Privilege);
static_assert(cal::SyscallFamilyLattice::leq(cal::SyscallFamily::NoSyscall, cal::SyscallFamily::Privilege),
              "Bottom must be leq top, or the chain ordering is broken.");

// At<T> pins a family at the type level so a binding site carries its
// syscall surface with no runtime data.  Emptiness is the load-bearing
// part: it is what lets the graded wrapper collapse to the size of its
// payload.  Bottom, top and one mid-chain element are witnessed.
static_assert(std::is_empty_v<cal::SyscallFamilyLattice::At<cal::SyscallFamily::NoSyscall>::element_type>,
              "At<T>::element_type must be an empty struct, so that a graded "
              "wrapper over it collapses to the size of its payload.");
static_assert(std::is_empty_v<cal::SyscallFamilyLattice::At<cal::SyscallFamily::FileMutation>::element_type>);
static_assert(std::is_empty_v<cal::SyscallFamilyLattice::At<cal::SyscallFamily::Privilege>::element_type>);

static_assert(cal::SyscallFamilyLattice::At<cal::SyscallFamily::VdsoOnly>::tier == cal::SyscallFamily::VdsoOnly,
              "At<T>::tier must equal T, so a wrapper can read the family "
              "without runtime data.");

// The vDSO, file-mutation, network and privilege ordering is the one a
// reader would expect from subset inclusion.  Pinning it catches an
// enumerator reordering during a merge.
static_assert(cal::SyscallFamilyLattice::leq(cal::SyscallFamily::VdsoOnly, cal::SyscallFamily::FileMutation));
static_assert(cal::SyscallFamilyLattice::leq(cal::SyscallFamily::FileMutation, cal::SyscallFamily::NetworkIo));
static_assert(cal::SyscallFamilyLattice::leq(cal::SyscallFamily::NetworkIo, cal::SyscallFamily::Privilege));

// The chain is totally ordered, so no two families are incomparable.
static_assert(!cal::SyscallFamilyLattice::leq(cal::SyscallFamily::NetworkIo, cal::SyscallFamily::FileMutation));

// Composing two sites takes the least upper bound of their surfaces,
// so the wider surface dominates.
static_assert(cal::SyscallFamilyLattice::join(cal::SyscallFamily::VdsoOnly, cal::SyscallFamily::FileMutation)
                  == cal::SyscallFamily::FileMutation,
              "Composing a weaker family with a stronger one must yield the "
              "stronger, so that a declared surface is the least upper bound of "
              "its constituents.");

// An admission gate asks what both sides will admit, which is the
// greatest lower bound, not the upper one.
static_assert(cal::SyscallFamilyLattice::meet(cal::SyscallFamily::Privilege, cal::SyscallFamily::VdsoOnly)
                  == cal::SyscallFamily::VdsoOnly,
              "Meeting a tight admission policy with a loose binding must yield "
              "the tight floor.");

// The static assertions above pin the inductive base.  The runtime
// smoke test closes the loop over non-constant operands, checking
// reflexivity, antisymmetry, transitivity, idempotence and absorption
// for every pair.

}  // namespace

int main() {
    cal::detail::syscall_family_lattice_self_test ::syscall_family_lattice_runtime_smoke_test();
    return 0;
}

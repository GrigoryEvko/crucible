#pragma once

// The barrier-strength atoms.  Every atom here engages
// Axis::BarrierStrength.
//
// The order is foundation's BarrierStrengthLattice, and its own words
// say what the grades are: the memory-fence strength a code region
// PROVIDES, bottom None and top FullFence, where a stronger fence
// satisfies a weaker requirement.  The domain brackets the standard
// memory-order tags at both ends.  CompilerBarrier sits below them
// because it constrains only the optimizer and emits no instruction.
// FullFence sits above them because a standalone fence orders every
// surrounding memory operation rather than the tagged one.
//
// Acquire and release are incomparable, as they are in the C++ memory
// model, so the order is a chain with one diamond and AcqRel is their
// join.  A release store does not satisfy an acquire floor, and an
// acquire load does not satisfy a release floor.  Nothing here proves a
// fence-then-relaxed pattern that depends on one architecture.  That is
// claimed separately.
//
// The standard memory orders are grades of this axis.  The Synchronization
// axis holds only the wait strategies, and fixy/Axis.h says so at the
// enumerator.
//
// ---------------------------------------------------------------------
// No lift
//
// fixy/atoms/Sync.h gives a lift three readings; this family takes the
// third, as fixy/atoms/Hw.h does.  A barrier strength is what a region
// PROVIDES to whoever reads through it, not an operation the region
// performs on some external surface, so it names nothing a context has
// to admit.  What a strength costs the hot path is V301's business, and
// what a strength fails to order across a scope is V401's, and both are
// collision rules rather than lifts.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <foundation/algebra/lattices/BarrierStrengthLattice.h>

#include <meta>
#include <tuple>
#include <type_traits>

namespace fixy::atom::barrier {

inline constexpr atom_seal atom_namespace_seal{};

namespace fal = ::foundation::algebra::lattices;

struct none final : atom_of<Axis::BarrierStrength> {
    static constexpr fal::BarrierStrength tier = fal::BarrierStrength::None;
};

struct compiler_barrier final : atom_of<Axis::BarrierStrength> {
    static constexpr fal::BarrierStrength tier = fal::BarrierStrength::CompilerBarrier;
};

struct acquire_load final : atom_of<Axis::BarrierStrength> {
    static constexpr fal::BarrierStrength tier = fal::BarrierStrength::AcquireLoad;
};

struct release_store final : atom_of<Axis::BarrierStrength> {
    static constexpr fal::BarrierStrength tier = fal::BarrierStrength::ReleaseStore;
};

struct acq_rel final : atom_of<Axis::BarrierStrength> {
    static constexpr fal::BarrierStrength tier = fal::BarrierStrength::AcqRel;
};

struct seq_cst final : atom_of<Axis::BarrierStrength> {
    static constexpr fal::BarrierStrength tier = fal::BarrierStrength::SeqCst;
};

struct full_fence final : atom_of<Axis::BarrierStrength> {
    static constexpr fal::BarrierStrength tier = fal::BarrierStrength::FullFence;
};

// Whether a provided strength satisfies a floor.  This is the lattice's
// own leq in its own admission direction, leq(required, provided), so
// the rules cannot invert it.
[[nodiscard]] consteval bool at_or_above(fal::BarrierStrength provided, fal::BarrierStrength floor) noexcept {
    return fal::BarrierStrengthLattice::leq(floor, provided);
}

}  // namespace fixy::atom::barrier

namespace fixy::atom::detail {

using barrier_atom_roster = std::tuple<barrier::none, barrier::compiler_barrier, barrier::acquire_load,
                                       barrier::release_store, barrier::acq_rel, barrier::seq_cst, barrier::full_fence>;

}  // namespace fixy::atom::detail

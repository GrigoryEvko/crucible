#pragma once

// The barrier-strength atoms.  Every atom here engages
// Axis::BarrierStrength.
//
// The ladder is foundation's BarrierStrengthLattice, and its own words
// say what the grades are: a chain over the memory-fence strength a code
// region PROVIDES, bottom None and top FullFence, where a stronger fence
// satisfies a weaker requirement.  The domain brackets the standard
// memory-order tags at both ends — CompilerBarrier sits below them
// because it constrains only the optimizer and emits no instruction,
// FullFence above them because a standalone fence orders every
// surrounding memory operation rather than the tagged one.
//
// Acquire and release are incomparable in the C++ memory model; the
// chain linearises them because gating asks only whether a region
// provides AT LEAST a required strength, and a stronger rung is always a
// safe over-approximation.  Nothing here proves a fence-then-relaxed
// pattern that depends on one architecture; that is claimed separately.
//
// This axis is not the memory-order tag on the Synchronization axis, and
// fixy/Axis.h says so at the enumerator: this is the standalone
// hardware-fence ladder.
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
#include <foundation/effects/Lift.h>

#include <cstddef>
#include <meta>
#include <tuple>
#include <type_traits>

namespace fixy::atom::barrier {

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
                                       barrier::release_store, barrier::acq_rel, barrier::seq_cst,
                                       barrier::full_fence>;

}  // namespace fixy::atom::detail

namespace fixy::atom::detail::barrier_atom_self_test {

namespace fal = ::foundation::algebra::lattices;

static_assert(every_atom_in_is_rostered_<^^::fixy::atom::barrier, barrier_atom_roster>(),
              "fixy/atoms/Barrier.h: an atom declared in fixy::atom::barrier is missing from "
              "barrier_atom_roster.");
static_assert(every_roster_member_is_atom_<barrier_atom_roster>(),
              "fixy/atoms/Barrier.h: a member of barrier_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<barrier_atom_roster, Axis::BarrierStrength>(),
              "fixy/atoms/Barrier.h: every barrier atom engages Axis::BarrierStrength.");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

template <fal::BarrierStrength T>
[[nodiscard]] consteval std::size_t atoms_claiming_() noexcept {
    std::size_t claims = 0;
    template for (constexpr auto member : roster_members_v<barrier_atom_roster>) {
        using A = [:member:];
        if constexpr (A::tier == T) ++claims;
    }
    return claims;
}

[[nodiscard]] consteval bool every_tier_has_exactly_one_atom_() noexcept {
    bool exact = true;
    static constexpr auto tiers = std::define_static_array(std::meta::enumerators_of(^^fal::BarrierStrength));
    template for (constexpr auto tier_member : tiers) {
        constexpr fal::BarrierStrength tier = [:tier_member:];
        exact = exact && (atoms_claiming_<tier>() == 1);
    }
    return exact;
}

[[nodiscard]] consteval bool no_member_lifts_() noexcept {
    bool none_lift = true;
    template for (constexpr auto member : roster_members_v<barrier_atom_roster>) {
        using A = [:member:];
        none_lift = none_lift && !::foundation::effects::LiftsToRow<A>;
    }
    return none_lift;
}

#pragma GCC diagnostic pop

static_assert(every_tier_has_exactly_one_atom_(),
              "fixy/atoms/Barrier.h: every BarrierStrength enumerator must be claimed by exactly one atom in "
              "fixy::atom::barrier.  A rung with no atom cannot be written, and one with two is unreachable.");

static_assert(no_member_lifts_(), "fixy/atoms/Barrier.h: a provided strength names no operation, so no atom here "
                                  "declares lifts_to.  The head of this file says why.");

// The chain at the two boundaries the rules read.  V301 refuses SeqCst
// or above on the hot path; V401 wants AcqRel or above for a wide scope.
static_assert(barrier::at_or_above(fal::BarrierStrength::FullFence, fal::BarrierStrength::SeqCst));
static_assert(barrier::at_or_above(fal::BarrierStrength::SeqCst, fal::BarrierStrength::SeqCst));
static_assert(!barrier::at_or_above(fal::BarrierStrength::AcqRel, fal::BarrierStrength::SeqCst));
static_assert(barrier::at_or_above(fal::BarrierStrength::AcqRel, fal::BarrierStrength::AcqRel));
static_assert(!barrier::at_or_above(fal::BarrierStrength::ReleaseStore, fal::BarrierStrength::AcqRel));
static_assert(!barrier::at_or_above(fal::BarrierStrength::None, fal::BarrierStrength::CompilerBarrier));

static_assert(!std::is_same_v<barrier::acquire_load, barrier::release_store>);

}  // namespace fixy::atom::detail::barrier_atom_self_test

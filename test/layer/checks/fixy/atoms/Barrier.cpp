// The compile-time checks of fixy/atoms/Barrier.h.

#include <fixy/atoms/Barrier.h>

namespace fixy::atom::detail::barrier_atom_self_test {

namespace fal = ::foundation::algebra::lattices;

static_assert(every_atom_in_is_rostered_<^^::fixy::atom::barrier, barrier_atom_roster>(),
              "fixy/atoms/Barrier.h: an atom declared in fixy::atom::barrier is missing from "
              "barrier_atom_roster.");
static_assert(every_roster_member_is_atom_<barrier_atom_roster>(),
              "fixy/atoms/Barrier.h: a member of barrier_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<barrier_atom_roster, Axis::BarrierStrength>(),
              "fixy/atoms/Barrier.h: every barrier atom engages Axis::BarrierStrength.");

static_assert(every_enumerator_has_exactly_one_atom_<barrier_atom_roster, fal::BarrierStrength>(),
              "fixy/atoms/Barrier.h: every BarrierStrength enumerator must be claimed by exactly one atom in "
              "fixy::atom::barrier.  A rung with no atom cannot be written, and one with two is unreachable.");

static_assert(no_roster_member_lifts_<barrier_atom_roster>(),
              "fixy/atoms/Barrier.h: a provided strength names no operation, so no atom here declares "
              "lifts_to.  The head of this file says why.");

// The order at the two boundaries the rules read.  V301 refuses SeqCst or
// above on the hot path, and V401 wants AcqRel or above for a wide scope.
// The two tags between them are incomparable, so neither is at or above
// the other.
static_assert(barrier::at_or_above(fal::BarrierStrength::FullFence, fal::BarrierStrength::SeqCst));
static_assert(barrier::at_or_above(fal::BarrierStrength::SeqCst, fal::BarrierStrength::SeqCst));
static_assert(!barrier::at_or_above(fal::BarrierStrength::AcqRel, fal::BarrierStrength::SeqCst));
static_assert(barrier::at_or_above(fal::BarrierStrength::AcqRel, fal::BarrierStrength::AcqRel));
static_assert(!barrier::at_or_above(fal::BarrierStrength::ReleaseStore, fal::BarrierStrength::AcqRel));
static_assert(!barrier::at_or_above(fal::BarrierStrength::None, fal::BarrierStrength::CompilerBarrier));
static_assert(!barrier::at_or_above(fal::BarrierStrength::ReleaseStore, fal::BarrierStrength::AcquireLoad));
static_assert(!barrier::at_or_above(fal::BarrierStrength::AcquireLoad, fal::BarrierStrength::ReleaseStore));
static_assert(barrier::at_or_above(fal::BarrierStrength::AcqRel, fal::BarrierStrength::AcquireLoad)
              && barrier::at_or_above(fal::BarrierStrength::AcqRel, fal::BarrierStrength::ReleaseStore));

static_assert(!std::is_same_v<barrier::acquire_load, barrier::release_store>);

}  // namespace fixy::atom::detail::barrier_atom_self_test

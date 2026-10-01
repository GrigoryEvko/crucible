// The compile-time checks of fixy/atoms/Sync.h.

#include <fixy/atoms/Sync.h>

namespace fixy::atom::detail::sync_atom_self_test {

namespace fal = ::foundation::algebra::lattices;
namespace fe = ::foundation::effects;

static_assert(every_atom_in_is_rostered_<^^::fixy::atom::sync, sync_atom_roster>(),
              "fixy/atoms/Sync.h: an atom declared in fixy::atom::sync is missing from sync_atom_roster.");

static_assert(every_roster_member_is_atom_<sync_atom_roster>(),
              "fixy/atoms/Sync.h: a member of sync_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<sync_atom_roster, Axis::Synchronization>(),
              "fixy/atoms/Sync.h: every synchronization atom engages Axis::Synchronization.");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// The lift agrees with the division: an atom lifts to Row<Block> exactly
// when its strategy enters the kernel.  This is the assertion that keeps
// the two from drifting apart — a new grade added on the wrong side of
// the line reddens here rather than silently admitting a kernel wait into
// a foreground context.
[[nodiscard]] consteval bool every_lift_matches_the_division_() noexcept {
    bool agrees = true;
    template for (constexpr auto member : roster_members_v<sync_atom_roster>) {
        using A = [:member:];
        constexpr bool lifts_block = std::is_same_v<fe::lift_row_t<A>, fe::Row<fe::Effect::Block>>;
        agrees = agrees && (lifts_block == ::fixy::atom::sync::enters_the_kernel(A::strategy));
    }
    return agrees;
}

#pragma GCC diagnostic pop

// The pin: the family covers WaitStrategy exactly, one atom per
// enumerator.  fixy/Atom.h holds the walk.
static_assert(every_enumerator_has_exactly_one_atom_<sync_atom_roster, fal::WaitStrategy>(),
              "fixy/atoms/Sync.h: every WaitStrategy enumerator must be claimed by exactly one atom in "
              "fixy::atom::sync.  A grade with no atom cannot be written by a caller, and a grade with two "
              "means one of them is unreachable.");

static_assert(every_lift_matches_the_division_(),
              "fixy/atoms/Sync.h: an atom lifts to Row<Block> without entering the kernel, or enters the "
              "kernel without lifting to Row<Block>.  The lift and enters_the_kernel must draw the same "
              "line, or a kernel wait reaches a context that never admitted Block.");

// Every atom here lifts, and the spins lift too.  An atom with no lift is
// invisible to the effect gates, and one that lifts to the empty row is
// visible and requires nothing.  The head of this file says why the spins
// want the second.
static_assert(every_roster_member_lifts_<sync_atom_roster>(),
              "fixy/atoms/Sync.h: every synchronization atom must declare lifts_to, the spins included.  An "
              "atom with no lift is skipped by the effect gates rather than admitted by them.");

// The two predicates classify, and they are not complements: UMWAIT is
// neither a kernel wait nor a core burner, which is the grade that makes
// W002 narrower than "not a kernel wait".
static_assert(::fixy::atom::sync::enters_the_kernel(fal::WaitStrategy::Block));
static_assert(::fixy::atom::sync::enters_the_kernel(fal::WaitStrategy::AcquireWait));
static_assert(!::fixy::atom::sync::enters_the_kernel(fal::WaitStrategy::UmwaitC01));
static_assert(!::fixy::atom::sync::burns_the_core(fal::WaitStrategy::UmwaitC01));
static_assert(!::fixy::atom::sync::burns_the_core(fal::WaitStrategy::Block));
static_assert(::fixy::atom::sync::burns_the_core(fal::WaitStrategy::SpinPause));
static_assert(::fixy::atom::sync::burns_the_core(fal::WaitStrategy::BoundedSpin));

// The grade is part of the identity, so tier 4 refuses a pack naming two.
static_assert(!std::is_same_v<sync::spin_pause, sync::bounded_spin>);
static_assert(!std::is_same_v<sync::block, sync::park>);

}  // namespace fixy::atom::detail::sync_atom_self_test

// The compile-time checks of fixy/atoms/Scope.h.

#include <fixy/atoms/Scope.h>

namespace fixy::atom::detail::scope_atom_self_test {

namespace fal = ::foundation::algebra::lattices;

static_assert(every_atom_in_is_rostered_<^^::fixy::atom::scope, scope_atom_roster>(),
              "fixy/atoms/Scope.h: an atom declared in fixy::atom::scope is missing from scope_atom_roster.");
static_assert(every_roster_member_is_atom_<scope_atom_roster>(),
              "fixy/atoms/Scope.h: a member of scope_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<scope_atom_roster, Axis::MemoryScope>(),
              "fixy/atoms/Scope.h: every scope atom engages Axis::MemoryScope.");

static_assert(every_enumerator_has_exactly_one_atom_<scope_atom_roster, fal::MemoryScope>(),
              "fixy/atoms/Scope.h: every MemoryScope enumerator must be claimed by exactly one atom in "
              "fixy::atom::scope.  A scope with no atom cannot be written, and one with two is unreachable.");

static_assert(no_roster_member_lifts_<scope_atom_roster>(),
              "fixy/atoms/Scope.h: a visibility scope names no operation, so no atom here declares lifts_to.  "
              "The head of this file says why.");

// The order the rules read, including the cross-trunk cell that makes
// V401 a two-trunk rule rather than a threshold: Inner is incomparable
// with Cluster, so it is neither at or above it nor below it.
static_assert(scope::at_or_above(fal::MemoryScope::Gpu, fal::MemoryScope::Cluster));
static_assert(scope::at_or_above(fal::MemoryScope::Cluster, fal::MemoryScope::Cluster));
static_assert(scope::at_or_above(fal::MemoryScope::System, fal::MemoryScope::Cluster),
              "the shared top reaches everywhere");
static_assert(!scope::at_or_above(fal::MemoryScope::Cta, fal::MemoryScope::Cluster));
static_assert(!scope::at_or_above(fal::MemoryScope::Inner, fal::MemoryScope::Cluster), "incomparable, not smaller");
static_assert(!scope::at_or_above(fal::MemoryScope::Cluster, fal::MemoryScope::Inner));

// The trunk predicates V402 reads.
static_assert(!scope::is_trunk_pinned(fal::MemoryScope::Thread));
static_assert(!scope::is_trunk_pinned(fal::MemoryScope::System));
static_assert(scope::is_trunk_pinned(fal::MemoryScope::Cta) && scope::is_trunk_pinned(fal::MemoryScope::Outer));
static_assert(scope::on_host_trunk(fal::MemoryScope::Inner) && scope::on_host_trunk(fal::MemoryScope::Outer));
static_assert(!scope::on_host_trunk(fal::MemoryScope::Cta) && !scope::on_host_trunk(fal::MemoryScope::Gpu));

static_assert(!std::is_same_v<scope::cta, scope::cluster>);

}  // namespace fixy::atom::detail::scope_atom_self_test

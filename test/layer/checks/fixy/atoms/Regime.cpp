// The compile-time checks of fixy/atoms/Regime.h.

#include <fixy/atoms/Regime.h>

namespace fixy::atom::detail::regime_atom_self_test {

namespace fal = ::foundation::algebra::lattices;

static_assert(every_atom_in_is_rostered_<^^::fixy::atom::regime, regime_atom_roster>(),
              "fixy/atoms/Regime.h: an atom declared in fixy::atom::regime is missing from "
              "regime_atom_roster.");

static_assert(every_roster_member_is_atom_<regime_atom_roster>(),
              "fixy/atoms/Regime.h: a member of regime_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<regime_atom_roster, Axis::Regime>(),
              "fixy/atoms/Regime.h: every regime atom engages Axis::Regime.");

// The pin: the family covers HotPathTier exactly, one atom per
// enumerator.  fixy/Atom.h holds the walk.
static_assert(every_enumerator_has_exactly_one_atom_<regime_atom_roster, fal::HotPathTier>(),
              "fixy/atoms/Regime.h: every HotPathTier enumerator must be claimed by exactly one atom in "
              "fixy::atom::regime.  A tier with no atom cannot be written by a caller, and a tier with two "
              "means one of them is unreachable.");

// The tier is part of the identity, so tier 4 refuses a pack that names
// two of them.  (fn's tier 4 is what does the refusing; these say the
// three types are distinct, which is what it reads.)
static_assert(!std::is_same_v<regime::hot, regime::warm>);
static_assert(!std::is_same_v<regime::hot, regime::cold>);
static_assert(!std::is_same_v<regime::warm, regime::cold>);

// No regime atom lifts.  The comment at the head of this file says why,
// and this assertion keeps the comment true.
static_assert(no_roster_member_lifts_<regime_atom_roster>(),
              "fixy/atoms/Regime.h: a latency budget names no operation, so no atom here declares lifts_to.");

}  // namespace fixy::atom::detail::regime_atom_self_test

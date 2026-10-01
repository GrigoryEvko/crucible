// The compile-time checks of fixy/atoms/Hw.h.

#include <fixy/atoms/Hw.h>

namespace fixy::atom::detail::hw_atom_self_test {

static_assert(every_atom_in_is_rostered_<^^::fixy::atom::hw, hw_atom_roster>(),
              "fixy/atoms/Hw.h: an atom declared in fixy::atom::hw is missing from hw_atom_roster.");
static_assert(every_roster_member_is_atom_<hw_atom_roster>(),
              "fixy/atoms/Hw.h: a member of hw_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<hw_atom_roster, Axis::HwInstruction>(),
              "fixy/atoms/Hw.h: every hardware-instruction atom engages Axis::HwInstruction.");

static_assert(every_enumerator_has_exactly_one_atom_<hw_atom_roster, hw::HwInstruction>(),
              "fixy/atoms/Hw.h: every HwInstruction enumerator must be claimed by exactly one atom in "
              "fixy::atom::hw.  A tier with no atom cannot be written by a caller, and a tier with two means "
              "one of them is unreachable.");

static_assert(no_roster_member_lifts_<hw_atom_roster>(),
              "fixy/atoms/Hw.h: an instruction class admits a set of operations and names none, so no atom "
              "here declares lifts_to.  The head of this file says why.");

// The chain, read at both ends and across the one boundary V201 cares
// about: a tier at or above NonDeterministicTsc is one the hot path
// refuses.
static_assert(hw::at_or_above(hw::HwInstruction::PrivilegedMsr, hw::HwInstruction::NonDeterministicTsc));
static_assert(hw::at_or_above(hw::HwInstruction::NonDeterministicTsc, hw::HwInstruction::NonDeterministicTsc));
static_assert(!hw::at_or_above(hw::HwInstruction::Vectorizable, hw::HwInstruction::NonDeterministicTsc));
static_assert(!hw::at_or_above(hw::HwInstruction::NoneAllowed, hw::HwInstruction::Scalar));

static_assert(!std::is_same_v<hw::scalar, hw::vectorizable>);

}  // namespace fixy::atom::detail::hw_atom_self_test

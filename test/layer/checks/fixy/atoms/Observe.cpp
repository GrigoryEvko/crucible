// The compile-time checks of fixy/atoms/Observe.h.

#include <fixy/atoms/Observe.h>

namespace fixy::atom::detail::observe_atom_self_test {

namespace fe = ::foundation::effects;

static_assert(every_roster_member_is_atom_<observe_atom_roster>(),
              "fixy/atoms/Observe.h: a member of observe_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<observe_atom_roster, Axis::Observability>(),
              "fixy/atoms/Observe.h: every observability atom engages Axis::Observability.");

// The row is part of the identity, so tier 4 refuses a pack naming two
// surfaces and the rules can tell one surface from another.
static_assert(!std::is_same_v<observe::surface<fe::Effect::IO>, observe::surface<fe::Effect::Bg>>);
static_assert(std::is_same_v<observe::surface<fe::Effect::IO>::row, fe::Row<fe::Effect::IO>>);
static_assert(std::is_same_v<observe::surface<>::row, fe::Row<>>);

// No lift.  The head of this file gives the three meanings of a lift and
// why this family wants the absent one; these are what keep that true.
static_assert(!fe::LiftsToRow<observe::surface<fe::Effect::IO>>);
static_assert(!fe::LiftsToRow<observe::surface<>>);

// The containment B002 reads, checked here on the rows themselves so that
// a Subrow that stopped meaning containment would redden beside the atom
// rather than only inside the rule.
static_assert(fe::Subrow<observe::surface<fe::Effect::IO>::row, fe::Row<fe::Effect::IO, fe::Effect::Bg>>);
static_assert(!fe::Subrow<observe::surface<fe::Effect::IO>::row, fe::Row<>>);
static_assert(fe::Subrow<observe::surface<>::row, fe::Row<>>);

}  // namespace fixy::atom::detail::observe_atom_self_test

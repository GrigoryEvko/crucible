#pragma once

// The observability atoms.  Every atom here engages
// Axis::Observability.
//
// ---------------------------------------------------------------------
// Observability is a projection of the Effect row, not a second row
//
// This axis does NOT say what the binding may do.  The Effect axis says
// that, and stays the only authority for it.  Observability names which
// PART of that row is observation rather than computation — which of the
// operations the binding already declared exist so that something
// outside can see a fact, as opposed to existing to compute a result.
//
// The invariant is therefore a containment, and B002 below enforces it:
//
//     Subrow<observability_row, effect_row>
//
// Three consequences, and the second is the one that matters:
//
//   1. Every existing gate is unchanged.  They read the Effect row, and
//      it did not move.
//
//   2. An observability atom cannot smuggle in an effect the binding
//      never declared.  Observation that requires an effect the
//      operation does not have is observation that changes behaviour,
//      which is precisely what a passive surface must not do.
//      CLAUDE.md L15 says Observe "records facts; it does not enforce
//      policy", and a declaration that could widen what the operation is
//      permitted to do is not passive.
//
//   3. The rules can fire, so the atoms are gates rather than
//      decoration.
//
// axis_traits<Axis::Observability> already said this before any atom
// existed: its pole is `derived_from = axis_traits<Axis::Effect>`, so the
// strict pole is Effect's empty row — "observes nothing".  The subrow
// reading is what those traits were already asserting.
//
// ---------------------------------------------------------------------
// No lift, and a third meaning for that
//
// fixy/atoms/Sync.h established two: an atom that declares
// `lifts_to = Row<...>` names an operation and states its requirement,
// and one that declares `lifts_to = Row<>` names an operation that
// requires nothing.  The spins are the second.
//
// An observability surface is neither.  It names NO operation: it
// re-describes operations the Effect grade already declared.  So it
// declares no `lifts_to` at all, and that absence is the third meaning.
// Were it to lift to its own row it would widen the required row, which
// is the failure B002 exists to refuse; were it to lift to Row<> it
// would claim to be an operation that happens to require nothing, which
// is false in a different way.
//
// ---------------------------------------------------------------------
// One limitation, stated rather than left to be discovered
//
// The family's only atom is parametric, so the namespace walk that keeps
// the other families' rosters honest covers nothing here: it reads plain
// atoms, and there are none.  The roster below therefore lists three
// representative instantiations, the way fixy/atoms/Stack.h lists two
// frame budgets, and the guard that has teeth is the axis check rather
// than the completeness check.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <tuple>
#include <type_traits>

namespace fixy::atom::observe {

namespace fe = ::foundation::effects;

// The effects of this binding that exist to let something outside see a
// fact.  Every effect named here must also be in the binding's Effect
// row, which is B002.
template <fe::Effect... Es>
struct surface final : atom_of<Axis::Observability> {
    using row = fe::Row<Es...>;
};

}  // namespace fixy::atom::observe

namespace fixy::atom::detail {

// Three representative instantiations.  A parametric atom cannot be
// enumerated, so these stand for the family in the joined roster the way
// stack::alloc<64> and stack::alloc<4096> stand for theirs.
using observe_atom_roster =
    std::tuple<observe::surface<::foundation::effects::Effect::IO>,
               observe::surface<::foundation::effects::Effect::Bg>,
               observe::surface<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Bg>>;

}  // namespace fixy::atom::detail

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

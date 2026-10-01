#pragma once

// Named effect rows using the F* effect-lattice vocabulary, ordered
// Pure  ⊑  Div  ⊑  ST  ⊑  All.  Each alias names the
// upper bound of its level, so row containment is the lattice order:
// `Subrow<R, XRow>` reads "R fits within X's effect budget", and the
// refinement implications between the levels need no separate encoding.
//
// The vocabulary is borrowed at the effect level only.  There are no
// refinement types, no decreases-clauses and no termination metric.
//
// Pure and Tot are the DetSafe band alone over the Computation.  Div
// has only its row form, DivRow.  A value form of Div would be the type
// `Tot<DivRow, T>` under a second name.

#include <fixy/Bands.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <type_traits>

namespace fixy {

using ::foundation::effects::Computation;
using ::foundation::effects::Effect;
using ::foundation::effects::Row;
using ::foundation::effects::Subrow;

// The empty row.  F* also names it Tot and Ghost, and here the one name
// serves for each: a second alias would be the same type.
using PureRow = Row<>;

// Block is the atom for a wait with no guaranteed bound, which is how
// divergence is spelled here.
using DivRow = Row<Effect::Block>;

using STRow = Row<Effect::Block, Effect::Alloc, Effect::IO>;

// The row of every atom that the Effect enum declares.  Row.h derives it
// from the enumerators, so a new atom is in it with no edit here.
using AllRow = ::foundation::effects::every_effect_row;

template <typename R>
concept IsPure = Subrow<R, PureRow>;

template <typename R>
concept IsDiv = Subrow<R, DivRow>;

template <typename R>
concept IsST = Subrow<R, STRow>;

template <typename R>
concept IsAll = Subrow<R, AllRow>;

// Value-carrying forms.  In a value form the DetSafe tier is always
// Pure and only the row changes.  So the two aliases below name every
// cell worth a name, and ST and All are row names.
//
// The alias `Pure` and the enum value `DetSafeTier_v::Pure` share a
// spelling.  The expansions qualify the enum value so the two cannot be
// confused at the definition site.

template <typename T>
using Pure = DetSafe<DetSafeTier_v::Pure, Computation<PureRow, T>>;

template <typename E_os, typename T>
using Tot = DetSafe<DetSafeTier_v::Pure, Computation<E_os, T>>;

}  // namespace fixy

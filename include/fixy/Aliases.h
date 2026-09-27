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
// Old spelling: include/crucible/effects/FxAliases.h.  The old value
// forms wrapped the DetSafe band in a Progress band, which pinned the
// termination class of the work.  Progress had no consumer outside the
// old substrate and did not survive the port, so Pure and Tot are the
// DetSafe band alone over the Computation, and Div has no value form:
// without Progress it would be the same type as Tot under a second
// name.  DivRow, the row form, stays.

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

static_assert(::foundation::effects::is_subrow_v<PureRow, DivRow>);
static_assert(::foundation::effects::is_subrow_v<DivRow, STRow>);
static_assert(::foundation::effects::is_subrow_v<STRow, AllRow>);
static_assert(::foundation::effects::is_subrow_v<DivRow, AllRow>);
static_assert(::foundation::effects::is_subrow_v<PureRow, AllRow>);

static_assert(!::foundation::effects::is_subrow_v<DivRow, PureRow>);
static_assert(!::foundation::effects::is_subrow_v<STRow, DivRow>);
static_assert(!::foundation::effects::is_subrow_v<AllRow, STRow>);

template <typename R>
concept IsPure = Subrow<R, PureRow>;

template <typename R>
concept IsDiv = Subrow<R, DivRow>;

template <typename R>
concept IsST = Subrow<R, STRow>;

template <typename R>
concept IsAll = Subrow<R, AllRow>;

// Value-carrying forms.  A row is the one discrete component that
// survived, so the two aliases below name every cell worth a name: ST
// and All are row names.
//
// The alias `Pure` and the enum value `DetSafeTier_v::Pure` share a
// spelling.  The expansions qualify the enum value so the two cannot be
// confused at the definition site.

template <typename T>
using Pure = DetSafe<DetSafeTier_v::Pure, Computation<PureRow, T>>;

template <typename E_os, typename T>
using Tot = DetSafe<DetSafeTier_v::Pure, Computation<E_os, T>>;

namespace detail::aliases_self_test {

static_assert(IsPure<PureRow>);
static_assert(!IsPure<Row<Effect::Alloc>>);
static_assert(!IsPure<Row<Effect::Block>>);

static_assert(IsDiv<PureRow>);
static_assert(IsDiv<Row<Effect::Block>>);
static_assert(!IsDiv<Row<Effect::Alloc>>);
static_assert(!IsDiv<Row<Effect::IO>>);
static_assert(!IsDiv<Row<Effect::Block, Effect::Alloc>>);

static_assert(IsST<PureRow>);
static_assert(IsST<Row<Effect::Block>>);
static_assert(IsST<Row<Effect::Alloc>>);
static_assert(IsST<Row<Effect::IO>>);
static_assert(IsST<Row<Effect::Alloc, Effect::IO>>);
static_assert(IsST<Row<Effect::Block, Effect::Alloc, Effect::IO>>);
static_assert(!IsST<Row<Effect::Bg>>);
static_assert(!IsST<Row<Effect::Init>>);
static_assert(!IsST<Row<Effect::Alloc, Effect::Bg>>);

static_assert(IsAll<PureRow>);
static_assert(IsAll<DivRow>);
static_assert(IsAll<STRow>);
static_assert(IsAll<AllRow>);
static_assert(IsAll<Row<Effect::Bg>>);
static_assert(IsAll<Row<Effect::Init, Effect::Test>>);

// Each named row sits at its own level and at every level above it.
static_assert(!IsPure<DivRow>);
static_assert(IsDiv<DivRow>);
static_assert(IsST<DivRow>);

static_assert(!IsPure<STRow>);
static_assert(!IsDiv<STRow>);
static_assert(IsST<STRow>);

static_assert(!IsPure<AllRow>);
static_assert(!IsDiv<AllRow>);
static_assert(!IsST<AllRow>);

using PureInt = Pure<int>;
using TotIoInt = Tot<Row<Effect::IO>, int>;
using TotAllVp = Tot<AllRow, void*>;

static_assert(std::is_same_v<PureInt, DetSafe<DetSafeTier_v::Pure, Computation<PureRow, int>>>);
static_assert(band_tier_v<PureInt> == DetSafeTier_v::Pure);
static_assert(std::is_same_v<PureInt::value_type::row_type, PureRow>);
static_assert(std::is_same_v<PureInt::value_type::value_type, int>);

static_assert(std::is_same_v<TotIoInt::value_type::row_type, Row<Effect::IO>>);
static_assert(band_tier_v<TotIoInt> == DetSafeTier_v::Pure);
static_assert(std::is_same_v<TotAllVp::value_type::row_type, AllRow>);

static_assert(sizeof(Pure<int>) == sizeof(Computation<PureRow, int>));
static_assert(sizeof(Tot<Row<Effect::IO>, int>) == sizeof(Computation<Row<Effect::IO>, int>));
static_assert(sizeof(Tot<AllRow, void*>) == sizeof(Computation<AllRow, void*>));

}  // namespace detail::aliases_self_test

}  // namespace fixy

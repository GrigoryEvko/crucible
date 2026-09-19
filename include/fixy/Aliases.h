#pragma once

// Named effect rows using the F* effect-lattice vocabulary, ordered
// Pure / Tot / Ghost  ⊑  Div  ⊑  ST  ⊑  All.  Each alias names the
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

using PureRow = Row<>;

// Synonym of Pure.  A separate name so a call site can state totality
// as its intent.
using TotRow = Row<>;

// No Ghost atom exists, so the row is empty.  The alias is reserved so
// later code can specialize on it.
using GhostRow = Row<>;

// Block is the atom for a wait with no guaranteed bound, which is how
// divergence is spelled here.
using DivRow = Row<Effect::Block>;

using STRow = Row<Effect::Block, Effect::Alloc, Effect::IO>;

using AllRow = Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>;

// The size check alone is not sufficient.  A rename that keeps the
// count constant slips through it, so the per-atom membership checks
// carry the guarantee.
static_assert(::foundation::effects::row_size_v<AllRow> == ::foundation::effects::effect_count,
              "AllRow must enumerate every atom in Effect; update both together");

static_assert(::foundation::effects::row_contains_v<AllRow, Effect::Alloc>,
              "AllRow missing Alloc — universe row out of sync with Effect enum");
static_assert(::foundation::effects::row_contains_v<AllRow, Effect::IO>,
              "AllRow missing IO — universe row out of sync with Effect enum");
static_assert(::foundation::effects::row_contains_v<AllRow, Effect::Block>,
              "AllRow missing Block — universe row out of sync with Effect enum");
static_assert(::foundation::effects::row_contains_v<AllRow, Effect::Bg>,
              "AllRow missing Bg — universe row out of sync with Effect enum");
static_assert(::foundation::effects::row_contains_v<AllRow, Effect::Init>,
              "AllRow missing Init — universe row out of sync with Effect enum");
static_assert(::foundation::effects::row_contains_v<AllRow, Effect::Test>,
              "AllRow missing Test — universe row out of sync with Effect enum");

static_assert(::foundation::effects::is_subrow_v<PureRow, TotRow>);
static_assert(::foundation::effects::is_subrow_v<TotRow, PureRow>);
static_assert(::foundation::effects::is_subrow_v<PureRow, GhostRow>);
static_assert(::foundation::effects::is_subrow_v<GhostRow, PureRow>);

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
concept IsTot = Subrow<R, TotRow>;

template <typename R>
concept IsGhost = Subrow<R, GhostRow>;

template <typename R>
concept IsDiv = Subrow<R, DivRow>;

template <typename R>
concept IsST = Subrow<R, STRow>;

template <typename R>
concept IsAll = Subrow<R, AllRow>;

// Value-carrying forms.  A row is the one discrete component that
// survived, so the two aliases below name every cell worth a name: ST
// and All are row names, and a Ghost form would expand exactly as Pure
// does.
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
static_assert(IsPure<TotRow>);
static_assert(IsPure<GhostRow>);
static_assert(!IsPure<Row<Effect::Alloc>>);
static_assert(!IsPure<Row<Effect::Block>>);

static_assert(IsTot<PureRow>);
static_assert(!IsTot<Row<Effect::Alloc>>);

static_assert(IsGhost<PureRow>);
static_assert(!IsGhost<Row<Effect::IO>>);

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
static_assert(!IsTot<DivRow>);
static_assert(!IsGhost<DivRow>);
static_assert(IsDiv<DivRow>);
static_assert(IsST<DivRow>);

static_assert(!IsPure<STRow>);
static_assert(!IsTot<STRow>);
static_assert(!IsGhost<STRow>);
static_assert(!IsDiv<STRow>);
static_assert(IsST<STRow>);

static_assert(!IsPure<AllRow>);
static_assert(!IsTot<AllRow>);
static_assert(!IsGhost<AllRow>);
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

// The alias types and concept names must also compile outside a
// static_assert operand, where consteval-versus-constexpr accessor
// regressions and inline-body faults surface.
inline void runtime_smoke_test_aliases() noexcept {
    [[maybe_unused]] constexpr bool pure_is_pure = IsPure<PureRow>;
    [[maybe_unused]] constexpr bool div_is_div = IsDiv<DivRow>;
    [[maybe_unused]] constexpr bool st_is_st = IsST<STRow>;
    [[maybe_unused]] constexpr bool all_is_all = IsAll<AllRow>;
    [[maybe_unused]] constexpr auto pure_size = ::foundation::effects::row_size_v<PureRow>;
    [[maybe_unused]] constexpr auto all_size = ::foundation::effects::row_size_v<AllRow>;

    static_assert(IsPure<PureRow> && IsAll<AllRow>);

    Pure<int> pure_value{};
    Tot<Row<Effect::IO>, int> tot_value{};

    [[maybe_unused]] auto pure_tier = tier_of(pure_value);
    [[maybe_unused]] auto tot_tier = tier_of(tot_value);

    static_assert(std::is_same_v<decltype(pure_value), Pure<int>>);
    static_assert(std::is_same_v<decltype(tot_value), Tot<Row<Effect::IO>, int>>);
}

}  // namespace fixy

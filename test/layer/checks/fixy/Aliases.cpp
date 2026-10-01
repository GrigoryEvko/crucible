// The compile-time checks of fixy/Aliases.h.

#include <fixy/Aliases.h>

namespace fixy {

static_assert(::foundation::effects::Subrow<PureRow, DivRow>);
static_assert(::foundation::effects::Subrow<DivRow, STRow>);
static_assert(::foundation::effects::Subrow<STRow, AllRow>);
static_assert(::foundation::effects::Subrow<DivRow, AllRow>);
static_assert(::foundation::effects::Subrow<PureRow, AllRow>);

static_assert(!::foundation::effects::Subrow<DivRow, PureRow>);
static_assert(!::foundation::effects::Subrow<STRow, DivRow>);
static_assert(!::foundation::effects::Subrow<AllRow, STRow>);

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

// The compile-time checks of foundation/Brand.h.

#include <foundation/Brand.h>

namespace foundation::brand {

namespace detail::brand_self_test {

struct Carrier {};
struct Branded {
    using brand_type = DefaultBrand;
};
struct BrandedFresh {
    using brand_type = decltype([] {});
};
struct NotABrand {
    using brand_type = int;
};

static_assert(IsBrand<DefaultBrand>);
static_assert(IsBrand<decltype([] {})>);
static_assert(!IsBrand<int>);
static_assert(!IsFreshBrand<DefaultBrand>);
static_assert(IsFreshBrand<decltype([] {})>);
static_assert(HasBrand<Branded>);
static_assert(HasBrand<Branded const&>);
static_assert(!HasBrand<Carrier>);
static_assert(!HasBrand<NotABrand>, "a brand_type that is not an empty class is not a brand");
static_assert(SameBrand<Branded, Branded const&>);
static_assert(!SameBrand<Branded, BrandedFresh>);
static_assert(!SameBrand<Branded, Carrier>);
static_assert(IsErased<Branded>);
static_assert(!IsBranded<Branded>);
static_assert(IsBranded<BrandedFresh>);
static_assert(std::is_same_v<inherited_or_fresh_brand_t<Carrier, int>, int>);
static_assert(std::is_same_v<inherited_or_fresh_brand_t<BrandedFresh, int>, BrandedFresh::brand_type>);
static_assert(std::is_same_v<inherited_or_fresh_brand_t<BrandedFresh const&, int>, BrandedFresh::brand_type>);

}  // namespace detail::brand_self_test

}  // namespace foundation::brand

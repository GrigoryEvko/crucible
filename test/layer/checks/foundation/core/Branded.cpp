// The compile-time checks of foundation/core/Branded.h.

#include <foundation/core/Branded.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::branded_checks {

// A brand that the checks name.  A program gets each brand from the mint.
struct CheckBrand final {};
struct OtherBrand final {};

// An index has 4 bytes, and its Option has 4 bytes too, because the value
// 0xFFFFFFFF is no index.
static_assert(sizeof(BrandedIndex<CheckBrand>) == 4);
static_assert(sizeof(Option<BrandedIndex<CheckBrand>>) == 4);
static_assert(std::is_trivially_destructible_v<BrandedIndex<CheckBrand>>);

// An index of one brand is no index of a different brand, and no number
// becomes an index.
static_assert(!std::is_convertible_v<BrandedIndex<CheckBrand>, BrandedIndex<OtherBrand>>);
static_assert(!std::is_constructible_v<BrandedIndex<CheckBrand>, std::uint32_t>);
static_assert(!std::is_constructible_v<BrandedView<int, CheckBrand>, int*, std::uint32_t>);

// The body of a scope gives a value that does not hold the brand.
struct GivesCount final {
    template <class Brand>
    constexpr std::size_t operator()(BrandedView<int, Brand> view) const noexcept {
        return view.size();
    }
};
struct GivesIndex final {
    template <class Brand>
    constexpr Option<BrandedIndex<Brand>> operator()(BrandedView<int, Brand> view) const noexcept {
        return view.index(0);
    }
};
struct GivesNothing final {
    template <class Brand>
    constexpr void operator()(BrandedView<int, Brand>) const noexcept {}
};
static_assert(CanMintBrandedScope<int, GivesCount, CheckBrand>);
static_assert(!CanMintBrandedScope<int, GivesIndex, CheckBrand>);
static_assert(!CanMintBrandedScope<int, GivesNothing, CheckBrand>);
static_assert(names_brand(^^Record<int, Option<BrandedIndex<CheckBrand>>>, ^^CheckBrand));
static_assert(!names_brand(^^Record<int, Option<BrandedIndex<CheckBrand>>>, ^^OtherBrand));
static_assert(names_brand(^^CheckBrand const&, ^^CheckBrand));

// A scope in a constant evaluation: index, at, indices and enumerate.
[[nodiscard]] consteval int scope_reads() noexcept {
    FixedArray<int, 5> values{10, 20, 30, 40, 50};
    View<int> const elements = values.view().window(1, 3).expect("three elements fit");
    return mint_branded_scope(elements, [](auto view) noexcept {
        int total = 0;
        for (auto const index : view.indices()) {
            total += view.at(index);
        }
        for (auto const [index, element] : view.enumerate()) {
            *element += static_cast<int>(index.raw());
        }
        int const first = view.at(view.index(0).expect("0 is below the count"));
        bool const is_refused = view.index(3).is_none();
        return is_refused ? total + first : -1;
    });
}
static_assert(scope_reads() == 20 + 30 + 40 + 20);

}  // namespace detail::branded_checks

}  // namespace foundation::core

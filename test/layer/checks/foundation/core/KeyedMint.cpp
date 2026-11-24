// The compile-time checks of foundation/core/KeyedMint.h.

#include <foundation/core/KeyedMint.h>

#include <foundation/core/Array.h>

#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::keyed_mint_checks {

struct CheckBrand final {};

// The proof of a brand is empty and copies, no caller makes one, and no copy
// of bytes builds one.
static_assert(std::is_empty_v<MintedBrand<CheckBrand>>
              && std::is_trivially_copy_constructible_v<MintedBrand<CheckBrand>>);
static_assert(!std::is_default_constructible_v<MintedBrand<CheckBrand>>);
static_assert(!std::is_trivially_copyable_v<MintedBrand<CheckBrand>>);
static_assert(std::is_same_v<MintedBrand<CheckBrand>::brand_type, CheckBrand>);

// A maker whose owner has no key brand, and a maker whose owner has a brand
// other than the brand of the mint.
struct UnkeyedOwner final {
    template <class Brand>
    [[nodiscard]] constexpr FixedArray<int, 4> operator()(MintedBrand<Brand> /*proof*/) const noexcept {
        return {};
    }
};

struct OtherBrand final {};

struct OwnerOfOtherBrand final {
    template <class Brand>
    [[nodiscard]] constexpr FixedArray<int, 4, OtherBrand> operator()(MintedBrand<Brand> /*proof*/) const noexcept {
        return {};
    }
};

// A maker that can throw, and a maker that gives a reference to an owner.
struct ThrowingOwner final {
    template <class Brand>
    [[nodiscard]] constexpr FixedArray<int, 4, Brand> operator()(MintedBrand<Brand> /*proof*/) const {
        return {};
    }
};

struct ReferenceOwner final {
    template <class Brand>
    [[nodiscard]] FixedArray<int, 4, Brand>& operator()(MintedBrand<Brand> /*proof*/) const noexcept;
};

// The gate takes the maker of a family, and refuses each maker above.
static_assert(KeyedOwnerMake<FixedArrayOwner<int, 4>, CheckBrand>);
static_assert(!KeyedOwnerMake<UnkeyedOwner, CheckBrand>);
static_assert(!KeyedOwnerMake<OwnerOfOtherBrand, CheckBrand>);
static_assert(!KeyedOwnerMake<ThrowingOwner, CheckBrand>);
static_assert(!KeyedOwnerMake<ReferenceOwner, CheckBrand>);

// The owner and the key of one mint carry one brand, and two mints give two
// brands.
consteval bool mints_pair_one_brand() {
    auto [first, first_key] = mint_keyed_owner(fixed_array_owner<int, 4>);
    auto [second, second_key] = mint_keyed_owner(fixed_array_owner<int, 4>);
    using FirstBrand = typename std::remove_cvref_t<decltype(first_key)>::brand_type;
    using SecondBrand = typename std::remove_cvref_t<decltype(second_key)>::brand_type;
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(first)>, FixedArray<int, 4, FirstBrand>>);
    static_assert(!std::is_same_v<FirstBrand, SecondBrand>);
    first_key = first.with_mut(std::move(first_key), [](auto run) noexcept {
        for (int& element : run) {
            element = 3;
        }
    });
    static_cast<void>(second_key);
    return first.at(index<3>()) == 3 && second.at(index<0>()) == 0;
}
static_assert(mints_pair_one_brand());

}  // namespace detail::keyed_mint_checks

}  // namespace foundation::core

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

// A held brand and its holder: the proof has a private constructor, its one
// friend is the holder, and the holder keeps its key beside its owner.
class CheckHolder;

struct CheckHeldBrand final {
    class key_proof final {
        friend class CheckHolder;

        constexpr key_proof() noexcept {}

    public:
        key_proof(key_proof const&) = delete("a proof of a held key stays with its holder");
        key_proof& operator=(key_proof const&) = delete("a proof of a held key stays with its holder");
    };
};

class CheckHolder final {
public:
    [[nodiscard]] constexpr int fill_and_read() noexcept {
        array_.with_mut(key_, [](auto run) noexcept {
            for (int& element : run) {
                element = 5;
            }
        });
        return array_.at(index<2>());
    }

private:
    FixedArray<int, 4, CheckHeldBrand> array_{};
    [[no_unique_address]] LoanKey<CheckHeldBrand> key_{mint_held_key<CheckHeldBrand>(CheckHeldBrand::key_proof{})};
};

// A brand with no proof, a proof with a public constructor, a proof that
// copies, a proof with a member, and a dispatch brand are no held brands.
struct NoProofBrand final {};

struct PublicProofBrand final {
    struct key_proof final {
        constexpr key_proof() noexcept {}
        key_proof(key_proof const&) = delete("a proof of a held key stays with its holder");
    };
};

struct CopiedProofBrand final {
    class key_proof final {
        constexpr key_proof() noexcept {}
    };
};

struct FullProofBrand final {
    class key_proof final {
        constexpr key_proof() noexcept {}
        int value_ = 0;

    public:
        key_proof(key_proof const&) = delete("a proof of a held key stays with its holder");
    };
};

struct[[= dispatch_brand{}]] DispatchHeldBrand final {
    class key_proof final {
        constexpr key_proof() noexcept {}

    public:
        key_proof(key_proof const&) = delete("a proof of a held key stays with its holder");
    };
};

static_assert(HeldKeyBrand<CheckHeldBrand>);
static_assert(!HeldKeyBrand<NoProofBrand>);
static_assert(!HeldKeyBrand<PublicProofBrand>);
static_assert(!HeldKeyBrand<CopiedProofBrand>);
static_assert(!HeldKeyBrand<FullProofBrand>);
static_assert(!HeldKeyBrand<DispatchHeldBrand>);
static_assert(!HeldKeyBrand<Unkeyed>);

// The key of the holder costs no byte, and it opens the frame of its owner.
static_assert(sizeof(CheckHolder) == sizeof(FixedArray<int, 4, CheckHeldBrand>));
consteval bool holder_opens_its_owner() {
    CheckHolder holder{};
    return holder.fill_and_read() == 5;
}
static_assert(holder_opens_its_owner());

}  // namespace detail::keyed_mint_checks

}  // namespace foundation::core

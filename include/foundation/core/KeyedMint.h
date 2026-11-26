#pragma once

// The mint of a keyed in-place owner and its key (the loan key of
// foundation/core/Frame.h).
//
//   auto [array, key] = mint_keyed_owner(fixed_array_owner<int, 64>);
//   auto [ring, key] = mint_keyed_owner(fixy::cyclic_buffer_owner<int, 16>);
//   auto [block, key] = mint_keyed_owner(foundation::keyed_bytes_owner, std::move(bytes));
//
// The mint gives a Record of the owner of the fresh brand of its call site and
// the one LoanKey of that brand, which a body uses to open the frames of the
// owner.  The maker of the family of the owner builds the owner: a stateless
// callable that takes the proof MintedBrand<Brand> and the arguments of the
// mint, and gives the owner of the brand as a prvalue.  Each family names its
// maker beside its owner, such as fixed_array_owner of foundation/core/Array.h,
// so a new family needs no mint of its own.  The Record takes the owner as a
// prvalue part, so an owner that does not move, such as a transaction log,
// comes back with no move.
//
// The brand is the fresh brand of the call site (CRUCIBLE_FRESH_BRAND), in
// undeducible_brand_t, so no caller names it, and no target type sets it.  The
// pack refuses each explicit argument.  The brand names the site, and not one
// object: two runs of the site give two owners of one brand (the brand of a
// key of foundation/core/Frame.h).
//
// MintedBrand<Brand> is the proof that the mint of the brand runs.  Its one
// constructor is private, and only this mint makes one, so a maker builds an
// owner only inside a mint.
//
// Complexity: the cost of the maker.
//
// Held key
//   A class that holds a keyed owner as a member, such as the replay context of
//   a Vigil, names the brand of the owner in the type of the member.  No class
//   names a fresh brand, so the brand of such an owner is a named class, and a
//   key of it comes from mint_held_key:
//     key_{mint_held_key<HeldBrand>(HeldBrand::key_proof{})}
//   The brand names an empty class key_proof that no code builds with no
//   argument and that does not copy or move (HeldKeyBrand).  Its constructor is
//   private, and its friends are the holders of the brand, so only a holder
//   mints a key of the brand.  One key opens each owner of its brand (the brand
//   of a key of foundation/core/Frame.h).  So a holder keeps its key private and
//   opens only its own owner with it, as the holder of a held split keeps its
//   split private (fixy/concurrent/SpscEnds.h).  The gate of each keyed door
//   refuses a body that reaches a key of the brand, so no code in a body opens
//   a second frame of an owner of the brand.  A dispatch brand is no held brand:
//   only the dispatch door mints its keys.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/core/Frame.h>
#include <foundation/core/LoanKeyTrait.h>
#include <foundation/core/Record.h>

#include <meta>
#include <type_traits>

namespace foundation::core {

class KeyedMintFrameKeyDoor;

// The proof that the mint of a keyed owner of the brand runs.  The class is
// empty and copies.  Its one constructor is private, and the door of the mint
// is its one friend.  The seal makes its assignment user-provided, so no copy
// of bytes builds a proof.
template <class Brand>
class [[nodiscard]] MintedBrand final {
    friend class KeyedMintFrameKeyDoor;

    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr MintedBrand() noexcept = default;

public:
    using brand_type = Brand;
};

namespace detail {

// True when the type is a keyed in-place owner of the brand: a complete class
// with loan_key_mark of foundation/core/LoanKeyTrait.h whose template arguments
// name the brand, and no LoanKey.
[[nodiscard]] consteval bool is_keyed_owner_of_(std::meta::info type, std::meta::info brand) {
    std::meta::info const bare = std::meta::dealias(std::meta::remove_cv(type));
    return !std::meta::is_reference_type(type) && !is_loan_key_type_(bare) && is_owner_or_key_of_(bare, brand, brand);
}

}  // namespace detail

// The owner that a maker gives for the brand and the arguments of a mint.
template <class Make, class Brand, class... Arguments>
using keyed_owner_t = std::invoke_result_t<Make const&, MintedBrand<Brand>, Arguments&&...>;

// A maker of a keyed owner for the brand and the arguments: a stateless
// callable that holds no linear value, takes the proof of the brand and the
// arguments with no throw, and gives a keyed in-place owner of the brand as a
// prvalue.
template <class Make, class Brand, class... Arguments>
concept KeyedOwnerMake =
    std::is_empty_v<Make> && ::foundation::linear::HoldsNoLinearValue<Make>
    && std::is_nothrow_invocable_v<Make const&, MintedBrand<Brand>, Arguments&&...>
    && detail::is_keyed_owner_of_(^^keyed_owner_t<Make, Brand, Arguments...>, std::meta::dealias(^^Brand));

// The mint of a keyed owner (the definition below).  KeyedMintFrameKeyDoor
// names it as a friend, so this declaration stands before the door, and the
// default argument of the brand belongs to it.
template <class... Refused, class Make, class... Arguments, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
    requires(sizeof...(Refused) == 0) && KeyedOwnerMake<Make, Fresh, Arguments...>
[[nodiscard]] constexpr Record<keyed_owner_t<Make, ::foundation::brand::undeducible_brand_t<Fresh>, Arguments...>,
                               LoanKey<::foundation::brand::undeducible_brand_t<Fresh>>>
mint_keyed_owner(Make const& make, Arguments&&... arguments) noexcept;

// A held brand (the held key): a named key brand that is no dispatch brand, and
// that names an empty class key_proof that no code builds with no argument and
// that does not copy or move.
template <class Brand>
concept HeldKeyBrand = IsKeyBrand<Brand> && !IsDispatchBrand<Brand> && requires { typename Brand::key_proof; }
                    && std::is_class_v<typename Brand::key_proof> && std::is_empty_v<typename Brand::key_proof>
                    && !std::is_default_constructible_v<typename Brand::key_proof>
                    && !std::is_copy_constructible_v<typename Brand::key_proof>
                    && !std::is_move_constructible_v<typename Brand::key_proof>;

// The mint of a held key (the definition below).  KeyedMintFrameKeyDoor names it
// as a friend, so this declaration stands before the door.
template <class Brand>
    requires HeldKeyBrand<Brand>
[[nodiscard]] constexpr LoanKey<Brand> mint_held_key(typename Brand::key_proof const& proof) noexcept;

// The door of the mint of a keyed owner to the key of the frame doors (the
// doors of the owners above foundation/core/Frame.h).  Its members are private,
// and mint_keyed_owner and mint_held_key are its friends.  The invariant:
// mint_keyed_owner gives the one key of the fresh brand of its call, beside the
// owner of that brand that the maker builds from the proof of the brand, and
// mint_held_key gives a key of a held brand only for its proof, which only a
// holder of the brand builds.  A home owner row of utils/rule-table.txt keeps
// this definition in this header.
class KeyedMintFrameKeyDoor final : ::foundation::NoObject<KeyedMintFrameKeyDoor> {
    template <class... Refused, class Make, class... Arguments, ::foundation::brand::IsBrand Fresh>
        requires(sizeof...(Refused) == 0) && KeyedOwnerMake<Make, Fresh, Arguments...>
    friend constexpr auto ::foundation::core::mint_keyed_owner(Make const& make, Arguments&&... arguments) noexcept
        -> Record<keyed_owner_t<Make, ::foundation::brand::undeducible_brand_t<Fresh>, Arguments...>,
                  LoanKey<::foundation::brand::undeducible_brand_t<Fresh>>>;
    template <class Brand>
        requires HeldKeyBrand<Brand>
    friend constexpr auto ::foundation::core::mint_held_key(typename Brand::key_proof const& proof) noexcept
        -> LoanKey<Brand>;

    // The key of the frame doors, for the key of a mint.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }

    // The proof of the brand of a mint.
    template <class Brand>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr MintedBrand<Brand> proof_() noexcept {
        return MintedBrand<Brand>{};
    }
};

// A keyed owner of the fresh brand of the call site and its LoanKey: auto
// [owner, key] = mint_keyed_owner(maker, arguments...).  The maker builds the
// owner from the proof of the brand and the arguments.  The owner and the key
// come back as prvalue parts of the Record.  Complexity: the cost of the
// maker.
template <class... Refused, class Make, class... Arguments, ::foundation::brand::IsBrand Fresh>
    requires(sizeof...(Refused) == 0) && KeyedOwnerMake<Make, Fresh, Arguments...>
[[nodiscard]] constexpr Record<keyed_owner_t<Make, ::foundation::brand::undeducible_brand_t<Fresh>, Arguments...>,
                               LoanKey<::foundation::brand::undeducible_brand_t<Fresh>>>
mint_keyed_owner(Make const& make, Arguments&&... arguments) noexcept {
    return {make(KeyedMintFrameKeyDoor::proof_<Fresh>(), static_cast<Arguments&&>(arguments)...),
            LoanKey<Fresh>{KeyedMintFrameKeyDoor::key_()}};
}

// A key of a held brand, for the holder that builds the proof of the brand (the
// held key).  The holder keeps the key as a member beside its owner.
// Complexity: O(1), and the key costs no byte.
template <class Brand>
    requires HeldKeyBrand<Brand>
[[nodiscard]] constexpr LoanKey<Brand> mint_held_key(typename Brand::key_proof const& /*proof*/) noexcept {
    return LoanKey<Brand>{KeyedMintFrameKeyDoor::key_()};
}

// A maker that holds a linear value by value binds to this overload, so the
// refusal gives its reason.
template <class... Refused, class Make, class... Arguments>
    requires(!::foundation::linear::HoldsNoLinearValue<Make>)
void mint_keyed_owner(Make const& make, Arguments&&... arguments) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

}  // namespace foundation::core

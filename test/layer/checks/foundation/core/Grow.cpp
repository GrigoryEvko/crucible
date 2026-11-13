// The compile-time checks of foundation/core/Grow.h.

#include <foundation/core/Grow.h>

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/core/Ref.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::grow_checks {

// A linear element: it moves, it does not copy, and the seal makes its
// assignment user-provided, so it is not trivially copyable.
struct[[= ::foundation::linear::linear{}]] LinearToken final {
    int id = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    constexpr explicit LinearToken(int from) noexcept : id{from} {}
    constexpr LinearToken(LinearToken&& other) noexcept : id{other.id} {}
    constexpr LinearToken& operator=(LinearToken&& other) noexcept {
        id = other.id;
        return *this;
    }
    LinearToken(LinearToken const&) = delete;
};

// An element whose move can throw: a growth could lose an element, so the
// list refuses it.
struct ThrowingMove final {
    int value = 0;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
};

// An owner holds a pointer and three counts.  It moves and does not copy,
// and a list of a linear element is linear and has no clear().
static_assert(sizeof(GrowList<int>) == 4 * sizeof(std::size_t));
static_assert(sizeof(GrowText) == 4 * sizeof(std::size_t));
static_assert(std::is_nothrow_move_constructible_v<GrowList<int>> && !std::is_copy_constructible_v<GrowList<int>>);
static_assert(std::is_nothrow_move_assignable_v<GrowText> && !std::is_copy_assignable_v<GrowText>);
static_assert(!std::is_default_constructible_v<GrowList<int>> && !std::is_default_constructible_v<GrowText>);
static_assert(::foundation::linear::is_linear_v<GrowList<LinearToken>>);
static_assert(!::foundation::linear::is_linear_v<GrowList<int>>);
static_assert(GrowListElement<int> && GrowListElement<GrowList<int>> && GrowListElement<Box<int>>);
static_assert(!GrowListElement<ThrowingMove> && !GrowListElement<int const>);

template <class List>
concept Clears = requires(List& list) { list.clear(); };
static_assert(Clears<GrowList<int>> && !Clears<GrowList<LinearToken>> && Clears<GrowText>);

// An append takes the allocation capability first, and a different tag is
// refused.  An append, a view and a read of a temporary owner are refused.
template <class Owner, class Marker, class Value>
concept Appends = requires(Owner& owner, Marker marker, Value value) { owner.append(marker, value); };
struct OtherTag final {};
static_assert(Appends<GrowList<int>, Alloc, int> && !Appends<GrowList<int>, OtherTag, int>);
static_assert(Appends<GrowText, Alloc, char> && Appends<GrowText, Alloc, TextView>);
static_assert(Appends<GrowText, Alloc, View<char const>> && Appends<GrowText, Alloc, View<char>>);
static_assert(!Appends<GrowText, OtherTag, TextView>);
template <class Owner>
concept AppendsToTemporary = requires { std::declval<Owner>().append(Alloc{}, 1); };
template <class Owner>
concept ViewsTemporary = requires { std::declval<Owner>().view(); };
template <class Owner>
concept ReadsTemporary = requires { std::declval<Owner>().get(0); };
static_assert(!AppendsToTemporary<GrowList<int>> && !ViewsTemporary<GrowList<int>> && !ReadsTemporary<GrowList<int>>);
static_assert(!ViewsTemporary<GrowList<int> const> && !ViewsTemporary<GrowText>);

// The mint gives an empty owner with no storage, also in a constant
// evaluation, and a maximum past the largest View becomes that count.
[[nodiscard]] consteval bool minted_owners_are_empty() noexcept {
    GrowList<int> const list = mint_grow_list<int>(8);
    GrowText const text = mint_grow_text(~std::size_t{0});
    return list.size() == 0 && list.view().size() == 0 && list.get(0).is_none() && text.size() == 0
        && text.view().size() == 0;
}
static_assert(minted_owners_are_empty());

}  // namespace detail::grow_checks

}  // namespace foundation::core

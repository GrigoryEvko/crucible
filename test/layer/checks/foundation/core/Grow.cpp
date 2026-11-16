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

// A pinned element that writes a move of its own.  Its address is its
// identity, so the list refuses it by its type, as it refuses an Atomic.
struct MovedPin final : ::foundation::Pinned<MovedPin> {
    int value = 0;
    MovedPin() noexcept = default;
    MovedPin(MovedPin&& other) noexcept : ::foundation::Pinned<MovedPin>{}, value{other.value} {}
};

// A growable owner holds a pointer, three counts and a word of its lend
// count and its padding.  A HeapText holds a pointer and two counts.  Each
// owner moves and does not copy, and a list of a linear element is linear and
// has no clear().
static_assert(sizeof(GrowList<int>) == 5 * sizeof(std::size_t));
static_assert(sizeof(GrowText) == 5 * sizeof(std::size_t));
static_assert(sizeof(HeapText) == 3 * sizeof(std::size_t));
static_assert(std::is_nothrow_move_constructible_v<GrowList<int>> && !std::is_copy_constructible_v<GrowList<int>>);
static_assert(std::is_nothrow_move_assignable_v<GrowText> && !std::is_copy_assignable_v<GrowText>);
static_assert(std::is_nothrow_move_assignable_v<HeapText> && !std::is_copy_constructible_v<HeapText>);
static_assert(!std::is_default_constructible_v<GrowList<int>> && !std::is_default_constructible_v<GrowText>);
static_assert(!std::is_default_constructible_v<HeapText>);
static_assert(OwnerState<owner::GrowListState<int>> && OwnerState<owner::GrowTextState>
              && OwnerState<owner::HeapTextState>);

// Only an rvalue of a GrowText freezes, and the freeze gives a HeapText.
template <class Text>
concept Freezes = requires(Text&& text) {
    { freeze(static_cast<Text&&>(text)) } -> std::same_as<HeapText>;
};
static_assert(Freezes<GrowText> && !Freezes<GrowText&> && !Freezes<GrowText const&> && !Freezes<GrowText const>);

// A frame door gives the result of its body when that result holds no
// borrow, and a temporary owner lends nothing.
template <class Owner, class Body>
concept Lends = requires(Owner&& owner, Body body) { static_cast<Owner&&>(owner).with_view(body); };
struct CountsElements final {
    [[nodiscard]] constexpr std::size_t operator()(View<int const> view) const noexcept { return view.size(); }
};
struct GivesElements final {
    [[nodiscard]] constexpr View<int const> operator()(View<int const> view) const noexcept { return view; }
};
struct CountsCharacters final {
    [[nodiscard]] constexpr std::size_t operator()(TextView text) const noexcept { return text.size(); }
};
struct GivesCharacters final {
    [[nodiscard]] constexpr TextView operator()(TextView text) const noexcept { return text; }
};
static_assert(Lends<GrowList<int>&, CountsElements> && Lends<GrowList<int> const&, CountsElements>);
static_assert(!Lends<GrowList<int>&, GivesElements> && !Lends<GrowList<int>, CountsElements>);
static_assert(Lends<GrowText&, CountsCharacters> && !Lends<GrowText&, GivesCharacters>);
static_assert(!Lends<GrowText, CountsCharacters>);
static_assert(::foundation::linear::is_linear_v<GrowList<LinearToken>>);
static_assert(!::foundation::linear::is_linear_v<GrowList<int>>);
static_assert(GrowListElement<int> && GrowListElement<GrowList<int>> && GrowListElement<Box<int>>);
static_assert(!GrowListElement<ThrowingMove> && !GrowListElement<int const>);
static_assert(std::is_nothrow_move_constructible_v<MovedPin> && !GrowListElement<MovedPin>);
static_assert(!GrowListElement<Atomic<int>> && GrowListElement<Box<Atomic<int>>> && GrowListElement<Box<MovedPin>>);

template <class List>
concept Clears = requires(List& list) { list.clear(); };
static_assert(Clears<GrowList<int>> && !Clears<GrowList<LinearToken>> && Clears<GrowText>);

// An append takes the allocation capability first, and a different tag is
// refused.  An append to a temporary owner is refused.
template <class Owner, class Marker, class Value>
concept Appends = requires(Owner& owner, Marker marker, Value value) { owner.append(marker, value); };
struct OtherTag final {};
static_assert(Appends<GrowList<int>, Alloc, int> && !Appends<GrowList<int>, OtherTag, int>);
static_assert(Appends<GrowText, Alloc, char> && Appends<GrowText, Alloc, TextView>);
static_assert(Appends<GrowText, Alloc, View<char const>> && Appends<GrowText, Alloc, View<char>>);
static_assert(!Appends<GrowText, OtherTag, TextView>);
template <class Owner>
concept AppendsToTemporary = requires { std::declval<Owner>().append(std::declval<Alloc>(), 1); };
static_assert(!AppendsToTemporary<GrowList<int>>);

// A growable owner gives no borrow out of a frame: no view(), no get() and no
// cursor.  A HeapText keeps view() and its loop, because no growth ends them.
template <class Owner>
concept Views = requires(Owner& owner) { owner.view(); };
template <class Owner>
concept Reads = requires(Owner& owner) { owner.get(0); };
template <class Owner>
concept Loops = requires(Owner& owner) { owner.begin(); };
static_assert(!Views<GrowList<int>> && !Reads<GrowList<int>> && !Loops<GrowList<int>>);
static_assert(!Views<GrowList<int> const> && !Reads<GrowList<int> const> && !Loops<GrowList<int> const>);
static_assert(!Views<GrowText> && !Loops<GrowText> && Views<HeapText const> && Loops<HeapText const>);

// The mint gives an empty owner with no storage, also in a constant
// evaluation, and a maximum past the largest View becomes that count.  The
// frame doors read an empty owner, and the freeze of an empty text gives an
// empty HeapText.
[[nodiscard]] consteval bool minted_owners_are_empty() noexcept {
    GrowList<int> const list = mint_grow_list<int>(8);
    GrowText text = mint_grow_text(~std::size_t{0});
    bool const is_list_empty =
        list.size() == 0 && list.with_view(CountsElements{}) == 0
        && list.with_element(0, [](int const&) noexcept { return false; }, [] noexcept { return true; });
    bool const is_text_empty = text.size() == 0 && text.with_view(CountsCharacters{}) == 0;
    HeapText const frozen = freeze(static_cast<GrowText&&>(text));
    return is_list_empty && is_text_empty && frozen.size() == 0 && frozen.view().size() == 0;
}
static_assert(minted_owners_are_empty());

}  // namespace detail::grow_checks

}  // namespace foundation::core

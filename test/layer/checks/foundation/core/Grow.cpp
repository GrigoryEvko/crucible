// The compile-time checks of foundation/core/Grow.h.

#include <foundation/core/Grow.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace foundation::core {

namespace detail::grow_checks {

// A GrowText holds a pointer, three counts and a word of its lend count and
// its padding.  A HeapText holds a pointer and two counts.  Each text moves and
// does not copy.
static_assert(sizeof(GrowText) == 5 * sizeof(std::size_t));
static_assert(sizeof(HeapText) == 3 * sizeof(std::size_t));
static_assert(std::is_nothrow_move_assignable_v<GrowText> && !std::is_copy_assignable_v<GrowText>);
static_assert(std::is_nothrow_move_assignable_v<HeapText> && !std::is_copy_constructible_v<HeapText>);
static_assert(!std::is_default_constructible_v<GrowText> && !std::is_default_constructible_v<HeapText>);
static_assert(OwnerState<owner::GrowTextState> && OwnerState<owner::HeapTextState>);

// Only an rvalue of a GrowText freezes, and the freeze gives a HeapText.
template <class Text>
concept Freezes = requires(Text&& text) {
    { freeze(static_cast<Text&&>(text)) } -> std::same_as<HeapText>;
};
static_assert(Freezes<GrowText> && !Freezes<GrowText&> && !Freezes<GrowText const&> && !Freezes<GrowText const>);

// The frame door gives the result of its body when that result holds no
// borrow, and a temporary text lends nothing.
template <class Owner, class Body>
concept Lends = requires(Owner&& owner, Body body) { static_cast<Owner&&>(owner).with_view(body); };
struct CountsCharacters final {
    [[nodiscard]] constexpr std::size_t operator()(TextView text) const noexcept { return text.size(); }
};
struct GivesCharacters final {
    [[nodiscard]] constexpr TextView operator()(TextView text) const noexcept { return text; }
};
static_assert(Lends<GrowText&, CountsCharacters> && Lends<GrowText const&, CountsCharacters>);
static_assert(!Lends<GrowText&, GivesCharacters> && !Lends<GrowText, CountsCharacters>);

// An append takes the allocation capability first, and a different tag is
// refused.  An append to a temporary text is refused.
template <class Owner, class Marker, class Value>
concept Appends = requires(Owner& owner, Marker marker, Value value) { owner.append(marker, value); };
struct OtherTag final {};
static_assert(Appends<GrowText, Alloc, char> && Appends<GrowText, Alloc, TextView>);
static_assert(Appends<GrowText, Alloc, View<char const>> && Appends<GrowText, Alloc, View<char>>);
static_assert(!Appends<GrowText, OtherTag, TextView>);
template <class Owner>
concept AppendsToTemporary = requires(Owner&& owner, Alloc marker) { static_cast<Owner&&>(owner).append(marker, 'x'); };
static_assert(AppendsToTemporary<GrowText&> && !AppendsToTemporary<GrowText>);

// A GrowText gives no borrow out of a frame: no view() and no cursor.  A
// HeapText keeps view() and its loop, because no growth ends them.
template <class Owner>
concept Views = requires(Owner& owner) { owner.view(); };
template <class Owner>
concept Loops = requires(Owner& owner) { owner.begin(); };
static_assert(!Views<GrowText> && !Loops<GrowText> && Views<HeapText const> && Loops<HeapText const>);

// The mint gives an empty text with no storage, also in a constant
// evaluation, and a maximum past the largest View becomes that count.  The
// frame door reads an empty text, and the freeze of one gives an empty
// HeapText.
[[nodiscard]] consteval bool minted_text_is_empty() noexcept {
    GrowText text = mint_grow_text(~std::size_t{0});
    bool const is_text_empty = text.size() == 0 && text.with_view(CountsCharacters{}) == 0;
    HeapText const frozen = freeze(static_cast<GrowText&&>(text));
    return is_text_empty && frozen.size() == 0 && frozen.view().size() == 0;
}
static_assert(minted_text_is_empty());

}  // namespace detail::grow_checks

}  // namespace foundation::core

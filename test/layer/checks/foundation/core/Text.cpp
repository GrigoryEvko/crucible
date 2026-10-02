// The compile-time checks of foundation/core/Text.h.

#include <foundation/core/Text.h>

#include <type_traits>

namespace foundation::core {

namespace detail::text_checks {

// A TextView is a pointer and a count.  The seal adds no byte, and it makes
// the view not trivially copyable, so std::bit_cast builds no view.
static_assert(sizeof(TextView) == 2 * sizeof(void*));
static_assert(!std::is_trivially_copyable_v<TextView>);
static_assert(std::is_trivially_copy_constructible_v<TextView> && std::is_trivially_destructible_v<TextView>);

// A literal converts to a TextView, and a pointer does not: a pointer
// carries no count and no proof of storage that outlives the view.
static_assert(std::is_convertible_v<char const (&)[4], TextView>);
static_assert(!std::is_convertible_v<char const*, TextView>);
static_assert(!std::is_constructible_v<TextView, char const*, std::size_t>);

// The count of a literal is its count of characters, without the
// terminating zero.  Two views compare their characters.
static_assert(TextView{}.size() == 0);
static_assert(TextView{""}.size() == 0);
static_assert(TextView{"abc"}.size() == 3);
static_assert(TextView{"abc"} == TextView{"abc"});
static_assert(!(TextView{"abc"} == TextView{"abd"}));
static_assert(!(TextView{"abc"} == TextView{"ab"}));
static_assert(TextView{} == TextView{""});

// A literal with a zero byte before its end fails the consteval
// constructor, so the build stops.  The fixture
// neg_core_text_view_no_terminating_zero holds the array with no zero.
[[nodiscard]] consteval bool forms_view(int which) noexcept {
    switch (which) {
        case 0:
            static_cast<void>(TextView{"abc"});
            return true;
        case 1:
            static_cast<void>(TextView{"a\0c"});
            return true;
        default:
            return false;
    }
}
template <int Which>
concept FormsView = requires { typename std::integral_constant<bool, forms_view(Which)>; };
static_assert(FormsView<0>);
static_assert(!FormsView<1>);

// A FixedText starts empty, and its capacity is its parameter.  A view of a
// temporary FixedText is refused.
static_assert(FixedText<8>{}.size() == 0);
static_assert(FixedText<8>::capacity == 8);
static_assert(sizeof(FixedText<8>) == 8 + sizeof(std::size_t));
static_assert(!std::is_trivially_copyable_v<FixedText<8>>);
template <class Text>
concept ViewsTemporary = requires { static_cast<Text&&>(std::declval<Text>()).view(); };
static_assert(!ViewsTemporary<FixedText<8>>);
static_assert(requires(FixedText<8> const& text) { text.view(); });

// A capacity of zero is no FixedText.
template <std::size_t Capacity>
concept HasFixedText = requires { typename FixedText<Capacity>; };
static_assert(HasFixedText<1>);
static_assert(!HasFixedText<0>);

// The resize door keeps the count inside the buffer.
[[nodiscard]] consteval std::size_t size_after_resize(std::size_t count) noexcept {
    FixedText<4> text;
    text_resize_(text, count);
    return text.size();
}
static_assert(size_after_resize(3) == 3);
static_assert(size_after_resize(9) == 4);

}  // namespace detail::text_checks

}  // namespace foundation::core

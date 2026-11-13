// The compile-time checks of foundation/core/Text.h.

#include <foundation/core/Text.h>

#include <cstdint>
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

// A FixedText holds the characters of a literal that fits it.  A longer
// literal builds no FixedText, and a literal does not convert to one.
static_assert(std::is_constructible_v<FixedText<3>, char const (&)[4]>);
static_assert(!std::is_constructible_v<FixedText<2>, char const (&)[4]>);
static_assert(!std::is_convertible_v<char const (&)[4], FixedText<8>>);
[[nodiscard]] consteval bool holds_its_literal() noexcept {
    FixedText<4> const text{"bbr"};
    return text.size() == 3 && text.view() == TextView{"bbr"};
}
static_assert(holds_its_literal());

// A literal with a zero byte before its end fails the consteval constructor.
[[nodiscard]] consteval bool forms_text(int which) noexcept {
    switch (which) {
        case 0:
            static_cast<void>(FixedText<4>{"abc"});
            return true;
        case 1:
            static_cast<void>(FixedText<4>{"a\0c"});
            return true;
        default:
            return false;
    }
}
template <int Which>
concept FormsText = requires { typename std::integral_constant<bool, forms_text(Which)>; };
static_assert(FormsText<0>);
static_assert(!FormsText<1>);

// A constant expression builds a name and gives it by value in a Result.
[[nodiscard]] constexpr Result<FixedText<4>, Truncated> build_name(TextView name) noexcept {
    FixedText<4> text{};
    if (text.append(name).is_err()) return err(Truncated{});
    return text;
}
[[nodiscard]] consteval bool builds_a_name_in_a_result() noexcept {
    FixedText<4> const fits = build_name("bbr").expect("the name fits");
    return fits.view() == TextView{"bbr"} && build_name("cubic").is_err();
}
static_assert(builds_a_name_in_a_result());

// push() refuses the character past the capacity.  A refused append()
// keeps the characters that the buffer held.
[[nodiscard]] consteval bool fills_and_refuses() noexcept {
    FixedText<2> pair{};
    bool const is_first_kept = pair.push('a').is_ok();
    bool const is_second_kept = pair.push('b').is_ok();
    bool const is_third_refused = pair.push('c').is_err();
    FixedText<4> name{"ab"};
    bool const is_long_refused = name.append("cde").is_err();
    bool const is_name_kept = name.view() == TextView{"ab"};
    bool const is_joined = name.append("cd").is_ok() && name.view() == TextView{"abcd"};
    return is_first_kept && is_second_kept && is_third_refused && pair.view() == TextView{"ab"} && is_long_refused
        && is_name_kept && is_joined;
}
static_assert(fills_and_refuses());

// append_hex writes the low digits with the zeros that lead, and refuses
// digits past the capacity with no change.
[[nodiscard]] consteval bool appends_hex_digits() noexcept {
    FixedText<6> text{"h:"};
    bool const is_written = append_hex<4>(text, 0x12abU).is_ok();
    bool const is_refused = append_hex<1>(text, 1).is_err();
    FixedText<3> padded{};
    bool const is_padded = append_hex<3>(padded, 0xfU).is_ok() && padded.view() == TextView{"00f"};
    return is_written && is_refused && text.view() == TextView{"h:12ab"} && is_padded;
}
static_assert(appends_hex_digits());

// The name of an enumerator is a TextView of its identifier, or of the
// sentinel when no enumerator holds the value.  A zero follows each text.
// The read of that zero is a constant expression only when the storage
// holds the byte, so a text with no zero fails the build here.
enum class Lane : std::uint8_t {
    Send,
    CopyHostToDevice,
    Alias = Send,
};
[[nodiscard]] consteval bool ends_with_zero(TextView text) noexcept { return text_first_(text)[text.size()] == '\0'; }
static_assert(enum_name_text(Lane::Send) == TextView{"Send"});
static_assert(enum_name_text(Lane::Alias) == TextView{"Send"});
static_assert(enum_name_text(static_cast<Lane>(9)) == TextView{"<unknown Lane>"});
static_assert(enum_words_text<Lane, '-'>(Lane::CopyHostToDevice) == TextView{"copy-host-to-device"});
static_assert(enum_words_text<Lane, '_'>(static_cast<Lane>(9)) == TextView{"<unknown Lane>"});
static_assert(ends_with_zero(enum_name_text(Lane::CopyHostToDevice)));
static_assert(ends_with_zero(enum_name_text(static_cast<Lane>(9))));
static_assert(ends_with_zero(enum_words_text<Lane, '_'>(Lane::CopyHostToDevice)));
static_assert(ends_with_zero(enum_words_text<Lane, '_'>(static_cast<Lane>(9))));

}  // namespace detail::text_checks

}  // namespace foundation::core

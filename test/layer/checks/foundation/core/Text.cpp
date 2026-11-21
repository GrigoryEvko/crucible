// The compile-time checks of foundation/core/Text.h.

#include <foundation/core/Text.h>

#include <cstdint>
#include <meta>
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

// A static text gives the view of its characters before its zero: the text
// of a literal, and of std::define_static_string, which is the static text
// of a reflected name.
static_assert(mint_static_text("abc") == TextView{"abc"});
static_assert(mint_static_text("").size() == 0);
static_assert(mint_static_text(std::define_static_string("reflected")) == TextView{"reflected"});
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
[[nodiscard]] constexpr Result<FixedText<4>, Full<char>> build_name(TextView name) noexcept {
    FixedText<4> text{};
    if (text.append(name).is_err()) return err(Full<char>{});
    return text;
}
[[nodiscard]] consteval bool builds_a_name_in_a_result() noexcept {
    FixedText<4> const fits = build_name("bbr").expect("the name fits");
    return fits.view() == TextView{"bbr"} && build_name("cubic").is_err();
}
static_assert(builds_a_name_in_a_result());

// append() of a character refuses the character past the capacity.  A
// refused append() of a text keeps the characters that the buffer held.
[[nodiscard]] consteval bool fills_and_refuses() noexcept {
    FixedText<2> pair{};
    bool const is_first_kept = pair.append('a').is_ok();
    bool const is_second_kept = pair.append('b').is_ok();
    bool const is_third_refused = pair.append('c').is_err();
    FixedText<4> name{"ab"};
    bool const is_long_refused = name.append("cde").is_err();
    bool const is_name_kept = name.view() == TextView{"ab"};
    bool const is_joined = name.append("cd").is_ok() && name.view() == TextView{"abcd"};
    return is_first_kept && is_second_kept && is_third_refused && pair.view() == TextView{"ab"} && is_long_refused
        && is_name_kept && is_joined;
}
static_assert(fills_and_refuses());

// The one verb of a character is append, and its error is Full<char>.
template <class Text>
concept HasPush = requires(Text& text) { text.push('a'); };
static_assert(!HasPush<FixedText<2>>);
static_assert(std::is_same_v<decltype(std::declval<FixedText<2>&>().append('a')), Result<Unit, Full<char>>>);
static_assert(std::is_same_v<decltype(std::declval<FixedText<2>&>().append(TextView{"a"})), Result<Unit, Full<char>>>);

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

// The sentinel is a constant, the search of the identifier gives no value for
// a value that no enumerator holds, and the walk gives each identifier in
// declaration order, an alias too.  A zero follows each text.
static_assert(unknown_enum_text<Lane> == TextView{"<unknown Lane>"});
static_assert(ends_with_zero(unknown_enum_text<Lane>));
static_assert(enumerator_name_text(Lane::Alias).expect("an enumerator holds the value") == TextView{"Send"});
static_assert(enumerator_name_text(static_cast<Lane>(9)).is_none());
static_assert(ends_with_zero(enumerator_name_text(Lane::CopyHostToDevice).expect("an enumerator holds the value")));
[[nodiscard]] consteval bool walks_each_identifier_in_order() noexcept {
    FixedText<32> names{};
    bool is_whole = true;
    for_each_enumerator_text<Lane>([&names, &is_whole](Lane, TextView name) noexcept {
        if (names.size() != 0) is_whole = is_whole && names.append(TextView{","}).is_ok();
        is_whole = is_whole && ends_with_zero(name) && names.append(name).is_ok();
    });
    return is_whole && names.view() == TextView{"Send,CopyHostToDevice,Alias"};
}
static_assert(walks_each_identifier_in_order());

// The search of an enumerator by its identifier.  An alias gives the value of
// the enumerator that it names, and a name of no enumerator, also a prefix of
// one, gives no value.  An enum of no enumerators gives no value.
enum class NoLane : std::uint8_t {
};
static_assert(enumerator_named<Lane>("CopyHostToDevice").expect("Lane declares it") == Lane::CopyHostToDevice);
static_assert(enumerator_named<Lane>("Alias").expect("Lane declares it") == Lane::Send);
static_assert(enumerator_named<Lane>("Copy").is_none() && enumerator_named<Lane>("").is_none());
static_assert(enumerator_named<Lane>("send").is_none() && enumerator_named<NoLane>("Send").is_none());
static_assert(has_enumerator_named<Lane>("Alias") && !has_enumerator_named<Lane>("Copy"));
static_assert(!has_enumerator_named<NoLane>("Send"));

// The identifier of a reflected entity: an enum, an enumerator, an alias
// enumerator by its own identifier, a class template and a data member.  A
// zero follows each text.
template <class Payload>
struct Carrier {
    Payload payload_field{};
};
static_assert(identifier_text(^^Lane) == TextView{"Lane"});
static_assert(identifier_text(^^Lane::CopyHostToDevice) == TextView{"CopyHostToDevice"});
static_assert(identifier_text(^^Lane::Alias) == TextView{"Alias"});
static_assert(identifier_text(^^Carrier) == TextView{"Carrier"});
static_assert(identifier_text(^^Carrier<int>::payload_field) == TextView{"payload_field"});
static_assert(ends_with_zero(identifier_text(^^Lane)) && ends_with_zero(identifier_text(^^Lane::Alias)));

}  // namespace detail::text_checks

}  // namespace foundation::core

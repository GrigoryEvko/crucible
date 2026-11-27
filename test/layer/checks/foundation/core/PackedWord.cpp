// The compile-time checks of foundation/core/PackedWord.h.

#include <foundation/core/PackedWord.h>

#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace detail::packed_word_checks {

struct Tags final {};
struct Data final {};
struct Other final {};
struct NotFinal {};

using Kinds = PackedWord<std::uint16_t, BitField<Tags, 10>, BitField<Data, 5>>;
using Wide = PackedWord<std::uint64_t, BitField<Tags, 40>, BitField<Data, 24>>;
using Byte = PackedWord<std::uint8_t, BitField<Data, 8>>;

// A tag is an empty final class, and a field has 1 to 64 bits.
static_assert(FieldTag<Tags> && !FieldTag<NotFinal> && !FieldTag<int>);
template <class Tag, std::size_t Width>
concept MakesField = requires { typename BitField<Tag, Width>; };
static_assert(MakesField<Tags, 1> && MakesField<Tags, 64> && !MakesField<Tags, 0> && !MakesField<Tags, 65>);
static_assert(!MakesField<NotFinal, 4>);

// A layout: an unsigned word, at least one field, each tag one time, and the
// widths at most the bits of the word.
static_assert(PackedLayout<std::uint16_t, BitField<Tags, 10>, BitField<Data, 6>>);
static_assert(!PackedLayout<std::uint16_t, BitField<Tags, 10>, BitField<Data, 7>>);
static_assert(!PackedLayout<std::uint16_t, BitField<Tags, 5>, BitField<Tags, 4>>);
static_assert(!PackedLayout<std::int16_t, BitField<Tags, 5>> && !PackedLayout<bool, BitField<Tags, 1>>);
static_assert(!PackedLayout<std::uint16_t, int> && !PackedLayout<std::uint16_t>);

// The size, the alignment and the layout of the word, and a trivial copy.
static_assert(sizeof(Kinds) == 2 && alignof(Kinds) == 2 && sizeof(Wide) == 8 && sizeof(Byte) == 1);
static_assert(std::is_trivially_copyable_v<Kinds> && std::is_standard_layout_v<Kinds>);
static_assert(std::is_trivially_destructible_v<Wide> && !std::is_aggregate_v<Kinds>);
static_assert(!std::is_convertible_v<std::uint16_t, Kinds> && !std::is_constructible_v<Kinds, std::uint16_t>);
static_assert(!std::is_convertible_v<Kinds, std::uint16_t>);

// The word of a field is the narrowest unsigned word of its width.
static_assert(std::is_same_v<Kinds::field_type<Tags>, std::uint16_t>);
static_assert(std::is_same_v<Kinds::field_type<Data>, std::uint8_t>);
static_assert(std::is_same_v<Wide::field_type<Tags>, std::uint64_t>);
static_assert(std::is_same_v<Wide::field_type<Data>, std::uint32_t>);

// get and with take only a tag of the layout.
template <class Word, class Tag>
concept Gets = requires(Word const& word) { word.template get<Tag>(); };
template <class Word, class Tag>
concept Sets = requires(Word const& word) { word.template with<Tag>(0); };
static_assert(Gets<Kinds, Tags> && Gets<Kinds, Data> && !Gets<Kinds, Other>);
static_assert(Sets<Kinds, Tags> && !Sets<Kinds, Other>);
static_assert(std::is_same_v<decltype(Kinds::from_bits(0)), Option<Kinds>>);
static_assert(std::is_same_v<decltype(Kinds{}.with<Data>(0)), Option<Kinds>>);

// with_fitting takes only a tag of the layout and an unsigned value whose type
// has no more bits than the field, and it gives the word itself.
template <class Word, class Tag, class Value>
concept Fits = requires(Word const& word, Value value) { word.template with_fitting<Tag>(value); };
static_assert(Fits<Kinds, Tags, std::uint8_t> && Fits<Kinds, Tags, std::uint8_t const>);
static_assert(!Fits<Kinds, Tags, std::uint16_t> && !Fits<Kinds, Data, std::uint8_t>);
static_assert(!Fits<Kinds, Tags, std::int8_t> && !Fits<Kinds, Tags, bool> && !Fits<Kinds, Tags, char>);
static_assert(!Fits<Kinds, Other, std::uint8_t> && !Fits<Kinds, Tags, int>);
static_assert(Fits<Wide, Tags, std::uint32_t> && !Fits<Wide, Data, std::uint32_t> && Fits<Byte, Data, std::uint8_t>);
static_assert(std::is_same_v<decltype(Kinds{}.with_fitting<Tags>(std::uint8_t{0})), Kinds>);

// with_low_bits takes only a tag of the layout and an unsigned value of each
// width, and it gives the word itself.
template <class Word, class Tag, class Value>
concept TakesLowBits = requires(Word const& word, Value value) { word.template with_low_bits<Tag>(value); };
static_assert(TakesLowBits<Kinds, Data, std::uint8_t> && TakesLowBits<Kinds, Data, std::uint64_t>);
static_assert(!TakesLowBits<Kinds, Data, std::int8_t> && !TakesLowBits<Kinds, Data, int>);
static_assert(!TakesLowBits<Kinds, Data, bool> && !TakesLowBits<Kinds, Other, std::uint8_t>);
static_assert(std::is_same_v<decltype(Kinds{}.with_low_bits<Data>(std::uint32_t{0})), Kinds>);

// A layout with a free bit has a niche, so its Option is one word.  A layout
// that fills its word has none, and its Option holds a flag.
static_assert(sizeof(Option<Kinds>) == sizeof(Kinds) && sizeof(Option<Byte>) > sizeof(Byte));
static_assert(sizeof(Option<Wide>) > sizeof(Wide));

// Each operation, and each refusal.
[[nodiscard]] consteval bool operates() {
    Kinds const word = Kinds::from_bits(0x7ABCu).expect("the word holds only the bits of the fields");
    bool const is_split = word.get<Tags>() == 0x2BCu && word.get<Data>() == 0x1Eu;
    bool const is_word_refused = Kinds::from_bits(0x8000u).is_none() && Wide::from_bits(~std::uint64_t{0}).is_some();
    Kinds const changed = word.with<Data>(std::uint8_t{5}).expect("five bits hold 5");
    bool const is_changed = changed.get<Data>() == 5 && changed.get<Tags>() == 0x2BCu && changed.bits() == 0x16BCu;
    bool const is_value_refused =
        word.with<Data>(std::uint8_t{0x20}).is_none() && word.with<Tags>(std::uint16_t{0x400}).is_none();
    Wide const wide = Wide{}.with<Data>(std::uint32_t{0xFFFFFFu}).expect("24 bits hold the value");
    bool const is_top = wide.bits() == 0xFFFFFF0000000000ull && wide.get<Tags>() == 0;
    Byte const byte = Byte{}.with<Data>(std::uint8_t{0xFF}).expect("a field of the whole word holds each value");
    Kinds const fitted = word.with_fitting<Tags>(std::uint8_t{0x0F});
    bool const is_fitted = fitted.bits() == 0x780Fu && fitted.get<Data>() == 0x1Eu
                        && Byte{}.with_fitting<Data>(std::uint8_t{0xA5}).bits() == 0xA5u
                        && word.with_low_bits<Data>(std::uint32_t{0x25}).bits() == 0x16BCu;
    // The niche of the word: the empty Option of each door, and the full
    // Option of the highest word that the fields hold.
    bool const is_niche_kept = Kinds::from_bits(0x7FFFu).is_some() && Kinds{}.with<Data>(std::uint8_t{0x1F}).is_some()
                            && Kinds::from_bits(0xFFFFu).is_none();
    return Kinds{}.bits() == 0 && is_split && is_word_refused && is_changed && is_value_refused && is_top
        && byte.get<Data>() == 0xFF && !(word == changed) && word == Kinds::from_bits(0x7ABCu).expect("as above")
        && is_niche_kept && is_fitted;
}
static_assert(operates());

}  // namespace detail::packed_word_checks

}  // namespace foundation::core

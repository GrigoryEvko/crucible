// The compile-time checks of foundation/reflect/EnumName.h.  The header
// gives the characters of each name to foundation/core/Text.h, so the checks
// of the names read the TextView forms of that header.

#include <foundation/reflect/EnumName.h>

#include <foundation/core/Text.h>

namespace foundation::reflect {

namespace detail::enum_name_self_test {

using ::foundation::core::enum_name_text;
using ::foundation::core::enum_words_text;
using ::foundation::core::enumerator_name_text;
using ::foundation::core::FixedText;
using ::foundation::core::TextView;
using ::foundation::core::unknown_enum_text;

enum class TestFlags : std::uint8_t {
    Alpha = 0x01,
    Beta = 0x02,
    Gamma = 0x04,
    Delta = 0x08,
    AlphaBeta = 0x03,
    None = 0x00,
};

using TF = TestFlags;

// True when an enumerator holds the value and its identifier is the text.
[[nodiscard]] consteval bool is_named(TF value, TextView expected) noexcept {
    return enumerator_name_text(value).match([expected](TextView name) noexcept { return name == expected; },
                                             [] noexcept { return false; });
}

inline constexpr bool is_alpha_named = is_named(TF::Alpha, "Alpha");
static_assert(is_alpha_named);

inline constexpr bool is_gamma_named = is_named(TF::Gamma, "Gamma");
static_assert(is_gamma_named);

inline constexpr bool is_alphabeta_named = is_named(TF::AlphaBeta, "AlphaBeta");
static_assert(is_alphabeta_named);

[[nodiscard]] consteval bool composite_value_lookup_returns_empty() noexcept {
    constexpr auto raw = static_cast<TF>(static_cast<std::uint8_t>(TF::Alpha) | static_cast<std::uint8_t>(TF::Gamma));
    return enumerator_name_text(raw).is_none();
}
static_assert(composite_value_lookup_returns_empty());

inline constexpr bool is_none_named = is_named(TF::None, "None");
static_assert(is_none_named);

static_assert(is_named(static_cast<TF>(0x03), "AlphaBeta"));
static_assert(enumerator_name_text(static_cast<TF>(0xFF)).is_none());

static_assert(enum_count<TF> == 6);

static_assert(unknown_enum_text<TF> == "<unknown TestFlags>");
static_assert(enum_name_text(TF::Alpha) == "Alpha");
static_assert(enum_name_text(TF::AlphaBeta) == "AlphaBeta");
static_assert(enum_name_text(TF::None) == "None");
static_assert(enum_name_text(static_cast<TF>(0xFF)) == "<unknown TestFlags>");
static_assert(enum_name_text(static_cast<TF>(0xFF)) == unknown_enum_text<TF>);

// A word starts at an upper-case letter after a lower-case letter or a
// digit, so a run of capitals and a digit run stay inside one word.
enum class WordShapes : std::uint8_t {
    CopyHostToDevice,
    NvlinkP2pCopy,
    Cpu,
    IPv4Header,
    Alias = CopyHostToDevice,
};
static_assert(enum_words_text<WordShapes, '_'>(WordShapes::CopyHostToDevice) == "copy_host_to_device");
static_assert(enum_words_text<WordShapes, '_'>(WordShapes::NvlinkP2pCopy) == "nvlink_p2p_copy");
static_assert(enum_words_text<WordShapes, '-'>(WordShapes::Cpu) == "cpu");
static_assert(enum_words_text<WordShapes, '-'>(WordShapes::IPv4Header) == "ipv4-header");
static_assert(enum_words_text<WordShapes, '-'>(WordShapes::Alias) == "copy-host-to-device",
              "the first enumerator that holds a value names it");
static_assert(enum_words_text<WordShapes, '_'>(static_cast<WordShapes>(0xFF)) == "<unknown WordShapes>");
static_assert(enum_words_text<TF, '_'>(TF::AlphaBeta) == "alpha_beta");

[[nodiscard]] consteval int count_enumerators() noexcept {
    int n = 0;
    for_each_enumerator_value<TF>([&](TF) noexcept { ++n; });
    return n;
}
static_assert(count_enumerators() == 6);

[[nodiscard]] consteval int count_single_bit_enumerators() noexcept {
    int n = 0;
    for_each_single_bit_enumerator_value<TF>([&](TF) noexcept { ++n; });
    return n;
}
static_assert(count_single_bit_enumerators() == 4);

[[nodiscard]] consteval bool single_bit_iteration_yields_declaration_order() noexcept {
    FixedText<64> names{};
    bool is_whole = true;
    for_each_single_bit_enumerator_value<TF>([&](TF flag) noexcept {
        if (names.size() != 0) is_whole = is_whole && names.append(TextView{","}).is_ok();
        is_whole = is_whole && names.append(enum_name_text(flag)).is_ok();
    });
    return is_whole && names.view() == "Alpha,Beta,Gamma,Delta";
}
static_assert(single_bit_iteration_yields_declaration_order());

}  // namespace detail::enum_name_self_test

}  // namespace foundation::reflect

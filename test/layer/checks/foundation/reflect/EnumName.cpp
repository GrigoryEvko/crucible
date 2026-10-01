// The compile-time checks of foundation/reflect/EnumName.h.

#include <foundation/reflect/EnumName.h>

namespace foundation::reflect {

namespace detail::enum_name_self_test {

enum class TestFlags : std::uint8_t {
    Alpha = 0x01,
    Beta = 0x02,
    Gamma = 0x04,
    Delta = 0x08,
    AlphaBeta = 0x03,
    None = 0x00,
};

using TF = TestFlags;

inline constexpr auto name_alpha = enumerator_name(TF::Alpha);
static_assert(name_alpha == "Alpha");

inline constexpr auto name_gamma = enumerator_name(TF::Gamma);
static_assert(name_gamma == "Gamma");

inline constexpr auto name_alphabeta = enumerator_name(TF::AlphaBeta);
static_assert(name_alphabeta == "AlphaBeta");

[[nodiscard]] consteval bool composite_value_lookup_returns_empty() noexcept {
    constexpr auto raw = static_cast<TF>(static_cast<std::uint8_t>(TF::Alpha) | static_cast<std::uint8_t>(TF::Gamma));
    return enumerator_name(raw).empty();
}
static_assert(composite_value_lookup_returns_empty());

inline constexpr auto name_none = enumerator_name(TF::None);
static_assert(name_none == "None");

static_assert(enumerator_name(static_cast<TF>(0x03)) == "AlphaBeta");
static_assert(enumerator_name(static_cast<TF>(0xFF)).empty());

static_assert(enum_count<TF> == 6);

static_assert(unknown_enum_sentinel<TF> == "<unknown TestFlags>");
static_assert(enum_name(TF::Alpha) == "Alpha");
static_assert(enum_name(TF::AlphaBeta) == "AlphaBeta");
static_assert(enum_name(TF::None) == "None");
static_assert(enum_name(static_cast<TF>(0xFF)) == "<unknown TestFlags>");
static_assert(enum_name(static_cast<TF>(0xFF)) == unknown_enum_sentinel<TF>);

// A word starts at an upper-case letter after a lower-case letter or a
// digit, so a run of capitals and a digit run stay inside one word.
enum class WordShapes : std::uint8_t {
    CopyHostToDevice,
    NvlinkP2pCopy,
    Cpu,
    IPv4Header,
    Alias = CopyHostToDevice,
};
static_assert(enum_words<WordShapes, '_'>(WordShapes::CopyHostToDevice) == "copy_host_to_device");
static_assert(enum_words<WordShapes, '_'>(WordShapes::NvlinkP2pCopy) == "nvlink_p2p_copy");
static_assert(enum_words<WordShapes, '-'>(WordShapes::Cpu) == "cpu");
static_assert(enum_words<WordShapes, '-'>(WordShapes::IPv4Header) == "ipv4-header");
static_assert(enum_words<WordShapes, '-'>(WordShapes::Alias) == "copy-host-to-device",
              "the first enumerator that holds a value names it");
static_assert(enum_words<WordShapes, '_'>(static_cast<WordShapes>(0xFF)) == "<unknown WordShapes>");
static_assert(enum_words<TF, '_'>(TF::AlphaBeta) == "alpha_beta");

[[nodiscard]] consteval int count_enumerators() noexcept {
    int n = 0;
    for_each_enumerator<TF>([&](TF, std::string_view) noexcept { ++n; });
    return n;
}
static_assert(count_enumerators() == 6);

[[nodiscard]] consteval int count_single_bit_enumerators() noexcept {
    int n = 0;
    for_each_single_bit_enumerator<TF>([&](TF, std::string_view) noexcept { ++n; });
    return n;
}
static_assert(count_single_bit_enumerators() == 4);

[[nodiscard]] consteval bool single_bit_iteration_yields_declaration_order() noexcept {
    char buf[64] = {};
    std::size_t pos = 0;
    bool first = true;
    for_each_single_bit_enumerator<TF>([&](TF, std::string_view name) noexcept {
        if (!first) buf[pos++] = ',';
        for (char c : name)
            buf[pos++] = c;
        first = false;
    });
    return std::string_view{buf, pos} == "Alpha,Beta,Gamma,Delta";
}
static_assert(single_bit_iteration_yields_declaration_order());

}  // namespace detail::enum_name_self_test

}  // namespace foundation::reflect

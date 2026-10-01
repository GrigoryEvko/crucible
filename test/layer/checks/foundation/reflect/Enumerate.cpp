// The compile-time checks of foundation/reflect/Enumerate.h.

#include <foundation/reflect/Enumerate.h>

namespace foundation::reflect {

namespace detail::reflected_self_test {

enum class TestFlags : std::uint8_t {
    Alpha = 0x01,
    Beta = 0x02,
    Gamma = 0x04,
    Delta = 0x08,
    AlphaBeta = 0x03,
    None = 0x00,
};

using TF = TestFlags;
using U = std::underlying_type_t<TF>;

// The word that a Bits<TF>{flags...} held: the enumerators' values ORed.
[[nodiscard]] constexpr U mask_of(std::initializer_list<TF> flags) noexcept {
    U acc = 0;
    for (TF f : flags) {
        acc = static_cast<U>(acc | static_cast<U>(f));
    }
    return acc;
}

[[nodiscard]] consteval bool empty_bits_writes_empty_string() noexcept {
    char buf[16] = {};
    U b = 0;
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    return n == 0 && buf[0] == '\0';
}
static_assert(empty_bits_writes_empty_string());

[[nodiscard]] consteval bool single_flag_writes_name() noexcept {
    char buf[16] = {};
    U b = mask_of({TF::Alpha});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    return n == 5 && std::string_view{buf} == "Alpha";
}
static_assert(single_flag_writes_name());

[[nodiscard]] consteval bool multi_flag_writes_pipe_separated() noexcept {
    char buf[64] = {};
    U b = mask_of({TF::Alpha, TF::Gamma, TF::Delta});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    return std::string_view{buf} == "Alpha|Gamma|Delta" && n == std::string_view{buf}.size();
}
static_assert(multi_flag_writes_pipe_separated());

[[nodiscard]] consteval bool composite_enumerator_is_skipped() noexcept {
    char buf[64] = {};
    U b = mask_of({TF::Alpha, TF::Beta});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    return std::string_view{buf} == "Alpha|Beta" && n == std::string_view{buf}.size();
}
static_assert(composite_enumerator_is_skipped());

[[nodiscard]] consteval bool zero_enumerator_is_skipped() noexcept {
    char buf[64] = {};
    U b = mask_of({TF::Alpha});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    return std::string_view{buf} == "Alpha" && n == 5;
}
static_assert(zero_enumerator_is_skipped());

[[nodiscard]] consteval bool truncation_returns_needed_length() noexcept {
    char buf[8] = {};
    U b = mask_of({TF::Alpha, TF::Beta});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    if (n != 10) return false;
    if (std::string_view{buf} != "Alpha|B") return false;
    return buf[7] == '\0';
}
static_assert(truncation_returns_needed_length());

[[nodiscard]] consteval bool zero_capacity_probes_size() noexcept {
    U b = mask_of({TF::Alpha, TF::Beta, TF::Gamma});
    auto n = bits_to_string<TF>(b, nullptr, 0);
    return n == std::string_view{"Alpha|Beta|Gamma"}.size();
}
static_assert(zero_capacity_probes_size());

[[nodiscard]] consteval bool unit_capacity_writes_nul_only() noexcept {
    char buf[1] = {'X'};
    U b = mask_of({TF::Alpha});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    return buf[0] == '\0' && n == 5;
}
static_assert(unit_capacity_writes_nul_only());

[[nodiscard]] consteval bool exact_fit_capacity_no_truncation() noexcept {
    char buf[11] = {};
    U b = mask_of({TF::Alpha, TF::Beta});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    if (n != 10) return false;
    if (std::string_view{buf} != "Alpha|Beta") return false;
    return buf[10] == '\0';
}
static_assert(exact_fit_capacity_no_truncation());

[[nodiscard]] consteval bool one_byte_short_truncates_one_char() noexcept {
    char buf[10] = {};
    U b = mask_of({TF::Alpha, TF::Beta});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    if (n != 10) return false;
    if (std::string_view{buf} != "Alpha|Bet") return false;
    return buf[9] == '\0';
}
static_assert(one_byte_short_truncates_one_char());

[[nodiscard]] consteval bool all_flags_set_emits_every_single_bit() noexcept {
    char buf[64] = {};
    U b = mask_of({TF::Alpha, TF::Beta, TF::Gamma, TF::Delta, TF::AlphaBeta, TF::None});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    return std::string_view{buf} == "Alpha|Beta|Gamma|Delta" && n == std::string_view{buf}.size();
}
static_assert(all_flags_set_emits_every_single_bit());

[[nodiscard]] consteval bool deterministic_output() noexcept {
    char buf1[64] = {};
    char buf2[64] = {};
    U b = mask_of({TF::Beta, TF::Delta});
    auto n1 = bits_to_string<TF>(b, buf1, sizeof(buf1));
    auto n2 = bits_to_string<TF>(b, buf2, sizeof(buf2));
    return n1 == n2 && std::string_view{buf1} == std::string_view{buf2};
}
static_assert(deterministic_output());

template <class T>
concept can_bits_to_string = requires(T t, char* p, size_t s) {
    { bits_to_string<T>(std::underlying_type_t<T>{}, p, s) } -> std::same_as<size_t>;
};
static_assert(can_bits_to_string<TF>);
static_assert(!can_bits_to_string<int>, "bits_to_string requires a scoped enum.  int does not satisfy "
                                        "ScopedEnum, so std::underlying_type_t<int> is itself ill-formed.");

}  // namespace detail::reflected_self_test

}  // namespace foundation::reflect

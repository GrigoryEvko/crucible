// SPDX-License-Identifier: Apache-2.0
//
// Sentinel TU for foundation/reflect.  Enumerate.h and Hash.h carry their
// own static_asserts.  This file includes both and runs a run-time body
// for each.  It drives the enumerate helpers and pin_enum
// over a local enum and pins the two id properties that a cache key rests
// on.  It also makes sure that the Murmur3 finalizer maps a non-zero seed
// to a non-zero hash, which the zero-means-empty slot conventions need.

#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/reflect/EnumName.h>
#include <foundation/reflect/EnumPins.h>
#include <foundation/reflect/Enumerate.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <type_traits>

// The two types whose ids are compared below.  They stand outside the
// unnamed namespace, because a stable id refuses a type with internal
// linkage.
namespace test_reflect_types {
struct Alpha {};
struct Beta {};
}  // namespace test_reflect_types

namespace {

namespace dc = ::foundation::decide;
namespace fr = ::foundation::reflect;

// A local enum with one zero enumerator, three single-bit enumerators and
// one composite, so that the single-bit filter sees every case.
enum class Lamp : std::uint8_t {
    Off = 0x00,
    Red = 0x01,
    Green = 0x02,
    Blue = 0x04,
    Magenta = 0x05,
};

using LampWord = std::underlying_type_t<Lamp>;

static_assert(fr::ScopedEnum<Lamp>);
static_assert(!fr::ScopedEnum<LampWord>);

static_assert(fr::enumerator_name(Lamp::Off) == "Off");
static_assert(fr::enumerator_name(Lamp::Red) == "Red");
static_assert(fr::enumerator_name(Lamp::Magenta) == "Magenta");
static_assert(fr::enumerator_name(static_cast<Lamp>(0x06)).empty());

// enum_count derives from the enumerator list, and enum_name answers the
// sentinel where enumerator_name answers empty.  The sentinel spells the
// unqualified enum name.
static_assert(fr::enum_count<Lamp> == 5);
static_assert(fr::unknown_enum_sentinel<Lamp> == "<unknown Lamp>");
static_assert(fr::enum_name(Lamp::Off) == "Off");
static_assert(fr::enum_name(Lamp::Magenta) == "Magenta");
static_assert(fr::enum_name(static_cast<Lamp>(0x06)) == "<unknown Lamp>");
static_assert(fr::enum_name(static_cast<Lamp>(0x06)) == fr::unknown_enum_sentinel<Lamp>);

// A pin table deduces its enum and holds the full underlying width.  The
// table for Lamp pins every value, and each table below drifts from it
// in one way.
inline constexpr std::array<fr::enum_pin<Lamp>, 5> lamp_pins{
    {{"Off", 0x00}, {"Red", 0x01}, {"Green", 0x02}, {"Blue", 0x04}, {"Magenta", 0x05}}};
static_assert(fr::pin_enum(lamp_pins));

inline constexpr std::array<fr::enum_pin<Lamp>, 5> lamp_pins_blue_moved{
    {{"Off", 0x00}, {"Red", 0x01}, {"Green", 0x02}, {"Blue", 0x08}, {"Magenta", 0x05}}};
static_assert(!fr::pin_enum(lamp_pins_blue_moved));

inline constexpr std::array<fr::enum_pin<Lamp>, 4> lamp_pins_magenta_missing{
    {{"Off", 0x00}, {"Red", 0x01}, {"Green", 0x02}, {"Blue", 0x04}}};
static_assert(!fr::pin_enum(lamp_pins_magenta_missing));

// A counter wider than a byte: a value that differs only above the low
// byte is caught.
enum class Stride : std::uint16_t {
    Short = 0x0010,
    Long = 0x1010,
};

inline constexpr std::array<fr::enum_pin<Stride>, 2> stride_pins{{{"Short", 0x0010}, {"Long", 0x1010}}};
static_assert(fr::pin_enum(stride_pins));

inline constexpr std::array<fr::enum_pin<Stride>, 2> stride_pins_low_byte_only{{{"Short", 0x0010}, {"Long", 0x0010}}};
static_assert(!fr::pin_enum(stride_pins_low_byte_only));

[[nodiscard]] consteval int count_lamp_enumerators() noexcept {
    int n = 0;
    fr::for_each_enumerator<Lamp>([&](Lamp, std::string_view) noexcept { ++n; });
    return n;
}
static_assert(count_lamp_enumerators() == 5);

[[nodiscard]] consteval int count_lamp_single_bit_enumerators() noexcept {
    int n = 0;
    fr::for_each_single_bit_enumerator<Lamp>([&](Lamp, std::string_view) noexcept { ++n; });
    return n;
}
static_assert(count_lamp_single_bit_enumerators() == 3);

// The composite enumerator's value names its two single bits, never itself.
[[nodiscard]] consteval bool magenta_spells_red_and_blue() noexcept {
    char buf[32] = {};
    auto n = fr::bits_to_string<Lamp>(static_cast<LampWord>(Lamp::Magenta), buf, sizeof(buf));
    return std::string_view{buf} == "Red|Blue" && n == std::string_view{"Red|Blue"}.size();
}
static_assert(magenta_spells_red_and_blue());

// An unscoped enum converts to its integer on its own, so the typed walks
// refuse it by the ScopedEnum constraint.  It has an underlying type, so
// the refusal is the constraint's and not a failed underlying_type_t.
enum LegacyLamp : std::uint8_t {
    LegacyRed = 0x01,
    LegacyBlue = 0x04,
};

template <typename E>
concept RendersBits =
    requires(char* out, std::size_t cap) { fr::bits_to_string<E>(std::underlying_type_t<E>{}, out, cap); };
template <typename E>
concept WalksEnumerators = requires { fr::for_each_enumerator<E>([](E, std::string_view) noexcept {}); };

static_assert(RendersBits<Lamp> && !RendersBits<LegacyLamp>);
static_assert(WalksEnumerators<Lamp> && !WalksEnumerators<LegacyLamp>);

// Two distinct types take distinct ids, and two spellings of one type take
// one id.
using test_reflect_types::Alpha;
using test_reflect_types::Beta;
using AlphaAlias = Alpha;

static_assert(fr::stable_type_id<Alpha> != fr::stable_type_id<Beta>);
static_assert(fr::stable_type_id<Alpha> == fr::stable_type_id<AlphaAlias>);
static_assert(fr::stable_type_id<int> == fr::stable_type_id<signed int>);

static_assert(fr::combine_ids(1, 2) != fr::combine_ids(2, 1));

// The mixer is the Murmur3 finalizer, a bijection on uint64_t with
// f(0) equal to 0. Every witness below rests on that.
static_assert(dc::is_non_zero(fr::fmix64(1)));
static_assert(dc::is_non_zero(fr::fmix64(0xDEADBEEFCAFEBABEULL)));
static_assert(dc::is_non_zero(fr::fmix64(0x9E3779B97F4A7C15ULL)));
static_assert(dc::is_non_zero(fr::fmix64(0xFFFFFFFFFFFFFFFFULL)));
static_assert(dc::is_non_zero(fr::fmix64(42)));
static_assert(dc::is_non_zero(fr::fmix64(0xDEADBEEFULL)));
static_assert(fr::fmix64(0) == 0);

[[nodiscard]] constexpr std::uint64_t make_non_zero_hash(std::uint64_t seed) noexcept {
    CRUCIBLE_PRE(dc::is_non_zero(seed));
    const std::uint64_t h = fr::fmix64(seed);
    CRUCIBLE_POST(h, dc::is_non_zero(h));
    return h;
}

static_assert(make_non_zero_hash(1) != 0);
static_assert(make_non_zero_hash(0xDEADBEEFCAFEBABEULL) != 0);
static_assert(make_non_zero_hash(0x9E3779B97F4A7C15ULL) != 0);
static_assert(make_non_zero_hash(0xFFFFFFFFFFFFFFFFULL) != 0);

// The function-scope using-directives give each body below the name
// lookup of its header.  The static_assert walls stay in the headers.
//
// A self test follows the code it tests, not the file it started in.
// A header split out of another header needs a caller for its self
// test, or the body compiles everywhere and runs nowhere.

void enumerate_runs_at_run_time() {
    using namespace ::foundation::reflect;
    using namespace ::foundation::reflect::detail::reflected_self_test;
    char buf[64] = {};

    U empty = 0;
    if (bits_to_string<TF>(empty, buf, sizeof(buf)) != 0) std::abort();
    if (buf[0] != '\0') std::abort();

    U a = mask_of({TF::Alpha});
    auto na = bits_to_string<TF>(a, buf, sizeof(buf));
    if (na != 5) std::abort();
    if (std::string_view{buf} != "Alpha") std::abort();

    U abc = mask_of({TF::Alpha, TF::Beta, TF::Gamma});
    auto nabc = bits_to_string<TF>(abc, buf, sizeof(buf));
    if (std::string_view{buf} != "Alpha|Beta|Gamma") std::abort();
    if (nabc != std::string_view{"Alpha|Beta|Gamma"}.size()) std::abort();

    U ab = mask_of({TF::Alpha, TF::Beta});
    auto nab = bits_to_string<TF>(ab, buf, sizeof(buf));
    if (std::string_view{buf} != "Alpha|Beta") std::abort();
    if (nab != 10) std::abort();

    char small[8] = {};
    auto nt = bits_to_string<TF>(ab, small, sizeof(small));
    if (nt != 10) std::abort();
    if (std::string_view{small} != "Alpha|B") std::abort();
    if (small[7] != '\0') std::abort();

    auto np = bits_to_string<TF>(abc, nullptr, 0);
    if (np != std::string_view{"Alpha|Beta|Gamma"}.size()) std::abort();

    {
        char tight[11] = {};
        auto nfit = bits_to_string<TF>(mask_of({TF::Alpha, TF::Beta}), tight, sizeof(tight));
        if (nfit != 10) std::abort();
        if (std::string_view{tight} != "Alpha|Beta") std::abort();
        if (tight[10] != '\0') std::abort();
    }
    {
        char short_buf[10] = {};
        auto nshort = bits_to_string<TF>(mask_of({TF::Alpha, TF::Beta}), short_buf, sizeof(short_buf));
        if (nshort != 10) std::abort();
        if (std::string_view{short_buf} != "Alpha|Bet") std::abort();
        if (short_buf[9] != '\0') std::abort();
    }
}

void enum_name_runs_at_run_time() {
    using namespace ::foundation::reflect;
    using namespace ::foundation::reflect::detail::enum_name_self_test;
    if (enumerator_name(TF::None) != "None") std::abort();
    if (enumerator_name(TF::Alpha) != "Alpha") std::abort();
    if (enumerator_name(TF::Delta) != "Delta") std::abort();
    if (enumerator_name(TF::AlphaBeta) != "AlphaBeta") std::abort();

    auto fancy = static_cast<TF>(static_cast<std::uint8_t>(TF::Alpha) | static_cast<std::uint8_t>(TF::Delta));
    if (!enumerator_name(fancy).empty()) std::abort();

    // enum_name runs under runtime semantics here, with the sentinel
    // read from static storage.
    if (enum_name(fancy) != "<unknown TestFlags>") std::abort();
    if (enum_name(TF::Gamma) != "Gamma") std::abort();
    if (enum_count<TF> != 6) std::abort();

    int counter = 0;
    for_each_enumerator<TF>([&](TF, std::string_view) noexcept { ++counter; });
    if (counter != 6) std::abort();

    int single_bit_counter = 0;
    for_each_single_bit_enumerator<TF>([&](TF, std::string_view) noexcept { ++single_bit_counter; });
    if (single_bit_counter != 4) std::abort();
}

void stable_name_runs_at_run_time() noexcept {
    using namespace ::foundation::reflect;
    volatile std::uint64_t sink = 0;
    sink ^= stable_type_id<int>;
    sink ^= stable_type_id<float>;
    sink ^= stable_type_id<double>;
    sink ^= stable_type_id<void>;
    sink ^= stable_type_id<unsigned char>;
    sink ^= stable_type_id<long>;
    sink ^= stable_type_id<short>;
    (void)sink;

    volatile std::size_t name_sink = 0;
    name_sink ^= stable_name_of<int>.size();
    name_sink ^= stable_name_of<float>.size();
    name_sink ^= stable_name_of<void>.size();
    (void)name_sink;

    using sorted2 = canonicalize_pack_t<int, float>;
    using sorted2b = canonicalize_pack_t<float, int>;
    bool const same = std::is_same_v<sorted2, sorted2b>;
    volatile bool sink_b = same;
    (void)sink_b;

    auto const fn_ptr = +[](int) noexcept -> int { return 0; };
    volatile std::uint64_t fid_sink = stable_function_id<+[](int) noexcept -> int { return 0; }>;
    fid_sink ^= std::bit_cast<std::uintptr_t>(fn_ptr);
    (void)fid_sink;
}

}  // namespace

int main() {
    enumerate_runs_at_run_time();
    enum_name_runs_at_run_time();
    stable_name_runs_at_run_time();

    int volatile sink = 0;

    // The enumerate helpers over the local enum, with the mask routed
    // through a volatile so that the call runs at runtime.
    {
        LampWord volatile word_v = static_cast<LampWord>(Lamp::Magenta);
        char buf[32] = {};
        auto n = fr::bits_to_string<Lamp>(static_cast<LampWord>(word_v), buf, sizeof(buf));
        if (std::string_view{buf} != "Red|Blue" || n != std::string_view{"Red|Blue"}.size()) {
            std::fprintf(stderr, "test_reflect: bits_to_string<Lamp>(Magenta) gave \"%s\" (%zu)\n", buf, n);
            return 1;
        }
        Lamp volatile lamp_v = Lamp::Green;
        if (fr::enumerator_name(static_cast<Lamp>(lamp_v)) != "Green") {
            std::fprintf(stderr, "test_reflect: enumerator_name(Green) is wrong\n");
            return 1;
        }
        if (fr::enum_name(static_cast<Lamp>(lamp_v)) != "Green") {
            std::fprintf(stderr, "test_reflect: enum_name(Green) is wrong\n");
            return 1;
        }
        Lamp volatile bad_v = static_cast<Lamp>(0x06);
        if (fr::enum_name(static_cast<Lamp>(bad_v)) != "<unknown Lamp>") {
            std::fprintf(stderr, "test_reflect: enum_name(0x06) is not the sentinel\n");
            return 1;
        }
        int counter = 0;
        fr::for_each_enumerator<Lamp>([&](Lamp, std::string_view) noexcept { ++counter; });
        int single_bit_counter = 0;
        fr::for_each_single_bit_enumerator<Lamp>([&](Lamp, std::string_view) noexcept { ++single_bit_counter; });
        if (counter != 5 || single_bit_counter != 3) {
            std::fprintf(stderr, "test_reflect: enumerator counts %d/%d, expected 5/3\n", counter, single_bit_counter);
            return 1;
        }
        sink += counter + single_bit_counter;
    }

    // The mixer maps a non-zero seed to a non-zero hash and zero to zero,
    // at runtime through volatile operands.
    {
        volatile std::uint64_t seed_nz = 0xDEADBEEFCAFEBABEULL;
        const std::uint64_t mix_nz = fr::fmix64(static_cast<std::uint64_t>(seed_nz));
        if (!dc::is_non_zero(mix_nz)) {
            std::fprintf(stderr, "test_reflect: fmix64 mapped a non-zero seed to zero\n");
            return 1;
        }
        volatile std::uint64_t zero_seed = 0;
        const std::uint64_t mix_of_zero = fr::fmix64(static_cast<std::uint64_t>(zero_seed));
        if (mix_of_zero != 0) {
            std::fprintf(stderr, "test_reflect: fmix64(0) returned non-zero — bijection "
                                 "theorem violated\n");
            return 1;
        }
        sink += static_cast<int>(mix_nz != 0) - static_cast<int>(mix_of_zero != 0);
        sink += static_cast<int>(make_non_zero_hash(static_cast<std::uint64_t>(seed_nz)) != 0);
    }

    // combine_ids order sensitivity, at runtime through volatile operands.
    {
        volatile std::uint64_t lhs_v = 1;
        volatile std::uint64_t rhs_v = 2;
        const std::uint64_t forward =
            fr::combine_ids(static_cast<std::uint64_t>(lhs_v), static_cast<std::uint64_t>(rhs_v));
        const std::uint64_t reverse =
            fr::combine_ids(static_cast<std::uint64_t>(rhs_v), static_cast<std::uint64_t>(lhs_v));
        if (forward == reverse) {
            std::fprintf(stderr, "test_reflect: combine_ids(1, 2) == combine_ids(2, 1)\n");
            return 1;
        }
        sink += static_cast<int>(forward != reverse);
    }

    if (sink == 0) {
        std::fprintf(stderr, "test_reflect: sink unexpectedly zero\n");
        return 1;
    }
    return 0;
}

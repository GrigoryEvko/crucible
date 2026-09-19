// SPDX-License-Identifier: Apache-2.0
//
// Sentinel TU for foundation/reflect.  Enumerate.h and Hash.h carry their
// own static_asserts and inline smoke tests.  This file includes both and
// calls the smoke tests.  It drives the enumerate helpers over a local
// enum and pins the two id properties that a cache key rests on.  It also
// holds the cells of test/test_decide.cpp that feed the real Murmur3
// finalizer through fmix_preserves_non_zero.

#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Pre.h>
#include <foundation/reflect/Enumerate.h>
#include <foundation/reflect/Hash.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <type_traits>

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

// Two distinct types take distinct ids, and two spellings of one type take
// one id.
struct Alpha {};
struct Beta {};
using AlphaAlias = Alpha;

static_assert(fr::stable_type_id<Alpha> != fr::stable_type_id<Beta>);
static_assert(fr::stable_type_id<Alpha> == fr::stable_type_id<AlphaAlias>);
static_assert(fr::stable_type_id<int> == fr::stable_type_id<signed int>);

static_assert(fr::combine_ids(1, 2) != fr::combine_ids(2, 1));

// The mixer is the Murmur3 finalizer, a bijection on uint64_t with
// f(0) equal to 0. Every witness below rests on that.
static_assert(dc::fmix_preserves_non_zero(1, fr::fmix64(1)));
static_assert(dc::fmix_preserves_non_zero(0xDEADBEEFCAFEBABEULL, fr::fmix64(0xDEADBEEFCAFEBABEULL)));
static_assert(dc::fmix_preserves_non_zero(0x9E3779B97F4A7C15ULL, fr::fmix64(0x9E3779B97F4A7C15ULL)));
static_assert(dc::fmix_preserves_non_zero(0xFFFFFFFFFFFFFFFFULL, fr::fmix64(0xFFFFFFFFFFFFFFFFULL)));
static_assert(dc::fmix_preserves_non_zero(42, fr::fmix64(42)));

static_assert(!dc::fmix_preserves_non_zero(0, fr::fmix64(0)));
static_assert(!dc::fmix_preserves_non_zero(0, 0));

// These pin the mixer itself rather than the predicate, which only
// compares the pair it is handed.
static_assert(fr::fmix64(0) == 0);
static_assert(fr::fmix64(1) != 0);
static_assert(fr::fmix64(0xDEADBEEFULL) != 0);
static_assert(fr::fmix64(0xFFFFFFFFFFFFFFFFULL) != 0);

[[nodiscard]] constexpr std::uint64_t make_non_zero_hash(std::uint64_t seed) noexcept {
    const std::uint64_t h = fr::fmix64(seed);
    CRUCIBLE_PRE(dc::fmix_preserves_non_zero(seed, h));
    return h;
}

static_assert(make_non_zero_hash(1) != 0);
static_assert(make_non_zero_hash(0xDEADBEEFCAFEBABEULL) != 0);
static_assert(make_non_zero_hash(0x9E3779B97F4A7C15ULL) != 0);
static_assert(make_non_zero_hash(0xFFFFFFFFFFFFFFFFULL) != 0);

}  // namespace

int main() {
    fr::detail::reflected_self_test::runtime_smoke_test();
    fr::runtime_smoke_test_stable_name();

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

    // fmix_preserves_non_zero
    {
        volatile std::uint64_t seed_nz = 0xDEADBEEFCAFEBABEULL;
        const std::uint64_t mix_nz = fr::fmix64(static_cast<std::uint64_t>(seed_nz));
        volatile std::uint64_t mix_nz_v = mix_nz;
        if (!dc::fmix_preserves_non_zero(static_cast<std::uint64_t>(seed_nz), static_cast<std::uint64_t>(mix_nz_v))) {
            std::fprintf(stderr, "test_reflect: fmix_preserves_non_zero(non-zero seed, fmix(seed)) "
                                 "WRONGLY rejected\n");
            return 1;
        }
        volatile std::uint64_t zero_seed = 0;
        const std::uint64_t mix_of_zero = fr::fmix64(static_cast<std::uint64_t>(zero_seed));
        if (mix_of_zero != 0) {
            std::fprintf(stderr, "test_reflect: fmix64(0) returned non-zero — bijection "
                                 "theorem violated\n");
            return 1;
        }
        if (dc::fmix_preserves_non_zero(static_cast<std::uint64_t>(zero_seed), static_cast<std::uint64_t>(mix_nz_v))) {
            std::fprintf(stderr, "test_reflect: fmix_preserves_non_zero(0, non-zero) "
                                 "WRONGLY accepted (seed-zero violator)\n");
            return 1;
        }
        // A mix output the real mixer cannot produce, so the second
        // clause is witnessed on its own.
        volatile std::uint64_t mix_zero_v = 0;
        if (dc::fmix_preserves_non_zero(static_cast<std::uint64_t>(seed_nz), static_cast<std::uint64_t>(mix_zero_v))) {
            std::fprintf(stderr, "test_reflect: fmix_preserves_non_zero(non-zero, 0) "
                                 "WRONGLY accepted (mix-zero collision violator)\n");
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

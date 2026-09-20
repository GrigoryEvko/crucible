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
#include <foundation/reflect/EnumName.h>
#include <foundation/reflect/Enumerate.h>
#include <foundation/reflect/Hash.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

// Each body below was an inline smoke test in its header, compiled into
// every translation unit that included it.  They are moved verbatim,
// and the function-scope using-directives reproduce the name lookup
// each had inside its header.  The static_assert walls stayed behind.
//
// The enum-name body had no caller anywhere in the tree before this
// one.  EnumName.h was split out of Enumerate.h to break an include
// cycle, and the split carried the self test across without giving the
// new header a caller, so the body was compiled everywhere and run
// nowhere.  The next person splitting a header should check the same
// thing: a self test follows the code it tests, not the file it
// started in.

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

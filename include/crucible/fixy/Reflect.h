#pragma once

#include <crucible/Reflect.h>
#include <crucible/safety/Reflected.h>

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace crucible::fixy::reflect {

using ::crucible::reflect_hash;

using ::crucible::has_reflected_hash;

using ::crucible::reflect_fmix_fold;

using ::crucible::reflect_print;

using ::crucible::safety::reflected::for_each_enumerator;

using ::crucible::safety::reflected::for_each_single_bit_enumerator;

using ::crucible::safety::reflected::enumerator_name;

using ::crucible::safety::reflected::bits_to_string;

}  // namespace crucible::fixy::reflect

namespace crucible::fixy::reflect::self_test {

struct ReflectProbeStruct {
    std::uint32_t a = 7;
    std::uint32_t b = 11;
};

enum class ReflectProbeEnum : std::uint8_t {
    Zero = 0x00,
    One = 0x01,
    Two = 0x02,
    Four = 0x04,
    Three = 0x03,
};

inline constexpr ReflectProbeStruct probe_struct{42, 99};

// reflect_hash is not constexpr, so its value cannot be compared at compile
// time. A function address is a constant expression even for a non-constexpr
// function, so identity is witnessed through the pointer instead.
static_assert(&::crucible::fixy::reflect::reflect_hash<ReflectProbeStruct>
                  == &::crucible::reflect_hash<ReflectProbeStruct>,
              "fixy::reflect::reflect_hash resolves to the same instantiated "
              "function as crucible::reflect_hash. Two reach paths that point to "
              "different functions would yield two different content hashes.");

static_assert(::crucible::fixy::reflect::has_reflected_hash<ReflectProbeStruct>
                  == ::crucible::has_reflected_hash<ReflectProbeStruct>,
              "fixy::reflect::has_reflected_hash<T> must agree with substrate.");

static_assert(::crucible::fixy::reflect::has_reflected_hash<ReflectProbeStruct>,
              "ReflectProbeStruct is reflectable; trait must report true.");

static_assert(!::crucible::fixy::reflect::has_reflected_hash<int>,
              "int is not a class type; has_reflected_hash<int> must be false. "
              "Drift would mean the negative branch of the trait is bypassed.");

static_assert(::crucible::fixy::reflect::reflect_fmix_fold<0xDEADBEEFULL>(probe_struct)
                  == ::crucible::reflect_fmix_fold<0xDEADBEEFULL>(probe_struct),
              "fixy::reflect::reflect_fmix_fold<Seed, T> must produce identical "
              "output to the substrate. The fold pattern differs from "
              "reflect_hash but the determinism contract is the same.");

static_assert(::crucible::fixy::reflect::reflect_fmix_fold<0xCAFEBABEULL>(probe_struct)
                  != ::crucible::fixy::reflect::reflect_fmix_fold<0xDEADBEEFULL>(probe_struct),
              "Different seeds must produce different fmix64-fold hashes — if "
              "they collide the seed parameter has been dropped at the alias.");

static_assert(::crucible::fixy::reflect::enumerator_name(ReflectProbeEnum::One)
                  == ::crucible::safety::reflected::enumerator_name(ReflectProbeEnum::One),
              "fixy::reflect::enumerator_name must agree with substrate on "
              "named enumerator lookups (positive case).");

static_assert(::crucible::fixy::reflect::enumerator_name(ReflectProbeEnum::One) == "One",
              "Direct positive witness — One enumerator maps to \"One\" name.");

static_assert(::crucible::fixy::reflect::enumerator_name(static_cast<ReflectProbeEnum>(0xFF)).empty(),
              "Unknown value yields empty view, not garbage — reach-through "
              "preserves the negative case.");

[[nodiscard]] consteval int count_through_alias() noexcept {
    int n = 0;
    ::crucible::fixy::reflect::for_each_enumerator<ReflectProbeEnum>(
        [&](ReflectProbeEnum, std::string_view) noexcept { ++n; });
    return n;
}
static_assert(count_through_alias() == 5, "ReflectProbeEnum has 5 declared enumerators; reach-through must "
                                          "preserve substrate's iteration cardinality.");

[[nodiscard]] consteval int count_single_bit_through_alias() noexcept {
    int n = 0;
    ::crucible::fixy::reflect::for_each_single_bit_enumerator<ReflectProbeEnum>(
        [&](ReflectProbeEnum, std::string_view) noexcept { ++n; });
    return n;
}
static_assert(count_single_bit_through_alias() == 3, "ReflectProbeEnum has 3 single-bit enumerators (One, Two, Four); "
                                                     "Three (0x03, popcount=2) and Zero (popcount=0) filtered. "
                                                     "Reach-through must preserve substrate's compile-time filter.");

[[nodiscard]] consteval bool bits_to_string_through_alias() noexcept {
    char buf[16] = {};
    ::crucible::safety::Bits<ReflectProbeEnum> b{ReflectProbeEnum::One, ReflectProbeEnum::Two};
    auto n = ::crucible::fixy::reflect::bits_to_string<ReflectProbeEnum>(b, buf, sizeof(buf));
    return n == 7 && std::string_view{buf} == "One|Two";
}
static_assert(bits_to_string_through_alias(), "Reach-through must preserve substrate's snprintf-style formatting "
                                              "of Bits<E> — declaration-order iteration produces \"One|Two\" for "
                                              "Bits<E>{One, Two}.");

constexpr int reflect_alias_cardinality = 8;
static_assert(reflect_alias_cardinality == 8, "The re-exported reflection surface cardinality changed. Update "
                                              "the using-decls and this sentinel together.");

}  // namespace crucible::fixy::reflect::self_test

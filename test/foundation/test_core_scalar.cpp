// Tests of foundation/core/Scalar.h at run time: enum_from gives an
// enumerator for each value of an enumerator and no value for each other
// value, and decode refuses each source whose bytes hold a bool other than
// 0 or 1, or an enumeration value that names no enumerator.  The loops go
// over each value of the small types.

#include <foundation/Quarantine.h>
#include <foundation/core/Scalar.h>

#include "../test_assert.h"

#include <cstdint>
#include <utility>

namespace {

using ::foundation::core::decode;
using ::foundation::core::enum_from;

enum class Contiguous : std::uint8_t {
    first = 3,
    second,
    third
};
enum class Shifted : std::int8_t {
    low = -3,
    middle = -2,
    high = -1
};
enum class Gapped : std::uint8_t {
    first = 1,
    second = 5,
    third = 64
};
enum class Unsorted : std::uint8_t {
    third = 9,
    first = 1,
    second = 4
};
enum class Wide : std::uint16_t {
    first = 1,
    second = 300,
    third = 60000
};
enum class Extremes : std::int64_t {
    lowest = INT64_MIN,
    zero = 0,
    highest = INT64_MAX
};

struct Flagged {
    bool is_ready = false;
    Contiguous phase = Contiguous::first;
};
struct Header {
    Shifted level = Shifted::low;
    std::uint8_t count = 0;
};
struct Nested : Header {
    Flagged flags{};
    std::uint16_t word = 0;
};
struct Triple {
    std::uint16_t first = 0;
    std::uint16_t second = 0;
    std::uint16_t third = 0;
};
struct Words {
    std::uint64_t low = 0;
    std::uint64_t high = 0;
};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: decode walks a C array of enumerations, the array form of a quarantined type")
struct Lanes {
    Contiguous lanes[4];
};
struct FlaggedPair {
    Flagged pair[2];
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The value, through a call that the optimizer cannot read, so each decode
// of a loop runs at run time.
template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

// enum_from gives exactly the values of the enumerators, and each one maps
// to its enumerator.
template <class E, class Underlying>
void expect_names(Underlying raw, bool is_enumerator) {
    auto const named = enum_from<E>(opaque(static_cast<std::underlying_type_t<E>>(raw)));
    assert(named.is_some() == is_enumerator);
    if (is_enumerator) {
        assert(std::to_underlying(enum_from<E>(static_cast<std::underlying_type_t<E>>(raw)).value_or(E{}))
               == static_cast<std::underlying_type_t<E>>(raw));
    }
}

void test_enum_from_gives_exactly_the_enumerators() {
    for (int raw = -128; raw <= 127; ++raw)
        expect_names<Shifted>(raw, raw >= -3 && raw <= -1);
    for (int raw = 0; raw <= 255; ++raw) {
        expect_names<Contiguous>(raw, raw >= 3 && raw <= 5);
        expect_names<Gapped>(raw, raw == 1 || raw == 5 || raw == 64);
        expect_names<Unsorted>(raw, raw == 1 || raw == 4 || raw == 9);
    }
    for (int raw = 0; raw <= 65535; ++raw)
        expect_names<Wide>(raw, raw == 1 || raw == 300 || raw == 60000);
    expect_names<Extremes>(INT64_MIN, true);
    expect_names<Extremes>(INT64_MIN + 1, false);
    expect_names<Extremes>(std::int64_t{-1}, false);
    expect_names<Extremes>(std::int64_t{0}, true);
    expect_names<Extremes>(std::int64_t{1}, false);
    expect_names<Extremes>(INT64_MAX - 1, false);
    expect_names<Extremes>(INT64_MAX, true);
}

// A bool takes only the bytes 0 and 1.  A byte of 2 gives no value, and no
// bool with that byte ever exists.
void test_decode_bool_takes_only_zero_and_one() {
    for (unsigned raw = 0; raw <= 255; ++raw) {
        auto const decoded = decode<bool>(opaque(static_cast<std::uint8_t>(raw)));
        assert(decoded.is_some() == (raw <= 1));
        if (raw <= 1) assert(decode<bool>(static_cast<std::uint8_t>(raw)).value_or(raw == 0) == (raw == 1));
    }
}

// A value between two enumerators names no enumerator.
void test_decode_refuses_a_value_between_enumerators() {
    for (unsigned raw = 0; raw <= 255; ++raw) {
        bool const is_enumerator = raw == 1 || raw == 5 || raw == 64;
        assert(decode<Gapped>(opaque(static_cast<std::uint8_t>(raw))).is_some() == is_enumerator);
    }
}

// The first byte is the bool and the second byte is the enumeration, so
// each of the 65536 patterns is checked against the two rules.
void test_decode_checks_each_part_of_a_class() {
    for (unsigned raw = 0; raw <= 65535; ++raw) {
        unsigned const flag = raw & 0xFFU;
        unsigned const phase = raw >> 8;
        bool const is_value = flag <= 1 && phase >= 3 && phase <= 5;
        assert(decode<Flagged>(opaque(static_cast<std::uint16_t>(raw))).is_some() == is_value);
    }
}

// A base and a nested member are checked at their offsets, and the value
// keeps each byte.
void test_decode_checks_a_base_and_a_nested_member() {
    Nested const nested = decode<Nested>(opaque(Triple{0x09FE, 0x0501, 0x1234})).expect("each part names a value");
    assert(nested.level == Shifted::middle);
    assert(nested.count == 9);
    assert(nested.flags.is_ready);
    assert(nested.flags.phase == Contiguous::third);
    assert(nested.word == 0x1234);
    // A level of 5 in the base, a bool of 3, and a phase of 6 in the member.
    assert(decode<Nested>(opaque(Triple{0x0905, 0x0501, 0x1234})).is_none());
    assert(decode<Nested>(opaque(Triple{0x09FE, 0x0503, 0x1234})).is_none());
    assert(decode<Nested>(opaque(Triple{0x09FE, 0x0601, 0x1234})).is_none());
    // The count and the word take each value.
    assert(decode<Nested>(opaque(Triple{0xFFFE, 0x0300, 0xFFFF})).is_some());
}

// Each element of an array is checked: an array of enumerations, and an
// array of classes.
void test_decode_checks_each_element_of_an_array() {
    Lanes const lanes = decode<Lanes>(opaque(std::uint32_t{0x05040303})).expect("each lane names an enumerator");
    assert(lanes.lanes[0] == Contiguous::first && lanes.lanes[1] == Contiguous::first);
    assert(lanes.lanes[2] == Contiguous::second && lanes.lanes[3] == Contiguous::third);
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        std::uint32_t const shift = 8 * lane;
        std::uint32_t const others = std::uint32_t{0x05040303} & ~(std::uint32_t{0xFF} << shift);
        for (std::uint32_t raw = 0; raw <= 255; ++raw) {
            bool const is_enumerator = raw >= 3 && raw <= 5;
            assert(decode<Lanes>(opaque(others | (raw << shift))).is_some() == is_enumerator);
        }
    }
    // Bytes: is_ready, phase, is_ready, phase.
    assert(decode<FlaggedPair>(opaque(std::uint32_t{0x04010300})).is_some());
    assert(decode<FlaggedPair>(opaque(std::uint32_t{0x04020300})).is_none());
    assert(decode<FlaggedPair>(opaque(std::uint32_t{0x07010300})).is_none());
    assert(decode<FlaggedPair>(opaque(std::uint32_t{0x04010200})).is_none());
}

// A type with no bool and no enumeration takes each bit pattern, and keeps
// it.
void test_decode_keeps_each_bit_pattern_of_a_plain_type() {
    Words const words = decode<Words>(opaque(Words{0x0123456789ABCDEF, 0xFEDCBA9876543210})).expect("two words");
    assert(words.low == 0x0123456789ABCDEF && words.high == 0xFEDCBA9876543210);
    // The steps reach each sign, zero, denormals, infinities and NaN payloads.
    for (std::uint32_t step = 0; step < 4096; ++step) {
        assert(decode<float>(opaque(step * std::uint32_t{0x00100401})).is_some());
        assert(decode<double>(opaque(std::uint64_t{step} * std::uint64_t{0x0010040100100401})).is_some());
    }
    assert(decode<float>(opaque(std::uint32_t{0x7FC00001})).is_some());
    assert(decode<double>(opaque(std::uint64_t{0x7FF0000000000001})).is_some());
}

}  // namespace

int main() {
    test_enum_from_gives_exactly_the_enumerators();
    test_decode_bool_takes_only_zero_and_one();
    test_decode_refuses_a_value_between_enumerators();
    test_decode_checks_each_part_of_a_class();
    test_decode_checks_a_base_and_a_nested_member();
    test_decode_checks_each_element_of_an_array();
    test_decode_keeps_each_bit_pattern_of_a_plain_type();
    crucible::test::pass("test_core_scalar: all tests passed\n");
    return 0;
}

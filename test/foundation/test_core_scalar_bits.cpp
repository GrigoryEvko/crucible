// Tests of foundation/core/Bits.h at run time.  Each operation is compared
// with a model that reads one bit at a time: over each 8-bit value, each
// 16-bit value, and Philox values of 32 and 64 bits.  A rotation is compared
// for a set of counts that holds the negative counts, the counts past the
// width and the limits of int.

#include <foundation/core/Bits.h>

#include <foundation/Platform.h>

#include "philox_stream.h"

#include <cstdint>

namespace {

namespace core = ::foundation::core;
using core::Option;

// The value, through a call that the optimizer cannot read, so each
// operation runs at run time.
template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

template <class T>
[[nodiscard]] bool bit_at(T value, int position) noexcept {
    return static_cast<T>(value >> position) % 2 == 1;
}

template <class T>
[[nodiscard]] T with_bit(T value, int position) noexcept {
    return static_cast<T>(value | static_cast<T>(T{1} << position));
}

template <class T>
[[nodiscard]] int model_popcount(T value) noexcept {
    int count = 0;
    for (int position = 0; position < core::bits_of<T>; ++position) {
        count += bit_at(value, position) ? 1 : 0;
    }
    return count;
}

template <class T>
[[nodiscard]] int model_countl_zero(T value) noexcept {
    int count = 0;
    for (int position = core::bits_of<T> - 1; position >= 0 && !bit_at(value, position); --position) {
        ++count;
    }
    return count;
}

template <class T>
[[nodiscard]] int model_countr_zero(T value) noexcept {
    int count = 0;
    for (int position = 0; position < core::bits_of<T> && !bit_at(value, position); ++position) {
        ++count;
    }
    return count;
}

// The rotation to the left by count: bit i of the result is bit i - count of
// the value, modulo the width.  The count is widened first, so its negation
// and its remainder cannot overflow.
template <class T>
[[nodiscard]] T model_rotl(T value, long long count) noexcept {
    long long const width = core::bits_of<T>;
    long long const step = ((count % width) + width) % width;
    T result{0};
    for (int position = 0; position < core::bits_of<T>; ++position) {
        auto const source = static_cast<int>((position - step + width) % width);
        if (bit_at(value, source)) result = with_bit(result, position);
    }
    return result;
}

template <class T>
[[nodiscard]] T model_low_mask(int count) noexcept {
    T result{0};
    for (int position = 0; position < count && position < core::bits_of<T>; ++position) {
        result = with_bit(result, position);
    }
    return result;
}

template <class T>
[[nodiscard]] T model_byteswap(T value) noexcept {
    T result{0};
    int const bytes = core::bits_of<T> / 8;
    for (int byte = 0; byte < bytes; ++byte) {
        for (int bit = 0; bit < 8; ++bit) {
            if (bit_at(value, byte * 8 + bit)) result = with_bit(result, (bytes - 1 - byte) * 8 + bit);
        }
    }
    return result;
}

// The counts of a rotation and a mask: each count from -130 to 130, and the
// limits of int with the count next to each limit.
template <class Body>
void for_each_count(Body&& body) {
    for (int count = -130; count <= 130; ++count) {
        body(count);
    }
    body(core::min_of<int>);
    body(core::min_of<int> + 1);
    body(core::max_of<int> - 1);
    body(core::max_of<int>);
}

template <class T>
void check_counts(T value) {
    int const width = core::bits_of<T>;
    int const ones = model_popcount(value);
    int const leading = model_countl_zero(value);
    CRUCIBLE_FATAL_INVARIANT(core::popcount(opaque(value)) == ones);
    CRUCIBLE_FATAL_INVARIANT(core::countl_zero(opaque(value)) == leading);
    CRUCIBLE_FATAL_INVARIANT(core::countr_zero(opaque(value)) == model_countr_zero(value));
    CRUCIBLE_FATAL_INVARIANT(core::countl_one(opaque(value)) == model_countl_zero(static_cast<T>(~value)));
    CRUCIBLE_FATAL_INVARIANT(core::countr_one(opaque(value)) == model_countr_zero(static_cast<T>(~value)));
    CRUCIBLE_FATAL_INVARIANT(core::bit_width(opaque(value)) == width - leading);
    CRUCIBLE_FATAL_INVARIANT(core::has_single_bit(opaque(value)) == (ones == 1));

    // The floor is the highest one bit alone, and the ceiling is the next
    // power when the value is not a power, or no value past the top bit.
    T const floor = value == T{0} ? T{0} : with_bit(T{0}, width - 1 - leading);
    CRUCIBLE_FATAL_INVARIANT(core::bit_floor(opaque(value)) == floor);
    Option<T> ceiling = core::bit_ceil(opaque(value));
    if (value <= T{1}) {
        CRUCIBLE_FATAL_INVARIANT(ceiling.is_some() && static_cast<Option<T>&&>(ceiling).value_or(T{0}) == T{1});
    } else if (ones == 1) {
        CRUCIBLE_FATAL_INVARIANT(ceiling.is_some() && static_cast<Option<T>&&>(ceiling).value_or(T{0}) == value);
    } else if (leading == 0) {
        CRUCIBLE_FATAL_INVARIANT(ceiling.is_none());
    } else {
        T const next = with_bit(T{0}, width - leading);
        CRUCIBLE_FATAL_INVARIANT(ceiling.is_some() && static_cast<Option<T>&&>(ceiling).value_or(T{0}) == next);
    }
    CRUCIBLE_FATAL_INVARIANT(core::byteswap(opaque(value)) == model_byteswap(value));
}

template <class T>
void check_rotations(T value) {
    for_each_count([value](int count) {
        T const left = model_rotl(value, count);
        T const right = model_rotl(value, -static_cast<long long>(count));
        CRUCIBLE_FATAL_INVARIANT(core::rotl(opaque(value), opaque(count)) == left);
        CRUCIBLE_FATAL_INVARIANT(core::rotr(opaque(value), opaque(count)) == right);
    });
}

template <class T>
void check_masks() {
    auto const check = [](int count) {
        CRUCIBLE_FATAL_INVARIANT(core::low_mask<T>(opaque(count)) == model_low_mask<T>(count));
    };
    for_each_count(check);
}

void test_each_value_of_8_and_16_bits() {
    for (int value = 0; value < 256; ++value) {
        check_counts(static_cast<std::uint8_t>(value));
        check_rotations(static_cast<std::uint8_t>(value));
    }
    for (int value = 0; value < 65536; ++value) {
        check_counts(static_cast<std::uint16_t>(value));
    }
    ::foundation::test::PhiloxStream stream{50};
    for (int index = 0; index < 4096; ++index) {
        check_rotations(static_cast<std::uint16_t>(stream.next_wide()));
    }
}

void test_philox_values_of_32_and_64_bits() {
    ::foundation::test::PhiloxStream stream{51};
    for (int index = 0; index < 100000; ++index) {
        // A shift of the draw reaches the values with many leading zeros.
        std::uint64_t const wide = stream.next_wide() >> stream.below(64);
        check_counts(wide);
        check_counts(static_cast<std::uint32_t>(wide));
        if (index % 100 == 0) {
            check_rotations(wide);
            check_rotations(static_cast<std::uint32_t>(wide));
        }
    }
    check_counts(core::max_of<std::uint64_t>);
    check_counts(core::max_of<unsigned long long>);
    check_counts(std::uint64_t{0});
}

void test_masks_and_signed_byteswap() {
    check_masks<std::uint8_t>();
    check_masks<std::uint16_t>();
    check_masks<std::uint32_t>();
    check_masks<std::uint64_t>();
    for (int value = -128; value < 128; ++value) {
        auto const byte = static_cast<std::int8_t>(value);
        CRUCIBLE_FATAL_INVARIANT(core::byteswap(opaque(byte)) == byte);
    }
    ::foundation::test::PhiloxStream stream{52};
    for (int index = 0; index < 10000; ++index) {
        std::uint64_t const wide = stream.next_wide();
        auto const signed_wide = static_cast<std::int64_t>(wide);
        auto const signed_half = static_cast<std::int16_t>(wide);
        CRUCIBLE_FATAL_INVARIANT(core::byteswap(opaque(signed_wide))
                                 == static_cast<std::int64_t>(model_byteswap(wide)));
        CRUCIBLE_FATAL_INVARIANT(core::byteswap(opaque(signed_half))
                                 == static_cast<std::int16_t>(model_byteswap(static_cast<std::uint16_t>(wide))));
    }
}

}  // namespace

int main() {
    test_each_value_of_8_and_16_bits();
    test_philox_values_of_32_and_64_bits();
    test_masks_and_signed_byteswap();
    return 0;
}

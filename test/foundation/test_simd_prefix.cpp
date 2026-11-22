// The prefix load and the prefix store of foundation/Simd.h, and the
// division of floating lanes.
//
// Each load reads a heap run of exactly the count of its lanes, so the
// address sanitizer of a Debug build finds a read past the run.  Each store
// writes into the first lanes of a larger run, and the test reads each
// element after them back.  A Release build for AVX-512 takes the masked arm,
// and each other build takes the copy arm.  Each lane type runs at a register
// of 16, 32 and 64 bytes.

#include <foundation/Simd.h>
#include <foundation/core/Array.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Scalar.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace simd = ::foundation::simd;
namespace fc = ::foundation::core;

// The bits of a lane type, for a compare of floating lanes.
template <typename T>
using Bits = std::conditional_t<sizeof(T) == 1, std::uint8_t,
                                std::conditional_t<sizeof(T) == 2, std::uint16_t,
                                                   std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;

// True when the two lanes hold the same bits.
template <typename T>
[[nodiscard]] bool is_same_lane(T left, T right) noexcept {
    return fc::bit_cast<Bits<T>>(left) == fc::bit_cast<Bits<T>>(right);
}

// A quiet NaN of a floating lane type, from its bits.
template <typename T>
[[nodiscard]] T quiet_nan() noexcept {
    if constexpr (sizeof(T) == 4) {
        return fc::bit_cast<T>(std::uint32_t{0x7fc00000u});
    } else {
        return fc::bit_cast<T>(std::uint64_t{0x7ff8000000000000u});
    }
}

// The value of the element at position of a loaded run.
template <typename T>
[[nodiscard]] T value_of(std::size_t position) noexcept {
    return static_cast<T>(position + 1);
}

// The prefix load and the prefix store of each count from zero to the lane
// count.
template <typename T, int Lanes>
void check_prefixes() {
    using V = simd::vec<T, Lanes>;
    constexpr std::size_t lanes = static_cast<std::size_t>(Lanes);
    auto const test = ::foundation::effects::testing::test();
    T const sentinel = static_cast<T>(77);
    for (std::size_t count = 0; count <= lanes; ++count) {
        fc::HeapArray<T> const run = fc::mint_heap_array_with<T>(
            test.alloc, count, [](std::size_t position) noexcept { return value_of<T>(position); });
        V const loaded = simd::load_prefix<V>(fc::View<T const>{run.view()});
        for (int lane = 0; lane < Lanes; ++lane) {
            std::size_t const position = static_cast<std::size_t>(lane);
            T const expected = position < count ? value_of<T>(position) : T{0};
            CRUCIBLE_FATAL_INVARIANT(is_same_lane(loaded[lane], expected));
        }

        fc::HeapArray<T> target = fc::mint_heap_array(test.alloc, lanes + 1, sentinel);
        V const stored{[](auto lane) noexcept { return static_cast<T>(decltype(lane)::value + 10); }};
        target = fc::with_mut(std::move(target), [&stored, count](auto cells) noexcept {
            simd::store_prefix(stored, cells.view().window(0, count).expect("the count is at most the lane count"));
        });
        std::size_t position = 0;
        for (T const element : target) {
            T const expected = position < count ? static_cast<T>(position + 10) : sentinel;
            CRUCIBLE_FATAL_INVARIANT(is_same_lane(element, expected));
            ++position;
        }
    }
}

// The registers of 16, 32 and 64 bytes for one lane type.
template <typename T>
void check_lane_type() {
    check_prefixes<T, static_cast<int>(16 / sizeof(T))>();
    check_prefixes<T, static_cast<int>(32 / sizeof(T))>();
    check_prefixes<T, static_cast<int>(64 / sizeof(T))>();
}

// The division of each lane is the IEEE division of the scalar step, also
// for a dividend of zero of each sign, for a NaN divisor, and for a quotient
// past the largest finite number.  No divisor is zero: full UBSan refuses a
// float division by zero in the scalar step.
template <typename T, int Lanes>
void check_division() {
    using V = simd::vec<T, Lanes>;
    fc::FixedArray<T, 8> const dividends{
        T{1}, T{-3}, T{0}, T{7}, T{1}, static_cast<T>(-0.0), static_cast<T>(2.5), static_cast<T>(1e30)};
    fc::FixedArray<T, 8> const divisors{T{3},  T{7}, static_cast<T>(0.5), static_cast<T>(-0.25),
                                        T{-8}, T{5}, quiet_nan<T>(),      static_cast<T>(1e-30)};
    V const left{[&dividends](auto lane) noexcept {
        return *dividends.get(static_cast<std::size_t>(decltype(lane)::value) % 8).expect("eight dividends");
    }};
    V const right{[&divisors](auto lane) noexcept {
        return *divisors.get(static_cast<std::size_t>(decltype(lane)::value) % 8).expect("eight divisors");
    }};
    V const quotient = left / right;
    for (int lane = 0; lane < Lanes; ++lane) {
        std::size_t const position = static_cast<std::size_t>(lane) % 8;
        T const expected = *dividends.get(position).expect("eight dividends") / *divisors.get(position).expect("eight");
        CRUCIBLE_FATAL_INVARIANT(is_same_lane(quotient[lane], expected));
    }
}

// The bits of an infinity of a floating lane type, with its sign.
template <typename T>
[[nodiscard]] Bits<T> infinity_bits(bool is_negative) noexcept {
    if constexpr (sizeof(T) == 4) {
        return is_negative ? std::uint32_t{0xff800000u} : std::uint32_t{0x7f800000u};
    } else {
        return is_negative ? std::uint64_t{0xfff0000000000000u} : std::uint64_t{0x7ff0000000000000u};
    }
}

// True when the lane holds a NaN of either sign and of each payload.
template <typename T>
[[nodiscard]] bool is_nan_lane(T lane) noexcept {
    Bits<T> const sign = infinity_bits<T>(true) ^ infinity_bits<T>(false);
    Bits<T> const magnitude = fc::bit_cast<Bits<T>>(lane) & static_cast<Bits<T>>(~sign);
    return magnitude > infinity_bits<T>(false);
}

// The division of each lane by a zero of each sign is the IEEE result: an
// infinity with the sign of the quotient, or a NaN for a zero dividend.  The
// expected bits are written out, because full UBSan refuses a float division
// by zero in a scalar step.  The bits of the NaN depend on the target, so the
// test accepts each NaN.
template <typename T, int Lanes>
void check_division_by_zero() {
    using V = simd::vec<T, Lanes>;
    fc::FixedArray<T, 4> const dividends{T{1}, T{-1}, T{1}, T{0}};
    fc::FixedArray<T, 4> const divisors{T{0}, T{0}, static_cast<T>(-0.0), static_cast<T>(-0.0)};
    V const left{[&dividends](auto lane) noexcept {
        return *dividends.get(static_cast<std::size_t>(decltype(lane)::value) % 4).expect("four dividends");
    }};
    V const right{[&divisors](auto lane) noexcept {
        return *divisors.get(static_cast<std::size_t>(decltype(lane)::value) % 4).expect("four divisors");
    }};
    V const quotient = left / right;
    // The NaN test accepts a quiet NaN and refuses the infinity of lane 0.
    CRUCIBLE_FATAL_INVARIANT(is_nan_lane(quiet_nan<T>()) && !is_nan_lane(quotient[0]));
    for (int lane = 0; lane < Lanes; ++lane) {
        std::size_t const position = static_cast<std::size_t>(lane) % 4;
        Bits<T> const bits = fc::bit_cast<Bits<T>>(quotient[lane]);
        if (position == 3) {
            CRUCIBLE_FATAL_INVARIANT(is_nan_lane(quotient[lane]));
        } else {
            CRUCIBLE_FATAL_INVARIANT(bits == infinity_bits<T>(position != 0));
        }
    }
}

}  // namespace

int main() {
    check_lane_type<std::uint8_t>();
    check_lane_type<std::uint16_t>();
    check_lane_type<std::uint32_t>();
    check_lane_type<float>();
    check_lane_type<double>();
    check_lane_type<std::uint64_t>();
    check_division<float, 16>();
    check_division<double, 8>();
    check_division<float, 4>();
    check_division_by_zero<float, 16>();
    check_division_by_zero<double, 8>();
    check_division_by_zero<float, 4>();
    return 0;
}

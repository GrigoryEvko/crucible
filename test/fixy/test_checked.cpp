// The test of the arithmetic sets through their names in namespace fixy,
// and of the compile-time capacity gates of fixy/Checked.h.
//
// fixy/Core.h puts the checked_*, wrapping_* and saturating_* sets of the
// Scalar family into namespace fixy.  Each set names its overflow behavior,
// and the three sets answer differently at the same operands.  The trapping
// form of an operation is its checked form and expect.  The static_asserts
// hold the corner cases at compile time, and the run-time part repeats them
// through operands that the optimizer cannot read.

#include <fixy/Checked.h>
#include <fixy/Core.h>
#include <foundation/Platform.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace {

using ::fixy::checked_abs;
using ::fixy::checked_add;
using ::fixy::checked_div;
using ::fixy::checked_mul;
using ::fixy::checked_neg;
using ::fixy::checked_rem;
using ::fixy::checked_shl;
using ::fixy::checked_shr;
using ::fixy::checked_sub;
using ::fixy::max_of;
using ::fixy::min_of;
using ::fixy::Option;
using ::fixy::saturating_add;
using ::fixy::saturating_mul;
using ::fixy::saturating_sub;
using ::fixy::wrapping_add;
using ::fixy::wrapping_mul;
using ::fixy::wrapping_sub;

using I8 = std::int8_t;
using U8 = std::uint8_t;

constexpr I8 i8_min = min_of<I8>;
constexpr I8 i8_max = max_of<I8>;

// The value, through a call that the optimizer cannot read, so each
// operation runs at run time.
template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

template <class T>
[[nodiscard]] constexpr bool holds(Option<T> result, T expected) noexcept {
    return result.is_some() && static_cast<Option<T>&&>(result).value_or(T{0}) == expected;
}

template <class T>
[[nodiscard]] constexpr bool empty(Option<T> result) noexcept {
    return result.is_none();
}

// True when the two results hold the same value, or are both empty.
template <class T>
[[nodiscard]] constexpr bool agree(Option<T> left, Option<T> right) noexcept {
    if (left.is_some() != right.is_some()) return false;
    return static_cast<Option<T>&&>(left).value_or(T{0}) == static_cast<Option<T>&&>(right).value_or(T{0});
}

// The checked set reports and does not produce.
static_assert(holds(checked_add<U8>(200, 55), U8{255}));
static_assert(empty(checked_add<U8>(200, 56)));
static_assert(empty(checked_sub<U8>(0, 1)));
static_assert(holds(checked_mul<U8>(16, 15), U8{240}));
static_assert(empty(checked_mul<U8>(16, 16)));
static_assert(empty(checked_add<I8>(i8_max, 1)));
static_assert(empty(checked_sub<I8>(i8_min, 1)));

// Division has two failures, and the remainder of the same operands does
// not share the second one.
static_assert(empty(checked_div<I8>(7, 0)));
static_assert(empty(checked_div<I8>(i8_min, -1)));
static_assert(empty(checked_rem<I8>(7, 0)));
static_assert(holds(checked_rem<I8>(i8_min, -1), I8{0}));
static_assert(holds(checked_div<I8>(-7, 2), I8{-3}));
static_assert(holds(checked_rem<I8>(-7, 2), I8{-1}));

// The negation and the absolute value fail at the one value with no
// positive twin.  An unsigned value has a negation only for 0, and it has
// no absolute value form.
static_assert(empty(checked_neg<I8>(i8_min)));
static_assert(holds(checked_neg<I8>(i8_max), static_cast<I8>(-i8_max)));
static_assert(holds(checked_neg<U8>(0), U8{0}) && empty(checked_neg<U8>(1)));
static_assert(empty(checked_abs<I8>(i8_min)));
static_assert(holds(checked_abs<I8>(-7), I8{7}));
static_assert(holds(checked_abs<I8>(7), I8{7}));

template <class T>
concept HasCheckedAbs = requires(T value) { checked_abs(value); };
static_assert(HasCheckedAbs<I8>);
static_assert(!HasCheckedAbs<U8>);

// A left shift gives the exact product value * 2^shift or no value.  The
// shift has no value when the count leaves the width, when a bit moves out
// of the width, and when the sign changes.  A negative value that keeps
// each bit shifts as each other value does.
static_assert(holds(checked_shl<U8>(1, 7), U8{128}));
static_assert(empty(checked_shl<U8>(1, 8)));
static_assert(empty(checked_shl<U8>(1, -1)));
static_assert(empty(checked_shl<U8>(0xFF, 4)));
static_assert(empty(checked_shl<std::uint32_t>(0xFFFFFFFFu, 4)));
static_assert(empty(checked_shl<std::int32_t>(1, 31)));
static_assert(empty(checked_shl<I8>(64, 1)));
static_assert(holds(checked_shl<I8>(-1, 1), I8{-2}));
static_assert(holds(checked_shl<I8>(-1, 7), i8_min));
static_assert(holds(checked_shl<I8>(-64, 1), i8_min));
static_assert(empty(checked_shl<I8>(-65, 1)));
static_assert(holds(checked_shr<U8>(128, 7), U8{1}));
static_assert(empty(checked_shr<U8>(128, 8)));
static_assert(holds(checked_shr<I8>(-8, 1), I8{-4}));

// The reference of checked_shl is the product value * 2^shift in a signed
// 128-bit integer.  That type holds the product for each type of 64 bits or
// less, so the reference cannot lose a bit.  Its answer is the product if
// the type holds it, and no value if the type does not.
__extension__ using WideProduct = __int128;

template <class T>
[[nodiscard]] constexpr Option<T> shl_reference(T value, int shift) noexcept {
    WideProduct const product = static_cast<WideProduct>(value) * (WideProduct{1} << shift);
    if (product < static_cast<WideProduct>(min_of<T>) || product > static_cast<WideProduct>(max_of<T>)) {
        return ::fixy::none;
    }
    return Option<T>::some(static_cast<T>(product));
}

// Each 8-bit value at each shift from 0 to 7, at compile time.
template <class T>
[[nodiscard]] consteval bool shl_matches_reference_for_each_8_bit_value() noexcept {
    for (unsigned bits = 0; bits < 256u; ++bits) {
        auto const value = static_cast<T>(bits);
        for (int shift = 0; shift < 8; ++shift) {
            if (!agree(checked_shl<T>(value, shift), shl_reference<T>(value, shift))) return false;
        }
    }
    return true;
}
static_assert(shl_matches_reference_for_each_8_bit_value<I8>());
static_assert(shl_matches_reference_for_each_8_bit_value<U8>());

// The wrapping set gives the truncated value of the hardware, and the
// saturating set gives the end of the range.  At the same operands the
// three sets answer differently.
static_assert(wrapping_add<U8>(200, 56) == 0);
static_assert(saturating_add<U8>(200, 56) == 255);
static_assert(empty(checked_add<U8>(200, 56)));

static_assert(wrapping_sub<U8>(0, 1) == 255);
static_assert(saturating_sub<U8>(0, 1) == 0);
static_assert(wrapping_mul<U8>(16, 16) == 0);
static_assert(saturating_mul<U8>(16, 16) == 255);
static_assert(saturating_add<I8>(i8_min, -1) == i8_min);
static_assert(saturating_mul<I8>(-16, -16) == i8_max);

// The three sets agree at each 8-bit pair whose sum fits.  A defect in one
// set shows as a disagreement here.
[[nodiscard]] consteval bool sets_agree_where_the_result_fits_u8() noexcept {
    for (unsigned first = 0; first < 256u; ++first) {
        for (unsigned second = 0; second < 256u; ++second) {
            auto const left = static_cast<U8>(first);
            auto const right = static_cast<U8>(second);
            bool const sum_fits = first + second <= 255u;
            if (checked_add(left, right).is_some() != sum_fits) return false;
            if (sum_fits) {
                if (!holds(checked_add(left, right), wrapping_add(left, right))) return false;
                if (!holds(checked_add(left, right), saturating_add(left, right))) return false;
            } else {
                if (saturating_add(left, right) != U8{255}) return false;
                if (wrapping_add(left, right) != static_cast<U8>(first + second)) return false;
            }
        }
    }
    return true;
}
static_assert(sets_agree_where_the_result_fits_u8());

// The trapping form of an operation is its checked form and expect.  It
// returns the exact result, and it ends the process where the checked form
// gives no value.  expect calls fatal(), which is not constexpr, so a
// constant evaluation of the end is no constant.  A trap that returned a
// wrapped value would fold, and the refusals below would then fail.
template <auto Operation, auto Left, auto Right>
concept FoldsToAConstant = requires { typename std::integral_constant<decltype(Left), Operation(Left, Right)>; };

inline constexpr auto trapping_add = [](auto left, auto right) {
    return checked_add(left, right).expect("the sum fits");
};
inline constexpr auto trapping_sub = [](auto left, auto right) {
    return checked_sub(left, right).expect("the difference fits");
};
inline constexpr auto trapping_mul = [](auto left, auto right) {
    return checked_mul(left, right).expect("the product fits");
};
inline constexpr auto trapping_div = [](auto left, auto right) {
    return checked_div(left, right).expect("the quotient fits");
};

static_assert(trapping_sub(U8{10}, U8{3}) == 7);
static_assert(trapping_mul(U8{16}, U8{15}) == 240);
static_assert(trapping_div(I8{-7}, I8{2}) == -3);
static_assert(trapping_div(U8{255}, U8{5}) == 51);

static_assert(FoldsToAConstant<trapping_add, U8{200}, U8{55}>);
static_assert(!FoldsToAConstant<trapping_add, U8{200}, U8{56}>);
static_assert(FoldsToAConstant<trapping_sub, U8{1}, U8{1}>);
static_assert(!FoldsToAConstant<trapping_sub, U8{0}, U8{1}>);
static_assert(!FoldsToAConstant<trapping_sub, i8_min, I8{1}>);
static_assert(FoldsToAConstant<trapping_mul, U8{16}, U8{15}>);
static_assert(!FoldsToAConstant<trapping_mul, U8{16}, U8{16}>);
static_assert(!FoldsToAConstant<trapping_mul, I8{-16}, I8{-16}>);
static_assert(FoldsToAConstant<trapping_div, i8_min, I8{1}>);
static_assert(!FoldsToAConstant<trapping_div, I8{7}, I8{0}>);
static_assert(!FoldsToAConstant<trapping_div, i8_min, I8{-1}>);
static_assert(!FoldsToAConstant<trapping_div, U8{7}, U8{0}>);

// Each set takes an integer and nothing else.  A floating-point operand has
// no overflow flag to report, and a bool or a character is no number.
template <class T>
concept SomeSetTakes = requires(T value) { checked_add(value, value); } || requires(T value) {
    wrapping_add(value, value);
} || requires(T value) { saturating_add(value, value); } || requires(T value) { checked_shl(value, 1); };
static_assert(SomeSetTakes<int>);
static_assert(!SomeSetTakes<double>);
static_assert(!SomeSetTakes<float>);
static_assert(!SomeSetTakes<bool>);
static_assert(!SomeSetTakes<char>);

// The compile-time budget gates carry the check through each step of a sum
// of products.
static_assert(::fixy::safe_add<std::size_t, 40u, 24u> == 64u);
static_assert(::fixy::safe_sub<std::size_t, 64u, 24u> == 40u);
static_assert(::fixy::safe_mul<std::size_t, std::size_t{1} << 20, std::size_t{1} << 20> == std::size_t{1} << 40);
static_assert(::fixy::safe_capacity<64u, 64u> == 4096u);
static_assert(::fixy::safe_array_bytes<std::uint32_t, 16u> == 64u);
static_assert(::fixy::safe_add_all<std::size_t, 8u, 16u, 40u> == 64u);
static_assert(::fixy::safe_struct_bytes<std::uint64_t, std::uint64_t> == 16u);
static_assert(::fixy::bytes_fit_v<64u, ::fixy::safe_struct_bytes<std::uint64_t, std::uint64_t>>);

// The same corners, at run time.
void check_runtime_matches_consteval() {
    U8 const big = opaque(U8{200});
    U8 const step = opaque(U8{56});

    CRUCIBLE_FATAL_INVARIANT(empty(checked_add(big, step)));
    CRUCIBLE_FATAL_INVARIANT(wrapping_add(big, step) == 0u);
    CRUCIBLE_FATAL_INVARIANT(saturating_add(big, step) == 255u);
    CRUCIBLE_FATAL_INVARIANT(trapping_add(big, U8{55}) == 255u);
    CRUCIBLE_FATAL_INVARIANT(trapping_sub(big, step) == 144u);
    CRUCIBLE_FATAL_INVARIANT(trapping_mul(step, U8{4}) == 224u);
    CRUCIBLE_FATAL_INVARIANT(trapping_div(big, step) == 3u);

    I8 const low = opaque(i8_min);
    I8 const minus_one = opaque(I8{-1});
    CRUCIBLE_FATAL_INVARIANT(empty(checked_div(low, minus_one)));
    CRUCIBLE_FATAL_INVARIANT(holds(checked_rem(low, minus_one), I8{0}));
    CRUCIBLE_FATAL_INVARIANT(empty(checked_neg(low)));
    CRUCIBLE_FATAL_INVARIANT(empty(checked_abs(low)));

    int const width = opaque(8);
    CRUCIBLE_FATAL_INVARIANT(empty(checked_shl<U8>(1, width)));
    CRUCIBLE_FATAL_INVARIANT(empty(checked_shr<U8>(128, width)));
    CRUCIBLE_FATAL_INVARIANT(holds(checked_shl<U8>(1, width - 1), U8{128}));
}

// Each value of the shift grid at each shift from 0 to the width minus one,
// against the 128-bit product.  The shifts at the width and at -1 give no
// value for each value.  Linear in the width.
template <class T>
void check_shl_at(T value) {
    constexpr int width = ::fixy::bits_of<T>;
    for (int step = 0; step < width; ++step) {
        int const shift = opaque(step);
        CRUCIBLE_FATAL_INVARIANT(agree(checked_shl<T>(value, shift), shl_reference<T>(value, shift)));
    }
    CRUCIBLE_FATAL_INVARIANT(empty(checked_shl<T>(value, opaque(width))));
    CRUCIBLE_FATAL_INVARIANT(empty(checked_shl<T>(value, opaque(-1))));
}

// The values of the shift grid of one type.  An 8-bit or a 16-bit type gives
// each value.  A wider type gives 0, the minimum, the maximum, two
// alternating bit patterns, and each power of two, each power of two plus
// and minus one, and the negation of each.  These values put the highest
// set bit and the sign bit at each position of the width.  Complexity:
// O(values * width).
template <class T>
void check_shl_grid_of() {
    using Bits = std::make_unsigned_t<T>;
    constexpr int width = ::fixy::bits_of<T>;
    if constexpr (width <= 16) {
        for (std::uint32_t bits = 0; bits <= max_of<Bits>; ++bits) {
            check_shl_at(static_cast<T>(bits));
        }
    } else {
        check_shl_at(T{0});
        check_shl_at(min_of<T>);
        check_shl_at(max_of<T>);
        check_shl_at(static_cast<T>(static_cast<Bits>(0x5555555555555555ull)));
        check_shl_at(static_cast<T>(static_cast<Bits>(0xAAAAAAAAAAAAAAAAull)));
        for (int position = 0; position < width; ++position) {
            auto const power = static_cast<Bits>(Bits{1} << position);
            for (int offset = -1; offset <= 1; ++offset) {
                auto const near = static_cast<Bits>(power + static_cast<Bits>(offset));
                check_shl_at(static_cast<T>(near));
                check_shl_at(static_cast<T>(static_cast<Bits>(Bits{0} - near)));
            }
        }
    }
}

void check_shl_grid() {
    check_shl_grid_of<std::int8_t>();
    check_shl_grid_of<std::uint8_t>();
    check_shl_grid_of<std::int16_t>();
    check_shl_grid_of<std::uint16_t>();
    check_shl_grid_of<std::int32_t>();
    check_shl_grid_of<std::uint32_t>();
    check_shl_grid_of<std::int64_t>();
    check_shl_grid_of<std::uint64_t>();
}

}  // namespace

int main() {
    check_runtime_matches_consteval();
    check_shl_grid();
    return 0;
}

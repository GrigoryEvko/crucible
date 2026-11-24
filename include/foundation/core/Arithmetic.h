#pragma once

// The Scalar family: arithmetic on an Integer that names the result of an
// overflow at the call site.
//
//   checked_add, checked_sub, checked_mul   the exact result, or no value
//   checked_div, checked_rem                no value for a zero divisor, and
//                                           the quotient of the minimum by
//                                           -1 has no value
//   checked_neg                             the exact negation, or no value
//   checked_abs                             the absolute value of a signed
//                                           value, or no value for the
//                                           minimum
//   checked_shl(v, n), checked_shr(v, n)    no value for a count outside
//                                           [0, bits), and checked_shl has
//                                           no value when v * 2^n does not
//                                           fit
//   wrapping_add, wrapping_sub, wrapping_mul, wrapping_neg
//                                           the result modulo 2^bits
//   wrapping_shl(v, n), wrapping_shr(v, n)  the count modulo bits
//   saturating_add, saturating_sub, saturating_mul
//                                           the exact result, or the
//                                           nearest limit of the type
//   saturating_div                          the quotient, and the maximum
//                                           for the minimum by -1.  A zero
//                                           divisor ends the process
//   div_ceil(a, b)                          the quotient rounded toward
//                                           positive infinity.  A zero
//                                           divisor and the minimum by -1
//                                           end the process
//   gcd(a, b)                             the greatest common divisor as
//                                           an unsigned value, for each input
//   midpoint(a, b)                          the floor of the exact mean of a
//                                           and b, for each pair.  The two
//                                           orders of the operands give one
//                                           result
//   widening_mul(a, b)                     the full product of two unsigned
//                                           values, as two halves
//   checked_mul_div(a, b, c)                the floor of a * b / c for an
//                                           unsigned type, from the exact
//                                           product, or no value for a zero
//                                           c and for a quotient past the
//                                           type
//   mul_div_floor(a, b, c)                  the same floor.  A zero c and a
//                                           quotient past the type end the
//                                           process
//   strict_add, strict_sub, strict_mul      the exact result.  A result that
//                                           the type does not hold ends the
//                                           process
//   strict_div, strict_rem                  the quotient and the remainder.
//                                           A zero divisor ends the process,
//                                           and so does the quotient of the
//                                           minimum by -1.  The minimum rem
//                                           -1 is 0
//   strict_shl(v, n), strict_shr(v, n)      the shift.  A count outside
//                                           [0, bits) ends the process, and
//                                           so does a strict_shl whose
//                                           v * 2^n does not fit
//   Strict<T>                               one T whose +, -, *, /, %, <<
//                                           and >> are the strict forms
//
// Each operation takes two operands of one Integer type, so a pair of
// different types does not compile and no promotion hides a conversion.
// No result is undefined for an input.  The modular forms use the overflow
// builtins, so no result changes with -fno-strict-overflow.  A checked form
// costs one test of the flag of the builtin.  The trapping form of an
// operation is its checked form and Option::expect with a reason.
//
// Strict forms
//   A strict form is the trapping form with no Option: the exact result, or
//   the end of the process in each build, with a report of the operation,
//   the two operands, the operand type and the site of the call.  It is for
//   own data, whose overflow is a defect of the program.  Outside data takes
//   a checked form at its boundary, so a bad value refuses with an error
//   and does not end the process.  A strict add, sub or mul costs the test
//   of the flag of its builtin and one branch that the hint predicts, and
//   the optimizer removes the two where it knows the ranges of the
//   operands.  A strict div and rem cost the tests of the checked form.
//   The report is a cold function out of line in
//   src/foundation/core/Report.cpp, because the formatter of the operands is
//   above this header.  The caller loads the parts of the report into
//   registers and calls it, so the hot path keeps only the branch.  An
//   overflow of a strict form in a constant expression is no constant, so
//   the build stops at the call.
//
//   Each strict function takes the Site of its caller as a default
//   argument.  An operator takes no default argument, so the report of an
//   operator of Strict<T> names the operator in this header, and the stack
//   of the core names the caller.  A size computation whose report must
//   name its own site calls the strict function.
//
//   Strict<T> holds one T, with its size and its bits.  Its constructor is
//   explicit and takes only a T, so no other integer type, bool, character,
//   enumeration or Strict of a different type becomes a Strict, and raw()
//   is the one road out.  Each operator takes two Strict of one type, so a
//   Strict does not mix with a T or with a Strict of a different type.  A
//   shift takes an int count, as each count of the family.
//
// A count of a shift is an int, as each count of the family.  In C++20 a
// shift of a negative signed value is defined, and so is the conversion of
// an unsigned value to a signed type: both are modular.  So the right shift
// of a negative value is the floor of the quotient by 2^n.
//
// The remainder has the sign of the dividend: -7 rem 3 is -1.  The minimum
// rem -1 is 0, although the quotient of the same operands has no value.
//
// A quotient has no nearest limit for a zero divisor, so saturating_div
// ends the process through fatal() there, as std::saturating_div has the
// precondition of a divisor other than 0.  div_ceil ends the process for
// the same divisor, and for the minimum by -1, whose quotient 2^(bits-1) the
// type does not hold.  For each other pair it computes no sum, so it has no
// overflow where (a + b - 1) / b has one: the truncated quotient gets 1
// when the remainder is not 0 and the exact quotient is positive.  The
// saturating form of a cast
// is saturating_narrow of Convert.h.  For an integer source it gives the
// value of std::saturating_cast.
//
// gcd(0, 0) is 0, and gcd(min_of<std::int64_t>, 0) is 2^63, which the
// unsigned result holds.  The loop is the binary algorithm: at most one
// step for each bit of the operands.
//
// WideProduct holds the low half first.  The x86-64 System V ABI gives the
// first half back in rax and the second in rdx, the two registers of the
// result of mul, so the return moves no register.
//
// The quotient of mul_div takes the full product of a * b, so no product
// overflows.  For a 64-bit type the product has 128 bits.  The quotient fits
// the type exactly when the high half of the product is below c, so one
// compare refuses each quotient past the type before the division.  A
// product with a high half of zero takes one division of 64 bits.  Each
// other product takes the division of 128 bits by 64 bits of libgcc, whose
// last step on x86_64 is one divq.  A narrower type takes its product in 64
// bits.  The result is an integer function of the operands, so it has the
// same bits on each target, where a float or a long double does not.

#include <foundation/core/Choice.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Number.h>

#include <compare>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace detail {

__extension__ using WideWord = unsigned __int128;

// The magnitude of a value as an unsigned value of the same width, for each
// value: the minimum of a signed type is 2^(bits-1).
template <Integer T>
[[nodiscard]] constexpr std::make_unsigned_t<T> magnitude_of(T value) noexcept {
    using Bits = std::make_unsigned_t<T>;
    if constexpr (std::is_signed_v<T>) {
        return value < T{0} ? static_cast<Bits>(Bits{0} - static_cast<Bits>(value)) : static_cast<Bits>(value);
    } else {
        return value;
    }
}

// The count of a shift modulo the width of T.  The width is a power of two,
// so the modulo is one AND, also for a negative count.
template <Integer T>
[[nodiscard]] constexpr unsigned shift_step_of(int count) noexcept {
    return static_cast<unsigned>(count) & static_cast<unsigned>(bits_of<T> - 1);
}

}  // namespace detail

template <Integer T>
[[nodiscard]] constexpr Option<T> checked_add(T left, T right) noexcept {
    T sum{};
    if (__builtin_add_overflow(left, right, &sum)) return none;
    return Option<T>::some(sum);
}

template <Integer T>
[[nodiscard]] constexpr Option<T> checked_sub(T left, T right) noexcept {
    T difference{};
    if (__builtin_sub_overflow(left, right, &difference)) return none;
    return Option<T>::some(difference);
}

template <Integer T>
[[nodiscard]] constexpr Option<T> checked_mul(T left, T right) noexcept {
    T product{};
    if (__builtin_mul_overflow(left, right, &product)) return none;
    return Option<T>::some(product);
}

namespace detail {

// True for the quotient of the minimum by -1, which a signed type does not
// hold.
template <Integer T>
[[nodiscard]] constexpr bool is_minimum_by_minus_one(T dividend, T divisor) noexcept {
    if constexpr (std::is_signed_v<T>) {
        return dividend == min_of<T> && divisor == T{-1};
    } else {
        return false;
    }
}

}  // namespace detail

template <Integer T>
[[nodiscard]] constexpr Option<T> checked_div(T dividend, T divisor) noexcept {
    if (divisor == T{0} || detail::is_minimum_by_minus_one(dividend, divisor)) return none;
    return Option<T>::some(static_cast<T>(dividend / divisor));
}

template <Integer T>
[[nodiscard]] constexpr Option<T> checked_rem(T dividend, T divisor) noexcept {
    if (divisor == T{0}) return none;
    if constexpr (std::is_signed_v<T>) {
        if (divisor == T{-1}) return Option<T>::some(T{0});
    }
    return Option<T>::some(static_cast<T>(dividend % divisor));
}

// An unsigned value has a negation only for 0.
template <Integer T>
[[nodiscard]] constexpr Option<T> checked_neg(T value) noexcept {
    T negated{};
    if (__builtin_sub_overflow(T{0}, value, &negated)) return none;
    return Option<T>::some(negated);
}

// An unsigned value has no sign, so the form takes a signed type only.
// unsigned_abs of Compare.h gives the magnitude of each value.
template <SignedInteger T>
[[nodiscard]] constexpr Option<T> checked_abs(T value) noexcept {
    if (value < T{0}) return checked_neg(value);
    return Option<T>::some(value);
}

// The right shift gives back the value only when the left shift lost no
// bit and kept the sign.
template <Integer T>
[[nodiscard]] constexpr Option<T> checked_shl(T value, int count) noexcept {
    if (count < 0 || count >= bits_of<T>) return none;
    using Bits = std::make_unsigned_t<T>;
    T const shifted = static_cast<T>(static_cast<Bits>(static_cast<Bits>(value) << count));
    if (static_cast<T>(shifted >> count) != value) return none;
    return Option<T>::some(shifted);
}

template <Integer T>
[[nodiscard]] constexpr Option<T> checked_shr(T value, int count) noexcept {
    if (count < 0 || count >= bits_of<T>) return none;
    return Option<T>::some(static_cast<T>(value >> count));
}

template <Integer T>
[[nodiscard]] constexpr T wrapping_add(T left, T right) noexcept {
    T sum{};
    static_cast<void>(__builtin_add_overflow(left, right, &sum));
    return sum;
}

template <Integer T>
[[nodiscard]] constexpr T wrapping_sub(T left, T right) noexcept {
    T difference{};
    static_cast<void>(__builtin_sub_overflow(left, right, &difference));
    return difference;
}

template <Integer T>
[[nodiscard]] constexpr T wrapping_mul(T left, T right) noexcept {
    T product{};
    static_cast<void>(__builtin_mul_overflow(left, right, &product));
    return product;
}

template <Integer T>
[[nodiscard]] constexpr T wrapping_neg(T value) noexcept {
    T negated{};
    static_cast<void>(__builtin_sub_overflow(T{0}, value, &negated));
    return negated;
}

template <Integer T>
[[nodiscard]] constexpr T wrapping_shl(T value, int count) noexcept {
    using Bits = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<Bits>(static_cast<Bits>(value) << detail::shift_step_of<T>(count)));
}

template <Integer T>
[[nodiscard]] constexpr T wrapping_shr(T value, int count) noexcept {
    return static_cast<T>(value >> detail::shift_step_of<T>(count));
}

// The exact result is the likely path.  With no hint, GCC 16 puts the
// clamp on the fall-through path, and the exact result costs two taken
// jumps.  An overflow of a sum goes the way of the sign of the second
// operand.
template <Integer T>
[[nodiscard]] constexpr T saturating_add(T left, T right) noexcept {
    T sum{};
    if (!__builtin_add_overflow(left, right, &sum)) [[likely]] {
        return sum;
    }
    if constexpr (std::is_signed_v<T>) {
        return right < T{0} ? min_of<T> : max_of<T>;
    } else {
        return max_of<T>;
    }
}

template <Integer T>
[[nodiscard]] constexpr T saturating_sub(T left, T right) noexcept {
    T difference{};
    if (!__builtin_sub_overflow(left, right, &difference)) [[likely]] {
        return difference;
    }
    if constexpr (std::is_signed_v<T>) {
        return right < T{0} ? max_of<T> : min_of<T>;
    } else {
        return T{0};
    }
}

template <Integer T>
[[nodiscard]] constexpr T saturating_mul(T left, T right) noexcept {
    T product{};
    if (!__builtin_mul_overflow(left, right, &product)) [[likely]] {
        return product;
    }
    if constexpr (std::is_signed_v<T>) {
        return (left < T{0}) != (right < T{0}) ? min_of<T> : max_of<T>;
    } else {
        return max_of<T>;
    }
}

template <Integer T>
[[nodiscard]] constexpr T saturating_div(T dividend, T divisor) noexcept {
    if (divisor == T{0}) [[unlikely]] {
        fatal("saturating_div got the divisor 0, which gives no quotient");
    }
    if (detail::is_minimum_by_minus_one(dividend, divisor)) [[unlikely]] {
        return max_of<T>;
    }
    return static_cast<T>(dividend / divisor);
}

// The remainder has the sign of the dividend, so the exact quotient is above
// the truncated one when the remainder is not 0 and the remainder and the
// divisor have one sign.  A remainder other than 0 needs a divisor of 2 or
// more in magnitude, so the truncated quotient is at most half the dividend,
// and the step of 1 does not overflow.
template <Integer T>
[[nodiscard]] constexpr T div_ceil(T dividend, T divisor) noexcept {
    if (divisor == T{0}) [[unlikely]] {
        fatal("div_ceil got the divisor 0, which gives no quotient");
    }
    if (detail::is_minimum_by_minus_one(dividend, divisor)) [[unlikely]] {
        fatal("div_ceil got the minimum by -1, whose quotient the type does not hold");
    }
    auto const quotient = static_cast<T>(dividend / divisor);
    auto const remainder = static_cast<T>(dividend % divisor);
    bool rounds_up = remainder != T{0};
    if constexpr (std::is_signed_v<T>) {
        rounds_up = rounds_up && (remainder < T{0}) == (divisor < T{0});
    }
    return rounds_up ? static_cast<T>(quotient + T{1}) : quotient;
}

template <Integer T>
[[nodiscard]] constexpr std::make_unsigned_t<T> gcd(T left, T right) noexcept {
    using Bits = std::make_unsigned_t<T>;
    Bits first = detail::magnitude_of(left);
    Bits second = detail::magnitude_of(right);
    if (first == 0) return second;
    if (second == 0) return first;
    int const shared = __builtin_ctzg(static_cast<Bits>(first | second));
    first = static_cast<Bits>(first >> __builtin_ctzg(first));
    do {
        second = static_cast<Bits>(second >> __builtin_ctzg(second));
        if (first > second) {
            Bits const smaller = second;
            second = first;
            first = smaller;
        }
        second = static_cast<Bits>(second - first);
    } while (second != 0);
    return static_cast<Bits>(first << shared);
}

// The floor of the exact mean of two values, for each pair.  It rounds toward
// negative infinity, and not toward its first operand as std::midpoint does.
// So the result is the same for the two orders of the operands, and the two
// parameters of one type can swap with no change of meaning.  The result lies between the two
// operands, so the type holds it, and no step overflows.  A type of fewer
// than 64 bits takes the sum in 64 bits, with one add and one shift.  A wider
// type takes the common bits and half of the different bits: the sum of two
// values is twice their common bits plus their different bits.  A right shift
// of a negative value is the floor of its half.  For a binary search on
// [low, high), the result is low + (high - low) / 2, with no precondition on
// the order.
template <Integer T>
[[nodiscard]] constexpr T midpoint(T first, T second) noexcept {
    if constexpr (sizeof(T) < sizeof(std::uint64_t)) {
        using Wide = std::conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t>;
        return static_cast<T>((static_cast<Wide>(first) + static_cast<Wide>(second)) >> 1);
    } else {
        return static_cast<T>((first & second) + ((first ^ second) >> 1));
    }
}

// The full product of two unsigned values: the low half and the high half.
template <UnsignedInteger T>
struct WideProduct final {
    T low = 0;
    T high = 0;
};

template <UnsignedInteger T>
[[nodiscard]] constexpr WideProduct<T> widening_mul(T left, T right) noexcept {
    if constexpr (sizeof(T) == sizeof(std::uint64_t)) {
        detail::WideWord const product = static_cast<detail::WideWord>(left) * static_cast<detail::WideWord>(right);
        return WideProduct<T>{static_cast<T>(product), static_cast<T>(product >> 64)};
    } else {
        std::uint64_t const product = std::uint64_t{left} * std::uint64_t{right};
        return WideProduct<T>{static_cast<T>(product), static_cast<T>(product >> bits_of<T>)};
    }
}

template <UnsignedInteger T>
[[nodiscard]] constexpr Option<T> checked_mul_div(T left, T right, T divisor) noexcept {
    if (divisor == T{0}) return none;
    if constexpr (sizeof(T) == sizeof(std::uint64_t)) {
        WideProduct<T> const product = widening_mul(left, right);
        if (product.high >= divisor) return none;
        if (product.high == T{0}) return Option<T>::some(static_cast<T>(product.low / divisor));
        detail::WideWord const wide = (static_cast<detail::WideWord>(product.high) << 64) | product.low;
        return Option<T>::some(static_cast<T>(wide / divisor));
    } else {
        std::uint64_t const quotient = (std::uint64_t{left} * std::uint64_t{right}) / std::uint64_t{divisor};
        if (quotient > std::uint64_t{max_of<T>}) return none;
        return Option<T>::some(static_cast<T>(quotient));
    }
}

template <UnsignedInteger T>
[[nodiscard]] constexpr T mul_div_floor(T left, T right, T divisor) noexcept {
    if (divisor == T{0}) [[unlikely]] {
        fatal("mul_div_floor got the divisor 0, which gives no quotient");
    }
    return checked_mul_div(left, right, divisor)
        .match([](T quotient) noexcept { return quotient; },
               [] noexcept -> T { fatal("mul_div_floor got a quotient that the type does not hold"); });
}

namespace detail {

// The operation of a strict form that ends the process.  align_up is the
// strict form of foundation/core/Bits.h.
enum class StrictOperation : std::uint8_t {
    add,
    sub,
    mul,
    div,
    rem,
    shl,
    shr,
    align_up,
};

// The sign of the operand type of a strict form.
enum class StrictSign : std::uint8_t {
    unsigned_type,
    signed_type,
};

// The operation and the operand type of a strict form that ends the process,
// in one word: the StrictOperation in bits 0 to 7, the StrictSign in bits 8
// to 15 and the width of the type in bits 16 to 23.  The word of each call
// is a constant, so the call loads it with one move.  A struct of the three
// parts made GCC 16 also store the parts to the stack, and a function with
// two strict forms then got a stack frame on its hot path.
enum class StrictFault : std::uint32_t {
};

template <Integer T>
[[nodiscard]] constexpr StrictFault strict_fault_of_(StrictOperation operation) noexcept {
    std::uint32_t const sign = std::is_signed_v<T> ? std::uint32_t{1} : std::uint32_t{0};
    return StrictFault{static_cast<std::uint32_t>(operation) | (sign << 8U)
                       | (static_cast<std::uint32_t>(bits_of<T>) << 16U)};
}

// The two operands of a strict form that ends the process, as the bits of
// their values: a signed operand as the bits of its std::int64_t value.  The
// count of a shift is the right operand, and it is signed.  The pair has 16
// bytes, so the call takes it in two registers.
//
// For an add and a sub, first holds the result modulo 2^bits, and the report
// computes the left operand from it: the result less the right operand, or
// the result plus the right operand, modulo 2^bits.  The add and the sub
// write their result over the left operand, so a copy of the left operand
// would stay live only for the report.  In a loop of sums, GCC 16 then moved
// the left operand to a second register in each step.  An align_up to a
// power of two holds the sum of the value and the alignment less one in the
// same way.  For each other operation, first is the left operand.
struct StrictOperands {
    std::uint64_t first = 0;
    std::uint64_t right = 0;
};

// Writes the operation, the operands, the operand type and the site to
// standard error, and ends the process.  The body is in
// src/foundation/core/Report.cpp.
[[noreturn, gnu::cold]] void report_strict_fault_(StrictFault fault, StrictOperands operands, char const* file,
                                                  std::uint32_t line, char const* function) noexcept;

// The bits of an operand in a StrictOperands.
template <Integer T>
[[nodiscard]] constexpr std::uint64_t strict_bits_of_(T value) noexcept {
    if constexpr (std::is_signed_v<T>) {
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
    } else {
        return static_cast<std::uint64_t>(value);
    }
}

// The end of a strict form of the operand type T.  It loads the parts of
// the report into registers and calls the cold report, so the caller keeps
// only the branch to the call.  first is the first word of the
// StrictOperands of the operation.
template <Integer T, Integer Right>
[[noreturn, gnu::always_inline]] inline void refuse_strict_(StrictOperation operation, T first, Right right,
                                                            Site site) noexcept {
    report_strict_fault_(strict_fault_of_<T>(operation), StrictOperands{strict_bits_of_(first), strict_bits_of_(right)},
                         site_file_(site), site_line_(site), site_function_(site));
}

}  // namespace detail

template <Integer T>
[[nodiscard]] constexpr T strict_add(T left, T right, Site site = Site{}) noexcept {
    T sum{};
    if (__builtin_add_overflow(left, right, &sum)) [[unlikely]] {
        detail::refuse_strict_(detail::StrictOperation::add, sum, right, site);
    }
    return sum;
}

template <Integer T>
[[nodiscard]] constexpr T strict_sub(T left, T right, Site site = Site{}) noexcept {
    T difference{};
    if (__builtin_sub_overflow(left, right, &difference)) [[unlikely]] {
        detail::refuse_strict_(detail::StrictOperation::sub, difference, right, site);
    }
    return difference;
}

template <Integer T>
[[nodiscard]] constexpr T strict_mul(T left, T right, Site site = Site{}) noexcept {
    T product{};
    if (__builtin_mul_overflow(left, right, &product)) [[unlikely]] {
        detail::refuse_strict_(detail::StrictOperation::mul, left, right, site);
    }
    return product;
}

template <Integer T>
[[nodiscard]] constexpr T strict_div(T dividend, T divisor, Site site = Site{}) noexcept {
    if (divisor == T{0} || detail::is_minimum_by_minus_one(dividend, divisor)) [[unlikely]] {
        detail::refuse_strict_(detail::StrictOperation::div, dividend, divisor, site);
    }
    return static_cast<T>(dividend / divisor);
}

// The remainder of the minimum by -1 is 0, as checked_rem gives it, and the
// division instruction of x86_64 traps on these operands.  So -1 takes no
// division.
template <Integer T>
[[nodiscard]] constexpr T strict_rem(T dividend, T divisor, Site site = Site{}) noexcept {
    if (divisor == T{0}) [[unlikely]] {
        detail::refuse_strict_(detail::StrictOperation::rem, dividend, divisor, site);
    }
    if constexpr (std::is_signed_v<T>) {
        if (divisor == T{-1}) return T{0};
    }
    return static_cast<T>(dividend % divisor);
}

// A negative count is a large unsigned value, so one compare refuses each
// count outside [0, bits).  The right shift gives back the value only when
// the left shift lost no bit and kept the sign.
template <Integer T>
[[nodiscard]] constexpr T strict_shl(T value, int count, Site site = Site{}) noexcept {
    using Bits = std::make_unsigned_t<T>;
    if (static_cast<unsigned>(count) < static_cast<unsigned>(bits_of<T>)) [[likely]] {
        T const shifted = static_cast<T>(static_cast<Bits>(static_cast<Bits>(value) << count));
        if (static_cast<T>(shifted >> count) == value) [[likely]] {
            return shifted;
        }
    }
    detail::refuse_strict_(detail::StrictOperation::shl, value, count, site);
}

template <Integer T>
[[nodiscard]] constexpr T strict_shr(T value, int count, Site site = Site{}) noexcept {
    if (static_cast<unsigned>(count) >= static_cast<unsigned>(bits_of<T>)) [[unlikely]] {
        detail::refuse_strict_(detail::StrictOperation::shr, value, count, site);
    }
    return static_cast<T>(value >> count);
}

// One T whose arithmetic is the strict forms.
template <Integer T>
class Strict final {
public:
    constexpr Strict() noexcept = default;

    constexpr explicit Strict(T value) noexcept : value_{value} {}

    template <class Other>
        requires(!std::same_as<Other, T>)
    explicit Strict(Other) = delete(
        "a Strict takes a value of its own type: give an integer of another type through widen or narrow, and the "
        "value of a Strict through raw()");

    // The number.
    [[nodiscard]] constexpr T raw() const noexcept { return value_; }

    [[nodiscard]] friend constexpr Strict operator+(Strict left, Strict right) noexcept {
        return Strict{strict_add(left.value_, right.value_)};
    }
    [[nodiscard]] friend constexpr Strict operator-(Strict left, Strict right) noexcept {
        return Strict{strict_sub(left.value_, right.value_)};
    }
    [[nodiscard]] friend constexpr Strict operator*(Strict left, Strict right) noexcept {
        return Strict{strict_mul(left.value_, right.value_)};
    }
    [[nodiscard]] friend constexpr Strict operator/(Strict dividend, Strict divisor) noexcept {
        return Strict{strict_div(dividend.value_, divisor.value_)};
    }
    [[nodiscard]] friend constexpr Strict operator%(Strict dividend, Strict divisor) noexcept {
        return Strict{strict_rem(dividend.value_, divisor.value_)};
    }
    [[nodiscard]] friend constexpr Strict operator<<(Strict value, int count) noexcept {
        return Strict{strict_shl(value.value_, count)};
    }
    [[nodiscard]] friend constexpr Strict operator>>(Strict value, int count) noexcept {
        return Strict{strict_shr(value.value_, count)};
    }

    constexpr Strict& operator+=(Strict right) noexcept {
        value_ = strict_add(value_, right.value_);
        return *this;
    }
    constexpr Strict& operator-=(Strict right) noexcept {
        value_ = strict_sub(value_, right.value_);
        return *this;
    }
    constexpr Strict& operator*=(Strict right) noexcept {
        value_ = strict_mul(value_, right.value_);
        return *this;
    }
    constexpr Strict& operator/=(Strict divisor) noexcept {
        value_ = strict_div(value_, divisor.value_);
        return *this;
    }
    constexpr Strict& operator%=(Strict divisor) noexcept {
        value_ = strict_rem(value_, divisor.value_);
        return *this;
    }
    constexpr Strict& operator<<=(int count) noexcept {
        value_ = strict_shl(value_, count);
        return *this;
    }
    constexpr Strict& operator>>=(int count) noexcept {
        value_ = strict_shr(value_, count);
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(Strict left, Strict right) noexcept = default;
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(Strict left, Strict right) noexcept = default;

private:
    T value_ = 0;
};

}  // namespace foundation::core

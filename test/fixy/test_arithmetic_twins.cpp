// The differential test of the three copies of the integer arithmetic set
// against the Scalar family of foundation/core/Arithmetic.h.
//
// fixy/Checked.h, foundation/Saturate.h and fixy/Saturate.h declare a second
// form of operations that the core set has.  For each input, each function of
// a copy gives the answer of its core twin:
//
//   fixy::checked_add, _sub, _mul, _div     core checked_add, _sub, _mul, _div
//   fixy::checked_mod                       core checked_rem
//   fixy::checked_neg                       core checked_neg
//   fixy::checked_abs                       core checked_neg of a negative
//                                           value, else the value
//   fixy::checked_shl, _shr                 core checked_shl, _shr
//   fixy::wrapping_add, _sub, _mul          core wrapping_add, _sub, _mul
//   fixy::trapping_add, _sub, _mul, _div    core checked form, and expect
//   fixy::saturating_add, _sub, _mul        core saturating_add, _sub, _mul
//   foundation::sat::add_sat, _sub, _mul    core saturating_add, _sub, _mul
//   fixy::sat::add_sat, _sub, _mul          core saturating_add, _sub, _mul
//   fixy::add_sat_checked, _sub, _mul       the value of core saturating_*,
//                                           and the flag of an empty core
//                                           checked_*
//
// An empty std::optional of a copy agrees with an empty Option of the core
// form, and a full one agrees with a full one of the same value.  A trapping
// form ends the process where the expect of the core form ends it.
//
// The inputs are each pair of 8-bit values, signed and unsigned, with each
// shift count from -2 to 9.  For each wider width the inputs are each pair of
// ten edge values and Philox pairs, with a small right operand that reaches
// the divisors 0 and -1 and each shift count.

#include <fixy/Checked.h>
#include <fixy/Saturate.h>
#include <fixy/Saturated.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/Saturate.h>
#include <foundation/core/Arithmetic.h>

#include "../foundation/abort_probe.h"
#include "../foundation/philox_stream.h"

#include <cstdint>
#include <optional>
#include <type_traits>

namespace {

namespace core = ::foundation::core;

// The value, through a call that the optimizer cannot read, so each
// operation runs at run time.
template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

// The plugin reads the reason of a region as one string literal in the call,
// so the call stays on one line.
// clang-format off
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: the std::optional of a copy of the arithmetic set is compared with its core twin")
// clang-format on

// True when the result of a copy and the result of the core form hold the
// same value, or are both empty.
template <class T>
[[nodiscard]] bool agree(std::optional<T> copy, core::Option<T> twin) noexcept {
    if (copy.has_value() != twin.is_some()) return false;
    return !copy.has_value() || *copy == static_cast<core::Option<T>&&>(twin).value_or(T{0});
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The absolute value of the core set: the negation of a negative value.
template <class T>
[[nodiscard]] core::Option<T> core_abs(T value) noexcept {
    if (value < T{0}) return core::checked_neg(value);
    return core::Option<T>::some(value);
}

// Each binary function of the copies on one pair.
template <class T>
void check_pair(T left, T right) {
    T const a = opaque(left);
    T const b = opaque(right);

    CRUCIBLE_FATAL_INVARIANT(agree(::fixy::checked_add(a, b), core::checked_add(a, b)));
    CRUCIBLE_FATAL_INVARIANT(agree(::fixy::checked_sub(a, b), core::checked_sub(a, b)));
    CRUCIBLE_FATAL_INVARIANT(agree(::fixy::checked_mul(a, b), core::checked_mul(a, b)));
    CRUCIBLE_FATAL_INVARIANT(agree(::fixy::checked_div(a, b), core::checked_div(a, b)));
    CRUCIBLE_FATAL_INVARIANT(agree(::fixy::checked_mod(a, b), core::checked_rem(a, b)));

    CRUCIBLE_FATAL_INVARIANT(::fixy::wrapping_add(a, b) == core::wrapping_add(a, b));
    CRUCIBLE_FATAL_INVARIANT(::fixy::wrapping_sub(a, b) == core::wrapping_sub(a, b));
    CRUCIBLE_FATAL_INVARIANT(::fixy::wrapping_mul(a, b) == core::wrapping_mul(a, b));

    // A trapping form returns only where its checked twin has a value.
    if (core::checked_add(a, b).is_some()) {
        CRUCIBLE_FATAL_INVARIANT(::fixy::trapping_add(a, b) == core::checked_add(a, b).expect("the sum fits"));
    }
    if (core::checked_sub(a, b).is_some()) {
        CRUCIBLE_FATAL_INVARIANT(::fixy::trapping_sub(a, b) == core::checked_sub(a, b).expect("the difference fits"));
    }
    if (core::checked_mul(a, b).is_some()) {
        CRUCIBLE_FATAL_INVARIANT(::fixy::trapping_mul(a, b) == core::checked_mul(a, b).expect("the product fits"));
    }
    if (core::checked_div(a, b).is_some()) {
        CRUCIBLE_FATAL_INVARIANT(::fixy::trapping_div(a, b) == core::checked_div(a, b).expect("the quotient fits"));
    }

    T const sum = core::saturating_add(a, b);
    T const difference = core::saturating_sub(a, b);
    T const product = core::saturating_mul(a, b);
    CRUCIBLE_FATAL_INVARIANT(::fixy::saturating_add(a, b) == sum);
    CRUCIBLE_FATAL_INVARIANT(::fixy::saturating_sub(a, b) == difference);
    CRUCIBLE_FATAL_INVARIANT(::fixy::saturating_mul(a, b) == product);
    CRUCIBLE_FATAL_INVARIANT(::foundation::sat::add_sat(a, b) == sum);
    CRUCIBLE_FATAL_INVARIANT(::foundation::sat::sub_sat(a, b) == difference);
    CRUCIBLE_FATAL_INVARIANT(::foundation::sat::mul_sat(a, b) == product);
    CRUCIBLE_FATAL_INVARIANT(::fixy::sat::add_sat(a, b) == sum);
    CRUCIBLE_FATAL_INVARIANT(::fixy::sat::sub_sat(a, b) == difference);
    CRUCIBLE_FATAL_INVARIANT(::fixy::sat::mul_sat(a, b) == product);

    ::fixy::Saturated<T> const flagged_sum = ::fixy::add_sat_checked(a, b);
    ::fixy::Saturated<T> const flagged_difference = ::fixy::sub_sat_checked(a, b);
    ::fixy::Saturated<T> const flagged_product = ::fixy::mul_sat_checked(a, b);
    CRUCIBLE_FATAL_INVARIANT(flagged_sum.value() == sum);
    CRUCIBLE_FATAL_INVARIANT(flagged_sum.was_clamped() == core::checked_add(a, b).is_none());
    CRUCIBLE_FATAL_INVARIANT(flagged_difference.value() == difference);
    CRUCIBLE_FATAL_INVARIANT(flagged_difference.was_clamped() == core::checked_sub(a, b).is_none());
    CRUCIBLE_FATAL_INVARIANT(flagged_product.value() == product);
    CRUCIBLE_FATAL_INVARIANT(flagged_product.was_clamped() == core::checked_mul(a, b).is_none());
}

// The negation, the absolute value and the shifts of one value, for each
// shift count from -2 to bits + 1.
template <class T>
void check_value(T input) {
    T const value = opaque(input);
    if constexpr (std::is_signed_v<T>) {
        CRUCIBLE_FATAL_INVARIANT(agree(::fixy::checked_neg(value), core::checked_neg(value)));
        CRUCIBLE_FATAL_INVARIANT(agree(::fixy::checked_abs(value), core_abs(value)));
    }
    for (int count = -2; count < core::bits_of<T> + 2; ++count) {
        int const shift = opaque(count);
        CRUCIBLE_FATAL_INVARIANT(agree(::fixy::checked_shl(value, shift), core::checked_shl(value, shift)));
        CRUCIBLE_FATAL_INVARIANT(agree(::fixy::checked_shr(value, shift), core::checked_shr(value, shift)));
    }
}

// The values at the edges of a type: the limits, zero, one, minus one and
// their neighbours, and the powers of two near the middle.
template <class T>
[[nodiscard]] T edge_value(int position) noexcept {
    switch (position) {
        case 0:
            return core::min_of<T>;
        case 1:
            return static_cast<T>(core::min_of<T> + 1);
        case 2:
            return core::max_of<T>;
        case 3:
            return static_cast<T>(core::max_of<T> - 1);
        case 4:
            return T{0};
        case 5:
            return T{1};
        case 6:
            return static_cast<T>(-1);
        case 7:
            return static_cast<T>(T{1} << (core::bits_of<T> / 2));
        case 8:
            return static_cast<T>(core::max_of<T> / 2);
        default:
            return static_cast<T>(core::max_of<T> / 2 + 1);
    }
}

inline constexpr int edge_count = 10;

template <class T>
void check_edges() {
    for (int first = 0; first < edge_count; ++first) {
        check_value(edge_value<T>(first));
        for (int second = 0; second < edge_count; ++second) {
            check_pair(edge_value<T>(first), edge_value<T>(second));
        }
    }
}

// A small right operand of each sign reaches the divisors 0 and -1.
template <class T>
void check_random(std::uint64_t seed, int count) {
    ::crucible::test::PhiloxStream stream{seed};
    for (int index = 0; index < count; ++index) {
        T const left = static_cast<T>(stream.next_wide());
        T const right = static_cast<T>(stream.next_wide());
        T const small = static_cast<T>(static_cast<std::int64_t>(stream.below(8)) - 4);
        check_pair(left, right);
        check_pair(left, small);
        check_value(left);
    }
}

void test_each_pair_of_bytes() {
    for (int left = 0; left < 256; ++left) {
        check_value(static_cast<std::uint8_t>(left));
        check_value(static_cast<std::int8_t>(left));
        for (int right = 0; right < 256; ++right) {
            check_pair(static_cast<std::uint8_t>(left), static_cast<std::uint8_t>(right));
            check_pair(static_cast<std::int8_t>(left), static_cast<std::int8_t>(right));
        }
    }
}

void test_edges_of_each_width() {
    check_edges<std::int16_t>();
    check_edges<std::uint16_t>();
    check_edges<std::int32_t>();
    check_edges<std::uint32_t>();
    check_edges<std::int64_t>();
    check_edges<std::uint64_t>();
    check_edges<long long>();
    check_edges<unsigned long long>();
}

void test_philox_pairs_of_each_width() {
    check_random<std::int16_t>(0x5EED0000000A7116u, 20000);
    check_random<std::uint16_t>(0x5EED0000000A7117u, 20000);
    check_random<std::int32_t>(0x5EED0000000A7118u, 20000);
    check_random<std::uint32_t>(0x5EED0000000A7119u, 20000);
    check_random<std::int64_t>(0x5EED0000000A711Au, 20000);
    check_random<std::uint64_t>(0x5EED0000000A711Bu, 20000);
}

// Each trapping form ends the process on the input where the expect of its
// core twin ends it.
void test_trapping_forms_end_where_expect_ends() {
    using ::crucible::test::aborts;
    constexpr std::int32_t high = core::max_of<std::int32_t>;
    constexpr std::int32_t low = core::min_of<std::int32_t>;
    CRUCIBLE_FATAL_INVARIANT(aborts([] { (void)::fixy::trapping_add(opaque(high), 1); }));
    CRUCIBLE_FATAL_INVARIANT(aborts([] { (void)core::checked_add(opaque(high), 1).expect("the sum fits"); }));
    CRUCIBLE_FATAL_INVARIANT(aborts([] { (void)::fixy::trapping_sub(opaque(low), 1); }));
    CRUCIBLE_FATAL_INVARIANT(aborts([] { (void)core::checked_sub(opaque(low), 1).expect("the difference fits"); }));
    CRUCIBLE_FATAL_INVARIANT(aborts([] { (void)::fixy::trapping_mul(opaque(high), 2); }));
    CRUCIBLE_FATAL_INVARIANT(aborts([] { (void)core::checked_mul(opaque(high), 2).expect("the product fits"); }));
    CRUCIBLE_FATAL_INVARIANT(aborts([] { (void)::fixy::trapping_div(opaque(low), -1); }));
    CRUCIBLE_FATAL_INVARIANT(aborts([] { (void)core::checked_div(opaque(low), -1).expect("the quotient fits"); }));
    CRUCIBLE_FATAL_INVARIANT(aborts([] { (void)::fixy::trapping_div(opaque(high), 0); }));
    CRUCIBLE_FATAL_INVARIANT(aborts([] { (void)core::checked_div(opaque(high), 0).expect("the quotient fits"); }));
}

}  // namespace

int main() {
    test_each_pair_of_bytes();
    test_edges_of_each_width();
    test_philox_pairs_of_each_width();
    test_trapping_forms_end_where_expect_ends();
    return 0;
}

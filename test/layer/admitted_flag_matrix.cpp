// The flag matrix of the audited standard library names.
//
// utils/scripts/quarantine-admitted-audit.txt gives one row for each standard
// library name that quarantined code can name, or that the audit examined as a
// candidate.  This file holds one static_assert for each property of each of
// those names: a type, a value, a size, a noexcept state or a result of a
// constant evaluation.  test/layer/CMakeLists.txt compiles this file one time
// for each flag set of the matrix: the Debug, Release, TSan, UBSan-strict,
// verify and PGO presets, -D_GLIBCXX_DEBUG, -D_GLIBCXX_ASSERTIONS, NDEBUG and
// each contract evaluation semantic.  So a flag that changes a property of a
// name stops the build in the unit of that flag set.
//
// A static_assert cannot see machine code.  The audit table gives the evidence
// for the code of each name: the text of its definition after the preprocessor,
// under each flag set.
//
// Each section starts with one line `// name: NAME` for each name that it
// examines.  A property that the audit found to change with a flag is in a
// block that the flag removes, after one line `// varies: MACRO`.  The check
// utils/scripts/check-admitted-audit.py makes sure that each row of the audit
// table has a section and that each section has a row.  It also makes sure
// that the row of a section with a `varies` line records that variance.
//
// The properties hold on x86_64 and on aarch64 Linux with glibc, the two
// targets of the tree.  The file names standard library entities only in
// static_assert conditions, in helper types and in helper concepts.  A
// static_assert makes no object at run time, so the quarantine plugin reads no
// static_assert condition.

#include <bit>
#include <compare>
#include <concepts>
#include <contracts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <meta>
#include <new>
#include <source_location>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// The helper types of the properties.
struct Plain {
    int first = 0;
    int second = 0;
};

enum class Narrow : std::uint16_t {
    low = 1,
    high = 0xBEEF
};

struct ThrowingMove {
    ThrowingMove() = default;
    ThrowingMove(const ThrowingMove&) = default;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
    ThrowingMove& operator=(const ThrowingMove&) = default;
    ThrowingMove& operator=(ThrowingMove&&) noexcept(false) { return *this; }
};

// True when a shift of std::byte{1} by Count is a constant expression.  The
// operator shifts an unsigned int, so a count of 32 or more is undefined.
template <int Count>
concept byte_shift_is_constant =
    requires { typename std::integral_constant<int, std::to_integer<int>(std::byte{1} << Count)>; };

// True when std::as_const accepts an rvalue of T.
template <typename T>
concept as_const_takes_rvalue = requires { std::as_const(std::declval<T>()); };

// True when std::tuple_size<T> is complete.
template <typename T>
concept has_tuple_size = requires { std::tuple_size<T>::value; };

// A namespace with two members, for the reflection queries.
namespace walked {
inline constexpr int first_member = 1;
inline constexpr int second_member = 2;
}  // namespace walked

// ── The headers that the admitted list names ────────────────────────────

// name: <type_traits>
static_assert(std::is_same_v<std::remove_cvref_t<const int&>, int>);
static_assert(std::is_same_v<std::conditional_t<true, int, long>, int>);
static_assert(std::is_same_v<std::common_type_t<int, long>, long>);
static_assert(std::is_same_v<std::underlying_type_t<Narrow>, std::uint16_t>);
static_assert(std::integral_constant<int, 3>::value == 3);
static_assert(std::is_trivially_copyable_v<Plain>);
static_assert(std::is_standard_layout_v<Plain>);
static_assert(std::is_aggregate_v<Plain>);
static_assert(std::has_unique_object_representations_v<Plain>);
static_assert(std::is_nothrow_move_constructible_v<Plain>);
static_assert(!std::is_nothrow_move_constructible_v<ThrowingMove>);
static_assert(std::is_invocable_r_v<int, int (*)(long), int>);
static_assert(std::is_signed_v<long>);
static_assert(std::is_unsigned_v<unsigned char>);
static_assert(std::alignment_of_v<Plain> == alignof(int));
static_assert(std::is_constant_evaluated());

// name: <concepts>
static_assert(std::same_as<int, int>);
static_assert(std::integral<unsigned char>);
static_assert(!std::floating_point<int>);
static_assert(std::floating_point<double>);
static_assert(std::convertible_to<int, long>);
static_assert(std::copyable<Plain>);
static_assert(std::regular<int>);
static_assert(std::totally_ordered<int>);
static_assert(std::invocable<int (*)(int), int>);
// std::ranges::swap is a function object of a class type.  It is the one
// entity of the header that does work at run time.
static_assert(std::is_class_v<std::remove_cvref_t<decltype(std::ranges::swap)>>);

// name: <meta>
static_assert(std::meta::is_type(^^int));
static_assert(std::meta::identifier_of(^^Plain) == "Plain");
static_assert(std::meta::nonstatic_data_members_of(^^Plain, std::meta::access_context::current()).size() == 2);
static_assert(std::meta::members_of(^^walked, std::meta::access_context::current()).size() == 2);
static_assert(std::meta::size_of(^^Plain) == 2 * sizeof(int));
static_assert(std::meta::enumerators_of(^^Narrow).size() == 2);
static_assert(std::meta::display_string_of(^^Narrow) == "{anonymous}::Narrow");
static_assert(std::is_same_v<decltype(std::meta::members_of(^^walked, std::meta::access_context::current())),
                             std::vector<std::meta::info>>);
// varies: _GLIBCXX_DEBUG
// Under _GLIBCXX_DEBUG, std::vector is std::__debug::vector.  Its debug
// iterator over std::meta::info does not compile in a constant evaluation, so
// std::define_static_array and a range-for over a query result do not compile.
// The display string of a library container also changes.  No preset defines
// the macro.
#if !defined(_GLIBCXX_DEBUG)
static_assert(std::define_static_array(std::meta::enumerators_of(^^Narrow)).size() == 2);
static_assert(std::meta::display_string_of(^^std::vector<int>) == "std::vector<int>");
#endif

// name: <limits>
static_assert(std::numeric_limits<std::uint32_t>::max() == 0xFFFFFFFFu);
static_assert(std::numeric_limits<std::int64_t>::min() == -0x7FFFFFFFFFFFFFFF - 1);
static_assert(std::numeric_limits<std::uint8_t>::digits == 8);
static_assert(!std::numeric_limits<int>::is_modulo);
static_assert(std::numeric_limits<unsigned int>::is_modulo);
// min() of a floating type is the smallest normal value, not the lowest value.
static_assert(std::bit_cast<std::uint32_t>(std::numeric_limits<float>::min()) == 0x00800000u);
static_assert(std::bit_cast<std::uint32_t>(std::numeric_limits<float>::lowest()) == 0xFF7FFFFFu);
static_assert(std::bit_cast<std::uint32_t>(std::numeric_limits<float>::denorm_min()) == 0x00000001u);
static_assert(std::bit_cast<std::uint32_t>(std::numeric_limits<float>::epsilon()) == 0x34000000u);
static_assert(std::bit_cast<std::uint64_t>(std::numeric_limits<double>::infinity()) == 0x7FF0000000000000u);
static_assert(std::numeric_limits<double>::is_iec559);
static_assert(std::numeric_limits<double>::has_quiet_NaN);
static_assert(std::numeric_limits<double>::has_signaling_NaN);
static_assert(std::numeric_limits<float>::round_style == std::round_to_nearest);
static_assert(!std::numeric_limits<float>::traps);
static_assert(!std::numeric_limits<float>::tinyness_before);
// A type that is not arithmetic gets the primary template: a value of zero
// and no compile error.
static_assert(!std::numeric_limits<Narrow>::is_specialized);
static_assert(std::numeric_limits<Narrow>::max() == Narrow{});

// ── The casts and the compile-time names ────────────────────────────────

// name: std::move
static_assert(std::is_same_v<decltype(std::move(std::declval<Plain&>())), Plain&&>);
static_assert(std::is_same_v<decltype(std::move(std::declval<const Plain&>())), const Plain&&>);
static_assert(noexcept(std::move(std::declval<Plain&>())));
static_assert([] {
    Plain source{.first = 1, .second = 2};
    Plain target = std::move(source);
    return target.first + target.second;
}() == 3);

// name: std::forward
static_assert(std::is_same_v<decltype(std::forward<Plain&>(std::declval<Plain&>())), Plain&>);
static_assert(std::is_same_v<decltype(std::forward<Plain>(std::declval<Plain&>())), Plain&&>);
static_assert(noexcept(std::forward<Plain>(std::declval<Plain&>())));

// name: std::move_if_noexcept
static_assert(std::is_same_v<decltype(std::move_if_noexcept(std::declval<Plain&>())), Plain&&>);
// A move that can throw gives a const lvalue reference, so the caller copies.
static_assert(std::is_same_v<decltype(std::move_if_noexcept(std::declval<ThrowingMove&>())), const ThrowingMove&>);

// name: std::as_const
static_assert(std::is_same_v<decltype(std::as_const(std::declval<Plain&>())), const Plain&>);
static_assert(noexcept(std::as_const(std::declval<Plain&>())));
static_assert(!as_const_takes_rvalue<Plain>);

// name: std::to_underlying
static_assert(std::to_underlying(Narrow::high) == 0xBEEF);
static_assert(std::is_same_v<decltype(std::to_underlying(Narrow::low)), std::uint16_t>);
static_assert(noexcept(std::to_underlying(Narrow::low)));

// name: std::bit_cast
static_assert(std::bit_cast<std::uint32_t>(1.0F) == 0x3F800000u);
static_assert(std::bit_cast<std::uint64_t>(-0.0) == 0x8000000000000000u);
static_assert(std::bit_cast<std::uint32_t>(std::bit_cast<float>(0x40490FDBu)) == 0x40490FDBu);
static_assert(noexcept(std::bit_cast<std::uint32_t>(1.0F)));
// The byte 2 is no value of bool.  The constant evaluation reads bit 0 only
// and gives false.  At run time the result is undefined, and the audit table
// gives what each optimization level does with it.
static_assert(!std::bit_cast<bool>(std::uint8_t{2}));

// name: std::declval
static_assert(std::is_same_v<decltype(std::declval<Plain>()), Plain&&>);
static_assert(std::is_same_v<decltype(std::declval<Plain&>()), Plain&>);
static_assert(noexcept(std::declval<ThrowingMove>()));

// name: std::integer_sequence
static_assert(std::integer_sequence<int, 4, 5>::size() == 2);
static_assert(std::is_empty_v<std::integer_sequence<int, 4, 5>>);
static_assert(std::is_same_v<std::integer_sequence<long, 1>::value_type, long>);

// name: std::make_integer_sequence
static_assert(std::is_same_v<std::make_integer_sequence<int, 3>, std::integer_sequence<int, 0, 1, 2>>);
static_assert(std::is_same_v<std::make_integer_sequence<int, 0>, std::integer_sequence<int>>);

// name: std::index_sequence
static_assert(std::is_same_v<std::index_sequence<0, 1>, std::integer_sequence<std::size_t, 0, 1>>);

// name: std::make_index_sequence
static_assert(std::is_same_v<std::make_index_sequence<2>, std::index_sequence<0, 1>>);

// name: std::index_sequence_for
static_assert(std::is_same_v<std::index_sequence_for<int, long, Plain>, std::index_sequence<0, 1, 2>>);

// name: std::tuple_size
static_assert(std::tuple_size_v<std::pair<int, long>> == 2);
static_assert(std::tuple_size<const std::pair<int, long>>::value == 2);
static_assert(!has_tuple_size<Plain>);

// name: std::tuple_element
static_assert(std::is_same_v<std::tuple_element_t<1, std::pair<int, long>>, long>);
static_assert(std::is_same_v<std::tuple_element_t<0, const std::pair<int, long>>, const int>);

// name: std::unreachable
// The code of std::unreachable depends on the flags, and the audit table gives
// the evidence.  These properties do not depend on them.
static_assert(std::is_same_v<decltype(std::unreachable()), void>);
static_assert(!noexcept(std::unreachable()));

// ── The candidates ──────────────────────────────────────────────────────

// name: std::initializer_list
static_assert(sizeof(std::initializer_list<int>) == 2 * sizeof(void*));
static_assert(std::initializer_list<int>{1, 2, 3}.size() == 3);
static_assert(std::initializer_list<int>{}.size() == 0);
// A copy copies the pointer to the array and not the array.
static_assert(std::is_trivially_copyable_v<std::initializer_list<int>>);
static_assert(std::is_same_v<decltype(std::initializer_list<int>{}.begin()), const int*>);

// name: std::strong_ordering
static_assert(sizeof(std::strong_ordering) == 1);
static_assert(std::is_same_v<decltype(1 <=> 2), std::strong_ordering>);
static_assert((1 <=> 2) == std::strong_ordering::less);
static_assert((2 <=> 2) == std::strong_ordering::equal);
static_assert(std::strong_ordering::equal == std::strong_ordering::equivalent);

// name: std::weak_ordering
static_assert(sizeof(std::weak_ordering) == 1);
static_assert(std::weak_ordering(std::strong_ordering::greater) == std::weak_ordering::greater);

// name: std::partial_ordering
static_assert(sizeof(std::partial_ordering) == 1);
static_assert(std::is_same_v<decltype(1.0 <=> 2.0), std::partial_ordering>);
static_assert((0.0 <=> std::numeric_limits<double>::quiet_NaN()) == std::partial_ordering::unordered);
static_assert((-0.0 <=> 0.0) == std::partial_ordering::equivalent);

// name: std::is_eq
static_assert(std::is_eq(2 <=> 2));
static_assert(noexcept(std::is_eq(2 <=> 2)));

// name: std::is_neq
static_assert(std::is_neq(1 <=> 2));

// name: std::is_lt
static_assert(std::is_lt(1 <=> 2));

// name: std::is_lteq
static_assert(std::is_lteq(2 <=> 2));

// name: std::is_gt
static_assert(std::is_gt(2 <=> 1));

// name: std::is_gteq
static_assert(std::is_gteq(2 <=> 2));
static_assert(!std::is_gteq(0.0 <=> std::numeric_limits<double>::quiet_NaN()));

// name: std::byte
static_assert(sizeof(std::byte) == 1);
static_assert(alignof(std::byte) == 1);
static_assert(std::is_enum_v<std::byte>);
static_assert(std::is_same_v<std::underlying_type_t<std::byte>, unsigned char>);
static_assert(std::to_integer<int>(std::byte{0x12} | std::byte{0x01}) == 0x13);
static_assert(std::to_integer<int>(std::byte{1} << 7) == 0x80);
// The shift truncates to eight bits.
static_assert(std::to_integer<int>(std::byte{1} << 8) == 0);
static_assert(byte_shift_is_constant<31>);
static_assert(!byte_shift_is_constant<32>);

// name: std::size_t
static_assert(std::is_same_v<std::size_t, decltype(sizeof(int))>);
static_assert(std::is_same_v<std::size_t, unsigned long>);

// name: std::ptrdiff_t
static_assert(std::is_same_v<std::ptrdiff_t, long>);

// name: std::int8_t
static_assert(std::is_same_v<std::int8_t, signed char>);

// name: std::int16_t
static_assert(std::is_same_v<std::int16_t, short>);

// name: std::int32_t
static_assert(std::is_same_v<std::int32_t, int>);

// name: std::int64_t
static_assert(std::is_same_v<std::int64_t, long>);

// name: std::uint8_t
static_assert(std::is_same_v<std::uint8_t, unsigned char>);

// name: std::uint16_t
static_assert(std::is_same_v<std::uint16_t, unsigned short>);

// name: std::uint32_t
static_assert(std::is_same_v<std::uint32_t, unsigned int>);

// name: std::uint64_t
static_assert(std::is_same_v<std::uint64_t, unsigned long>);

// name: std::intptr_t
static_assert(std::is_same_v<std::intptr_t, long>);

// name: std::uintptr_t
static_assert(std::is_same_v<std::uintptr_t, unsigned long>);

// name: std::intmax_t
static_assert(std::is_same_v<std::intmax_t, long>);

// name: std::uintmax_t
static_assert(std::is_same_v<std::uintmax_t, unsigned long>);

// name: std::int_least8_t
static_assert(std::is_same_v<std::int_least8_t, signed char>);

// name: std::int_least16_t
static_assert(std::is_same_v<std::int_least16_t, short>);

// name: std::int_least32_t
static_assert(std::is_same_v<std::int_least32_t, int>);

// name: std::int_least64_t
static_assert(std::is_same_v<std::int_least64_t, long>);

// name: std::uint_least8_t
static_assert(std::is_same_v<std::uint_least8_t, unsigned char>);

// name: std::uint_least16_t
static_assert(std::is_same_v<std::uint_least16_t, unsigned short>);

// name: std::uint_least32_t
static_assert(std::is_same_v<std::uint_least32_t, unsigned int>);

// name: std::uint_least64_t
static_assert(std::is_same_v<std::uint_least64_t, unsigned long>);

// name: std::int_fast8_t
static_assert(std::is_same_v<std::int_fast8_t, signed char>);

// name: std::int_fast16_t
static_assert(std::is_same_v<std::int_fast16_t, long>);

// name: std::int_fast32_t
static_assert(std::is_same_v<std::int_fast32_t, long>);

// name: std::int_fast64_t
static_assert(std::is_same_v<std::int_fast64_t, long>);

// name: std::uint_fast8_t
static_assert(std::is_same_v<std::uint_fast8_t, unsigned char>);

// name: std::uint_fast16_t
static_assert(std::is_same_v<std::uint_fast16_t, unsigned long>);

// name: std::uint_fast32_t
static_assert(std::is_same_v<std::uint_fast32_t, unsigned long>);

// name: std::uint_fast64_t
static_assert(std::is_same_v<std::uint_fast64_t, unsigned long>);

// name: std::nullptr_t
static_assert(std::is_same_v<decltype(nullptr), std::nullptr_t>);
static_assert(sizeof(std::nullptr_t) == sizeof(void*));
static_assert(std::is_null_pointer_v<std::nullptr_t>);

// name: std::meta::info
static_assert(std::is_same_v<decltype(^^int), std::meta::info>);
static_assert(std::is_scalar_v<std::meta::info>);
static_assert(^^int == ^^int);
static_assert(^^int != ^^long);

// name: std::source_location
static_assert(std::source_location::current().line() == __LINE__);
static_assert(__builtin_strcmp(std::source_location::current().file_name(), __FILE__) == 0);
static_assert(std::source_location{}.line() == 0);
static_assert(sizeof(std::source_location) == sizeof(void*));
static_assert(std::is_nothrow_default_constructible_v<std::source_location>);

// name: std::contracts::contract_violation
static_assert(!std::is_copy_constructible_v<std::contracts::contract_violation>);
static_assert(!std::is_default_constructible_v<std::contracts::contract_violation>);
static_assert(
    std::is_same_v<decltype(std::declval<const std::contracts::contract_violation&>().comment()), const char*>);
static_assert(std::to_underlying(std::contracts::evaluation_semantic::enforce) == 3);
static_assert(std::to_underlying(std::contracts::assertion_kind::pre) == 1);

// name: std::align_val_t
static_assert(std::is_enum_v<std::align_val_t>);
static_assert(std::is_same_v<std::underlying_type_t<std::align_val_t>, std::size_t>);

// name: std::nothrow_t
static_assert(std::is_empty_v<std::nothrow_t>);
static_assert(std::is_trivially_default_constructible_v<std::nothrow_t>);

}  // namespace

// Sentinel TU for fixy/FixedArray.h: the storage zero-fills without
// braces, the in_place constructor takes exactly N arguments, and the
// two checked accessors carry their bound differently.
//
// The index_type is a Refined whose value constructor is private, so
// every index in this TU is minted.  That is the property
// worth pinning: the bound is checked once, at the one door.

#include <fixy/FixedArray.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace {

using ::fixy::bounded_above;
using ::fixy::FixedArray;
using ::fixy::mint_refined;

using FA4 = FixedArray<int, 4>;
using FA1 = FixedArray<int, 1>;

static_assert(sizeof(FA4) == 4 * sizeof(int));
static_assert(std::is_standard_layout_v<FA4>);
static_assert(FA4::capacity == 4);
static_assert(FA4::wrapper_kind() == "structural::FixedArray");

// A zero capacity has no front and no back, so it is not nameable.
template <std::size_t N>
concept CanNameFixedArray = requires { typename FixedArray<int, N>; };
static_assert(CanNameFixedArray<1>);
static_assert(CanNameFixedArray<4>);
static_assert(!CanNameFixedArray<0>, "front and back would have no element");

// The in_place constructor takes exactly N arguments: a partial fill is
// rejected rather than leaving a silent tail.
static_assert(std::is_constructible_v<FA4, std::in_place_t, int, int, int, int>);
static_assert(!std::is_constructible_v<FA4, std::in_place_t, int, int, int>);
static_assert(!std::is_constructible_v<FA4, std::in_place_t, int, int, int, int, int>);

// Declaring one without braces still zero-fills.
[[nodiscard]] consteval bool default_is_zero_filled() noexcept {
    FA4 a;
    for (std::size_t i = 0; i < FA4::capacity; ++i) {
        if (a[i] != 0) return false;
    }
    return true;
}
static_assert(default_is_zero_filled());

// The runtime index carries a proof token, minted at the one door.
[[nodiscard]] consteval bool minted_index_reads_the_right_slot() noexcept {
    FA4 a{std::in_place, 10, 20, 30, 40};
    for (std::size_t i = 0; i < FA4::capacity; ++i) {
        const auto idx = mint_refined<bounded_above<FA4::capacity - 1>>(i);
        if (a.at(idx) != static_cast<int>(10 * (i + 1))) return false;
    }
    return true;
}
static_assert(minted_index_reads_the_right_slot());

// The compile-time index needs no token, because out of range is a
// compile error rather than undefined behaviour.
template <std::size_t I>
concept CanAtCompileTime = requires(FA4 a) { a.template at<I>(); };
static_assert(CanAtCompileTime<0>);
static_assert(CanAtCompileTime<3>);
static_assert(!CanAtCompileTime<4>, "one past the end is a compile error");
static_assert(!CanAtCompileTime<99>);

// An index minted for a smaller array does not fit a larger one's
// accessor, because the bound is part of the type.
using Idx4 = FA4::index_type;
using Idx1 = FA1::index_type;
static_assert(!std::is_same_v<Idx4, Idx1>);
template <typename A, typename I>
concept CanAtWith = requires(A a, I i) { a.at(i); };
static_assert(CanAtWith<FA4, Idx4>);
static_assert(!CanAtWith<FA4, Idx1>, "a tighter bound is a different proof");
static_assert(!CanAtWith<FA4, std::size_t>, "the raw index has no proof");

[[nodiscard]] consteval bool fill_with_sets_every_slot() noexcept {
    const FA4 a = FA4::fill_with(7);
    for (std::size_t i = 0; i < FA4::capacity; ++i) {
        if (a[i] != 7) return false;
    }
    return a.front() == 7 && a.back() == 7 && a.size() == 4 && !a.empty();
}
static_assert(fill_with_sets_every_slot());

int check_runtime_indexing() {
    volatile std::size_t seed = 2;
    FA4 a{std::in_place, 1, 2, 3, 4};

    const auto idx = mint_refined<bounded_above<FA4::capacity - 1>>(static_cast<std::size_t>(seed));
    if (a.at(idx) != 3) return 10;

    a.at(idx) = 30;
    if (a[2] != 30) return 11;

    if (a.front() != 1 || a.back() != 4) return 12;
    if (a.size() != 4 || a.empty()) return 13;

    int total = 0;
    for (int v : a)
        total += v;
    if (total != 1 + 2 + 30 + 4) return 14;

    const FA1 one{std::in_place, 42};
    if (one.front() != 42 || one.back() != 42) return 15;
    if (one.template at<0>() != 42) return 16;

    return 0;
}

// The whole-array operations: the two constructors, the two fills, the
// ordering, the span view, equality and swap.
int check_whole_array_operations() {
    using FA8 = FixedArray<int, 8>;

    FA8 zeroed{};
    for (std::size_t i = 0; i < 8; ++i)
        if (zeroed[i] != 0) return 20;

    FA8 counted{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    int sum = 0;
    for (auto v : counted)
        sum += v;
    if (sum != 36) return 21;

    const auto filled = FA8::fill_with(99);
    if (filled.front() != 99 || filled.back() != 99) return 22;

    zeroed.fill(7);
    for (std::size_t i = 0; i < 8; ++i)
        if (zeroed[i] != 7) return 23;

    const auto i5 = mint_refined<bounded_above<FA8::capacity - 1>>(std::size_t{5});
    if (counted.at(i5) != 6) return 24;
    if (counted.at<0>() != 1 || counted.at<7>() != 8) return 25;

    // The ordering is lexicographic over the elements, so a difference
    // in the last slot decides it.
    FA8 ord_b{std::in_place, 1, 2, 3, 4, 5, 6, 7, 9};
    if (!std::is_lt(counted <=> ord_b)) return 26;
    if (!std::is_gt(ord_b <=> counted)) return 27;

    const auto view = counted.as_span();
    if (view.size() != 8) return 28;
    int span_sum = 0;
    for (auto v : view)
        span_sum += v;
    if (span_sum != 36) return 29;

    FA8 eq_a = FA8::fill_with(5);
    FA8 eq_b = FA8::fill_with(5);
    if (!(eq_a == eq_b)) return 30;
    eq_b.fill(4);
    if (eq_a == eq_b) return 31;

    FA8 sw_a{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    FA8 sw_b = FA8::fill_with(0);
    sw_a.swap(sw_b);
    if (sw_b.front() != 1 || sw_a.front() != 0) return 32;

    // The alignment specifier reaches the elements, because the storage
    // member sits at offset zero.
    alignas(64) FixedArray<std::int64_t, 8> aligned_buf{};
    if (std::bit_cast<std::uintptr_t>(aligned_buf.data()) % 64 != 0) return 33;

    FixedArray<std::int64_t, 4> wide{};
    if (wide.size() != 4) return 34;
    for (std::size_t i = 0; i < 4; ++i)
        if (wide[i] != 0) return 35;

    return 0;
}

}  // namespace

int main() {
    if (int rc = check_whole_array_operations(); rc != 0) return rc;
    if (int rc = check_runtime_indexing(); rc != 0) return rc;

    return 0;
}

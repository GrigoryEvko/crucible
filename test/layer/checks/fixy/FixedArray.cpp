// The compile-time checks of fixy/FixedArray.h.

#include <fixy/FixedArray.h>

namespace fixy {

static_assert(sizeof(FixedArray<int, 1>) == sizeof(int[1]));
static_assert(sizeof(FixedArray<int, 8>) == sizeof(int[8]));
static_assert(sizeof(FixedArray<int, 64>) == sizeof(int[64]));
static_assert(sizeof(FixedArray<double, 8>) == sizeof(double[8]));
static_assert(sizeof(FixedArray<int64_t, 8>) == sizeof(int64_t[8]));
static_assert(sizeof(FixedArray<char, 16>) == sizeof(char[16]));

static_assert(std::is_trivially_copyable_v<FixedArray<int, 8>>);
static_assert(std::is_trivially_destructible_v<FixedArray<int, 8>>);
static_assert(std::is_standard_layout_v<FixedArray<int, 8>>);

static_assert(alignof(FixedArray<int, 8>) == alignof(int[8]));
static_assert(alignof(FixedArray<int64_t, 8>) == alignof(int64_t[8]));
static_assert(alignof(FixedArray<double, 8>) == alignof(double[8]));

static_assert(std::ranges::contiguous_range<FixedArray<int, 8>>);
static_assert(std::ranges::sized_range<FixedArray<int, 8>>);

static_assert(!std::is_same_v<FixedArray<int, 8>, std::array<int, 8>>,
              "FixedArray<T, N> and std::array<T, N> must be distinct types. "
              "This is what prevents an array that zero-initializes by default "
              "from being swapped for one that does not.");

// A capacity of zero has no assertion here on purpose. The constraint
// rejects it so hard that merely naming such a type inside a
// requires-expression is a hard error rather than a soft substitution
// failure, so the rejection cannot be witnessed from within this file.
// A negative-compile fixture witnesses it instead.

namespace detail::fixed_array_self_test {

using FA8 = FixedArray<int, 8>;

[[nodiscard]] consteval bool default_zeroes() noexcept {
    FA8 a{};
    for (std::size_t i = 0; i < 8; ++i) {
        if (a[i] != 0) return false;
    }
    return a.size() == 8 && !a.empty() && a.capacity == 8;
}
static_assert(default_zeroes());

[[nodiscard]] consteval bool in_place_ctor() noexcept {
    FA8 a{std::in_place, 10, 20, 30, 40, 50, 60, 70, 80};
    return a[0] == 10 && a[7] == 80 && a.size() == 8;
}
static_assert(in_place_ctor());

[[nodiscard]] consteval bool fill_factory() noexcept {
    FA8 a = FA8::fill_with(7);
    for (auto v : a) {
        if (v != 7) return false;
    }
    return true;
}
static_assert(fill_factory());

[[nodiscard]] consteval bool fill_mutates() noexcept {
    FA8 a{};
    a.fill(99);
    for (std::size_t i = 0; i < 8; ++i) {
        if (a[i] != 99) return false;
    }
    return true;
}
static_assert(fill_mutates());

[[nodiscard]] consteval bool iteration_works() noexcept {
    FA8 a{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    int sum = 0;
    for (auto v : a)
        sum += v;
    return sum == 36;
}
static_assert(iteration_works());

[[nodiscard]] consteval bool front_back_works() noexcept {
    FA8 a{std::in_place, 100, 0, 0, 0, 0, 0, 0, 200};
    return a.front() == 100 && a.back() == 200;
}
static_assert(front_back_works());

[[nodiscard]] consteval bool equality_works() noexcept {
    FA8 a{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    FA8 b{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    FA8 c{std::in_place, 1, 2, 3, 4, 5, 6, 7, 9};
    return (a == b) && !(a == c);
}
static_assert(equality_works());

[[nodiscard]] consteval bool refined_at_works() noexcept {
    FA8 a{std::in_place, 10, 20, 30, 40, 50, 60, 70, 80};
    // The index is minted, not constructed: Refined's value constructor
    // is private, so the bound is checked at the one door.
    auto i3 = mint_refined<bounded_above<FA8::capacity - 1>>(std::size_t{3});
    auto i7 = mint_refined<bounded_above<FA8::capacity - 1>>(std::size_t{7});
    return a.at(i3) == 40 && a.at(i7) == 80;
}
static_assert(refined_at_works());

[[nodiscard]] consteval bool compile_time_at_works() noexcept {
    FA8 a{std::in_place, 10, 20, 30, 40, 50, 60, 70, 80};
    return a.at<0>() == 10 && a.at<3>() == 40 && a.at<7>() == 80;
}
static_assert(compile_time_at_works());

template <class FA, std::size_t I>
concept can_compile_at = requires(FA a) {
    { a.template at<I>() };
};
static_assert(can_compile_at<FA8, 0>);
static_assert(can_compile_at<FA8, 7>);
static_assert(!can_compile_at<FA8, 8>, "at<8>() on a capacity of 8, whose valid indices are 0 through 7, "
                                       "must be ill-formed. If this fires, the index constraint has "
                                       "regressed and compile-time access can read out of range.");
static_assert(!can_compile_at<FA8, 99>);

[[nodiscard]] consteval bool ordering_works() noexcept {
    FA8 a{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    FA8 b{std::in_place, 1, 2, 3, 4, 5, 6, 7, 9};
    FA8 c{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    FA8 d{std::in_place, 0, 9, 9, 9, 9, 9, 9, 9};
    // Named comparisons again, so that no ordering is compared against
    // a literal zero.
    return std::is_lt(a <=> b) && std::is_eq(a <=> c) && std::is_gt(a <=> d) && std::is_gt(b <=> a);
}
static_assert(ordering_works());

[[nodiscard]] consteval bool span_escape_fixed_extent() noexcept {
    FA8 a{std::in_place, 1, 1, 1, 1, 1, 1, 1, 1};
    auto s = a.as_span();
    static_assert(decltype(s)::extent == 8, "as_span() must return a span whose extent is part of its type. "
                                            "If this fires the extent is dynamic, and a consumer can no "
                                            "longer resolve an overload on the bound.");
    int sum = 0;
    for (auto v : s)
        sum += v;
    return sum == 8;
}
static_assert(span_escape_fixed_extent());

[[nodiscard]] consteval bool swap_exchanges() noexcept {
    FA8 a{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    FA8 b{std::in_place, 11, 12, 13, 14, 15, 16, 17, 18};
    a.swap(b);
    return a[0] == 11 && b[0] == 1 && a[7] == 18 && b[7] == 8;
}
static_assert(swap_exchanges());

template <class FA, class... Args>
concept can_in_place_construct = requires { FA{std::in_place, std::declval<Args>()...}; };

static_assert(can_in_place_construct<FA8, int, int, int, int, int, int, int, int>);
static_assert(!can_in_place_construct<FA8, int, int, int>,
              "Constructing a capacity of 8 from 3 arguments must fail. Without "
              "that rejection a partially filled array would pass, with a tail "
              "that no initializer ever reached.");
static_assert(!can_in_place_construct<FA8, int, int, int, int, int, int, int, int, int>,
              "Constructing a capacity of 8 from 9 arguments must fail.");

static_assert(FA8::wrapper_kind() == "structural::FixedArray");

// The smallest valid capacity, where the only admissible index is
// zero, front and back name the same element, and the exactly-N
// argument gate has to accept a single argument.
using FA1 = FixedArray<int, 1>;
static_assert(sizeof(FA1) == sizeof(int));
static_assert(FA1::capacity == 1);

[[nodiscard]] consteval bool n_one_boundary() noexcept {
    FA1 a{};
    if (a[0] != 0) return false;
    if (a.front() != a.back()) return false;

    FA1 b{std::in_place, 42};
    if (b[0] != 42) return false;
    if (b.front() != 42 || b.back() != 42) return false;
    if (b.size() != 1 || b.empty()) return false;

    auto i0 = mint_refined<bounded_above<FA1::capacity - 1>>(std::size_t{0});
    if (b.at(i0) != 42) return false;

    if (b.at<0>() != 42) return false;

    auto c = FA1::fill_with(7);
    if (c.front() != 7) return false;

    FA1 eq_a{std::in_place, 5};
    FA1 eq_b{std::in_place, 5};
    if (!(eq_a == eq_b)) return false;
    return true;
}
static_assert(n_one_boundary());

template <class FA, std::size_t I>
concept can_compile_at_n1 = requires(FA a) {
    { a.template at<I>() };
};
static_assert(can_compile_at_n1<FA1, 0>);
static_assert(!can_compile_at_n1<FA1, 1>, "at<1>() on a capacity of 1, whose only valid index is 0, must be "
                                          "ill-formed. If this fires, the index constraint has regressed at "
                                          "the smallest valid capacity.");

static_assert(std::is_default_constructible_v<FA8>);
static_assert(std::is_nothrow_default_constructible_v<FA8>);

}  // namespace detail::fixed_array_self_test

}  // namespace fixy

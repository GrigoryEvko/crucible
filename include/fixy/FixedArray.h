#pragma once

// A fixed-capacity array that differs from the standard one in three
// ways that matter here.
//
// Declaring one without braces still zero-fills every element, because
// the storage member carries a default initializer. The standard array
// leaves the elements uninitialized in that form, which is the same
// trap a raw C array sets.
//
// There is no throwing accessor, because exceptions are off. Bounds
// come instead from three tiers: a subscript the caller vouches for, a
// proof-token index that was checked once when it was built, and an
// index fixed at compile time that cannot be out of range at all.
//
// It is a distinct type, so it cannot be swapped for the standard one
// by accident.
//
// An alignment specifier on an instance reaches the elements, because
// the single storage member sits at offset zero.
//
// The bound is a structural property of the storage rather than a
// graded one, so no lattice applies and this joins the wrappers that
// are deliberately not graded.
//
// Old spelling: include/crucible/safety/FixedArray.h.

#include <fixy/Refined.h>
#include <foundation/Platform.h>

#include <algorithm>
#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fixy {

template <typename T, std::size_t N>
    requires(N > 0)
class [[nodiscard]] FixedArray {
public:
    using element_type = T;
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = T const&;
    using pointer = T*;
    using const_pointer = T const*;
    using iterator = T*;
    using const_iterator = T const*;

    static constexpr size_type capacity = N;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::FixedArray"; }

    // An upper bound of N - 1 is exactly the valid index range. The
    // refinement takes the predicate as a value, so this names the
    // lowercase instance and not the struct template beside it.
    using index_type = Refined<bounded_above<N - 1>, size_type>;

private:
    T data_[N]{};

public:
    constexpr FixedArray() noexcept(std::is_nothrow_default_constructible_v<T>) = default;

    // Exactly N arguments, so a partial fill is rejected rather than
    // silently leaving a tail. The tag is what keeps this unambiguous
    // against copy-initialization when N is one.
    template <typename... Args>
        requires(sizeof...(Args) == N) && (std::convertible_to<Args, T> && ...)
    constexpr explicit FixedArray(std::in_place_t,
                                  Args&&... args) noexcept((std::is_nothrow_constructible_v<T, Args> && ...))
        : data_{static_cast<T>(std::forward<Args>(args))...} {}

    constexpr FixedArray(FixedArray const&) = default;
    constexpr FixedArray(FixedArray&&) = default;
    constexpr FixedArray& operator=(FixedArray const&) = default;
    constexpr FixedArray& operator=(FixedArray&&) = default;
    ~FixedArray() = default;

    [[nodiscard]] static constexpr FixedArray fill_with(T const& v) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        FixedArray result{};
        for (auto& e : result.data_)
            e = v;
        return result;
    }

    // The caller vouches for the bound here. There is deliberately no
    // precondition clause: a constexpr one evaluated from a consteval
    // context breaks. The standard library's debug overlay catches an
    // out-of-range subscript at run time, and a caller who wants the
    // bound checked structurally uses one of the two accessors below.
    [[nodiscard]] constexpr reference operator[](size_type i) noexcept { return data_[i]; }
    [[nodiscard]] constexpr const_reference operator[](size_type i) const noexcept { return data_[i]; }

    // The index carries its own proof: the bound was checked once when
    // the index was built, so these accesses trust it and do not check
    // again.
    [[nodiscard]] constexpr reference at(index_type i) noexcept { return data_[i.value()]; }
    [[nodiscard]] constexpr const_reference at(index_type i) const noexcept { return data_[i.value()]; }

    // An index known at compile time needs no proof token, because
    // out of range is a compile error here rather than undefined
    // behaviour.
    template <size_type I>
        requires(I < N)
    [[nodiscard]] constexpr reference at() noexcept {
        return data_[I];
    }
    template <size_type I>
        requires(I < N)
    [[nodiscard]] constexpr const_reference at() const noexcept {
        return data_[I];
    }

    // Both are always valid, because a capacity of zero is rejected.
    [[nodiscard]] constexpr reference front() noexcept { return data_[0]; }
    [[nodiscard]] constexpr const_reference front() const noexcept { return data_[0]; }
    [[nodiscard]] constexpr reference back() noexcept { return data_[N - 1]; }
    [[nodiscard]] constexpr const_reference back() const noexcept { return data_[N - 1]; }

    [[nodiscard]] constexpr pointer data() noexcept { return data_; }
    [[nodiscard]] constexpr const_pointer data() const noexcept { return data_; }
    [[nodiscard]] constexpr iterator begin() noexcept { return data_; }
    [[nodiscard]] constexpr const_iterator begin() const noexcept { return data_; }
    [[nodiscard]] constexpr iterator end() noexcept { return data_ + N; }
    [[nodiscard]] constexpr const_iterator end() const noexcept { return data_ + N; }

    [[nodiscard]] constexpr size_type size() const noexcept { return N; }
    [[nodiscard]] constexpr bool empty() const noexcept { return false; }

    // The extent is part of the returned type, so a consumer that
    // takes a fixed-extent span has the bound checked during overload
    // resolution. A dynamic-extent span would lose that.
    [[nodiscard]] constexpr std::span<T, N> as_span() noexcept { return std::span<T, N>{data_}; }
    [[nodiscard]] constexpr std::span<const T, N> as_span() const noexcept { return std::span<const T, N>{data_}; }

    constexpr void fill(T const& v) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        for (auto& e : data_)
            e = v;
    }

    constexpr void swap(FixedArray& other) noexcept(std::is_nothrow_swappable_v<T>) {
        for (size_type i = 0; i < N; ++i) {
            using std::swap;
            swap(data_[i], other.data_[i]);
        }
    }

    friend constexpr void swap(FixedArray& a, FixedArray& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    [[nodiscard]] friend constexpr bool operator==(FixedArray const& a,
                                                   FixedArray const& b) noexcept(noexcept(a.data_[0] == b.data_[0]))
        requires std::equality_comparable<T>
    {
        for (size_type i = 0; i < N; ++i) {
            if (!(a.data_[i] == b.data_[i])) return false;
        }
        return true;
    }

    [[nodiscard]] friend constexpr auto operator<=>(FixedArray const& a,
                                                    FixedArray const& b) noexcept(noexcept(a.data_[0] <=> b.data_[0]))
        requires std::three_way_comparable<T>
    {
        using ordering = std::compare_three_way_result_t<T>;
        for (size_type i = 0; i < N; ++i) {
            // The named comparison avoids comparing the ordering
            // against a literal zero, which the compiler reads as a
            // null pointer constant and rejects.
            if (auto cmp = a.data_[i] <=> b.data_[i]; std::is_neq(cmp)) {
                return cmp;
            }
        }
        return ordering::equivalent;
    }
};

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

inline void runtime_smoke_test() {
    FA8 a{};
    for (std::size_t i = 0; i < 8; ++i) {
        if (a[i] != 0) std::abort();
    }

    FA8 b{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    int sum = 0;
    for (auto v : b)
        sum += v;
    if (sum != 36) std::abort();

    auto c = FA8::fill_with(99);
    if (c.front() != 99 || c.back() != 99) std::abort();

    a.fill(7);
    for (std::size_t i = 0; i < 8; ++i) {
        if (a[i] != 7) std::abort();
    }

    auto i5 = mint_refined<bounded_above<FA8::capacity - 1>>(std::size_t{5});
    if (b.at(i5) != 6) std::abort();

    if (b.at<0>() != 1) std::abort();
    if (b.at<7>() != 8) std::abort();

    FA8 ord_a{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    FA8 ord_b{std::in_place, 1, 2, 3, 4, 5, 6, 7, 9};
    if (!std::is_lt(ord_a <=> ord_b)) std::abort();
    if (!std::is_gt(ord_b <=> ord_a)) std::abort();

    auto s = b.as_span();
    if (s.size() != 8) std::abort();
    int sspan = 0;
    for (auto v : s)
        sspan += v;
    if (sspan != 36) std::abort();

    FA8 eq_a = FA8::fill_with(5);
    FA8 eq_b = FA8::fill_with(5);
    if (!(eq_a == eq_b)) std::abort();
    eq_b.fill(4);
    if (eq_a == eq_b) std::abort();

    FA8 sw_a{std::in_place, 1, 2, 3, 4, 5, 6, 7, 8};
    FA8 sw_b = FA8::fill_with(0);
    sw_a.swap(sw_b);
    if (sw_b.front() != 1 || sw_a.front() != 0) std::abort();

    FixedArray<int, 4> small{};
    if (small.size() != 4) std::abort();

    // The alignment specifier reaches the elements, because the
    // storage member sits at offset zero.
    alignas(64) FixedArray<int64_t, 8> aligned_buf{};
    if (std::bit_cast<std::uintptr_t>(aligned_buf.data()) % 64 != 0) {
        std::abort();
    }

    FixedArray<int64_t, 4> z{};
    for (std::size_t i = 0; i < 4; ++i) {
        if (z[i] != 0) std::abort();
    }
}

}  // namespace detail::fixed_array_self_test

}  // namespace fixy

#pragma once

// A fixed-capacity array that differs from the standard one in three
// ways that matter here.
//
// Declaring one without braces still zero-fills every element, because
// the storage member carries a default initializer. The standard array
// leaves the elements uninitialized in that form, which is the same
// trap a raw C array sets.
//
// There is no throwing accessor, because nothing in this tree throws.
// utils/scripts/check-no-throw-no-rtti.sh holds that property on each
// artifact. Bounds come instead from three tiers: a subscript the
// caller vouches for, a proof-token index that was checked once when it
// was built, and an index fixed at compile time that cannot be out of
// range at all.
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

#include <fixy/Refined.h>
#include <foundation/Platform.h>

#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
    // A standard array, not a C array, so every subscript below goes
    // through its operator[], which the standard library's debug
    // assertions check.  A subscript of a C array member is checked by
    // no build, and an overflow of it lands in the next member, where
    // the address sanitizer sees valid memory.
    std::array<T, N> data_{};

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

    [[nodiscard]] constexpr pointer data() noexcept { return data_.data(); }
    [[nodiscard]] constexpr const_pointer data() const noexcept { return data_.data(); }
    [[nodiscard]] constexpr iterator begin() noexcept { return data_.data(); }
    [[nodiscard]] constexpr const_iterator begin() const noexcept { return data_.data(); }
    [[nodiscard]] constexpr iterator end() noexcept { return data_.data() + N; }
    [[nodiscard]] constexpr const_iterator end() const noexcept { return data_.data() + N; }

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

}  // namespace fixy

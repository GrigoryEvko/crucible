#pragma once

// Stateless handle onto a thread-local cell keyed by (Tag, T).
//
// Every handle naming the same Tag and T reads and writes one cell per
// thread, and copying a handle does not create a second cell.  A
// distinct Tag buys a distinct cell.  Nothing here can check that a
// Tag is unique to one logical slot, so choosing it is the caller's
// obligation.

#include <crucible/Platform.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename Tag, typename T>
    requires std::is_default_constructible_v<T>
class [[nodiscard]] ThreadLocalRef {
public:
    using tag_type = Tag;
    using value_type = T;

private:
    [[nodiscard]] static T& storage_() noexcept(std::is_nothrow_default_constructible_v<T>) {
        thread_local T storage{};
        return storage;
    }

public:
    // Constructing a handle does not materialize the cell.  The first
    // access does.
    constexpr ThreadLocalRef() noexcept = default;

    constexpr ThreadLocalRef(ThreadLocalRef const&) noexcept = default;
    constexpr ThreadLocalRef(ThreadLocalRef&&) noexcept = default;
    constexpr ThreadLocalRef& operator=(ThreadLocalRef const&) noexcept = default;
    constexpr ThreadLocalRef& operator=(ThreadLocalRef&&) noexcept = default;
    ~ThreadLocalRef() = default;

    [[nodiscard]] T const& peek() const noexcept(std::is_nothrow_default_constructible_v<T>) { return storage_(); }

    // The mutating accessors are const on the handle because the
    // handle owns no state.  What they mutate is the per-thread cell,
    // which a const handle is still entitled to write.
    [[nodiscard]] T& peek_mut() const noexcept(std::is_nothrow_default_constructible_v<T>) { return storage_(); }

    template <typename U>
        requires std::is_assignable_v<T&, U&&>
    void store(U&& v) const
        noexcept(std::is_nothrow_default_constructible_v<T> && std::is_nothrow_assignable_v<T&, U&&>) {
        storage_() = std::forward<U>(v);
    }

    void reset() const noexcept(std::is_nothrow_default_constructible_v<T> && std::is_nothrow_move_assignable_v<T>) {
        storage_() = T{};
    }

    [[nodiscard]] static consteval std::string_view tag_name() noexcept { return std::meta::display_string_of(^^Tag); }
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return std::meta::display_string_of(^^T);
    }
};

template <typename Tag, typename T>
    requires std::is_default_constructible_v<T>
[[nodiscard]] constexpr ThreadLocalRef<Tag, T> mint_thread_local_ref() noexcept {
    return ThreadLocalRef<Tag, T>{};
}

namespace detail {
struct LayoutAnchorTag {};
}  // namespace detail
static_assert(sizeof(ThreadLocalRef<detail::LayoutAnchorTag, int>) == 1);
static_assert(sizeof(ThreadLocalRef<detail::LayoutAnchorTag, double>) == 1);
static_assert(std::is_empty_v<ThreadLocalRef<detail::LayoutAnchorTag, int>>);
static_assert(std::is_trivially_copyable_v<ThreadLocalRef<detail::LayoutAnchorTag, int>>);

namespace detail::thread_local_ref_self_test {

struct CounterTag {};
struct AccumulatorTag {};
struct OtherTag {};

using IntCounter = ThreadLocalRef<CounterTag, int>;
using IntAccumulator = ThreadLocalRef<AccumulatorTag, int>;
using DoubleOther = ThreadLocalRef<OtherTag, double>;

inline constexpr IntCounter c_default{};
inline constexpr IntCounter c_copy = c_default;
static_assert(sizeof(IntCounter) == 1);

static_assert(std::is_same_v<IntCounter::tag_type, CounterTag>);
static_assert(std::is_same_v<IntCounter::value_type, int>);
static_assert(std::is_same_v<DoubleOther::tag_type, OtherTag>);
static_assert(std::is_same_v<DoubleOther::value_type, double>);

static_assert(!std::is_same_v<IntCounter, IntAccumulator>);
static_assert(!std::is_same_v<IntCounter, DoubleOther>);

static_assert(std::is_copy_constructible_v<IntCounter>);
static_assert(std::is_copy_assignable_v<IntCounter>);
static_assert(std::is_move_constructible_v<IntCounter>);
static_assert(std::is_move_assignable_v<IntCounter>);
static_assert(std::is_trivially_copyable_v<IntCounter>);

inline constexpr auto minted_counter = mint_thread_local_ref<CounterTag, int>();
static_assert(std::is_same_v<decltype(minted_counter), const IntCounter>);

static_assert(IntCounter::value_type_name() == "int");
static_assert(DoubleOther::value_type_name() == "double");

inline void runtime_smoke_test() {
    int seed = 17;

    IntCounter c{};
    if (c.peek() != 0) std::abort();

    c.store(seed * 2);
    if (c.peek() != 34) std::abort();

    c.peek_mut() = seed * 3;
    if (c.peek() != 51) std::abort();

    c.reset();
    if (c.peek() != 0) std::abort();

    // Same T, different Tag: a write to one must not reach the other.
    IntAccumulator a{};
    c.store(100);
    a.store(200);
    if (c.peek() != 100) std::abort();
    if (a.peek() != 200) std::abort();

    // Same Tag: both handles reach one cell.
    IntCounter c1{};
    IntCounter c2{};
    c1.store(seed);
    if (c2.peek() != 17) std::abort();

    auto m = mint_thread_local_ref<CounterTag, int>();
    if (m.peek() != 17) std::abort();

    // The compare goes through bit_cast because equality on a
    // floating-point value is an error under the project warning set.
    DoubleOther d{};
    d.store(2.5);
    {
        auto observed = std::bit_cast<std::uint64_t>(d.peek());
        auto expected = std::bit_cast<std::uint64_t>(2.5);
        if (observed != expected) std::abort();
    }

    // The cells outlive this function, so every one touched above is
    // returned to its default before returning.
    c.reset();
    c1.reset();
    a.reset();
    d.reset();
}

}  // namespace detail::thread_local_ref_self_test

}  // namespace crucible::safety

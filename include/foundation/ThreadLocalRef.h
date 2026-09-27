#pragma once

// Stateless handle onto a thread-local cell keyed by (Tag, T).
//
// Every handle naming the same Tag and T reads and writes one cell per
// thread, and copying a handle does not create a second cell.  A
// distinct Tag buys a distinct cell.  Nothing here can check that a
// Tag is unique to one logical slot, so choosing it is the caller's
// obligation.
//
// The handle grants nothing that its type does not name, so building
// one authorizes nothing, and the public default constructor is the one
// door.  There is no mint.

#include <foundation/Platform.h>

#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation {

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

namespace detail::thread_local_ref_self_test {

struct CounterTag {};
struct AccumulatorTag {};
struct OtherTag {};

using IntCounter = ThreadLocalRef<CounterTag, int>;
using IntAccumulator = ThreadLocalRef<AccumulatorTag, int>;
using DoubleOther = ThreadLocalRef<OtherTag, double>;

// The handle is an empty tag: it holds nothing, so it costs one byte and
// collapses to none as a member under [[no_unique_address]].
static_assert(sizeof(IntCounter) == 1 && sizeof(DoubleOther) == 1);
static_assert(std::is_empty_v<IntCounter> && std::is_trivially_copyable_v<IntCounter>);

inline constexpr IntCounter c_default{};
inline constexpr IntCounter c_copy = c_default;

static_assert(std::is_same_v<IntCounter::tag_type, CounterTag>);
static_assert(std::is_same_v<IntCounter::value_type, int>);
static_assert(std::is_same_v<DoubleOther::tag_type, OtherTag>);
static_assert(std::is_same_v<DoubleOther::value_type, double>);

static_assert(!std::is_same_v<IntCounter, IntAccumulator>);
static_assert(!std::is_same_v<IntCounter, DoubleOther>);

static_assert(std::is_copy_constructible_v<IntCounter> && std::is_copy_assignable_v<IntCounter>);
static_assert(std::is_move_constructible_v<IntCounter> && std::is_move_assignable_v<IntCounter>);

static_assert(IntCounter::value_type_name() == "int");
static_assert(DoubleOther::value_type_name() == "double");

}  // namespace detail::thread_local_ref_self_test

}  // namespace foundation

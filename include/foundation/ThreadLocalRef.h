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

}  // namespace foundation

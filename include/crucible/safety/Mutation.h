#pragma once

// ── Mutation-mode wrappers ─────────────────────────────────────────
//
// Make the allowed update discipline part of the type.
//
//   Axiom coverage: MemSafe, DetSafe.
//   Runtime cost:   zero beyond the wrapped container (AppendOnly),
//                   one contract check per advance (Monotonic),
//                   one bool per instance (WriteOnce).
//
// AppendOnly<T, Storage>  — Storage<T> restricted to grow-only.
// Monotonic<T, Cmp>       — single value that only advances per Cmp.
// WriteOnce<T>            — settable exactly once, then read-only.

#include <crucible/Platform.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace crucible::safety {

// ── AppendOnly ──────────────────────────────────────────────────────
//
// Default storage is std::vector; users may substitute arena-backed
// or inplace_vector backing by specifying the second template param.

template <typename T, template <typename...> class Storage = std::vector>
class [[nodiscard]] AppendOnly {
    Storage<T> data_;

public:
    using value_type     = T;
    using storage_type   = Storage<T>;
    using const_iterator = typename Storage<T>::const_iterator;

    AppendOnly() = default;

    explicit AppendOnly(std::size_t reserve_hint) {
        if constexpr (requires { data_.reserve(reserve_hint); })
            data_.reserve(reserve_hint);
    }

    // The only mutation permitted: grow the tail.
    template <typename... Args>
    void emplace(Args&&... args) {
        data_.emplace_back(std::forward<Args>(args)...);
    }

    void append(T item) { data_.emplace_back(std::move(item)); }

    // Read-only access.
    [[nodiscard]] const T& operator[](std::size_t i) const noexcept { return data_[i]; }
    [[nodiscard]] const T& front() const noexcept { return data_.front(); }
    [[nodiscard]] const T& back()  const noexcept { return data_.back(); }
    [[nodiscard]] std::size_t size()  const noexcept { return data_.size(); }
    [[nodiscard]] bool        empty() const noexcept { return data_.empty(); }

    [[nodiscard]] const_iterator begin() const noexcept { return data_.begin(); }
    [[nodiscard]] const_iterator end()   const noexcept { return data_.end(); }

    // Consuming drain — yield the collected storage and leave *this empty.
    [[nodiscard]] Storage<T> drain() && noexcept(std::is_nothrow_move_constructible_v<Storage<T>>) {
        return std::move(data_);
    }
};

// ── Monotonic ───────────────────────────────────────────────────────
//
// Cmp defaults to std::less<T>; advance(v) requires `!(v < current)`,
// i.e. `v >= current`.  Use std::greater for decreasing-only semantics.

template <typename T, typename Cmp = std::less<T>>
class [[nodiscard]] Monotonic {
    T   value_;
    Cmp cmp_{};

public:
    using value_type      = T;
    using comparator_type = Cmp;

    constexpr explicit Monotonic(T initial)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_{std::move(initial)} {}

    Monotonic(const Monotonic&)            = default;
    Monotonic(Monotonic&&)                 = default;
    Monotonic& operator=(const Monotonic&) = default;
    Monotonic& operator=(Monotonic&&)      = default;

    [[nodiscard]] constexpr const T& get()     const noexcept { return value_; }
    [[nodiscard]] constexpr const T& current() const noexcept { return value_; }

    // Advance.  Contract-checks that the new value does not go backward.
    constexpr void advance(T new_value) noexcept(std::is_nothrow_move_assignable_v<T>)
#if CRUCIBLE_HAS_CONTRACTS
        pre(!Cmp{}(new_value, value_))
#endif
    {
        value_ = std::move(new_value);
    }

    // Compare-and-advance.  Returns true iff advanced.  Useful when
    // multiple threads attempt to advance and only the monotonic-valid
    // ones should succeed.
    constexpr bool try_advance(T new_value)
        noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        if (cmp_(new_value, value_)) return false;
        value_ = std::move(new_value);
        return true;
    }
};

// ── WriteOnce ───────────────────────────────────────────────────────
//
// Settable exactly once.  Subsequent attempts contract-fail.  After
// set, the value is immutable.  Reads before set contract-fail.
//
// Runtime cost: one bool per instance (implicit in std::optional tag).
// Use for init-time constants whose value is discovered at startup.

template <typename T>
class [[nodiscard]] WriteOnce {
    std::optional<T> value_;

public:
    using value_type = T;

    constexpr WriteOnce() = default;

    WriteOnce(const WriteOnce&)            = default;
    WriteOnce(WriteOnce&&)                 = default;
    WriteOnce& operator=(const WriteOnce&) = default;
    WriteOnce& operator=(WriteOnce&&)      = default;

    // Set exactly once.  Contract-checks that value has not been set.
    constexpr void set(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
#if CRUCIBLE_HAS_CONTRACTS
        pre(!value_.has_value())
#endif
    {
        value_.emplace(std::move(v));
    }

    // Try-set — returns true iff this was the first set.
    constexpr bool try_set(T v) noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (value_) return false;
        value_.emplace(std::move(v));
        return true;
    }

    // Read the set value.  Contract-fails if not yet set.
    [[nodiscard]] constexpr const T& get() const noexcept
#if CRUCIBLE_HAS_CONTRACTS
        pre(value_.has_value())
#endif
    {
        return *value_;
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return value_.has_value(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_.has_value(); }
};

} // namespace crucible::safety

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
// AppendOnly<T, Storage>               — Storage<T> restricted to grow-only.
// OrderedAppendOnly<T, KeyFn, Cmp, St> — AppendOnly + per-emplace key
//                                        monotonicity (nested composition).
// Monotonic<T, Cmp>                    — single value that only advances per Cmp.
// WriteOnce<T>                         — settable exactly once, then read-only.

#include <crucible/Platform.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
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

// ── OrderedAppendOnly ───────────────────────────────────────────────
//
// AppendOnly + per-emplace monotonicity check on a key projection.
// Nested composition of AppendOnly (grow-only) and Monotonic's ordering
// invariant: each appended element's projected key must not go backward
// per Cmp relative to the last appended element's key.
//
// Typical use: an append-only log whose entries carry a monotonically
// non-decreasing step_id / epoch / timestamp whose ordering is relied
// upon by downstream code (e.g. binary search).  Without this wrapper
// the ordering lives as a runtime pre() on the writer plus a doc
// comment on the reader; here it is the log's type.
//
// KeyFn and Cmp must be stateless (matches Monotonic's idiom) so the
// pre-condition can construct them fresh per-call; [[no_unique_address]]
// collapses empty functors to zero layout cost.
//
//   Axiom coverage: MemSafe (inherits AppendOnly) + DetSafe.
//   Runtime cost:   storage of the wrapped Storage<T>, plus one KeyFn
//                   invocation + one Cmp invocation per append under
//                   contract semantic=enforce/observe; zero under
//                   semantic=ignore (hot-path TUs).

template <typename T,
          typename KeyFn = std::identity,
          typename Cmp   = std::less<>,
          template <typename...> class Storage = std::vector>
class [[nodiscard]] OrderedAppendOnly {
    AppendOnly<T, Storage>     inner_;
    [[no_unique_address]] KeyFn key_{};
    [[no_unique_address]] Cmp   cmp_{};

public:
    using value_type     = T;
    using key_type       = std::invoke_result_t<KeyFn, const T&>;
    using key_fn_type    = KeyFn;
    using comparator     = Cmp;
    using storage_type   = Storage<T>;
    using const_iterator = typename AppendOnly<T, Storage>::const_iterator;

    OrderedAppendOnly() = default;
    explicit OrderedAppendOnly(std::size_t reserve_hint) : inner_{reserve_hint} {}

    // The only mutation permitted: grow the tail, and only with a key
    // that does not go backward relative to the last entry.  Contract
    // fires via std::terminate under enforce; collapses to [[assume]]
    // under ignore.  KeyFn/Cmp are stateless — this idiom matches
    // Monotonic::advance's `pre(!Cmp{}(new, old))`.
    void append(T item)
        pre(inner_.empty() || !Cmp{}(KeyFn{}(item), KeyFn{}(inner_.back())))
    {
        inner_.append(std::move(item));
    }

    // Forwarding emplace: constructs the element, then contract-checks
    // via append().  The temp construction is unavoidable — we can't
    // evaluate the key of a not-yet-constructed element.
    template <typename... Args>
    void emplace(Args&&... args) {
        append(T{std::forward<Args>(args)...});
    }

    // Read-only access — delegates to the wrapped AppendOnly.
    [[nodiscard]] const T& operator[](std::size_t i) const noexcept { return inner_[i]; }
    [[nodiscard]] const T& front() const noexcept { return inner_.front(); }
    [[nodiscard]] const T& back()  const noexcept { return inner_.back(); }
    [[nodiscard]] std::size_t size()  const noexcept { return inner_.size(); }
    [[nodiscard]] bool        empty() const noexcept { return inner_.empty(); }

    [[nodiscard]] const_iterator begin() const noexcept { return inner_.begin(); }
    [[nodiscard]] const_iterator end()   const noexcept { return inner_.end(); }

    // Consuming drain — yield the collected storage and leave *this empty.
    [[nodiscard]] Storage<T> drain() && noexcept(std::is_nothrow_move_constructible_v<Storage<T>>) {
        return std::move(inner_).drain();
    }
};

// Zero-cost guarantee: stateless KeyFn + Cmp collapse via [[no_unique_address]],
// so OrderedAppendOnly<T> with default projections has the same layout as
// the wrapped AppendOnly<T>.
static_assert(sizeof(OrderedAppendOnly<std::uint64_t>) == sizeof(AppendOnly<std::uint64_t>),
              "OrderedAppendOnly must collapse empty KeyFn/Cmp to zero layout cost");

// ── Monotonic ───────────────────────────────────────────────────────
//
// Cmp defaults to std::less<T>; advance(v) requires `!(v < current)`,
// i.e. `v >= current`.  Use std::greater for decreasing-only semantics.

template <typename T, typename Cmp = std::less<T>>
class [[nodiscard]] Monotonic {
    T                          value_;
    [[no_unique_address]] Cmp  cmp_{};   // zero-cost when Cmp is empty

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
        pre(!Cmp{}(new_value, value_))
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

    // Convenience for integral counters: increment by one.  Contract
    // catches wraparound (the only way an integral counter can violate
    // monotonicity is overflow).  Equivalent to advance(get() + 1).
    constexpr void bump() noexcept
        requires std::integral<T>
        pre(value_ != std::numeric_limits<T>::max())
    {
        ++value_;
    }
};

// Zero-cost guarantee: empty Cmp must collapse via [[no_unique_address]].
static_assert(sizeof(Monotonic<uint32_t, std::less<uint32_t>>) == sizeof(uint32_t),
              "Monotonic<T, EmptyCmp> must be zero-cost: same size as T");
static_assert(sizeof(Monotonic<uint64_t, std::less<uint64_t>>) == sizeof(uint64_t),
              "Monotonic<T, EmptyCmp> must be zero-cost: same size as T");

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
        pre(!value_.has_value())
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
        pre(value_.has_value())
    {
        return *value_;
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return value_.has_value(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value_.has_value(); }
};

} // namespace crucible::safety

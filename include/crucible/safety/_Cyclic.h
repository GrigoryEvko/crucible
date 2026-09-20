#pragma once

// Free-running modular counter for a power-of-two ring buffer.  The
// stored value is an absolute count.  The observable state is
// `counter & (N-1)`, the ring slot.
//
// The counter is deliberately not pre-masked.  Keeping the absolute
// count is what makes the `- 1 - i` reverse-offset arithmetic of
// index_back correct, and what lets a separate fill counter coexist
// with the cursor.  A pre-masked cursor loses both.
//
// The wrap is deterministic, not undefined.  advance() increments an
// unsigned counter, which is modular arithmetic.  When the counter
// wraps past its own 2^bits ceiling the slot stays correct because N
// divides 2^bits, so the low log2(N) bits — the only bits the mask
// reads — are unaffected by the high-bit wrap.

#include <crucible/Platform.h>

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace crucible::safety {

template <std::unsigned_integral T, T N>
    requires(N > T{0} && (N & (N - T{1})) == T{0})
class [[nodiscard]] Cyclic {
public:
    using value_type = T;

    static constexpr T capacity = N;
    static constexpr T mask = N - T{1};

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::Cyclic"; }

private:
    T counter_ = T{0};

public:
    constexpr Cyclic() noexcept = default;

    constexpr explicit Cyclic(T start) noexcept : counter_{start} {}

    constexpr Cyclic(Cyclic const&) = default;
    constexpr Cyclic(Cyclic&&) = default;
    constexpr Cyclic& operator=(Cyclic const&) = default;
    constexpr Cyclic& operator=(Cyclic&&) = default;
    ~Cyclic() = default;

    // Every arithmetic result is cast back to T because a narrow T
    // promotes to int.  The cast is value-exact: the low log2(N) bits,
    // the only bits the mask reads, are the same in either domain.

    [[nodiscard]] constexpr T index() const noexcept { return static_cast<T>(counter_ & mask); }

    // index_back(0) is the slot the producer last advanced past.  The
    // result is meaningful only for i < N, because looking back N or
    // more aliases a still-live slot.  There is no precondition: the
    // mask makes every result a valid slot, so the bound on i is a
    // logic bound and not a safety bound.
    [[nodiscard]] constexpr T index_back(T i) const noexcept { return static_cast<T>((counter_ - T{1} - i) & mask); }

    constexpr void advance() noexcept { ++counter_; }

    constexpr void advance_by(T k) noexcept { counter_ = static_cast<T>(counter_ + k); }

    [[nodiscard]] constexpr T raw() const noexcept { return counter_; }

    [[nodiscard]] constexpr explicit operator T() const noexcept { return counter_; }

    [[nodiscard]] friend constexpr bool operator==(Cyclic const& a, Cyclic const& b) noexcept = default;
};

static_assert(sizeof(Cyclic<uint32_t, 8>) == sizeof(uint32_t));
static_assert(sizeof(Cyclic<uint32_t, 16>) == sizeof(uint32_t));
static_assert(sizeof(Cyclic<uint64_t, 64>) == sizeof(uint64_t));
static_assert(sizeof(Cyclic<uint8_t, 4>) == sizeof(uint8_t));

static_assert(alignof(Cyclic<uint32_t, 8>) == alignof(uint32_t));
static_assert(alignof(Cyclic<uint64_t, 64>) == alignof(uint64_t));

static_assert(std::is_trivially_copyable_v<Cyclic<uint32_t, 8>>);
static_assert(std::is_trivially_destructible_v<Cyclic<uint32_t, 8>>);
static_assert(std::is_standard_layout_v<Cyclic<uint32_t, 8>>);

static_assert(!std::is_same_v<Cyclic<uint32_t, 8>, uint32_t>);
static_assert(!std::is_convertible_v<uint32_t, Cyclic<uint32_t, 8>>);
static_assert(!std::is_convertible_v<Cyclic<uint32_t, 8>, uint32_t>);

namespace detail::cyclic_self_test {

using C8 = Cyclic<uint32_t, 8>;
using C16 = Cyclic<uint32_t, 16>;

[[nodiscard]] consteval bool default_at_slot_zero() noexcept {
    C8 c{};
    return c.raw() == 0 && c.index() == 0;
}
static_assert(default_at_slot_zero());

[[nodiscard]] consteval bool advance_walks_and_wraps() noexcept {
    C8 c{};
    for (uint32_t expected = 0; expected < 8; ++expected) {
        if (c.index() != expected) return false;
        c.advance();
    }
    return c.index() == 0 && c.raw() == 8;
}
static_assert(advance_walks_and_wraps());

[[nodiscard]] consteval bool index_back_is_mru_first() noexcept {
    C8 c{};
    c.advance_by(4);
    return c.index() == 4 && c.index_back(0) == 3 && c.index_back(1) == 2 && c.index_back(2) == 1
        && c.index_back(3) == 0;
}
static_assert(index_back_is_mru_first());

[[nodiscard]] consteval bool index_back_wraps_through_zero() noexcept {
    C8 c{};
    return c.index_back(0) == 7 && c.index_back(1) == 6;
}
static_assert(index_back_wraps_through_zero());

[[nodiscard]] consteval bool wrap_at_type_ceiling_is_slot_exact() noexcept {
    Cyclic<uint8_t, 4> c{uint8_t{255}};
    if (c.index() != 3) return false;
    c.advance();
    return c.raw() == 0 && c.index() == 0;
}
static_assert(wrap_at_type_ceiling_is_slot_exact());

[[nodiscard]] consteval bool equality_compares_counter() noexcept {
    C8 a{3};
    C8 b{3};
    C8 c{4};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_counter());

[[nodiscard]] consteval bool explicit_t_conversion_is_raw() noexcept {
    C16 c{5};
    c.advance();
    return static_cast<uint32_t>(c) == 6;
}
static_assert(explicit_t_conversion_is_raw());

static_assert(C8::capacity == 8 && C8::mask == 7);
static_assert(C16::capacity == 16 && C16::mask == 15);

static_assert(C8::wrapper_kind() == "structural::Cyclic");

inline void runtime_smoke_test() {
    volatile uint32_t seed = 3;
    C8 c{static_cast<uint32_t>(seed)};

    if (c.raw() != 3) std::abort();
    if (c.index() != 3) std::abort();
    if (c.index_back(0) != 2) std::abort();
    if (c.index_back(3) != 7) std::abort();

    c.advance();
    if (c.index() != 4 || c.raw() != 4) std::abort();

    c.advance_by(5);
    if (c.raw() != 9 || c.index() != 1) std::abort();

    if (static_cast<uint32_t>(c) != 9) std::abort();

    volatile uint8_t hi = 255;
    Cyclic<uint8_t, 4> n{static_cast<uint8_t>(hi)};
    if (n.index() != 3) std::abort();
    n.advance();
    if (n.raw() != 0 || n.index() != 0) std::abort();

    C8 d{};
    if (d.raw() != 0 || d.index() != 0) std::abort();
    C8 e{};
    if (!(d == e)) std::abort();
    e.advance();
    if (d == e) std::abort();
}

}  // namespace detail::cyclic_self_test

}  // namespace crucible::safety

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

#include <foundation/Platform.h>

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace fixy {

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

}  // namespace fixy

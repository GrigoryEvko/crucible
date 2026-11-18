#pragma once

// The spin budget of the base: a time in nanoseconds that bounds one spin,
// and the opaque deadline that the spin reads.
//
// THE RATE
//   A spin converts its budget to a deadline of the counter of
//   foundation/os/SpinHint.h one time, before the spin:
//
//     ticks = (nanoseconds * multiplier) >> shift
//
//   The rate of the process holds the multiplier and the shift.  The start-up
//   hook of src/fixy/os/SpinBudget.cpp computes them one time:
//     aarch64           from CNTFRQ_EL0, the rate of the counter
//     x86_64, Intel     from CPUID leaf 0x15, when its three words are not
//                       zero and the core runs on no hypervisor
//     each other core   from a calibration against CLOCK_MONOTONIC through
//                       the clock door of fixy/os/MonotonicClock.h
//   The calibration takes two samples about 100 microseconds apart.  Each
//   sample reads the counter before and after one read of the clock, takes
//   the middle of the two counter reads, and keeps the narrowest of some such
//   brackets.  An AMD core and a virtual machine take the calibration, because
//   leaf 0x15 is absent there or does not give the rate of the guest.
//
//   The multiplier is less than 2^32, and a budget is less than 2^32
//   nanoseconds, so the product fits in 64 bits, and the conversion has no
//   division.  The largest budget is about 4.3 seconds.  A rate of zero is
//   the rate before the hook, or the rate of a failed calibration.  It gives
//   a budget of zero ticks, so a spin ends at its first check and its caller
//   does the slower wait at once.
//
// THE DEADLINE
//   SpinDeadline holds a value of the counter.  It gives only has_passed():
//   no count of ticks, no compare and no text.  So no result of a
//   computation can read the time of the counter through a deadline.
//
// THE GATE
//   The counter changes from run to run, so each read of it reads a source of
//   nondeterminism.  SpinDeadline::after, has_passed() and the two forms each
//   read the counter, and each takes the bare tag of Nondet, as the read of a
//   clock reader of fixy/os/Time.h does.  Only a context whose row holds
//   Nondet gives the tag, through extract_bare_from_ctx.  So a deadline that a
//   reference takes into a body with no Nondet does not read the counter
//   there.  A foreground, replay or kernel context holds no Nondet and takes
//   no deadline: the hot path keeps CRUCIBLE_SPIN_PAUSE of
//   foundation/Platform.h.
//
// DETERMINISM
//   A budget only chooses between a spin and the slower wait after it, such
//   as a park.  Each of the two waits ends at the same event and gives the
//   same value, so the result of the wait does not depend on the budget, on
//   the rate or on the counter.  A caller whose answer depends on the end of
//   a spin states it with its own tag of Nondet.  A thread that moves to a
//   different core during a spin reads the counter of that core.  The
//   counters of the cores of one host agree to a small amount, so the move
//   changes the length of one spin by that amount.
//
// THE FORMS
//   spin_while_equal waits on a word with the word arm of the process.
//   spin_until waits on a predicate with the plain arm of the process.  Each
//   form ends at the deadline, and its caller then does the slower wait.
//
// This header is in the layer X3_os of utils/rule-table.txt.  The door file
// is src/fixy/os/SpinBudget.cpp.

#include <foundation/ByteSeal.h>
#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Quantity.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/SpinHint.h>

#include <cstdint>
#include <type_traits>

namespace fixy::time {

namespace eff = ::foundation::effects;

// A time that bounds one spin.  The 32 bits bound it to about 4.3 seconds
// (THE RATE).
using SpinBudget = ::foundation::core::Quantity<::foundation::core::unit::Nanosecond, std::uint32_t>;

// A count of ticks of the counter of the core.
using CounterTicks = ::foundation::core::Quantity<::foundation::core::unit::Cycle, std::uint64_t>;

// A span of time in nanoseconds: the length of a calibration.
using SpanNanos = ::foundation::core::Quantity<::foundation::core::unit::Nanosecond, std::uint64_t>;

// The rate of the counter: ticks = (nanoseconds * multiplier) >> shift.
struct TickRate {
    std::uint32_t multiplier = 0;
    std::uint32_t shift = 0;

    [[nodiscard]] constexpr bool operator==(TickRate const& other) const noexcept = default;
};

namespace detail {

// The product of a count of ticks and a power of two of the shift.
__extension__ using WideTicks = unsigned __int128;

}  // namespace detail

// The rate of a counter that counted TICKS in SPAN.  The shift is the
// largest one, at most 63, that keeps the multiplier below 2^32, and the
// multiplier is rounded to the nearest integer.  Zero ticks, a span of zero,
// or a rate of 2^32 ticks or more in each nanosecond give the rate of zero.
// Complexity: at most 64 steps.
[[nodiscard]] constexpr TickRate tick_rate_of(CounterTicks ticks, SpanNanos span) noexcept {
    std::uint64_t const count = ticks.raw();
    std::uint64_t const nanoseconds = span.raw();
    if (count == 0 || nanoseconds == 0) return TickRate{};
    constexpr detail::WideTicks kMultiplierLimit = detail::WideTicks{1} << 32;
    for (std::uint32_t shift = 63;; --shift) {
        detail::WideTicks const scaled = detail::WideTicks{count} << shift;
        detail::WideTicks const multiplier = (scaled + nanoseconds / 2) / nanoseconds;
        if (multiplier < kMultiplierLimit) {
            return TickRate{.multiplier = static_cast<std::uint32_t>(multiplier), .shift = shift};
        }
        if (shift == 0) return TickRate{};
    }
}

// The ticks of BUDGET at RATE.  The product of a 32-bit budget and a 32-bit
// multiplier fits in 64 bits.
[[nodiscard]] constexpr CounterTicks ticks_of(TickRate rate, SpinBudget budget) noexcept {
    return CounterTicks{(std::uint64_t{budget.raw()} * rate.multiplier) >> rate.shift};
}

namespace detail {

// The rate of the process, from the start-up hook (THE RATE), with no gate.
// SpinDeadline::after is its one caller.
[[nodiscard]] TickRate read_tick_rate_() noexcept;

}  // namespace detail

// The rate of the process, from the start-up hook (THE RATE).  The rate is a
// fact of the host, so the read takes the bare tag of Nondet.
[[nodiscard]] inline TickRate tick_rate_of_process(eff::cap::Nondet /*nondet*/) noexcept {
    return detail::read_tick_rate_();
}

class SpinDeadline;

[[nodiscard]] ::foundation::os::SpinEnd spin_while_equal(eff::cap::Nondet nondet,
                                                         ::foundation::core::Atomic<std::uint32_t> const& word,
                                                         std::uint32_t expected, SpinDeadline const& deadline) noexcept;

// The end of a spin (THE DEADLINE).
class SpinDeadline {
public:
    // The deadline BUDGET after now.  A sum past the last value of the
    // counter gives the last value.  The read takes the bare tag of Nondet
    // (THE GATE).
    [[nodiscard]] static SpinDeadline after(eff::cap::Nondet /*nondet*/, SpinBudget budget) noexcept {
        std::uint64_t end = 0;
        if (__builtin_add_overflow(::foundation::os::read_spin_counter(),
                                   ticks_of(detail::read_tick_rate_(), budget).raw(), &end)) {
            end = ~std::uint64_t{0};
        }
        return SpinDeadline{end};
    }

    // A deadline that has passed, with no read of the counter.  A spin that
    // starts its budget at its first check holds one before that check.
    [[nodiscard]] static constexpr SpinDeadline passed() noexcept { return SpinDeadline{0}; }

    // True when the counter is at the deadline or past it.  The read takes
    // the bare tag of Nondet (THE GATE).
    [[nodiscard]] bool has_passed(eff::cap::Nondet /*nondet*/) const noexcept {
        return ::foundation::os::read_spin_counter() >= ticks_;
    }

private:
    explicit constexpr SpinDeadline(std::uint64_t ticks) noexcept : ticks_{ticks} {}

    friend ::foundation::os::SpinEnd spin_while_equal(eff::cap::Nondet nondet,
                                                      ::foundation::core::Atomic<std::uint32_t> const& word,
                                                      std::uint32_t expected, SpinDeadline const& deadline) noexcept;

    std::uint64_t ticks_ = 0;
    // No byte route builds a deadline, so each deadline comes from a read of
    // the counter or from passed().
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// Waits with the word arm of the process until WORD holds a value other than
// EXPECTED, or until DEADLINE passes (THE FORMS).  The wait reads the counter,
// so it takes the bare tag of Nondet.
[[nodiscard]] inline ::foundation::os::SpinEnd spin_while_equal(eff::cap::Nondet /*nondet*/,
                                                                ::foundation::core::Atomic<std::uint32_t> const& word,
                                                                std::uint32_t expected,
                                                                SpinDeadline const& deadline) noexcept {
    return ::foundation::os::wait_for_change(word, expected, deadline.ticks_,
                                             ::foundation::os::spin_choice_of_process().word);
}

// Spins with the plain arm of the process until PREDICATE gives true, or
// until DEADLINE passes.  True when the predicate gave true.  The predicate
// runs before each check of the deadline, so a predicate that holds at the
// start gives true with each deadline.  Complexity: one call of the
// predicate and one read of the counter for each turn of the arm.
template <class Predicate>
    requires std::is_nothrow_invocable_r_v<bool, Predicate&>
[[nodiscard]] bool spin_until(eff::cap::Nondet nondet, SpinDeadline const& deadline, Predicate&& predicate) noexcept {
    ::foundation::os::SpinArm const arm = ::foundation::os::spin_choice_of_process().plain;
    for (;;) {
        if (predicate()) return true;
        if (deadline.has_passed(nondet)) return false;
        ::foundation::os::spin_turn(arm);
    }
}

}  // namespace fixy::time

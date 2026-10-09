#pragma once

// The futex door: a wait on a word with an optional deadline, and a wake of
// one waiter or of each waiter of the word (futex(2)).
//
// THE WORD
//   The word is a foundation::core::Atomic<std::uint32_t>, which holds the
//   32-bit word that the kernel reads.  The door gives the kernel the address
//   of the cell, and the checks of src/fixy/os/Futex.cpp hold its layout: the
//   cell is the word, with the alignment of the word.  The word is private to
//   the process, so the kernel keys its waiters by the address in the
//   process.
//
// THE WAIT
//   wait() sleeps while the word holds EXPECTED, until a wake on the word, a
//   handler of a signal, or the deadline.  The kernel reads the word in the
//   lock of its queue of waiters, so a wake after a change of the word is not
//   lost.  The Result tells how the wait ended:
//     woken            The kernel ended the wait with no error: a wake, or no
//                      cause.  The caller reads the word again.
//     word_changed     The word did not hold EXPECTED when the wait began.
//     interrupted      A handler of a signal ran.
//     deadline_passed  The monotonic clock passed the deadline.
//   Each other answer of the kernel is an Errno.
//
// THE WAKE
//   wake_one() wakes at most one waiter of the word, and wake_all() each
//   waiter.  Each gives the number of the waiters that it woke.
//
// THE DEADLINE
//   A deadline is an absolute time of CLOCK_MONOTONIC.  Deadline::after reads
//   the monotonic clock through a reader of fixy/os/Time.h, whose mint holds
//   the gate of a clock read, and adds a bound.  A caller that waits again
//   after interrupted keeps the same deadline, so a signal does not make the
//   wait longer.
//
// THE GATE
//   A wait blocks the thread, and a wake serves the waiters of a blocking
//   protocol.  Each operation takes a context that owns Effect::Block, so a
//   foreground context touches no futex word.  The bare blocking lock of
//   fixy/os/SpinLock.h calls the operations of the namespace detail, and its
//   gate asks for the same context.
//
// THE SANITIZERS
//   The door has one code path in each build.  In a build with
//   ThreadSanitizer, the raw system call primitive tells the sanitizer of
//   each futex call (THE SANITIZERS of foundation/os/Syscall.h).

#include <fixy/os/ClockSource.h>
#include <foundation/ByteSeal.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>

#include <concepts>
#include <cstdint>
#include <utility>

namespace fixy::futex {

namespace core = ::foundation::core;
namespace eff = ::foundation::effects;
namespace os = ::foundation::os;

// The word of a futex (THE WORD).
using FutexWord = core::Atomic<std::uint32_t>;

// How a wait ended (THE WAIT).
enum class WaitEnd : std::uint8_t {
    woken,
    word_changed,
    interrupted,
    deadline_passed,
};

// A reader of the monotonic clock of fixy/os/Time.h: its read gives a
// reading of CLOCK_MONOTONIC, the clock of a deadline.
template <typename Clock>
concept MonotonicClockReader =
    std::same_as<typename Clock::result_type, MonotonicClockBytes<std::uint64_t>> && requires(Clock const& clock) {
        { clock.read().error().value() } -> std::same_as<int>;
    };

// An absolute time of CLOCK_MONOTONIC, in nanoseconds (THE DEADLINE).
class Deadline final {
public:
    // The time BOUND_NANOS after a read of CLOCK, as a Result<Deadline,
    // os::Errno>.  A sum past the last time of the clock gives the last time.
    // A read that fails gives its error number.  The return type is deduced,
    // because the class is complete only in the body.
    template <MonotonicClockReader Clock>
    [[nodiscard]] static auto after(Clock const& clock, std::uint64_t bound_nanos) noexcept {
        using Answer = core::Result<Deadline, os::Errno>;
        auto const reading = clock.read();
        if (!reading.has_value()) {
            return Answer{core::err(static_cast<os::Errno>(reading.error().value()))};
        }
        std::uint64_t end = 0;
        if (__builtin_add_overflow(reading->peek(), bound_nanos, &end)) {
            end = kLastTime;
        }
        return Answer{Deadline{end}};
    }

    [[nodiscard]] constexpr std::uint64_t nanoseconds() const noexcept { return nanoseconds_; }

private:
    static constexpr std::uint64_t kLastTime = ~std::uint64_t{0};

    explicit constexpr Deadline(std::uint64_t nanoseconds) noexcept : nanoseconds_{nanoseconds} {}

    std::uint64_t nanoseconds_ = 0;
    // No byte route builds a deadline, so each deadline comes from a read of
    // the clock.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// A context that may wait on a futex word or wake its waiters: one that owns
// Effect::Block (THE GATE).
template <typename Ctx>
concept CtxMayUseFutex = eff::CtxOwnsCapability<Ctx, eff::Effect::Block>;

namespace detail {

// The count of a wake of each waiter: the largest count that the kernel
// reads, INT_MAX.
inline constexpr std::uint32_t kAllWaiters = 0x7FFFFFFFU;

// The operations of the door with no gate.  The gate of the caller holds the
// rule of THE GATE, as the gate of the blocking lock of fixy/os/SpinLock.h
// does.
[[nodiscard]] core::Result<WaitEnd, os::Errno> wait_on(FutexWord const& word, std::uint32_t expected,
                                                       core::Option<Deadline> deadline) noexcept;

[[nodiscard]] core::Result<std::uint32_t, os::Errno> wake_waiters(FutexWord const& word, std::uint32_t count) noexcept;

}  // namespace detail

// Sleeps while WORD holds EXPECTED, until a wake, a handler of a signal or
// DEADLINE (THE WAIT).
template <typename Ctx>
    requires CtxMayUseFutex<Ctx>
[[nodiscard]] core::Result<WaitEnd, os::Errno> wait(Ctx const& /*ctx*/, FutexWord const& word, std::uint32_t expected,
                                                    core::Option<Deadline> deadline = core::none) noexcept {
    return detail::wait_on(word, expected, std::move(deadline));
}

// Wakes at most one waiter of WORD, and gives the number that it woke.
template <typename Ctx>
    requires CtxMayUseFutex<Ctx>
[[nodiscard]] core::Result<std::uint32_t, os::Errno> wake_one(Ctx const& /*ctx*/, FutexWord const& word) noexcept {
    return detail::wake_waiters(word, 1);
}

// Wakes each waiter of WORD, and gives the number that it woke.
template <typename Ctx>
    requires CtxMayUseFutex<Ctx>
[[nodiscard]] core::Result<std::uint32_t, os::Errno> wake_all(Ctx const& /*ctx*/, FutexWord const& word) noexcept {
    return detail::wake_waiters(word, detail::kAllWaiters);
}

}  // namespace fixy::futex

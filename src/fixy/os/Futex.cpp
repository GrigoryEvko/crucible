// The door of fixy/os/Futex.h: the wait and the wake of a futex word through
// the raw system call primitive of foundation/os/Syscall.h.
//
// The argument words come from foundation/os/FutexWords.h, which the door
// file of the primitive compares with the kernel headers, so this file
// includes no header of the kernel and no header of the C library.

#include <fixy/os/Futex.h>

#include <foundation/os/FutexWords.h>
#include <foundation/os/Syscall.h>

#include <cstdint>
#include <type_traits>

namespace fixy::futex {

namespace {

// THE WORD: the kernel reads the 32 bits at the address of the cell.  The
// cell of an Atomic has one member, the word, and a class of the standard
// layout puts its first member at its own address.
static_assert(std::is_standard_layout_v<FutexWord>, "the futex word must have the standard layout");
static_assert(sizeof(FutexWord) == sizeof(std::uint32_t) && alignof(FutexWord) == alignof(std::uint32_t),
              "the futex word must have the size and the alignment of the 32-bit word that the kernel reads");

constexpr std::uint64_t kNanosecondsPerSecond = 1000000000U;

[[nodiscard]] os::Word address_of(FutexWord const& word) noexcept { return os::Word{static_cast<void const*>(&word)}; }

}  // namespace

namespace detail {

core::Result<WaitEnd, os::Errno> wait_on(FutexWord const& word, std::uint32_t expected,
                                         core::Option<Deadline> deadline) noexcept {
    // A wait with no deadline gives the kernel the address zero.
    os::KernelTimespec time{};
    os::Word time_address{};
    for (Deadline const& end : deadline) {
        time.seconds = static_cast<std::int64_t>(end.nanoseconds() / kNanosecondsPerSecond);
        time.nanoseconds = static_cast<std::int64_t>(end.nanoseconds() % kNanosecondsPerSecond);
        time_address = os::Word{static_cast<void const*>(&time)};
    }
    long const answer = os::raw_syscall(os::Syscall::futex, address_of(word), os::kFutexWaitBitsetPrivate, expected,
                                        time_address, os::Word{}, os::kFutexBitsetMatchAny);
    return os::result_of(answer).match(
        [](std::uint64_t) noexcept -> core::Result<WaitEnd, os::Errno> { return WaitEnd::woken; },
        [](os::Errno error) noexcept -> core::Result<WaitEnd, os::Errno> {
            switch (error) {
                case os::Errno::eagain:
                    return WaitEnd::word_changed;
                case os::Errno::eintr:
                    return WaitEnd::interrupted;
                case os::Errno::etimedout:
                    return WaitEnd::deadline_passed;
                default:
                    return core::err(error);
            }
        });
}

core::Result<std::uint32_t, os::Errno> wake_waiters(FutexWord const& word, std::uint32_t count) noexcept {
    long const answer = os::raw_syscall(os::Syscall::futex, address_of(word), os::kFutexWakePrivate, count);
    return os::result_of(answer).match(
        [](std::uint64_t woken) noexcept -> core::Result<std::uint32_t, os::Errno> {
            return static_cast<std::uint32_t>(woken);
        },
        [](os::Errno error) noexcept -> core::Result<std::uint32_t, os::Errno> { return core::err(error); });
}

}  // namespace detail

}  // namespace fixy::futex

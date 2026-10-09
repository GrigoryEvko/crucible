#pragma once

// The routes of the raw system call primitive in a build with
// ThreadSanitizer, for a call that can block (THE SANITIZERS of
// foundation/os/Syscall.h).  Only src/foundation/os/Syscall.cpp includes this
// header, and only a build with ThreadSanitizer calls the functions.

#include <foundation/os/Syscall.h>

#include <cstdint>

namespace foundation::os::detail {

// Issues CALL through the function of the C library that ThreadSanitizer
// intercepts, with the descriptor FIRST, the address SECOND and the count
// THIRD.  The result is the word of the kernel: a value, or -E for the error
// number E.  A call that the route does not know gives -ENOSYS.
[[nodiscard]] long issue_intercepted(Syscall call, std::uint64_t first, std::uint64_t second,
                                     std::uint64_t third) noexcept;

// What a futex call does for the sanitizer: a wake publishes the writes of
// the thread on the word, and a wait can see the writes of a wake.
enum class FutexRole : std::uint8_t {
    wake,
    wait,
    other,
};

// One raw futex call: its role and its six argument words.
struct FutexCall {
    FutexRole role = FutexRole::other;
    std::uint64_t address = 0;
    std::uint64_t operation = 0;
    std::uint64_t value = 0;
    std::uint64_t timeout = 0;
    std::uint64_t second_address = 0;
    std::uint64_t third_value = 0;
};

// Tells ThreadSanitizer of a raw futex call before the call.  A wake
// publishes the writes of the thread on the word, and the thread is in a
// blocking call until leave_futex.
void enter_futex(FutexCall const& call) noexcept;

// Tells ThreadSanitizer of a raw futex call after the call, with the word
// RESULT of the kernel.  The thread is out of the blocking call, and a wait
// that ended with no error sees the writes that a wake on the word published.
void leave_futex(FutexCall const& call, long result) noexcept;

}  // namespace foundation::os::detail

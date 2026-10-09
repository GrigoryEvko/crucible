#pragma once

// The routes of the raw system call primitive in a build with
// ThreadSanitizer, for a call that can block (THE SANITIZERS of
// foundation/os/Syscall.h).  Only src/foundation/os/Syscall.cpp includes this
// header, and only a build with ThreadSanitizer calls the functions.

#include <cstdint>

namespace foundation::os::detail {

// Issues a read or a write through the function of the C library that
// ThreadSanitizer intercepts, with the descriptor, the address and the count
// of the raw call.  The result is the word of the kernel: a value, or -E for
// the error number E.  These are the only calls of Syscall that can block and
// that the C library gives, and src/foundation/os/Syscall.cpp refuses a
// blocking call with no route at compile time.
[[nodiscard]] long read_intercepted(std::uint64_t descriptor, std::uint64_t address, std::uint64_t count) noexcept;
[[nodiscard]] long write_intercepted(std::uint64_t descriptor, std::uint64_t address, std::uint64_t count) noexcept;

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

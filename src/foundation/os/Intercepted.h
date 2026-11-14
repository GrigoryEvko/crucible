#pragma once

// The routes of the raw system call primitive in a build with
// ThreadSanitizer, for a call that can block and for a call that opens or
// closes a descriptor (THE SANITIZERS of foundation/os/Syscall.h).  Only
// src/foundation/os/Syscall.cpp includes this header, and only a build with
// ThreadSanitizer calls the functions.

#include <cstdint>

namespace foundation::os::detail {

// Issues a read or a write through the function of the C library that
// ThreadSanitizer intercepts, with the descriptor, the address and the count
// of the raw call.  The result is the word of the kernel: a value, or -E for
// the error number E.  These are the only calls of Syscall that can block and
// that the C library gives, and src/foundation/os/Syscall.cpp refuses a
// blocking call with no route at compile time.
//
// The two functions have the attribute nothrow and no noexcept specifier.
// src/foundation/os/Intercepted.cpp tells why.
[[nodiscard, gnu::nothrow]] long read_intercepted(std::uint64_t descriptor, std::uint64_t address, std::uint64_t count);
[[nodiscard, gnu::nothrow]] long write_intercepted(std::uint64_t descriptor, std::uint64_t address,
                                                   std::uint64_t count);

// Issues a map or an unmap through the function of the C library that
// ThreadSanitizer intercepts, with the words of the raw call.  The result is
// the word of the kernel: the address of the map or zero, or -E for the
// error number E.  Only the interceptors of mmap and munmap reset the shadow
// of the range and free its sync objects.  The system call hooks of the two
// calls are empty, and the sanitizer has no interface for a map (the owner
// decision of 2026-11-14).  The two functions of the C library are not
// cancellation points, so the two routes are noexcept.
[[nodiscard]] long map_intercepted(std::uint64_t address, std::uint64_t length, std::uint64_t protection,
                                   std::uint64_t flags, std::uint64_t descriptor, std::uint64_t offset) noexcept;
[[nodiscard]] long unmap_intercepted(std::uint64_t address, std::uint64_t length) noexcept;

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

// Tells ThreadSanitizer of a raw close of DESCRIPTOR before the call.  The
// sanitizer checks the close against each use of the descriptor in a
// different thread, and then it clears its state of the descriptor.  So a
// later descriptor with the same number starts with no state.
void enter_close(std::uint64_t descriptor) noexcept;

// Tells ThreadSanitizer of a raw close after the call, with the word RESULT
// of the kernel.
void leave_close(std::uint64_t descriptor, long result) noexcept;

// One raw open of the *at family: the call, the directory, the address of the
// path, and the two last words.  For openat they are the flags and the mode,
// and for openat2 the address and the size of the struct open_how.
enum class OpenCall : std::uint8_t {
    openat,
    openat2,
};

struct OpenWords {
    OpenCall call = OpenCall::openat;
    std::uint64_t directory = 0;
    std::uint64_t path = 0;
    std::uint64_t third = 0;
    std::uint64_t fourth = 0;
};

// Tells ThreadSanitizer of a raw open before the call.  The sanitizer checks
// the read of the path, and for openat2 the read of the struct open_how,
// against each write of another thread.  The sanitizer keeps no state of a
// new descriptor that a hook can give.
void enter_open(OpenWords const& words) noexcept;

// Tells ThreadSanitizer of a raw open after the call, with the word RESULT of
// the kernel.
void leave_open(OpenWords const& words, long result) noexcept;

}  // namespace foundation::os::detail

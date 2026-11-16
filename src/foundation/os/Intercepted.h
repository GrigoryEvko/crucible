#pragma once

// The calls of the floor that go to the C library (src/foundation/os/Intercepted.cpp).
//
// THE ROUTES
//   The routes of the raw system call primitive in a build with
//   ThreadSanitizer, for a call that can block, for a map and an unmap, and
//   for a call that opens or closes a descriptor (THE SANITIZERS of
//   foundation/os/Syscall.h).  Only src/foundation/os/Syscall.cpp calls them,
//   and only a build with ThreadSanitizer.
//
// THE PROCESS CALLS
//   fork, waitpid and _exit of the C library, in each build.  The deny rows
//   of utils/rule-table.txt refuse a raw clone, fork, exit and exit_group: the
//   C library runs the fork hooks, keeps the state of its threads, and the
//   sanitizers intercept each of the three calls.  Only the child door
//   src/foundation/os/ChildProcess.cpp calls them.

#include <cstdint>

namespace foundation::os::detail {

// How a child process ended: by an exit with a code, or by a signal.
enum class ProcessEnd : std::uint8_t {
    exited,
    signaled,
};

// The end of a child process: the exit code or the number of the signal.
// has_core_dump is true when a signal ended the child and the kernel wrote
// its core dump.
struct ProcessStatus {
    ProcessEnd end = ProcessEnd::exited;
    std::uint8_t number = 0;
    bool has_core_dump = false;
};

// The end that a status word of waitpid tells, through the macros of
// <sys/wait.h>.  wait_process passes no option that reports a stopped or a
// continued child, so each word of a wait tells an exit or a signal.
[[nodiscard]] ProcessStatus decode_process_status(std::int32_t word) noexcept;

// fork of the C library.  In the parent, the id of the child, above zero.  In
// the child, zero.  On an error, -E for the error number E.
[[nodiscard]] long fork_process() noexcept;

// waitpid of the C library for the child CHILD, with no option, so the call
// waits until the child ends.  The word is zero, and STATUS holds the end
// that decode_process_status gives, or -E for the error number E.  An eintr
// comes back to the caller.  The function has the attribute nothrow and no
// noexcept specifier, for the reason that src/foundation/os/Intercepted.cpp
// gives at read_intercepted.
[[nodiscard, gnu::nothrow]] long wait_process(std::int32_t child, ProcessStatus& status);

// _exit of the C library: the process ends with CODE, and no exit hook and
// no static destructor runs.  The attribute nothrow has the same reason as
// at wait_process.
[[noreturn, gnu::nothrow]] void exit_process(std::uint8_t code);

// Issues a read or a write through the function of the C library that
// ThreadSanitizer intercepts, with the descriptor, the address and the count
// of the raw call.  The result is the word of the kernel: a value, or -E for
// the error number E.  These are the only calls of Syscall that can block and
// that go through the C library.  The sanitizer does not intercept the
// clock_nanosleep of the C library, so that call stays raw (enter_sleep).
// src/foundation/os/Syscall.cpp refuses a blocking call with no route at
// compile time.
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

// One raw clock_nanosleep: the clock, the flags, the address of the requested
// time and the address of the time that remains.
struct SleepWords {
    std::uint64_t clock = 0;
    std::uint64_t flags = 0;
    std::uint64_t request = 0;
    std::uint64_t remain = 0;
};

// Tells ThreadSanitizer of a raw clock_nanosleep before the call.  The
// sanitizer checks the read of the requested time.  Then the thread is in a
// blocking call until leave_sleep, and a handler of a signal runs at once.
void enter_sleep(SleepWords const& words) noexcept;

// Tells ThreadSanitizer of a raw clock_nanosleep after the call, with the word
// RESULT of the kernel.  The thread is out of the blocking call, and then the
// sanitizer sees the write of the time that remains.
void leave_sleep(SleepWords const& words, long result) noexcept;

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

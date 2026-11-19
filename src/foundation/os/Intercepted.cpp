// The calls of the floor that go to the C library
// (src/foundation/os/Intercepted.h): the process calls in each build, and the
// routes of the raw system call primitive in a build with ThreadSanitizer.
// This file is the one door of <errno.h>, <unistd.h>, <sys/mman.h> and
// <sys/wait.h>.
//
// THE PROCESS CALLS.  fork, waitpid and _exit stay in the C library (decision
// D12 of misc/01_10_2026_quarantine.md).  The C library runs the fork hooks
// and keeps the state of its threads in the child, and the sanitizers
// intercept the three calls.  The deny rows of utils/rule-table.txt refuse a
// raw clone, fork, exit and exit_group.
//
// THE ROUTES.  ThreadSanitizer delivers a signal only inside a call that it
// intercepts, or inside a call that it knows to block.  A read or a write that
// blocks on a pipe goes through read(2) or write(2) of the C library, which
// the sanitizer intercepts.  The C library has no function for futex, so a
// futex call stays raw, and this file tells the sanitizer of it through its
// hooks.
//
// The sanitizer intercepts sleep, usleep and nanosleep, and not
// clock_nanosleep.  The hooks of clock_nanosleep check the read of the
// requested time and the write of the time that remains, and they mark no
// blocking call.  The hook pair of futex is the one public pair that marks a
// blocking call, and its two hooks read no word of the call.  So a sleep
// stays raw between the blocking marks of the futex hooks, and the hooks of
// clock_nanosleep go around them (the owner decision of 2026-11-14).  Each
// check of an access stays outside the mark, because the sanitizer ignores
// the hooks of an access inside a blocking call.
//
// The sanitizer keeps a shadow of each address that a thread reads or
// writes, and only its interceptors of mmap(2) and munmap(2) reset the shadow
// of a range.  So a map and an unmap go through the two functions of the C
// library (the owner decision of 2026-11-14).
//
// The sanitizer also keeps a state for each descriptor number.  Its hook of
// close checks the close against the uses of the descriptor in other threads,
// and then it clears that state, as its interceptor of close(2) does.  So a
// close stays raw between the two hooks.  The sanitizer intercepts neither
// openat nor openat2, and their hooks only check the reads of the path and of
// the struct open_how.  No hook gives the state of a new descriptor, and the
// clear of the close keeps a reused number free of an old state.
//
// Each other build compiles no route.  A unit of the C library and a unit of
// the kernel headers stay apart, so this file includes no header of the
// kernel.

#include "Intercepted.h"

#include <errno.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

namespace foundation::os::detail {

long fork_process() noexcept {
    ::pid_t const child = ::fork();
    return child < 0 ? -static_cast<long>(errno) : static_cast<long>(child);
}

ProcessStatus decode_process_status(std::int32_t word) noexcept {
    if (WIFSIGNALED(word)) {
        return ProcessStatus{ProcessEnd::signaled, static_cast<std::uint8_t>(WTERMSIG(word)), WCOREDUMP(word) != 0};
    }
    return ProcessStatus{ProcessEnd::exited, static_cast<std::uint8_t>(WEXITSTATUS(word)), false};
}

// waitpid is a cancellation point of the C library, and _exit has no
// __THROW, so <unistd.h> and <sys/wait.h> declare them without __THROW.  The
// attributes nothrow and no_sanitize_thread have the reason that
// read_intercepted below gives.
[[gnu::no_sanitize_thread]] long wait_process(std::int32_t child, ProcessStatus& status) {
    int word = 0;
    if (::waitpid(child, &word, 0) < 0) {
        return -static_cast<long>(errno);
    }
    status = decode_process_status(word);
    return 0;
}

[[gnu::no_sanitize_thread]] long wait_process_event(std::int32_t child, ProcessWait mode, ProcessEvent& event) {
    int word = 0;
    int const options = mode == ProcessWait::poll ? WNOHANG : 0;
    ::pid_t const changed = ::waitpid(child, &word, options);
    if (changed < 0) {
        return -static_cast<long>(errno);
    }
    if (changed == 0) {
        event = ProcessEvent{};
    } else if (WIFSTOPPED(word)) {
        event = ProcessEvent{ProcessChange::stopped, ProcessStatus{}, static_cast<std::uint8_t>(WSTOPSIG(word))};
    } else {
        event = ProcessEvent{ProcessChange::ended, decode_process_status(word), 0};
    }
    return 0;
}

[[gnu::no_sanitize_thread]] void exit_process(std::uint8_t code) { ::_exit(code); }

}  // namespace foundation::os::detail

#if defined(__SANITIZE_THREAD__)

// The system call hooks of ThreadSanitizer for futex(2), which libtsan
// defines.  GCC installs no <sanitizer/linux_syscall_hooks.h>, so this file
// declares them.  The pre hook takes the six words of the call as long: the
// address of the word, the operation, the value, the timeout, the second
// address and the third value.  The post hook takes the result of the kernel
// and then the same six words.  The pre hook marks the thread as in a
// blocking call, and a handler of a signal then runs at once.  The post hook
// ends the mark.
//
// The two annotations of <sanitizer/tsan_interface.h>: a release on an
// address happens before each later acquire on the same address.  This file
// declares them too, so the routes name no header outside the allowance of
// their layer.  Each declaration is noexcept, because a hook of the sanitizer
// throws no exception.  A noexcept function then has no table of the exception
// path, and the member imports no personality routine of libstdc++.
//
// The hooks of clock_nanosleep(2), close(2), openat(2) and openat2(2) take
// the words of their call as long, and the post hook takes the result first,
// in the same form.
extern "C" {
void __sanitizer_syscall_pre_impl_futex(long address, long operation, long value, long timeout, long second_address,
                                        long third_value) noexcept;
void __sanitizer_syscall_post_impl_futex(long result, long address, long operation, long value, long timeout,
                                         long second_address, long third_value) noexcept;
void __sanitizer_syscall_pre_impl_clock_nanosleep(long clock, long flags, long request, long remain) noexcept;
void __sanitizer_syscall_post_impl_clock_nanosleep(long result, long clock, long flags, long request,
                                                   long remain) noexcept;
void __sanitizer_syscall_pre_impl_close(long descriptor) noexcept;
void __sanitizer_syscall_post_impl_close(long result, long descriptor) noexcept;
void __sanitizer_syscall_pre_impl_openat(long directory, long path, long flags, long mode) noexcept;
void __sanitizer_syscall_post_impl_openat(long result, long directory, long path, long flags, long mode) noexcept;
void __sanitizer_syscall_pre_impl_openat2(long directory, long path, long how, long size) noexcept;
void __sanitizer_syscall_post_impl_openat2(long result, long directory, long path, long how, long size) noexcept;
void __tsan_acquire(void* address) noexcept;
void __tsan_release(void* address) noexcept;
}

namespace foundation::os::detail {

namespace {

// The word of the kernel for the result DONE of a function of the C library:
// the value, or -E for the error number E that the function set in errno.
[[nodiscard]] long kernel_word(::ssize_t done) noexcept {
    return done < 0 ? -static_cast<long>(errno) : static_cast<long>(done);
}

}  // namespace

// read(2) and write(2) are cancellation points of the C library.  A
// cancellation of a thread unwinds through them.  For that reason,
// <unistd.h> declares them without __THROW.  A noexcept function that calls
// them needs a table of the exception path, and that table imports the
// personality routine of libstdc++.  Rule R5 gives libfoundation.a no symbol
// of libstdc++.
//
// No code of the program cancels a thread, and no exception goes out of these
// two calls.  The attribute nothrow on read_intercepted and write_intercepted
// tells each caller this fact, and the two functions have no noexcept
// specifier.  ThreadSanitizer puts a
// cleanup for its exit hook on each call that can unwind.  The attribute
// no_sanitize_thread removes that cleanup.  The interceptors of read and write
// stay, because they replace the functions of the C library at link time.
//
// A second declaration of read or write with the attribute nothrow is not a
// solution.  It gives the function the source location of this file.  The
// quarantine plugin then sees no call of the C library, and the rule
// syscalls does not count the call.

[[gnu::no_sanitize_thread]] long read_intercepted(std::uint64_t descriptor, std::uint64_t address,
                                                  std::uint64_t count) {
    return kernel_word(::read(static_cast<int>(descriptor), __builtin_bit_cast(void*, address), count));
}

[[gnu::no_sanitize_thread]] long write_intercepted(std::uint64_t descriptor, std::uint64_t address,
                                                   std::uint64_t count) {
    return kernel_word(::write(static_cast<int>(descriptor), __builtin_bit_cast(void const*, address), count));
}

long map_intercepted(std::uint64_t address, std::uint64_t length, std::uint64_t protection, std::uint64_t flags,
                     std::uint64_t descriptor, std::uint64_t offset) noexcept {
    void* const mapped = ::mmap(__builtin_bit_cast(void*, address), length, static_cast<int>(protection),
                                static_cast<int>(flags), static_cast<int>(descriptor), static_cast<::off_t>(offset));
    // MAP_FAILED is the address of the word -1.
    long const word = __builtin_bit_cast(long, mapped);
    return word == -1 ? -static_cast<long>(errno) : word;
}

long unmap_intercepted(std::uint64_t address, std::uint64_t length) noexcept {
    return ::munmap(__builtin_bit_cast(void*, address), length) == 0 ? 0 : -static_cast<long>(errno);
}

void enter_futex(FutexCall const& call) noexcept {
    if (call.role == FutexRole::wake) {
        __tsan_release(__builtin_bit_cast(void*, call.address));
    }
    __sanitizer_syscall_pre_impl_futex(static_cast<long>(call.address), static_cast<long>(call.operation),
                                       static_cast<long>(call.value), static_cast<long>(call.timeout),
                                       static_cast<long>(call.second_address), static_cast<long>(call.third_value));
}

void leave_futex(FutexCall const& call, long result) noexcept {
    __sanitizer_syscall_post_impl_futex(result, static_cast<long>(call.address), static_cast<long>(call.operation),
                                        static_cast<long>(call.value), static_cast<long>(call.timeout),
                                        static_cast<long>(call.second_address), static_cast<long>(call.third_value));
    if (call.role == FutexRole::wait && result == 0) {
        __tsan_acquire(__builtin_bit_cast(void*, call.address));
    }
}

// The futex hooks get words of zero, because they read none.  The pre hook of
// futex runs the handler of each pending signal, and then it marks the
// blocking call.  So the sanitizer does not hold a signal that comes before
// the raw call until the end of the sleep.
void enter_sleep(SleepWords const& words) noexcept {
    __sanitizer_syscall_pre_impl_clock_nanosleep(static_cast<long>(words.clock), static_cast<long>(words.flags),
                                                 static_cast<long>(words.request), static_cast<long>(words.remain));
    __sanitizer_syscall_pre_impl_futex(0, 0, 0, 0, 0, 0);
}

void leave_sleep(SleepWords const& words, long result) noexcept {
    __sanitizer_syscall_post_impl_futex(result, 0, 0, 0, 0, 0, 0);
    __sanitizer_syscall_post_impl_clock_nanosleep(result, static_cast<long>(words.clock),
                                                  static_cast<long>(words.flags), static_cast<long>(words.request),
                                                  static_cast<long>(words.remain));
}

void enter_close(std::uint64_t descriptor) noexcept {
    __sanitizer_syscall_pre_impl_close(static_cast<long>(descriptor));
}

void leave_close(std::uint64_t descriptor, long result) noexcept {
    __sanitizer_syscall_post_impl_close(result, static_cast<long>(descriptor));
}

void enter_open(OpenWords const& words) noexcept {
    auto const directory = static_cast<long>(words.directory);
    auto const path = static_cast<long>(words.path);
    auto const third = static_cast<long>(words.third);
    auto const fourth = static_cast<long>(words.fourth);
    if (words.call == OpenCall::openat) {
        __sanitizer_syscall_pre_impl_openat(directory, path, third, fourth);
    } else {
        __sanitizer_syscall_pre_impl_openat2(directory, path, third, fourth);
    }
}

void leave_open(OpenWords const& words, long result) noexcept {
    auto const directory = static_cast<long>(words.directory);
    auto const path = static_cast<long>(words.path);
    auto const third = static_cast<long>(words.third);
    auto const fourth = static_cast<long>(words.fourth);
    if (words.call == OpenCall::openat) {
        __sanitizer_syscall_post_impl_openat(result, directory, path, third, fourth);
    } else {
        __sanitizer_syscall_post_impl_openat2(result, directory, path, third, fourth);
    }
}

}  // namespace foundation::os::detail

#endif

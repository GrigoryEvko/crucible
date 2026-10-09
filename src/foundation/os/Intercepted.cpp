// The routes of the raw system call primitive in a build with
// ThreadSanitizer (src/foundation/os/Intercepted.h).
//
// ThreadSanitizer delivers a signal only inside a call that it intercepts, or
// inside a call that it knows to block.  A read or a write that blocks on a
// pipe goes through read(2) or write(2) of the C library, which the sanitizer
// intercepts.  The C library has no function for futex, so a futex call stays
// raw, and this file tells the sanitizer of it through its hooks.  Each other
// build compiles no code from this file.  A unit of the C library and a unit
// of the kernel headers stay apart, so this file includes no header of the
// kernel.

#include "Intercepted.h"

#if defined(__SANITIZE_THREAD__)

#include <cerrno>
#include <cstddef>

#include <unistd.h>

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
// their layer.
extern "C" {
void __sanitizer_syscall_pre_impl_futex(long address, long operation, long value, long timeout, long second_address,
                                        long third_value);
void __sanitizer_syscall_post_impl_futex(long result, long address, long operation, long value, long timeout,
                                         long second_address, long third_value);
void __tsan_acquire(void* address);
void __tsan_release(void* address);
}

namespace foundation::os::detail {

long issue_intercepted(Syscall call, std::uint64_t first, std::uint64_t second, std::uint64_t third) noexcept {
    int const descriptor = static_cast<int>(first);
    void* const address = __builtin_bit_cast(void*, second);
    std::size_t const count = third;
    ::ssize_t done = -1;
    switch (call) {
        case Syscall::read:
            done = ::read(descriptor, address, count);
            break;
        case Syscall::write:
            done = ::write(descriptor, address, count);
            break;
        default:
            return -static_cast<long>(ENOSYS);
    }
    return done < 0 ? -static_cast<long>(errno) : static_cast<long>(done);
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

}  // namespace foundation::os::detail

#endif

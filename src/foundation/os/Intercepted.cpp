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

#include <errno.h>
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
// their layer.  Each declaration is noexcept, because a hook of the sanitizer
// throws no exception.  A noexcept function then has no table of the exception
// path, and the member imports no personality routine of libstdc++.
extern "C" {
void __sanitizer_syscall_pre_impl_futex(long address, long operation, long value, long timeout, long second_address,
                                        long third_value) noexcept;
void __sanitizer_syscall_post_impl_futex(long result, long address, long operation, long value, long timeout,
                                         long second_address, long third_value) noexcept;
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
// syscall-capability does not count the call.

[[gnu::no_sanitize_thread]] long read_intercepted(std::uint64_t descriptor, std::uint64_t address,
                                                  std::uint64_t count) {
    return kernel_word(::read(static_cast<int>(descriptor), __builtin_bit_cast(void*, address), count));
}

[[gnu::no_sanitize_thread]] long write_intercepted(std::uint64_t descriptor, std::uint64_t address,
                                                   std::uint64_t count) {
    return kernel_word(::write(static_cast<int>(descriptor), __builtin_bit_cast(void const*, address), count));
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

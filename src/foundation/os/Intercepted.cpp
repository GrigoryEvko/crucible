// The route of the raw system call primitive through the C library, in a
// build with ThreadSanitizer (src/foundation/os/Intercepted.h).
//
// ThreadSanitizer delivers a signal only inside a call that it intercepts, so
// a read or a write that blocks on a pipe goes through read(2) or write(2) of
// the C library there.  Each other build compiles no code from this file.  A
// unit of the C library and a unit of the kernel headers stay apart, so this
// file includes no header of the kernel.

#include "Intercepted.h"

#if defined(__SANITIZE_THREAD__)

#include <cerrno>
#include <cstddef>

#include <unistd.h>

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

}  // namespace foundation::os::detail

#endif

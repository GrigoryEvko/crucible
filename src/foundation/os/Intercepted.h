#pragma once

// The route of the raw system call primitive through the C library, for a
// call that can block in a build with ThreadSanitizer (THE SANITIZERS of
// foundation/os/Syscall.h).  Only src/foundation/os/Syscall.cpp includes this
// header, and only a build with ThreadSanitizer calls the function.

#include <foundation/os/Syscall.h>

#include <cstdint>

namespace foundation::os::detail {

// Issues CALL through the function of the C library that ThreadSanitizer
// intercepts, with the descriptor FIRST, the address SECOND and the count
// THIRD.  The result is the word of the kernel: a value, or -E for the error
// number E.  A call that the route does not know gives -ENOSYS.
[[nodiscard]] long issue_intercepted(Syscall call, std::uint64_t first, std::uint64_t second,
                                     std::uint64_t third) noexcept;

}  // namespace foundation::os::detail

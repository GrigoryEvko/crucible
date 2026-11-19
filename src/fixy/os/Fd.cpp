// The descriptor door of fixy (fixy/os/Fd.h): the open, the close and the
// query of a descriptor.  It issues the raw primitive of
// foundation/os/Syscall.h, and foundation/os/OpenFlags.h gives the words of
// the open, so this file includes no system header.

#include <fixy/os/Fd.h>

#include <foundation/core/Choice.h>
#include <foundation/os/Errno.h>
#include <foundation/os/OpenFlags.h>
#include <foundation/os/Syscall.h>

#include <cstdint>

namespace fixy::fs::detail {

::foundation::core::Result<int, ::foundation::os::Errno> open_descriptor_(char const* path, int flags,
                                                                          std::uint32_t mode) noexcept {
    namespace os = ::foundation::os;
    return os::result_of(os::raw_syscall(os::Syscall::openat, os::kAtCurrentDirectory, path, flags, mode))
        .map([](std::uint64_t descriptor) noexcept { return static_cast<int>(descriptor); });
}

void close_descriptor_(int descriptor) noexcept {
    static_cast<void>(::foundation::os::raw_syscall(::foundation::os::Syscall::close, descriptor));
}

::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
query_descriptor_(int descriptor, std::uint64_t request, void* argument) noexcept {
    namespace core = ::foundation::core;
    namespace os = ::foundation::os;
    return os::result_of(os::raw_syscall(os::Syscall::ioctl, descriptor, request, argument))
        .map([](std::uint64_t) noexcept { return core::Unit{}; });
}

}  // namespace fixy::fs::detail

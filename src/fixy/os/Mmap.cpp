// The map door of fixy (fixy/os/OwnedMmap.h and fixy/os/Mmap.h): the map,
// the unmap and the advice of a region.  It issues the raw primitive of
// foundation/os/Syscall.h, and foundation/os/MapWords.h gives the words of
// a map, so this file includes no system header.  The size of sealed memory
// comes from the file door (fixy/os/Fs.h).

#include <fixy/os/OwnedMmap.h>

#include <fixy/os/Fs.h>
#include <foundation/core/Choice.h>
#include <foundation/os/Errno.h>
#include <foundation/os/Syscall.h>

#include <cstddef>
#include <cstdint>

namespace fixy::mmap::detail {

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;

}  // namespace

core::Result<void*, os::Errno> map_region_(std::size_t length, int protection, int flags, int descriptor,
                                           std::int64_t offset) noexcept {
    CRUCIBLE_TRY(address, os::result_of(os::raw_syscall(os::Syscall::mmap, os::Word{}, length, protection, flags,
                                                        descriptor, offset)));
    return __builtin_bit_cast(void*, address);
}

core::Result<void*, os::Errno> map_sealed_region_(std::size_t length, int protection, int flags, int descriptor,
                                                  std::int64_t offset) noexcept {
    CRUCIBLE_TRY(size, ::fixy::fs::detail::file_size_(descriptor));
    // The compares take no sum, so no length overflows.
    bool const is_inside = offset >= 0 && offset <= size && length <= static_cast<std::uint64_t>(size - offset);
    if (!is_inside) {
        return core::err(os::Errno::enxio);
    }
    return map_region_(length, protection, flags, descriptor, offset);
}

void unmap_region_(void* address, std::size_t length) noexcept {
    static_cast<void>(os::raw_syscall(os::Syscall::munmap, address, length));
}

core::Result<core::Unit, os::Errno> advise_region_(void* address, std::size_t length, int advice) noexcept {
    if (address == nullptr) {
        return core::err(os::Errno::einval);
    }
    return os::result_of(os::raw_syscall(os::Syscall::madvise, address, length, advice))
        .map([](std::uint64_t) noexcept { return core::Unit{}; });
}

}  // namespace fixy::mmap::detail

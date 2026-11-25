// The map door of fixy (fixy/os/OwnedMmap.h and fixy/os/Mmap.h): the map,
// the unmap and the advice of a region.  It issues the raw primitive of
// foundation/os/Syscall.h, and foundation/os/MapWords.h gives the words of
// a map, so this file includes no system header.  The size of sealed memory
// comes from the file door (fixy/os/Fs.h).

#include <fixy/os/OwnedMmap.h>

#include <fixy/os/Fs.h>
#include <foundation/core/Choice.h>
#include <foundation/os/Auxiliary.h>
#include <foundation/os/Errno.h>
#include <foundation/os/MapWords.h>
#include <foundation/os/Syscall.h>

#include <cstddef>
#include <cstdint>

namespace fixy::mmap::detail {

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;

// The verdict of the test of a foreign object.
using Tested = core::Result<core::Unit, ForeignTestFault>;

// An advice that no kernel has.  A kernel gives einval for it.
constexpr int kNoAdvice = -1;

// The advice of the test, with the fault of a refusal.
[[nodiscard]] Tested advise(void* address, std::size_t length, int advice, ForeignMapFault fault) noexcept {
    return advise_region_(address, length, advice).map_err([fault](os::Errno error) noexcept {
        return ForeignTestFault{fault, error};
    });
}

// Asks the kernel to load the page at address again.  A page that does not
// come back gives the fault lost.
[[nodiscard]] Tested load_page(void* address, ForeignMapFault lost) noexcept {
    return advise_region_(address, 1, os::kAdvisePopulateRead).map_err([lost](os::Errno error) noexcept {
        return ForeignTestFault{error == os::Errno::efault ? lost : ForeignMapFault::no_load, error};
    });
}

// The test of the map at address, which no other code reads (the test of a
// foreign object of fixy/os/LockedMmap.h).  An advice for zero bytes judges
// only its word.
[[nodiscard]] Tested test_mapped_object(void* address, std::size_t length) noexcept {
    if (advise_region_(address, 0, kNoAdvice).is_ok()) {
        return core::err(ForeignTestFault{ForeignMapFault::no_populate_read, os::Errno::eopnotsupp});
    }
    Tested const known = advise(address, 0, os::kAdvisePopulateRead, ForeignMapFault::no_populate_read);
    if (known.is_err()) {
        return known;
    }
    std::uint64_t const page_bytes = os::read_auxiliary(os::AuxiliaryEntry::page_size).value_or(0);
    if (page_bytes == 0) {
        return core::err(ForeignTestFault{ForeignMapFault::no_load, os::Errno::einval});
    }
    Tested const discarded = advise(address, length, os::kAdviseDontNeed, ForeignMapFault::no_discard);
    if (discarded.is_err()) {
        return discarded;
    }
    Tested const first = load_page(address, ForeignMapFault::first_page_lost);
    // A map of no byte does not exist, so the length is above zero.
    std::size_t const last_start = (length - 1) - ((length - 1) % static_cast<std::size_t>(page_bytes));
    if (first.is_err() || last_start == 0) {
        return first;
    }
    return load_page(static_cast<std::byte*>(address) + last_start, ForeignMapFault::last_page_lost);
}

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

core::Result<core::Unit, ForeignTestFault> test_foreign_object_(std::size_t length, int protection, int flags,
                                                                int descriptor, std::int64_t offset) noexcept {
    CRUCIBLE_TRY(address,
                 map_region_(length, protection, flags, descriptor, offset).map_err([](os::Errno error) noexcept {
                     return ForeignTestFault{ForeignMapFault::test_map_refused, error};
                 }));
    Tested const tested = test_mapped_object(address, length);
    unmap_region_(address, length);
    return tested;
}

}  // namespace fixy::mmap::detail

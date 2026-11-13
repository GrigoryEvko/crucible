// The guarded memory door of the base (foundation/os/GuardedRegion.h).  It
// issues the raw primitive, and foundation/os/MapWords.h gives the words of
// the map.

#include <foundation/os/GuardedRegion.h>

#include <foundation/core/Choice.h>
#include <foundation/core/Report.h>
#include <foundation/os/MapWords.h>
#include <foundation/os/Syscall.h>

#include <cstddef>
#include <cstdint>

namespace foundation::os {

namespace detail {

struct GuardedRegionDoor {
    [[nodiscard]] static GuardedRegion build(char* first, std::size_t total_bytes) noexcept {
        return GuardedRegion{first, total_bytes};
    }
};

}  // namespace detail

namespace {

// The largest usable count that the door takes: the rounding and the guard
// then stay inside a std::size_t.
constexpr std::size_t kLargestUsable = ~std::size_t{0} - 2 * kGuardBytes;

// The file descriptor of a map with no file.
constexpr int kNoFile = -1;

}  // namespace

core::Result<GuardedRegion, Errno> map_guarded_region(std::size_t usable_bytes) noexcept {
    if (usable_bytes > kLargestUsable) return core::err(Errno::enomem);
    std::size_t const rounded =
        usable_bytes == 0 ? kGuardBytes : (usable_bytes + kGuardBytes - 1) / kGuardBytes * kGuardBytes;
    std::size_t const total = kGuardBytes + rounded;
    CRUCIBLE_TRY(address,
                 result_of(raw_syscall(Syscall::mmap, Word{}, total, kProtectNone, kMapPrivateAnonymous, kNoFile, 0)));
    char* const first = __builtin_bit_cast(char*, address);
    core::Result<std::uint64_t, Errno> const opened =
        result_of(raw_syscall(Syscall::mprotect, first + kGuardBytes, rounded, kProtectReadWrite));
    if (opened.is_err()) {
        static_cast<void>(raw_syscall(Syscall::munmap, first, total));
        return core::err(opened.err().value_or(Errno::enomem));
    }
    return detail::GuardedRegionDoor::build(first, total);
}

GuardedRegion::~GuardedRegion() {
    if (first_ == nullptr) return;
    core::Result<std::uint64_t, Errno> const unmapped = result_of(raw_syscall(Syscall::munmap, first_, total_bytes_));
    if (unmapped.is_err()) {
        core::fatal("GuardedRegion: the unmap of a map that the door made failed with the error {}.  The kernel "
                    "refuses an unmap only for an address or a count that no map of the door has",
                    static_cast<std::uint16_t>(unmapped.err().value_or(Errno::einval)));
    }
}

}  // namespace foundation::os

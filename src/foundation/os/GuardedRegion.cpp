// The guarded memory door of the base (foundation/os/GuardedRegion.h).  It
// issues the raw primitive, and foundation/os/MapWords.h gives the words of
// the map.

#include <foundation/os/GuardedRegion.h>

#include <foundation/Platform.h>
#include <foundation/core/Bits.h>
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

// The place of a map of the door.
enum class MapPlacement : std::uint8_t {
    // The kernel selects the range.
    anywhere,
    // The range starts at the address of the call, and the kernel replaces no
    // map (MAP_FIXED_NOREPLACE).
    fixed_no_replace,
};

// The map flags of a map at PLACEMENT.
[[nodiscard]] constexpr int map_flags_of(MapPlacement placement) noexcept {
    return placement == MapPlacement::fixed_no_replace ? kMapPrivateAnonymous | kMapFixedNoReplace
                                                       : kMapPrivateAnonymous;
}

// The usable bytes of a region of USABLE_BYTES: a multiple of kGuardBytes,
// and kGuardBytes for zero.  The caller refuses a count above kLargestUsable
// first.
[[nodiscard]] core::ByteCount round_usable(core::ByteCount usable_bytes) noexcept {
    return core::ByteCount{usable_bytes.raw() == 0
                               ? kGuardBytes
                               : core::align_up(usable_bytes.raw(), kGuardBytes)
                                     .expect("a usable size at most kLargestUsable fits in size_t after its rounding")};
}

// Maps the guard and the ROUNDED usable bytes with no access, at the address
// WANTED for the place PLACEMENT.  Then it gives read and write access to the
// usable bytes, and gives the region of the map.  A refused access unmaps the
// whole map and gives the error of the kernel.  Each raw call of a map of the
// door is here.
[[nodiscard]] core::Result<GuardedRegion, Errno> map_region(Word wanted, MapPlacement placement,
                                                            core::ByteCount rounded) noexcept {
    std::size_t const total = kGuardBytes + rounded.raw();
    CRUCIBLE_TRY(address, result_of(raw_syscall(Syscall::mmap, wanted, total, kProtectNone, map_flags_of(placement),
                                                kNoFile, 0)));
    char* const first = __builtin_bit_cast(char*, address);
    core::Result<std::uint64_t, Errno> const opened =
        result_of(raw_syscall(Syscall::mprotect, first + kGuardBytes, rounded.raw(), kProtectReadWrite));
    if (opened.is_err()) {
        static_cast<void>(raw_syscall(Syscall::munmap, first, total));
        return core::err(opened.err().value_or(Errno::enomem));
    }
    return detail::GuardedRegionDoor::build(first, total);
}

}  // namespace

core::Result<GuardedRegion, Errno> map_guarded_region(core::ByteCount usable_bytes) noexcept {
    if (usable_bytes.raw() > kLargestUsable) return core::err(Errno::enomem);
    return map_region(Word{}, MapPlacement::anywhere, round_usable(usable_bytes));
}

core::Result<GuardedRegion, Errno> map_guarded_region_at(RegionAddress first_usable,
                                                         core::ByteCount usable_bytes) noexcept {
    if (usable_bytes.raw() > kLargestUsable) return core::err(Errno::enomem);
    std::uint64_t const usable_at = static_cast<std::uint64_t>(first_usable);
    if (usable_at < kGuardBytes) return core::err(Errno::einval);
    char* const wanted = __builtin_bit_cast(char*, usable_at - kGuardBytes);
    CRUCIBLE_TRY(region, map_region(wanted, MapPlacement::fixed_no_replace, round_usable(usable_bytes)));
    // The row map_fixed_no_replace is of the need each_call, so each kernel
    // that the start-up check permits maps at the address or refuses.
    CRUCIBLE_FATAL_INVARIANT(region.address() == first_usable);
    return region;
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

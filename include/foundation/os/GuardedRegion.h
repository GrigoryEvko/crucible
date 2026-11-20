#pragma once

// The guarded memory door of the base: a run of read and write pages with a
// guard of no access below it.  Code that runs down past the first usable
// byte, as a stack does, then faults in the guard and does not write into
// other memory.
//
// map_guarded_region maps the guard and the usable pages in one private map
// with no file.  The whole map starts with no access, and a second call gives
// read and write access to the usable pages.  The destructor unmaps the whole
// map.  The region owns its map: it moves and it does not copy, so no two
// owners unmap one map.  keep_until_exit() gives up the map for a region that
// the process uses until it ends.
//
// THE FIXED MAP
//   address() gives the address of the first usable byte of a region as a
//   RegionAddress.  map_guarded_region_at maps a region whose first usable
//   byte is at that address, with MAP_FIXED_NOREPLACE.  The kernel replaces
//   no map: a map that holds a part of the range gives the error eexist, and
//   the kernel then maps nothing.  So a caller can map the range of a region
//   that ended, and a different map of the process cannot lose its pages.
//   The row map_fixed_no_replace of foundation/os/KernelFloor.h is of the
//   need each_call.  So each kernel that the start-up check permits maps at
//   the address or refuses, and a map at a different address ends the
//   process.
//
// The guard has kGuardBytes, a multiple of each page size of x86_64 and of
// aarch64 (4 KiB, 16 KiB and 64 KiB).  So the first usable byte is the first
// byte of a page, and the door reads no page size.  The door rounds the
// usable part up to a multiple of kGuardBytes too.
//
// The door issues the raw primitive of foundation/os/Syscall.h.  In a build
// with ThreadSanitizer the primitive gives the map and the unmap to the C
// library, so that sanitizer resets its shadow of the range (THE SANITIZERS
// of foundation/os/Syscall.h).  AddressSanitizer does not see the map, so its
// shadow memory can hold the state of an earlier map at the same addresses.
// Use the door only for memory that instrumented code does not read or write
// in a run that continues, such as an alternate signal stack: only the
// handler of a fatal signal writes to that stack, and the process ends after
// the handler.
//
// This header is in the layer F0_doors of utils/rule-table.txt.  The door file
// is src/foundation/os/GuardedRegion.cpp, which issues raw_syscall.

#include <foundation/core/Choice.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>

namespace foundation::os {

// The bytes of the guard below the usable pages of each region.
inline constexpr std::size_t kGuardBytes = std::size_t{64} * 1024;

// The address of the first usable byte of a region, as a number (THE FIXED
// MAP).  It names no object: a region that ended leaves its address, and a
// later map can take the address again.
enum class RegionAddress : std::uint64_t {
};

namespace detail {

// The one builder of a region over a map.  src/foundation/os/GuardedRegion.cpp
// defines it, so only the door makes a region.
struct GuardedRegionDoor;

}  // namespace detail

class GuardedRegion final {
public:
    GuardedRegion(GuardedRegion&& other) noexcept : first_{other.first_}, total_bytes_{other.total_bytes_} {
        other.first_ = nullptr;
        other.total_bytes_ = 0;
    }

    GuardedRegion(GuardedRegion const&) = delete("two owners of one map would unmap it two times");
    GuardedRegion& operator=(GuardedRegion const&) = delete("two owners of one map would unmap it two times");
    GuardedRegion& operator=(GuardedRegion&&) = delete("a region owns one map for its life and takes no other map");

    // Unmaps the guard and the usable pages.
    ~GuardedRegion();

    // The first usable byte, above the guard.  A region that gave up its map
    // gives no byte.
    [[nodiscard]] char* data_mut() noexcept { return first_ == nullptr ? nullptr : first_ + kGuardBytes; }

    // The address of the first usable byte (THE FIXED MAP).  A region that
    // gave up its map gives the zero address.
    [[nodiscard]] RegionAddress address() const noexcept {
        return first_ == nullptr ? RegionAddress{}
                                 : RegionAddress{__builtin_bit_cast(std::uint64_t, first_) + kGuardBytes};
    }

    // The count of the usable bytes, a multiple of kGuardBytes.
    [[nodiscard]] std::size_t usable_bytes() const noexcept {
        return total_bytes_ == 0 ? 0 : total_bytes_ - kGuardBytes;
    }

    // Gives up the map, and the map stays until the process ends.  Use it
    // only for memory that the process uses until its end, such as the
    // alternate signal stack of the thread that installs the signal handlers.
    void keep_until_exit() && noexcept {
        first_ = nullptr;
        total_bytes_ = 0;
    }

private:
    friend struct detail::GuardedRegionDoor;

    GuardedRegion(char* first, std::size_t total_bytes) noexcept : first_{first}, total_bytes_{total_bytes} {}

    char* first_ = nullptr;
    std::size_t total_bytes_ = 0;
};

// Maps USABLE_BYTES, rounded up to a multiple of kGuardBytes, above a guard
// of kGuardBytes.  A count that the rounding cannot hold gives Errno::enomem,
// as a map that the kernel refuses does.
[[nodiscard]] core::Result<GuardedRegion, Errno> map_guarded_region(std::size_t usable_bytes) noexcept;

// Maps a region of USABLE_BYTES, rounded as map_guarded_region rounds them,
// whose first usable byte is at FIRST_USABLE (THE FIXED MAP).  A map that
// holds a part of the range gives eexist, and the kernel maps nothing.  An
// address that is not the first byte of a page gives the einval of the
// kernel.  An address below kGuardBytes, which leaves no room for the guard,
// gives einval with no call.  A count that the rounding cannot hold gives
// enomem.
[[nodiscard]] core::Result<GuardedRegion, Errno> map_guarded_region_at(RegionAddress first_usable,
                                                                       std::size_t usable_bytes) noexcept;

}  // namespace foundation::os

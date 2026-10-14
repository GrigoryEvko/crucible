// The affinity door of fixy/os/CpuPinned.h: the set and the read of the
// affinity mask of the calling thread, through the raw system call primitive
// of foundation/os/Syscall.h, and the record of the pin event of each set.
//
// The kernel reads and writes an affinity mask as an array of words of 64
// bits, with the bit of CPU N in the word N / 64 at the place N % 64, on
// x86_64 and aarch64.  An AffinityMask has that layout, so the door gives the
// kernel the address and the size of the mask.  This file includes no header
// of the kernel and no header of the C library.

#include <fixy/os/CpuPinned.h>

#include <foundation/os/Errno.h>
#include <foundation/os/Syscall.h>

#include <cstdint>
#include <type_traits>

namespace fixy::detail {

namespace {

// THE MASK: the kernel reads or writes sizeof(AffinityMask) bytes at the
// address of the mask.  The mask has one member, its array of words, and a
// class of the standard layout puts its first member at its own address.
static_assert(std::is_standard_layout_v<AffinityMask> && std::is_trivially_copyable_v<AffinityMask>,
              "the kernel writes the bytes of an affinity mask, so the mask must have the standard layout");
static_assert(sizeof(AffinityMask) == AffinityMask::kWords * sizeof(std::uint64_t),
              "an affinity mask must hold its words of 64 bits and nothing more");

// The thread id that names the calling thread in an affinity call.
constexpr int kCallingThread = 0;

}  // namespace

// The kernel changes the mask, and then the door takes the next pin event of
// the process and puts it in force on the calling thread.  A refused call
// takes no event.
::foundation::core::Result<std::uint64_t, ::foundation::os::Errno>
sched_setaffinity_sys(AffinityMask const& mask) noexcept {
    long const answer = ::foundation::os::raw_syscall(::foundation::os::Syscall::sched_setaffinity, kCallingThread,
                                                      sizeof(AffinityMask), static_cast<void const*>(&mask));
    return ::foundation::os::result_of(answer).match(
        [](std::uint64_t) noexcept -> ::foundation::core::Result<std::uint64_t, ::foundation::os::Errno> {
            std::uint64_t const event = pin_event_count.fetch_add_acq_rel(std::uint64_t{1}) + 1;
            tls_pin_event = event;
            return event;
        },
        [](::foundation::os::Errno error) noexcept
            -> ::foundation::core::Result<std::uint64_t, ::foundation::os::Errno> {
            return ::foundation::core::err(error);
        });
}

::foundation::core::Result<AffinityMask, ::foundation::os::Errno> sched_getaffinity_sys() noexcept {
    AffinityMask mask{};
    long const answer = ::foundation::os::raw_syscall(::foundation::os::Syscall::sched_getaffinity, kCallingThread,
                                                      sizeof(AffinityMask), static_cast<void const*>(&mask));
    return ::foundation::os::result_of(answer).match(
        [&mask](std::uint64_t) noexcept -> ::foundation::core::Result<AffinityMask, ::foundation::os::Errno> {
            return mask;
        },
        [](::foundation::os::Errno error) noexcept
            -> ::foundation::core::Result<AffinityMask, ::foundation::os::Errno> {
            return ::foundation::core::err(error);
        });
}

}  // namespace fixy::detail

// The descriptor door of fixy (fixy/os/Fd.h): the open, the close and the
// query of a descriptor.  It issues the raw primitive of
// foundation/os/Syscall.h, and foundation/os/OpenFlags.h gives the words of
// the open, so this file includes no system header.

#include <fixy/os/Fd.h>

#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Index.h>
#include <foundation/os/Errno.h>
#include <foundation/os/OpenFlags.h>
#include <foundation/os/Syscall.h>

#include <cstddef>
#include <cstdint>

namespace fixy::fs::detail {

// The generation of each number (THE GENERATION OF A DESCRIPTOR of
// fixy/os/Fd.h).  The kernel gives one table of descriptors to the process,
// so one table of generations serves the process too, and a Handle of one
// shared library makes the refs of another library stale.  The table starts
// as zero pages, and a process touches only the pages of the numbers that it
// opens: one page of 4 KiB for each 1024 numbers.  The Handle that owns a
// number is the one writer of its slot, from its open to its close, because
// the kernel gives the number to no other descriptor in that time.  A door of
// another thread reads the slot with acquire order.  A count of 32 bits
// matches an old generation again only after 2^31 opens and closes of one
// number while a ref of it stays.
CRUCIBLE_PROCESS_WIDE inline constinit ::foundation::core::FixedArray<::foundation::core::Atomic<std::uint32_t>,
                                                                      descriptor_generation_slots>
    descriptor_generations_{};

namespace {

using GenerationIndex = ::foundation::core::Index<descriptor_generation_slots>;

// The index of the slot of the number of descriptor, or no value for a
// negative number and a number past the slots.
[[nodiscard]] ::foundation::core::Option<GenerationIndex> slot_of(int descriptor) noexcept {
    if (descriptor < 0) {
        return ::foundation::core::none;
    }
    return GenerationIndex::check(static_cast<std::size_t>(descriptor));
}

}  // namespace

std::uint32_t descriptor_generation_(int descriptor) noexcept {
    return slot_of(descriptor)
        .map([](GenerationIndex slot) noexcept { return descriptor_generations_.at(slot).load_acquire(); })
        .value_or(0);
}

bool is_descriptor_generation_(int descriptor, std::uint32_t generation) noexcept {
    return slot_of(descriptor).is_some_and([generation](GenerationIndex const& slot) noexcept {
        return descriptor_generations_.at(slot).load_acquire() == generation;
    });
}

void advance_descriptor_generation_(int descriptor) noexcept {
    for (GenerationIndex const slot : slot_of(descriptor)) {
        static_cast<void>(descriptor_generations_.at(slot).fetch_add_acq_rel(1));
    }
}

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

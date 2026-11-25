// The descriptor door of fixy (fixy/os/Fd.h): the open, the close and the
// query of a descriptor, the read of the type of an opened file, and the
// generations of the numbers.  It issues the raw primitive of
// foundation/os/Syscall.h, and foundation/os/OpenFlags.h gives the words of the
// open.  It is the one includer of <linux/stat.h>, for the struct of statx.

#include <fixy/os/Fd.h>

#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Index.h>
#include <foundation/os/Errno.h>
#include <foundation/os/OpenFlags.h>
#include <foundation/os/Syscall.h>

#include <linux/stat.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace fixy::fs::detail {

// The generation of each number (the generation of a descriptor of
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

// The type bits of the mode of the file of descriptor, from a status read of
// the type alone (the opened descriptor of fixy/os/Fd.h).
[[nodiscard]] ::foundation::core::Result<std::uint32_t, ::foundation::os::Errno>
read_type_bits(int descriptor) noexcept {
    namespace os = ::foundation::os;
    ::statx status{};
    return os::result_of_restarted([descriptor, &status] {
               return os::raw_syscall(os::Syscall::statx, descriptor, "", os::kAtEmptyPath | os::kAtStatusNoSync,
                                      STATX_TYPE, static_cast<void const*>(&status));
           })
        .map([&status](std::uint64_t) noexcept { return std::uint32_t{status.stx_mode} & os::kFileTypeMask; });
}

// The number of opened when its file has the type.  A file of another type is
// closed, and so is a file whose type read failed.
[[nodiscard]] ::foundation::core::Result<int, ::foundation::os::Errno>
admit_type(::foundation::core::Result<int, ::foundation::os::Errno> opened, ::foundation::os::FileType type) noexcept {
    namespace core = ::foundation::core;
    namespace os = ::foundation::os;
    using Admitted = core::Result<int, os::Errno>;
    // The error number zero is no error, so it stands for a file that passes.
    constexpr os::Errno kPasses{};
    return admit_opened_number_(std::move(opened)).and_then([type](int descriptor) noexcept -> Admitted {
        os::Errno const refusal = read_type_bits(descriptor)
                                      .match(
                                          [type](std::uint32_t bits) noexcept {
                                              return bits == std::to_underlying(type) ? kPasses : os::Errno::eftype;
                                          },
                                          [](os::Errno error) noexcept { return error; });
        if (refusal != kPasses) {
            close_descriptor_(descriptor);
            return core::err(refusal);
        }
        return descriptor;
    });
}

}  // namespace

bool is_descriptor_generation_(int descriptor, std::uint32_t generation) noexcept {
    return slot_of(descriptor).is_some_and([generation](GenerationIndex const& slot) noexcept {
        return descriptor_generations_.at(slot).load_acquire() == generation;
    });
}

::foundation::core::Result<int, ::foundation::os::Errno>
admit_opened_number_(::foundation::core::Result<int, ::foundation::os::Errno> opened) noexcept {
    return std::move(opened).and_then([](int descriptor) noexcept {
        return OpenedDescriptorDoor::admit_below<descriptor_generation_slots>(descriptor);
    });
}

::foundation::core::Result<int, ::foundation::os::Errno>
admit_regular_file_(::foundation::core::Result<int, ::foundation::os::Errno> opened) noexcept {
    return admit_type(std::move(opened), ::foundation::os::FileType::regular);
}

::foundation::core::Result<int, ::foundation::os::Errno>
admit_character_device_(::foundation::core::Result<int, ::foundation::os::Errno> opened) noexcept {
    return admit_type(std::move(opened), ::foundation::os::FileType::character_device);
}

int current_descriptor_(int descriptor, std::uint32_t generation) noexcept {
    return is_descriptor_generation_(descriptor, generation) ? descriptor : -1;
}

void advance_descriptor_generation_(int descriptor) noexcept {
    // Each generation is an Atomic, so two threads step the generations of
    // their descriptors through the element door at the same time.
    for (GenerationIndex const slot : slot_of(descriptor)) {
        descriptor_generations_.with_element(slot, [](::foundation::core::Atomic<std::uint32_t>& generation) noexcept {
            static_cast<void>(generation.fetch_add_acq_rel(1));
        });
    }
}

::foundation::core::Result<int, ::foundation::os::Errno> open_descriptor_(char const* path, int flags,
                                                                          std::uint32_t mode) noexcept {
    namespace os = ::foundation::os;
    return os::result_of_restarted([path, flags, mode] {
               return os::raw_syscall(os::Syscall::openat, os::kAtCurrentDirectory, path, flags, mode);
           })
        .map([](std::uint64_t descriptor) noexcept { return static_cast<int>(descriptor); });
}

void close_descriptor_(int descriptor) noexcept {
    static_cast<void>(::foundation::os::raw_syscall(::foundation::os::Syscall::close, descriptor));
}

void close_owned_descriptor_(int descriptor) noexcept {
    advance_descriptor_generation_(descriptor);
    close_descriptor_(descriptor);
}

::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
query_descriptor_(int descriptor, std::uint64_t request, void* argument) noexcept {
    namespace core = ::foundation::core;
    namespace os = ::foundation::os;
    return os::result_of(os::raw_syscall(os::Syscall::ioctl, descriptor, request, argument))
        .map([](std::uint64_t) noexcept { return core::Unit{}; });
}

}  // namespace fixy::fs::detail

namespace fixy::fs {

// A number past the slots gives the generation zero, and no stamp of it is
// current (the generation of a descriptor).
DescriptorStamp DescriptorStamp::read_now(int descriptor) noexcept {
    if (descriptor < 0) {
        return DescriptorStamp{};
    }
    std::uint32_t const generation = detail::slot_of(descriptor)
                                         .map([](detail::GenerationIndex slot) noexcept {
                                             return detail::descriptor_generations_.at(slot).load_acquire();
                                         })
                                         .value_or(0);
    return DescriptorStamp{descriptor, generation};
}

}  // namespace fixy::fs

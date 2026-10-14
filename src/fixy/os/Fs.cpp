// The file door of fixy (fixy/os/Fs.h): the syncs, the renames, the reads
// and the writes of a whole buffer, and the size of a file.  It issues the
// raw primitive of foundation/os/Syscall.h.  It is the one includer of
// <asm/stat.h>, for the struct of fstat, and of <linux/fs.h>, for the flag of
// renameat2.  It includes no header of the C library, because some
// declarations of the two kinds of header conflict.  In a build with
// ThreadSanitizer, the primitive gives each read and each write to the
// function of the C library (THE SANITIZERS of foundation/os/Syscall.h).

#include <fixy/os/Fs.h>

#include <foundation/core/Choice.h>
#include <foundation/os/Errno.h>
#include <foundation/os/OpenFlags.h>
#include <foundation/os/Syscall.h>

#include <asm/stat.h>
#include <linux/fs.h>

#include <cstddef>
#include <cstdint>

namespace fixy::fs::detail {

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;

using UnitResult = core::Result<core::Unit, os::Errno>;

// The Unit of a word of the kernel that gives no value, or its error.
[[nodiscard]] UnitResult unit_of(long word) noexcept {
    return os::result_of(word).match([](std::uint64_t) -> UnitResult { return core::Unit{}; },
                                     [](os::Errno error) -> UnitResult { return core::err(error); });
}

// The descriptor of a closed handle, which gets EBADF with no system call.
[[nodiscard]] constexpr bool is_closed(int descriptor) noexcept { return descriptor < 0; }

}  // namespace

UnitResult sync_data_(int descriptor) noexcept {
    if (is_closed(descriptor)) {
        return core::err(os::Errno::ebadf);
    }
    return unit_of(os::raw_syscall(os::Syscall::fdatasync, descriptor));
}

UnitResult sync_all_(int descriptor) noexcept {
    if (is_closed(descriptor)) {
        return core::err(os::Errno::ebadf);
    }
    return unit_of(os::raw_syscall(os::Syscall::fsync, descriptor));
}

UnitResult rename_replacing_(char const* from, char const* to) noexcept {
    return unit_of(os::raw_syscall(os::Syscall::renameat, os::kAtCurrentDirectory, from, os::kAtCurrentDirectory, to));
}

UnitResult rename_no_replace_(char const* from, char const* to) noexcept {
    return unit_of(os::raw_syscall(os::Syscall::renameat2, os::kAtCurrentDirectory, from, os::kAtCurrentDirectory, to,
                                   RENAME_NOREPLACE));
}

// Complexity: one read for each part of the buffer that the kernel fills,
// and one more read at the end of the file.
core::Result<std::size_t, os::Errno> read_full_(int descriptor, std::byte* first, std::size_t count) noexcept {
    if (is_closed(descriptor)) {
        return core::err(os::Errno::ebadf);
    }
    std::size_t total = 0;
    bool is_at_end = false;
    bool has_failed = false;
    os::Errno failure = os::Errno::eio;
    while (!is_at_end && !has_failed && total < count) {
        os::result_of(os::raw_syscall(os::Syscall::read, descriptor, first + total, count - total))
            .match(
                [&](std::uint64_t taken) {
                    is_at_end = taken == 0;
                    total += static_cast<std::size_t>(taken);
                },
                [&](os::Errno error) {
                    if (error != os::Errno::eintr) {
                        failure = error;
                        has_failed = true;
                    }
                });
    }
    if (has_failed) {
        return core::err(failure);
    }
    return total;
}

// Complexity: one write for each part of the buffer that the kernel takes.
UnitResult write_full_(int descriptor, std::byte const* first, std::size_t count) noexcept {
    if (is_closed(descriptor)) {
        return core::err(os::Errno::ebadf);
    }
    std::size_t total = 0;
    bool has_failed = false;
    os::Errno failure = os::Errno::eio;
    while (!has_failed && total < count) {
        os::result_of(os::raw_syscall(os::Syscall::write, descriptor, first + total, count - total))
            .match([&](std::uint64_t taken) { total += static_cast<std::size_t>(taken); },
                   [&](os::Errno error) {
                       if (error != os::Errno::eintr) {
                           failure = error;
                           has_failed = true;
                       }
                   });
    }
    if (has_failed) {
        return core::err(failure);
    }
    return core::Unit{};
}

core::Result<std::int64_t, os::Errno> file_size_(int descriptor) noexcept {
    if (is_closed(descriptor)) {
        return core::err(os::Errno::ebadf);
    }
    ::stat status{};
    return os::result_of(os::raw_syscall(os::Syscall::fstat, descriptor, static_cast<void const*>(&status)))
        .match(
            [&status](std::uint64_t) -> core::Result<std::int64_t, os::Errno> {
                return static_cast<std::int64_t>(status.st_size);
            },
            [](os::Errno error) -> core::Result<std::int64_t, os::Errno> { return core::err(error); });
}

}  // namespace fixy::fs::detail

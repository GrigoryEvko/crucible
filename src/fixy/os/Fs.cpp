// The file door of fixy (fixy/os/Fs.h): the syncs, the renames, the reads
// and the writes of a whole buffer, the size of a file, and the opens, the
// removes, the new directories and the status reads under a directory.  A
// call that a signal interrupts runs again.  It issues the
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
#include <utility>

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

// The word of a call that a signal interrupted.  Each call of this file that
// gets it runs again.
constexpr long kInterrupted = -static_cast<long>(std::to_underlying(os::Errno::eintr));

// The bytes of one read of read_all_into_.
constexpr std::size_t kChunkBytes = 4096;

}  // namespace

UnitResult sync_data_(int descriptor) noexcept {
    if (is_closed(descriptor)) {
        return core::err(os::Errno::ebadf);
    }
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::fdatasync, descriptor);
    } while (word == kInterrupted);
    return unit_of(word);
}

UnitResult sync_all_(int descriptor) noexcept {
    if (is_closed(descriptor)) {
        return core::err(os::Errno::ebadf);
    }
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::fsync, descriptor);
    } while (word == kInterrupted);
    return unit_of(word);
}

UnitResult rename_replacing_at_(int directory, char const* from, char const* to) noexcept {
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::renameat, directory, from, directory, to);
    } while (word == kInterrupted);
    return unit_of(word);
}

UnitResult rename_no_replace_at_(int directory, char const* from, char const* to) noexcept {
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::renameat2, directory, from, directory, to, RENAME_NOREPLACE);
    } while (word == kInterrupted);
    return unit_of(word);
}

core::Result<int, os::Errno> open_at_(int directory, char const* path, int flags, std::uint32_t mode) noexcept {
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::openat, directory, path, flags, mode);
    } while (word == kInterrupted);
    return os::result_of(word).match(
        [](std::uint64_t descriptor) -> core::Result<int, os::Errno> { return static_cast<int>(descriptor); },
        [](os::Errno error) -> core::Result<int, os::Errno> { return core::err(error); });
}

UnitResult unlink_at_(int directory, char const* path) noexcept {
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::unlinkat, directory, path, 0);
    } while (word == kInterrupted);
    return unit_of(word);
}

UnitResult make_directory_at_(int directory, char const* path, std::uint32_t mode) noexcept {
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::mkdirat, directory, path, mode);
    } while (word == kInterrupted);
    return unit_of(word);
}

core::Result<bool, os::Errno> has_entry_at_(int directory, char const* path) noexcept {
    ::stat status{};
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::newfstatat, directory, path, static_cast<void const*>(&status),
                               os::kAtSymlinkNoFollow);
    } while (word == kInterrupted);
    return os::result_of(word).match([](std::uint64_t) -> core::Result<bool, os::Errno> { return true; },
                                     [](os::Errno error) -> core::Result<bool, os::Errno> {
                                         if (error == os::Errno::enoent) return false;
                                         return core::err(error);
                                     });
}

// Complexity: one read for each chunk of the file, and one more read at its
// end, and one copy of each byte into the text.
core::Result<std::size_t, os::Errno> read_all_into_(int descriptor, core::Alloc allocation,
                                                    core::GrowText& text) noexcept {
    char chunk[kChunkBytes]{};
    std::size_t total = 0;
    bool is_at_end = false;
    bool has_failed = false;
    os::Errno failure = os::Errno::eio;
    while (!is_at_end && !has_failed) {
        read_full_(descriptor, chunk, kChunkBytes)
            .match(
                [&](std::size_t count) {
                    is_at_end = count < kChunkBytes;
                    if (text.append(allocation, core::detail::text_over_(chunk, count)).is_err()) {
                        failure = os::Errno::efbig;
                        has_failed = true;
                        return;
                    }
                    total += count;
                },
                [&](os::Errno error) {
                    failure = error;
                    has_failed = true;
                });
    }
    if (has_failed) {
        return core::err(failure);
    }
    return total;
}

// Complexity: one read for each part of the buffer that the kernel fills,
// and one more read at the end of the file.
core::Result<std::size_t, os::Errno> read_full_(int descriptor, void* first, std::size_t count) noexcept {
    if (is_closed(descriptor)) {
        return core::err(os::Errno::ebadf);
    }
    char* const bytes = static_cast<char*>(first);
    std::size_t total = 0;
    bool is_at_end = false;
    bool has_failed = false;
    os::Errno failure = os::Errno::eio;
    while (!is_at_end && !has_failed && total < count) {
        os::result_of(os::raw_syscall(os::Syscall::read, descriptor, bytes + total, count - total))
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
UnitResult write_full_(int descriptor, void const* first, std::size_t count) noexcept {
    if (is_closed(descriptor)) {
        return core::err(os::Errno::ebadf);
    }
    char const* const bytes = static_cast<char const*>(first);
    std::size_t total = 0;
    bool has_failed = false;
    os::Errno failure = os::Errno::eio;
    while (!has_failed && total < count) {
        os::result_of(os::raw_syscall(os::Syscall::write, descriptor, bytes + total, count - total))
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

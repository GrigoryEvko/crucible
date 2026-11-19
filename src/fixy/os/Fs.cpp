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
#include <foundation/os/PipeSignal.h>
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
    return os::result_of(word).map([](std::uint64_t) noexcept { return core::Unit{}; });
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

namespace {

// The rename of a commit: renameat, which replaces a file at the target, or
// renameat2 with RENAME_NOREPLACE, which gives eexist for it.
enum class RenameKind : std::uint8_t {
    replacing,
    no_replace,
};

// Renames FROM under FROM_DIRECTORY to TO under TO_DIRECTORY.
[[nodiscard]] UnitResult rename_entry(int from_directory, char const* from, int to_directory, char const* to,
                                      RenameKind kind) noexcept {
    long word = 0;
    do {
        word = kind == RenameKind::replacing
                 ? os::raw_syscall(os::Syscall::renameat, from_directory, from, to_directory, to)
                 : os::raw_syscall(os::Syscall::renameat2, from_directory, from, to_directory, to, RENAME_NOREPLACE);
    } while (word == kInterrupted);
    return unit_of(word);
}

// Removes the entry NAME of DIRECTORY, which is no directory.
[[nodiscard]] UnitResult unlink_entry(int directory, char const* name) noexcept {
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::unlinkat, directory, name, 0);
    } while (word == kInterrupted);
    return unit_of(word);
}

// Makes the directory NAME of DIRECTORY with the permission bits MODE.
[[nodiscard]] UnitResult make_directory_entry(int directory, char const* name, std::uint32_t mode) noexcept {
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::mkdirat, directory, name, mode);
    } while (word == kInterrupted);
    return unit_of(word);
}

// The open flags of a directory part (THE FINAL NAME of fixy/os/Fs.h).  The
// descriptor of O_PATH is the directory of a call of the *at family, and the
// open reads no data of the directory.
constexpr int kDirectoryPartFlags = os::kOpenPath | os::kOpenDirectory | os::kOpenCloseOnExec;

// Gives ACT(PARENT, NAME), where PARENT is the descriptor of the directory
// PART beneath DIRECTORY.  The door closes PARENT after ACT.
template <class T, class Act>
[[nodiscard]] core::Result<T, os::Errno> act_in_part(int directory, char const* part, char const* name,
                                                     Act const& act) noexcept {
    return open_beneath_(directory, part, kDirectoryPartFlags, 0)
        .and_then([&](int parent) noexcept -> core::Result<T, os::Errno> {
            core::Result<T, os::Errno> acted = act(parent, name);
            close_descriptor_(parent);
            return acted;
        });
}

// Gives ACT(PARENT, NAME) for the split of PATH beneath DIRECTORY (THE FINAL
// NAME of fixy/os/Fs.h): PARENT is the descriptor of the directory part, or
// DIRECTORY for a path with no directory part, and NAME is the final name.
// The final name is the rest of PATH after its directory part, so it keeps
// the zero of PATH.  Complexity: linear in the size of the path, and one
// open and one close for a path with a directory part.
template <class T, class Act>
[[nodiscard]] core::Result<T, os::Errno> act_on_final_name(int directory, core::CText path, Act const& act) noexcept {
    FinalSplit const split = split_final_name_(path.view());
    char const* const text = core::detail::c_text_first_(path);
    if (split.is_whole) return act_in_part<T>(directory, text, ".", act);
    char const* const name = text + split.final_at;
    if (split.parent.size() == 0) return act(directory, name);
    os::PathText<::fixy::kPathBytes> part{};
    if (part.append(split.parent).is_err()) return core::err(os::Errno::enametoolong);
    return act_in_part<T>(directory, core::detail::c_text_first_(part.c_text()), name, act);
}

// Renames FROM to TO, the two beneath DIRECTORY.  Two paths whose directory
// parts are the same text take one open of that directory, so the rename
// stays in one directory.
[[nodiscard]] UnitResult rename_beneath(int directory, core::CText from, core::CText to, RenameKind kind) noexcept {
    FinalSplit const from_split = split_final_name_(from.view());
    FinalSplit const to_split = split_final_name_(to.view());
    bool const is_one_part = !from_split.is_whole && !to_split.is_whole && from_split.parent == to_split.parent;
    if (is_one_part) {
        char const* const to_name = core::detail::c_text_first_(to) + to_split.final_at;
        return act_on_final_name<core::Unit>(directory, from, [to_name, kind](int parent, char const* from_name) {
            return rename_entry(parent, from_name, parent, to_name, kind);
        });
    }
    return act_on_final_name<core::Unit>(
        directory, from, [directory, to, kind](int from_parent, char const* from_name) {
            return act_on_final_name<core::Unit>(
                directory, to, [from_parent, from_name, kind](int to_parent, char const* to_name) {
                    return rename_entry(from_parent, from_name, to_parent, to_name, kind);
                });
        });
}

}  // namespace

UnitResult rename_replacing_at_(int directory, char const* from, char const* to) noexcept {
    return rename_entry(directory, from, directory, to, RenameKind::replacing);
}

UnitResult rename_no_replace_at_(int directory, char const* from, char const* to) noexcept {
    return rename_entry(directory, from, directory, to, RenameKind::no_replace);
}

UnitResult rename_replacing_beneath_(int directory, core::CText from, core::CText to) noexcept {
    return rename_beneath(directory, from, to, RenameKind::replacing);
}

UnitResult rename_no_replace_beneath_(int directory, core::CText from, core::CText to) noexcept {
    return rename_beneath(directory, from, to, RenameKind::no_replace);
}

core::Result<int, os::Errno> open_beneath_(int directory, char const* path, int flags, std::uint32_t mode) noexcept {
    // The flags are not negative, so the cast to a word keeps each bit.  The
    // kernel reads the mode only for an open that makes a file.
    bool const makes_file = (flags & os::kOpenCreate) != 0 || (flags & os::kOpenTemporary) == os::kOpenTemporary;
    os::OpenHow const how{
        .flags = static_cast<std::uint32_t>(flags),
        .mode = makes_file ? mode : 0U,
        .resolve = os::kResolveBeneath | os::kResolveNoMagicLinks,
    };
    return open_beneath_steps_([directory, path, &how] {
        return os::result_of(
            os::raw_syscall(os::Syscall::openat2, directory, path, static_cast<void const*>(&how), sizeof how));
    });
}

UnitResult unlink_beneath_(int directory, core::CText path) noexcept {
    return act_on_final_name<core::Unit>(directory, path,
                                         [](int parent, char const* name) { return unlink_entry(parent, name); });
}

UnitResult make_directory_beneath_(int directory, core::CText path, std::uint32_t mode) noexcept {
    return act_on_final_name<core::Unit>(
        directory, path, [mode](int parent, char const* name) { return make_directory_entry(parent, name, mode); });
}

core::Result<bool, os::Errno> has_entry_beneath_(int directory, core::CText path) noexcept {
    constexpr int kEntryFlags = os::kOpenPath | os::kOpenNoFollow | os::kOpenCloseOnExec;
    return open_beneath_(directory, core::detail::c_text_first_(path), kEntryFlags, 0)
        .match(
            [](int entry) -> core::Result<bool, os::Errno> {
                close_descriptor_(entry);
                return true;
            },
            [](os::Errno error) -> core::Result<bool, os::Errno> {
                if (error == os::Errno::enoent) return false;
                return core::err(error);
            });
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

core::Result<std::size_t, os::Errno> read_full_(int descriptor, void* first, std::size_t count) noexcept {
    if (is_closed(descriptor)) {
        return core::err(os::Errno::ebadf);
    }
    char* const bytes = static_cast<char*>(first);
    return read_full_steps_(count, [descriptor, bytes](std::size_t offset, std::size_t remaining) {
        return os::result_of(os::raw_syscall(os::Syscall::read, descriptor, bytes + offset, remaining));
    });
}

UnitResult write_full_(int descriptor, void const* first, std::size_t count) noexcept {
    if (is_closed(descriptor)) {
        return core::err(os::Errno::ebadf);
    }
    char const* const bytes = static_cast<char const*>(first);
    // A write to a pipe or a socket with no reader raises SIGPIPE, and
    // write(2) has no MSG_NOSIGNAL.  The block of the signal holds for the
    // whole loop (THE PIPE SIGNAL of foundation/os/PipeSignal.h).
    os::PipeSignalBlock block = os::PipeSignalBlock::block_calling_thread();
    UnitResult written = write_full_steps_(count, [descriptor, bytes](std::size_t offset, std::size_t remaining) {
        return os::result_of(os::raw_syscall(os::Syscall::write, descriptor, bytes + offset, remaining));
    });
    if (written.err().value_or(os::Errno{}) == os::Errno::epipe) {
        block.mark_broken_pipe();
    }
    return written;
}

core::Result<std::int64_t, os::Errno> file_size_(int descriptor) noexcept {
    if (is_closed(descriptor)) {
        return core::err(os::Errno::ebadf);
    }
    ::stat status{};
    return os::result_of(os::raw_syscall(os::Syscall::fstat, descriptor, static_cast<void const*>(&status)))
        .map([&status](std::uint64_t) noexcept { return static_cast<std::int64_t>(status.st_size); });
}

}  // namespace fixy::fs::detail

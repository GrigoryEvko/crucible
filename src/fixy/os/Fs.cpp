// The file door of fixy (fixy/os/Fs.h): the syncs, the renames, the reads
// and the writes of a whole buffer, the read at an offset, the read and the
// change of the size of a file, the opens, the removes, the new
// directories and the status reads under a directory, and the removal of a
// tree under a directory.  A
// call that a signal interrupts runs again.  It issues the
// raw primitive of foundation/os/Syscall.h.  It is the one includer of
// <asm/stat.h>, for the struct of fstat, and of <linux/fs.h>, for the flag of
// renameat2.  It includes no header of the C library, because some
// declarations of the two kinds of header conflict.  In a build with
// ThreadSanitizer, the primitive gives each read and each write to the
// function of the C library (THE SANITIZERS of foundation/os/Syscall.h).

#include <fixy/os/Fs.h>

#include <foundation/core/Choice.h>
#include <foundation/os/Directory.h>
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
// PART beneath DIRECTORY.  The door closes PARENT after ACT.  The arm of the
// open also closes PARENT, so the open is a match and not an and_then.
template <class T, class Act>
[[nodiscard]] core::Result<T, os::Errno> act_in_part(int directory, char const* part, char const* name,
                                                     Act const& act) noexcept {
    return open_beneath_(directory, part, kDirectoryPartFlags, 0)
        .match(
            [&](int parent) noexcept -> core::Result<T, os::Errno> {
                core::Result<T, os::Errno> acted = act(parent, name);
                close_descriptor_(parent);
                return acted;
            },
            [](os::Errno error) noexcept -> core::Result<T, os::Errno> { return core::err(error); });
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

namespace {

// The open flags of a directory of the walk of a tree removal (THE TREE
// REMOVAL of fixy/os/Fs.h): a read lists its entries, O_DIRECTORY refuses an
// entry of another kind, and O_NOFOLLOW refuses a symbolic link.
constexpr int kTreeLevelFlags = os::kOpenReadOnly | os::kOpenDirectory | os::kOpenNoFollow | os::kOpenCloseOnExec;

// Removes the empty directory NAME of DIRECTORY.
[[nodiscard]] UnitResult remove_directory_entry(int directory, char const* name) noexcept {
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::unlinkat, directory, name, os::kAtRemoveDirectory);
    } while (word == kInterrupted);
    return unit_of(word);
}

// The descriptor of a new open of the directory LEVEL, which lists the
// entries of LEVEL from the first one.
[[nodiscard]] core::Result<int, os::Errno> open_again(int level) noexcept {
    return open_beneath_(level, ".", kTreeLevelFlags, 0);
}

// The end of a step or of a pass of the walk.
enum class WalkEnd : std::uint8_t {
    // The entry went, or the pass read to the end of the listing.
    removed,
    // A directory that holds an entry opened beneath the directory of the walk.
    opened,
    // A removal, a listing or an open failed.
    failed,
};

// A step or a pass of the walk: its end, the descriptor of the directory that
// it opened, and the error of a failure.  The plain record keeps the walk free
// of a sum in a sum, which the visitor of the listing inlines.
struct WalkStep {
    WalkEnd end = WalkEnd::removed;
    int below = -1;
    os::Errno error = os::Errno::eio;
};

// The step of the walk at the entry NAME of the directory LEVEL, which a
// listing gave.  The step removes a file or a link, or an empty directory.
// For a directory that holds an entry, it opens that directory beneath LEVEL.
// An entry that is gone at its removal counts as removed.
[[nodiscard]] WalkStep remove_or_open(int level, char const* name) noexcept {
    // The error number zero is no error, so it stands for a call that took the
    // entry.
    constexpr os::Errno kTaken{};
    os::Errno const unlinked = unlink_entry(level, name).err().value_or(kTaken);
    if (unlinked == kTaken || unlinked == os::Errno::enoent) return WalkStep{};
    if (unlinked != os::Errno::eisdir) return WalkStep{.end = WalkEnd::failed, .error = unlinked};
    os::Errno const held = remove_directory_entry(level, name).err().value_or(kTaken);
    if (held == kTaken || held == os::Errno::enoent) return WalkStep{};
    if (held != os::Errno::enotempty && held != os::Errno::eexist) {
        return WalkStep{.end = WalkEnd::failed, .error = held};
    }
    WalkStep step{.end = WalkEnd::failed};
    open_beneath_(level, name, kTreeLevelFlags, 0)
        .match(
            [&step](int descriptor) noexcept {
                step.end = WalkEnd::opened;
                step.below = descriptor;
            },
            [&step](os::Errno error) noexcept { step.error = error; });
    return step;
}

// One pass of the walk over the entries of LEVEL, from the offset of its
// descriptor.  The pass takes the step of remove_or_open at each entry, and it
// stops at the first step that opens a directory or fails.  It gives that step,
// or a removed step when it reads to the end.
[[nodiscard]] WalkStep clear_pass(int level) noexcept {
    WalkStep step{};
    auto visit = [level, &step](core::CText name) noexcept -> os::Visit {
        step = remove_or_open(level, core::detail::c_text_first_(name));
        return step.end == WalkEnd::removed ? os::Visit::proceed : os::Visit::stop;
    };
    os::Errno const listed =
        os::detail::list_open_directory_(level, os::detail::erase_visitor_(visit)).err().value_or(os::Errno{});
    if (listed != os::Errno{}) return WalkStep{.end = WalkEnd::failed, .error = listed};
    return step;
}

// Removes each entry below the directory LEVEL, whose descriptor lists from the
// first entry, and closes LEVEL.  DEPTH counts the directories of the walk
// above LEVEL.  After the walk below one directory, a new open lists LEVEL
// again, because the pass stopped in the middle of the listing.
[[nodiscard]] UnitResult clear_and_close(int level, std::size_t depth) noexcept {
    int current = level;
    UnitResult outcome = core::Unit{};
    bool is_done = false;
    while (!is_done) {
        WalkStep const pass = clear_pass(current);
        switch (pass.end) {
            case WalkEnd::removed:
                is_done = true;
                break;
            case WalkEnd::failed:
                outcome = core::err(pass.error);
                is_done = true;
                break;
            case WalkEnd::opened:
            default: {
                if (depth + 1 == kRemoveTreeDepth) {
                    close_descriptor_(pass.below);
                    outcome = core::err(os::Errno::enametoolong);
                    is_done = true;
                    break;
                }
                UnitResult inner = clear_and_close(pass.below, depth + 1);
                if (inner.is_err()) {
                    outcome = std::move(inner);
                    is_done = true;
                    break;
                }
                open_again(current).match(
                    [&current](int again) noexcept {
                        close_descriptor_(current);
                        current = again;
                    },
                    [&](os::Errno error) noexcept {
                        outcome = core::err(error);
                        is_done = true;
                    });
                break;
            }
        }
    }
    close_descriptor_(current);
    return outcome;
}

}  // namespace

UnitResult remove_entries_below_(int directory) noexcept {
    if (is_closed(directory)) {
        return core::err(os::Errno::ebadf);
    }
    return open_again(directory).and_then([](int level) noexcept { return clear_and_close(level, 0); });
}

UnitResult remove_tree_beneath_(int directory, core::CText path) noexcept {
    if (split_final_name_(path.view()).is_whole) {
        return core::err(os::Errno::einval);
    }
    return act_on_final_name<core::Unit>(directory, path, [](int parent, char const* name) noexcept -> UnitResult {
        return unlink_entry(parent, name)
            .match([](core::Unit) noexcept -> UnitResult { return core::Unit{}; },
                   [parent, name](os::Errno unlinked) noexcept -> UnitResult {
                       if (unlinked != os::Errno::eisdir) return core::err(unlinked);
                       return open_beneath_(parent, name, kTreeLevelFlags, 0)
                           .and_then([](int level) noexcept { return clear_and_close(level, 0); })
                           .and_then(
                               [parent, name](core::Unit) noexcept { return remove_directory_entry(parent, name); });
                   });
    });
}

UnitResult remove_empty_directory_beneath_(int directory, core::CText path) noexcept {
    if (split_final_name_(path.view()).is_whole) {
        return core::err(os::Errno::einval);
    }
    return act_on_final_name<core::Unit>(
        directory, path, [](int parent, char const* name) { return remove_directory_entry(parent, name); });
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

// Each step reads at the offset of the call plus the bytes that the steps
// before it read.  A sum past the largest signed offset of 64 bits gives
// einval, as the kernel gives for such an offset.
core::Result<std::size_t, os::Errno> read_full_at_(int descriptor, void* first, std::size_t count,
                                                   std::int64_t offset) noexcept {
    if (is_closed(descriptor)) {
        return core::err(os::Errno::ebadf);
    }
    if (offset < 0) {
        return core::err(os::Errno::einval);
    }
    char* const bytes = static_cast<char*>(first);
    return read_full_steps_(
        count,
        [descriptor, bytes, offset](std::size_t done, std::size_t remaining) -> core::Result<std::uint64_t, os::Errno> {
            std::int64_t at = 0;
            if (__builtin_add_overflow(offset, done, &at)) {
                return core::err(os::Errno::einval);
            }
            return os::result_of(os::raw_syscall(os::Syscall::pread, descriptor, bytes + done, remaining, at));
        });
}

UnitResult set_size_(int descriptor, std::int64_t length) noexcept {
    if (is_closed(descriptor)) {
        return core::err(os::Errno::ebadf);
    }
    long word = 0;
    do {
        word = os::raw_syscall(os::Syscall::ftruncate, descriptor, length);
    } while (word == kInterrupted);
    return unit_of(word);
}

}  // namespace fixy::fs::detail

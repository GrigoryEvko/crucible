#pragma once

// The file syscalls: open, the two syncs, the atomic commit, and the
// directory descriptor that flushes an entry.
//
// An atom pack says how a file is opened — its mode, its flags, and the
// durability and commit it is opened FOR — and the pack is read twice.
// Once for the O_* word the syscall takes, and once for the effect row
// the calling context has to admit.
//
// The door file src/fixy/os/Fs.cpp holds the system calls, and it issues
// them with the raw primitive of foundation/os/Syscall.h.  The templates
// of this header read the atom packs at compile time and call the door.
//
// Design notes:
//
//  1. The atoms fixy::atom::fs::{mode, with_flag, durable, atomic_write}
//     carry the axis and the row.  The context gate reads the row off
//     the pack and does not name IO and Block by hand.  A check in the
//     check file of this header pins the derived row to IO and Block.
//
//  2. A tag exists only when it reaches an operation that works.  A mode
//     that the open gate refuses, a flag that nothing reads, a sync that
//     fails with EINVAL or a commit that fails with ENOSYS has no tag.
//
//  3. The open modes and the flags are closed tables, and the sync and
//     atomicity tags are closed lists.  A tag with no row has no bits, so
//     a read of its bits is a compile error and not O_RDONLY or zero.
//     The known-tag concepts that the gates read are the tables, and
//     walks over the fixy::fs namespaces check that each declared tag has
//     a row.
//
//  4. A file and a directory are Handles of fixy/os/Handle.h, with the
//     rights of their open in the type.  The Handle has a private
//     constructor, and FileDoor here makes each open of a path.  The members
//     of the door are private, and its friends are the gated mints that use
//     it.  So a file or a directory that is owned is a descriptor that the
//     kernel handed out to a gated mint.  A call takes the borrow of a
//     handle, and asks for the rights of its work in its constraint.
//
//  5. open_directory takes a context and a sanitized path, so the path
//     discipline and the effect row reach it as they reach each other
//     call in the family.
//
//  6. The two durable mints carry a requires-clause of their own and do
//     not rely on the one inside mint_file.  A constraint failure a
//     layer down is a hard error at the call site.

#include <fixy/os/Path.h>
#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
#include <fixy/os/AtomPack.h>
#include <fixy/os/Fd.h>
#include <fixy/os/Handle.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Region.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/os/Errno.h>
#include <foundation/os/OpenFlags.h>
#include <foundation/os/PathText.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace fixy::fs {

namespace eff = ::foundation::effects;

// The O_* word of each open mode and the O_* bits of each flag, with the
// values of foundation/os/OpenFlags.h.  A tag reaches the open call only
// through a row of these tables, and fixy/os/AtomPack.h says why a table
// is closed.  O_RDONLY is zero, so a map that answers zero for an unknown
// mode opens it for reading.  A class template map also takes a
// specialization for a class of the caller, and that class can add
// O_TRUNC to an open whose mode is ReadOnly.
inline constexpr ::fixy::atom_pack::tag_row<int> open_mode_table[] = {
    {^^open_mode::ReadOnly, ::foundation::os::kOpenReadOnly},
    {^^open_mode::WriteCreate, ::foundation::os::kOpenWriteOnly | ::foundation::os::kOpenCreate},
    {^^open_mode::WriteAppend,
     ::foundation::os::kOpenWriteOnly | ::foundation::os::kOpenCreate | ::foundation::os::kOpenAppend},
    {^^open_mode::WriteTruncate,
     ::foundation::os::kOpenWriteOnly | ::foundation::os::kOpenCreate | ::foundation::os::kOpenTruncate},
    {^^open_mode::ReadWrite, ::foundation::os::kOpenReadWrite | ::foundation::os::kOpenCreate},
};

// The rights of the file of each open mode (THE RIGHTS of fixy/os/Handle.h).
// A map reads its file, so only a mode that reads gives the map right.  An
// open for an append writes each byte at the end of the file, so it gives no
// seek right.
inline constexpr ::fixy::atom_pack::tag_row<::fixy::os::RightSet> open_mode_rights_table[] = {
    {^^open_mode::ReadOnly,
     ::fixy::os::RightSet::read | ::fixy::os::RightSet::seek | ::fixy::os::RightSet::sync | ::fixy::os::RightSet::map},
    {^^open_mode::WriteCreate, ::fixy::os::RightSet::write | ::fixy::os::RightSet::seek | ::fixy::os::RightSet::sync},
    {^^open_mode::WriteAppend, ::fixy::os::RightSet::write | ::fixy::os::RightSet::sync},
    {^^open_mode::WriteTruncate, ::fixy::os::RightSet::write | ::fixy::os::RightSet::seek | ::fixy::os::RightSet::sync},
    {^^open_mode::ReadWrite, ::fixy::os::RightSet::read | ::fixy::os::RightSet::write | ::fixy::os::RightSet::seek
                                 | ::fixy::os::RightSet::sync | ::fixy::os::RightSet::map},
};

// The rights of a file with no name, which the door opens for reading and
// writing (THE TEMPORARY FILE below).
inline constexpr ::fixy::os::RightSet temporary_file_rights = ::fixy::os::RightSet::read | ::fixy::os::RightSet::write
                                                            | ::fixy::os::RightSet::seek | ::fixy::os::RightSet::sync
                                                            | ::fixy::os::RightSet::map;

// The rights of a directory: a lookup of a path beneath it, a create and a
// remove of an entry, and the sync that flushes its entries.
inline constexpr ::fixy::os::RightSet directory_rights = ::fixy::os::RightSet::lookup | ::fixy::os::RightSet::create
                                                       | ::fixy::os::RightSet::remove | ::fixy::os::RightSet::sync;

// The handle of a directory.
using DirectoryHandle = ::fixy::os::Handle<::fixy::os::kind::Directory, directory_rights>;

// The handle of a file with no name.
using TemporaryFile = ::fixy::os::Handle<::fixy::os::kind::File, temporary_file_rights>;

inline constexpr ::fixy::atom_pack::tag_row<int> flag_table[] = {
    {^^flag::CloseOnExec, ::foundation::os::kOpenCloseOnExec}, {^^flag::NoFollow, ::foundation::os::kOpenNoFollow},
    {^^flag::DataSync, ::foundation::os::kOpenDataSync},       {^^flag::FullSync, ::foundation::os::kOpenFullSync},
    {^^flag::Direct, ::foundation::os::kOpenDirect},
};

// The sync and atomicity tags have no bits, so each is a closed list.
inline constexpr std::meta::info sync_op_tags[] = {^^sync_op::None, ^^sync_op::Fdatasync, ^^sync_op::Fsync,
                                                   ^^sync_op::FsyncParentDir};

inline constexpr std::meta::info atomicity_tags[] = {^^atomicity::None, ^^atomicity::Rename,
                                                     ^^atomicity::RenameAt2NoReplace};

// A tag is known when its table or its list names it.  The walks at the
// foot of this header read the fixy::fs namespaces and fail if a declared
// tag has no row.
template <typename Mode>
concept MappedOpenMode = ::fixy::atom_pack::has_row(open_mode_table, ^^Mode);

template <typename Flag>
concept MappedFlag = ::fixy::atom_pack::has_row(flag_table, ^^Flag);

template <typename SyncOp>
concept KnownSyncOp = ::fixy::atom_pack::names_tag(sync_op_tags, ^^SyncOp);

template <typename Atomicity>
concept KnownAtomicity = ::fixy::atom_pack::names_tag(atomicity_tags, ^^Atomicity);

// The bits of a tag that has a row.  Each lookup is a function and not a
// variable template, because a caller can specialize a variable template
// for one tag and give it bits of its own.  A function over a reflection
// takes no specialization.
[[nodiscard]] consteval int open_mode_flags_of(std::meta::info mode_tag) noexcept {
    return ::fixy::atom_pack::value_for(open_mode_table, mode_tag);
}

[[nodiscard]] consteval int flag_bits_of(std::meta::info flag_tag) noexcept {
    return ::fixy::atom_pack::value_for(flag_table, flag_tag);
}

namespace detail {

// The file calls of the door file src/fixy/os/Fs.cpp.  Each call takes a
// descriptor or a path that a zero ends, and it gives the error of the
// kernel in its Result.  A call that a signal interrupts starts again, and
// each other error returns.  A descriptor below zero, the descriptor of a
// closed handle, gives EBADF and makes no system call.  DIRECTORY is the
// descriptor of a directory, and a call reads PATH relative to it, or
// relative to the current directory for kAtCurrentDirectory.

// Flushes the data of DESCRIPTOR, with the metadata that a read of the data
// needs (fdatasync).
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sync_data_(int descriptor) noexcept;

// Flushes the data and all the metadata of DESCRIPTOR (fsync).  On a
// descriptor of a directory, the flush writes the entries of the directory.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sync_all_(int descriptor) noexcept;

// Renames FROM to TO under DIRECTORY, and replaces a file at TO (renameat).
// The kernel reads an absolute path and a ".." part with no limit, so only
// commit_atomic, which takes the current directory and full paths, calls
// this and the rename below.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
rename_replacing_at_(int directory, char const* from, char const* to) noexcept;

// Renames FROM to TO under DIRECTORY, and gives eexist when a file is at TO
// (renameat2 with RENAME_NOREPLACE).  A file system with no such rename gives
// einval, and a kernel older than renameat2 gives enosys.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
rename_no_replace_at_(int directory, char const* from, char const* to) noexcept;

// Opens PATH beneath DIRECTORY with the open flags FLAGS of
// foundation/os/OpenFlags.h, and gives the descriptor (openat2 with
// RESOLVE_BENEATH and RESOLVE_NO_MAGICLINKS).  A path that leaves the
// directory gives exdev, and a magic link of procfs gives eloop.  The door
// gives MODE to the kernel only when FLAGS holds kOpenCreate.  It tries again
// after eagain, as open_beneath_steps_ tells.  FileDoor is the one caller
// that keeps the descriptor.
[[nodiscard]] ::foundation::core::Result<int, ::foundation::os::Errno>
open_beneath_(int directory, char const* path, int flags, std::uint32_t mode) noexcept;

// The count of the tries of an open beneath a directory.  The kernel gives
// eagain for RESOLVE_BENEATH when a rename or a mount on the host races the
// lookup of a ".." part.  A path with no ".." part gets no eagain.
inline constexpr std::size_t kBeneathTries = 32;

// Opens with STEP until a step gives a descriptor or an error other than
// eintr and eagain: STEP() gives the descriptor or the error of the kernel.
// A step that a signal interrupts runs again with no bound, as each call of
// the door file does.  A step that gives eagain runs again, and the open gives
// eagain after kBeneathTries steps that gave it.  Complexity: at most
// kBeneathTries steps, and one more step for each signal.
template <class Step>
[[nodiscard]] constexpr ::foundation::core::Result<int, ::foundation::os::Errno>
open_beneath_steps_(Step step) noexcept {
    std::size_t raced = 0;
    bool is_open = false;
    bool has_failed = false;
    int descriptor = -1;
    ::foundation::os::Errno failure = ::foundation::os::Errno::eagain;
    while (!is_open && !has_failed) {
        step().match(
            [&](std::uint64_t opened) {
                descriptor = static_cast<int>(opened);
                is_open = true;
            },
            [&](::foundation::os::Errno error) {
                if (error == ::foundation::os::Errno::eagain) {
                    raced += 1;
                    has_failed = raced == kBeneathTries;
                } else if (error != ::foundation::os::Errno::eintr) {
                    failure = error;
                    has_failed = true;
                }
            });
    }
    if (has_failed) {
        return ::foundation::core::err(failure);
    }
    return descriptor;
}

// True when PATH names an entry under DIRECTORY, and false when no entry has
// the name (newfstatat with kAtSymlinkNoFollow).  A symbolic link is an
// entry, and the call does not follow it.  The kernel reads an absolute path
// and a ".." part with no limit, so only has_entry, which takes the current
// directory and a full path, calls this.
[[nodiscard]] ::foundation::core::Result<bool, ::foundation::os::Errno> has_entry_at_(int directory,
                                                                                      char const* path) noexcept;

// THE FINAL NAME
//   unlinkat, mkdirat and renameat take no resolve flags.  So the door of
//   each of them opens the directory part of its path beneath the directory
//   (open_beneath_, with O_PATH), and it gives the call one final name under
//   that directory.  The final name is the last part of the path, with the
//   slashes after it, so the call keeps the meaning of a slash at the end.
//   A path with no slash before its last part has no directory part, and
//   the call takes the directory itself.  A last part "." or "..", and a path
//   of slashes only, name a directory and no entry of one.  For such a path
//   the door opens the whole path beneath the directory, and the final name
//   is ".".
//   So no final name holds a slash before its end, and no final name is
//   "..".

// The split of a path for a call of one final name (THE FINAL NAME).
struct FinalSplit {
    // The characters of the directory part, with the slash after them.  The
    // text is empty when the final name is under the directory itself, and it is
    // the whole path when is_whole is true.
    ::foundation::core::TextView parent{};
    // The offset of the final name in the path.
    std::size_t final_at = 0;
    // True when the door opens the whole path, and the final name is ".".
    bool is_whole = false;
};

// The split of PATH (THE FINAL NAME).  An empty path has no directory part,
// and the kernel gives enoent for its empty final name.  Complexity: linear
// in the size of the path.
[[nodiscard]] constexpr FinalSplit split_final_name_(::foundation::core::TextView path) noexcept {
    std::size_t index = 0;
    std::size_t start = 0;
    std::size_t end = 0;
    bool is_after_slash = true;
    for (char const character : path) {
        if (character != '/') {
            if (is_after_slash) start = index;
            end = index + 1;
        }
        is_after_slash = character == '/';
        index += 1;
    }
    if (end == 0) return FinalSplit{.parent = path, .final_at = 0, .is_whole = path.size() != 0};
    ::foundation::core::TextView const last = path.window(start, end - start).expect("the last part is in the path");
    if (last == ::foundation::core::TextView{"."} || last == ::foundation::core::TextView{".."}) {
        return FinalSplit{.parent = path, .final_at = 0, .is_whole = true};
    }
    return FinalSplit{
        .parent = path.window(0, start).expect("the directory part is in the path"),
        .final_at = start,
        .is_whole = false,
    };
}

// Removes the entry PATH beneath DIRECTORY, which is no directory (unlinkat
// of the final name).
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
unlink_beneath_(int directory, ::foundation::core::CText path) noexcept;

// Makes the directory PATH beneath DIRECTORY with the permission bits MODE
// (mkdirat of the final name).  An entry at PATH gives eexist.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
make_directory_beneath_(int directory, ::foundation::core::CText path, std::uint32_t mode) noexcept;

// Renames FROM to TO, the two beneath DIRECTORY, and replaces a file at TO
// (renameat of the two final names).  When the two directory parts are the
// same text, the door opens that directory one time.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
rename_replacing_beneath_(int directory, ::foundation::core::CText from, ::foundation::core::CText to) noexcept;

// The rename of rename_replacing_beneath_, which gives eexist when a file is
// at TO (renameat2 with RENAME_NOREPLACE).
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
rename_no_replace_beneath_(int directory, ::foundation::core::CText from, ::foundation::core::CText to) noexcept;

// True when PATH names an entry beneath DIRECTORY, and false when no entry
// has the name.  The door opens the whole path with O_PATH and O_NOFOLLOW
// beneath the directory, and it closes the descriptor.  So it reads no data,
// and a symbolic link at the last part is an entry that the door does not
// follow.
[[nodiscard]] ::foundation::core::Result<bool, ::foundation::os::Errno>
has_entry_beneath_(int directory, ::foundation::core::CText path) noexcept;

// Reads each byte of DESCRIPTOR from its offset to the end of the file, and
// puts the bytes after the characters of TEXT.  It gives the count of the
// bytes, or efbig when they do not fit below the maximum of TEXT.
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
read_all_into_(int descriptor, ::foundation::core::Alloc allocation, ::foundation::core::GrowText& text) noexcept;

// Reads into the COUNT bytes at FIRST until they are full or the file ends,
// and gives the count of the bytes that it read (read_full_steps_).
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno> read_full_(int descriptor, void* first,
                                                                                          std::size_t count) noexcept;

// Writes the COUNT bytes at FIRST (write_full_steps_).  A write to a pipe or
// a socket with no reader gives epipe, and the thread gets no SIGPIPE (THE
// PIPE SIGNAL of foundation/os/PipeSignal.h).
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
write_full_(int descriptor, void const* first, std::size_t count) noexcept;

// THE FULL TRANSFERS
//   The loops of read_full_ and write_full_.  STEP moves at most REMAINING
//   bytes at OFFSET, and gives the count that the kernel moved or the error of
//   the kernel: STEP(OFFSET, REMAINING).  The door file gives a step that
//   issues read or write, and a test gives a step of its own.  A step that a
//   signal interrupts runs again, and each other error ends the loop.

// Reads with STEP until COUNT bytes are in, or until a step gives zero at the
// end of the file.  It gives the count of the bytes.  Complexity: one step
// for each part that the kernel fills, and one more step at the end of the
// file.
template <class Step>
[[nodiscard]] constexpr ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
read_full_steps_(std::size_t count, Step step) noexcept {
    std::size_t total = 0;
    bool is_at_end = false;
    bool has_failed = false;
    ::foundation::os::Errno failure = ::foundation::os::Errno::eio;
    while (!is_at_end && !has_failed && total < count) {
        step(total, count - total)
            .match(
                [&](std::uint64_t taken) {
                    is_at_end = taken == 0;
                    total += static_cast<std::size_t>(taken);
                },
                [&](::foundation::os::Errno error) {
                    if (error != ::foundation::os::Errno::eintr) {
                        failure = error;
                        has_failed = true;
                    }
                });
    }
    if (has_failed) {
        return ::foundation::core::err(failure);
    }
    return total;
}

// Writes with STEP until COUNT bytes are out.  A step that takes no byte of a
// part that is not empty gives eio, as write_all of Rust gives WriteZero.  The
// kernel gave no error, so the next step can also take no byte, and a loop
// that asks again does not end.  Complexity: one step for each part that the
// kernel takes.
template <class Step>
[[nodiscard]] constexpr ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
write_full_steps_(std::size_t count, Step step) noexcept {
    std::size_t total = 0;
    bool has_failed = false;
    ::foundation::os::Errno failure = ::foundation::os::Errno::eio;
    while (!has_failed && total < count) {
        step(total, count - total)
            .match(
                [&](std::uint64_t taken) {
                    has_failed = taken == 0;
                    total += static_cast<std::size_t>(taken);
                },
                [&](::foundation::os::Errno error) {
                    if (error != ::foundation::os::Errno::eintr) {
                        failure = error;
                        has_failed = true;
                    }
                });
    }
    if (has_failed) {
        return ::foundation::core::err(failure);
    }
    return ::foundation::core::Unit{};
}

// The size in bytes of the file of DESCRIPTOR (fstat).
[[nodiscard]] ::foundation::core::Result<std::int64_t, ::foundation::os::Errno> file_size_(int descriptor) noexcept;

// Reads into the COUNT bytes at FIRST from the byte OFFSET of the file of
// DESCRIPTOR, until they are full or the file ends, and gives the count of the
// bytes that it read (pread64 in the loop of read_full_steps_).  The read does
// not move the offset of the descriptor.  A negative offset gives einval.
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
read_full_at_(int descriptor, void* first, std::size_t count, std::int64_t offset) noexcept;

// Sets the size of the file of DESCRIPTOR to LENGTH bytes (ftruncate).  The
// file loses each byte at LENGTH or after it, and a file shorter than LENGTH
// gets zero bytes up to it.  A negative length gives einval.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_size_(int descriptor, std::int64_t length) noexcept;

}  // namespace detail

class FileDoor;

namespace detail {

template <typename A>
inline constexpr bool is_mode_atom_v = ::fixy::atom_pack::IsAtomOf<A, ^^::fixy::atom::fs::mode>;
template <typename A>
inline constexpr bool is_flag_atom_v = ::fixy::atom_pack::IsAtomOf<A, ^^::fixy::atom::fs::with_flag>;
template <typename A>
inline constexpr bool is_durable_atom_v = ::fixy::atom_pack::IsAtomOf<A, ^^::fixy::atom::fs::durable>;
template <typename A>
inline constexpr bool is_atomic_write_atom_v = ::fixy::atom_pack::IsAtomOf<A, ^^::fixy::atom::fs::atomic_write>;

template <typename A>
struct extract_mode {
    using type = void;
};
template <typename Mode>
struct extract_mode<::fixy::atom::fs::mode<Mode>> {
    using type = Mode;
};
template <typename A>
using extract_mode_t = typename extract_mode<std::remove_cvref_t<A>>::type;

template <typename A>
struct extract_flag {
    using type = void;
};
template <typename Flag>
struct extract_flag<::fixy::atom::fs::with_flag<Flag>> {
    using type = Flag;
};
template <typename A>
using extract_flag_t = typename extract_flag<std::remove_cvref_t<A>>::type;

template <typename A>
struct extract_sync_op {
    using type = void;
};
template <typename SyncOp>
struct extract_sync_op<::fixy::atom::fs::durable<SyncOp>> {
    using type = SyncOp;
};
template <typename A>
using extract_sync_op_t = typename extract_sync_op<std::remove_cvref_t<A>>::type;

template <typename A>
struct extract_atomicity {
    using type = void;
};
template <typename Atomicity>
struct extract_atomicity<::fixy::atom::fs::atomic_write<Atomicity>> {
    using type = Atomicity;
};
template <typename A>
using extract_atomicity_t = typename extract_atomicity<std::remove_cvref_t<A>>::type;

// A durable or atomic_write atom declares intent for a later sync or
// commit call and adds nothing to the open flags, so those two fold to
// zero here on purpose rather than by omission.
//
// The branches are discarded statements, not a conditional expression.
// A conditional instantiates the bits of each arm, and a mode atom has
// no flag, so the closed map refuses the arm that the atom does not take.
template <typename A>
[[nodiscard]] consteval int atom_open_flags() noexcept {
    if constexpr (is_mode_atom_v<A>) {
        return open_mode_flags_of(^^extract_mode_t<A>);
    } else if constexpr (is_flag_atom_v<A>) {
        return flag_bits_of(^^extract_flag_t<A>);
    } else {
        return 0;
    }
}

template <typename A>
inline constexpr int atom_open_flags_v = atom_open_flags<A>();

// The rights that an atom gives the file: the rights of a mode atom, and no
// right for each other atom.
template <typename A>
[[nodiscard]] consteval ::fixy::os::RightSet atom_rights() noexcept {
    if constexpr (is_mode_atom_v<A>) {
        return ::fixy::atom_pack::value_for(open_mode_rights_table, ^^extract_mode_t<A>);
    } else {
        return ::fixy::os::RightSet::none;
    }
}

// The rights of the file of a pack, which holds one mode atom.
template <typename... Atoms>
[[nodiscard]] consteval ::fixy::os::RightSet fold_rights() noexcept {
    ::fixy::os::RightSet rights = ::fixy::os::RightSet::none;
    ((rights = rights | atom_rights<Atoms>()), ...);
    return rights;
}

// O_CLOEXEC is folded in unconditionally.  A descriptor that survives
// execve leaks into every child process.
template <typename... Atoms>
[[nodiscard]] consteval int fold_open_flags() noexcept {
    int acc = ::foundation::os::kOpenCloseOnExec;
    ((acc |= atom_open_flags_v<Atoms>), ...);
    return acc;
}

// The rights that an open of a pack under a directory needs of the
// directory: a lookup, and a create when the mode makes a missing file.
template <typename... Atoms>
[[nodiscard]] consteval ::fixy::os::RightSet open_at_needs() noexcept {
    bool const is_create = (fold_open_flags<Atoms...>() & ::foundation::os::kOpenCreate) != 0;
    return is_create ? ::fixy::os::RightSet::lookup | ::fixy::os::RightSet::create : ::fixy::os::RightSet::lookup;
}

// An atom of another kind answers true, so the fold is a conjunction
// over the whole pack.
template <typename A>
inline constexpr bool atom_tag_is_known_v =
    (!is_mode_atom_v<A> || MappedOpenMode<extract_mode_t<A>>) && (!is_flag_atom_v<A> || MappedFlag<extract_flag_t<A>>)
    && (!is_durable_atom_v<A> || KnownSyncOp<extract_sync_op_t<A>>)
    && (!is_atomic_write_atom_v<A> || KnownAtomicity<extract_atomicity_t<A>>);

template <typename... Atoms>
inline constexpr bool all_atom_tags_known_v = (atom_tag_is_known_v<Atoms> && ... && true);

}  // namespace detail

// The handle of a file that an open of the pack Atoms gives: a File with the
// rights of the mode of the pack.
template <typename... Atoms>
using FileOf = ::fixy::os::Handle<::fixy::os::kind::File, detail::fold_rights<Atoms...>()>;

// The borrow of a directory with the rights Rights, and the borrow of a file.
template <::fixy::os::RightSet Rights>
using DirectoryRef = ::fixy::os::HandleRef<::fixy::os::kind::Directory, Rights>;

template <::fixy::os::RightSet Rights>
using FileRef = ::fixy::os::HandleRef<::fixy::os::kind::File, Rights>;

// The first clause reads the row off the pack, and the mode clause asks
// for exactly one mode atom.
template <typename Ctx, typename... Atoms>
concept CtxFitsFileMint = ::fixy::atom_pack::CtxAdmitsAtomRow<Ctx, Atoms...> && detail::all_atom_tags_known_v<Atoms...>
                       && ::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::fs::mode, Atoms...>;

// The open of a pack under a directory with the rights Held: the gate of
// mint_file, and the rights of the open on the directory.
template <typename Ctx, ::fixy::os::RightSet Held, typename... Atoms>
concept CtxFitsFileMintAt =
    CtxFitsFileMint<Ctx, Atoms...> && ::fixy::os::HoldsRights<Held, detail::open_at_needs<Atoms...>()>;

// A call that needs the rights Needed of a handle with the rights Held.
template <typename Ctx, ::fixy::os::RightSet Held, ::fixy::os::RightSet Needed>
concept CtxFitsHandleCall = CtxAdmitsFs<Ctx> && ::fixy::os::HoldsRights<Held, Needed>;

// The open of a file with no name under a directory with the rights Held,
// which needs the create right.
template <typename Ctx, ::fixy::os::RightSet Held>
concept CtxFitsTemporaryMint = CtxFitsHandleCall<Ctx, Held, ::fixy::os::RightSet::create>;

// sync_op::None is refused.  A caller that needs no durability does not
// call sync at all, and a tag with no syscall behind it must not reach
// the switch below and fall out of it.
template <typename Ctx, typename SyncOp>
concept CtxFitsSync = CtxAdmitsFs<Ctx> && KnownSyncOp<SyncOp> && !std::is_same_v<SyncOp, sync_op::None>;

// The sync of a file and the sync of a directory.  FsyncParentDir flushes
// the entries of a directory, so it takes a directory and no file.  An fsync
// of a file flushes that file and no entry of its directory.  Each other op
// takes a file.  Each needs the sync right of the handle.
template <typename Ctx, typename SyncOp, ::fixy::os::RightSet Held>
concept CtxFitsDescriptorSync = CtxFitsSync<Ctx, SyncOp> && !std::is_same_v<SyncOp, sync_op::FsyncParentDir>
                             && ::fixy::os::HoldsRights<Held, ::fixy::os::RightSet::sync>;

template <typename Ctx, typename SyncOp, ::fixy::os::RightSet Held>
concept CtxFitsDirectorySync = CtxFitsSync<Ctx, SyncOp> && std::is_same_v<SyncOp, sync_op::FsyncParentDir>
                            && ::fixy::os::HoldsRights<Held, ::fixy::os::RightSet::sync>;

// atomicity::None is refused.  It renames nothing, so a success from it
// would say that a commit was made when none was.  A caller that needs
// no commit does not call commit_atomic.
template <typename Ctx, typename Atomicity>
concept CtxFitsCommitAtomic =
    CtxAdmitsFs<Ctx> && KnownAtomicity<Atomicity> && !std::is_same_v<Atomicity, atomicity::None>;

// Each call below that takes a sanitized path gives the kernel the CText of
// its PathText, so the kernel reads each character and the zero after them.
//
// The Atoms pack precedes Ctx because every explicit template argument
// fills the pack and Ctx deduces from the first function argument.
//
// Declared here and defined below FileDoor, because FileDoor names it as
// a friend, and a friend must already have been declared.  The default
// argument belongs to this declaration.
template <typename... Atoms, eff::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, Atoms...>
[[nodiscard]] inline ::foundation::core::Result<Linear<FileOf<Atoms...>>, ::foundation::os::Errno>
mint_file(Ctx const&, Path<tags::source::Sanitized> const& sanitized_path,
          FileMode perms = FileMode::owner_read_write_others_read) noexcept;

// Declared here and defined below FileDoor for the same reason.
template <eff::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] ::foundation::core::Result<DirectoryHandle, ::foundation::os::Errno>
open_directory(Ctx const&, Path<tags::source::Sanitized> const& dir) noexcept;

// The opens of a path under a directory (THE CALLS UNDER A DIRECTORY below),
// declared here and defined below FileDoor for the same reason.
template <typename... Atoms, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsFileMintAt<Ctx, Rights, Atoms...>
[[nodiscard]] inline ::foundation::core::Result<Linear<FileOf<Atoms...>>, ::foundation::os::Errno>
mint_file_at(Ctx const&, DirectoryRef<Rights> dir, ::foundation::core::CText path,
             FileMode perms = FileMode::owner_read_write_others_read) noexcept;

template <::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsHandleCall<Ctx, Rights, ::fixy::os::RightSet::lookup>
[[nodiscard]] ::foundation::core::Result<DirectoryHandle, ::foundation::os::Errno>
open_directory_at(Ctx const&, DirectoryRef<Rights> dir, ::foundation::core::CText path) noexcept;

// The open of a file with no name under a directory (THE TEMPORARY FILE
// below), declared here and defined below FileDoor for the same reason.
template <::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsTemporaryMint<Ctx, Rights>
[[nodiscard]] inline ::foundation::core::Result<Linear<TemporaryFile>, ::foundation::os::Errno>
mint_temporary_file_at(Ctx const&, DirectoryRef<Rights> dir, FileMode perms = FileMode::owner_read_write) noexcept;

// The door to the open of a path.  No object of it exists.  Its members
// are private, and its friends are the gated mints that open a path:
// mint_file, open_directory, mint_file_at, open_directory_at and
// mint_temporary_file_at.  A Handle befriends this class, so a handle over a
// path comes only from one of those mints, after their gates.
//
// The trailing return types are necessary: fixy/os/CpuPinned.h gives the
// parse reason.
class FileDoor final : ::foundation::NoObject<FileDoor> {
    template <typename... FriendAtoms, eff::IsExecCtx FriendCtx>
        requires CtxFitsFileMint<FriendCtx, FriendAtoms...>
    friend auto mint_file(FriendCtx const&, Path<tags::source::Sanitized> const&, FileMode) noexcept
        -> ::foundation::core::Result<Linear<FileOf<FriendAtoms...>>, ::foundation::os::Errno>;

    template <eff::IsExecCtx FriendCtx>
        requires ::fixy::fs::CtxAdmitsFs<FriendCtx>
    friend auto open_directory(FriendCtx const&, Path<tags::source::Sanitized> const&) noexcept
        -> ::foundation::core::Result<DirectoryHandle, ::foundation::os::Errno>;

    template <typename... FriendAtoms, ::fixy::os::RightSet FriendRights, eff::IsExecCtx FriendCtx>
        requires CtxFitsFileMintAt<FriendCtx, FriendRights, FriendAtoms...>
    friend auto mint_file_at(FriendCtx const&, DirectoryRef<FriendRights>, ::foundation::core::CText, FileMode) noexcept
        -> ::foundation::core::Result<Linear<FileOf<FriendAtoms...>>, ::foundation::os::Errno>;

    template <::fixy::os::RightSet FriendRights, eff::IsExecCtx FriendCtx>
        requires CtxFitsHandleCall<FriendCtx, FriendRights, ::fixy::os::RightSet::lookup>
    friend auto open_directory_at(FriendCtx const&, DirectoryRef<FriendRights>, ::foundation::core::CText) noexcept
        -> ::foundation::core::Result<DirectoryHandle, ::foundation::os::Errno>;

    template <::fixy::os::RightSet FriendRights, eff::IsExecCtx FriendCtx>
        requires CtxFitsTemporaryMint<FriendCtx, FriendRights>
    friend auto mint_temporary_file_at(FriendCtx const&, DirectoryRef<FriendRights>, FileMode) noexcept
        -> ::foundation::core::Result<Linear<TemporaryFile>, ::foundation::os::Errno>;

    // The open flags of a file with no name: it reads and writes, and it
    // closes when a thread starts a program.
    static constexpr int temporary_flags_ =
        ::foundation::os::kOpenTemporary | ::foundation::os::kOpenReadWrite | ::foundation::os::kOpenCloseOnExec;

    // The open flags of a directory: O_DIRECTORY refuses anything else,
    // O_NOFOLLOW refuses a symlink that stands in for the directory.
    static constexpr int directory_flags_ = ::foundation::os::kOpenDirectory | ::foundation::os::kOpenNoFollow
                                          | ::foundation::os::kOpenCloseOnExec | ::foundation::os::kOpenReadOnly;

    // The handle Owned of a descriptor that the open gave, or the error of
    // the open.
    template <class Owned>
    [[nodiscard]] static ::foundation::core::Result<Owned, ::foundation::os::Errno>
    own_(::foundation::core::Result<int, ::foundation::os::Errno> opened) noexcept {
        return std::move(opened).map([](int descriptor) noexcept { return Owned{descriptor}; });
    }

    // The handle Owned of the open, in a Linear, or the error of the open.
    template <class Owned>
    [[nodiscard]] static ::foundation::core::Result<Linear<Owned>, ::foundation::os::Errno>
    own_linear_(::foundation::core::Result<int, ::foundation::os::Errno> opened) noexcept {
        return std::move(opened).map([](int descriptor) noexcept { return mint_linear<Owned>(Owned{descriptor}); });
    }

    // The open reads perms only when flags holds O_CREAT and ignores it
    // otherwise, so it is passed in each case.
    [[nodiscard]] static ::foundation::core::Result<int, ::foundation::os::Errno>
    open_path_(const char* path, int flags, FileMode perms) noexcept {
        return detail::open_descriptor_(path, flags, std::to_underlying(perms));
    }

    // The descriptor is what a later fsync flushes the entry through.
    [[nodiscard]] static ::foundation::core::Result<int, ::foundation::os::Errno>
    open_directory_(const char* dir_path) noexcept {
        return detail::open_descriptor_(dir_path, directory_flags_, 0);
    }

    // The opens of a path beneath the directory of the descriptor directory.
    [[nodiscard]] static ::foundation::core::Result<int, ::foundation::os::Errno>
    open_path_at_(int directory, const char* path, int flags, FileMode perms) noexcept {
        return detail::open_beneath_(directory, path, flags, std::to_underlying(perms));
    }

    [[nodiscard]] static ::foundation::core::Result<int, ::foundation::os::Errno>
    open_directory_at_(int directory, const char* path) noexcept {
        return detail::open_beneath_(directory, path, directory_flags_, 0);
    }

    // The open of a file with no name in the directory of the descriptor.
    [[nodiscard]] static ::foundation::core::Result<int, ::foundation::os::Errno>
    open_temporary_at_(int directory, FileMode perms) noexcept {
        return detail::open_beneath_(directory, ".", temporary_flags_, std::to_underlying(perms));
    }
};

template <typename... Atoms, eff::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, Atoms...>
[[nodiscard]] inline ::foundation::core::Result<Linear<FileOf<Atoms...>>, ::foundation::os::Errno>
mint_file(Ctx const&, Path<tags::source::Sanitized> const& sanitized_path, FileMode perms) noexcept {
    constexpr int flags = detail::fold_open_flags<Atoms...>();
    return FileDoor::own_linear_<FileOf<Atoms...>>(
        FileDoor::open_path_(::foundation::core::detail::c_text_first_(sanitized_path.value().c_text()), flags, perms));
}

// Fdatasync or Fsync of a file.  The gate refuses FsyncParentDir, which
// takes a directory, and a file with no sync right.  The door gives EBADF for
// the borrow of an empty handle.  Not a mint: this acts on an existing handle
// and synthesizes nothing.
template <typename SyncOp, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsDescriptorSync<Ctx, SyncOp, Rights>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sync(Ctx const&, FileRef<Rights> file) noexcept {
    int const descriptor = ::fixy::os::detail::HandleDoor::descriptor_of(file);
    return std::is_same_v<SyncOp, sync_op::Fdatasync> ? detail::sync_data_(descriptor) : detail::sync_all_(descriptor);
}

// FsyncParentDir: the fsync of a directory, which flushes its entries.  It
// takes the borrow of a directory by its own kind, so a file cannot be
// handed in where an entry flush was meant.  The door gives EBADF for the
// borrow of an empty handle.
template <typename SyncOp, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsDirectorySync<Ctx, SyncOp, Rights>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sync(Ctx const&, DirectoryRef<Rights> dir) noexcept {
    return detail::sync_all_(::fixy::os::detail::HandleDoor::descriptor_of(dir));
}

// Rename uses renameat, where the last writer wins.  RenameAt2NoReplace
// uses renameat2(RENAME_NOREPLACE), which fails with EEXIST when the
// target exists instead of overwriting it.  A filesystem without that
// flag reports EINVAL, and that is passed through rather than replaced
// by a plain rename: the caller chose the no-replace semantics, and a
// silent fallback would be a commit that overwrote what it promised not
// to.  The gate refuses None.
//
// Not a mint: this renames existing paths and synthesizes nothing.
template <typename Atomicity, eff::IsExecCtx Ctx>
    requires CtxFitsCommitAtomic<Ctx, Atomicity>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
commit_atomic(Ctx const&, Path<tags::source::Sanitized> const& tmp,
              Path<tags::source::Sanitized> const& target) noexcept {
    constexpr int here = ::foundation::os::kAtCurrentDirectory;
    char const* const source = ::foundation::core::detail::c_text_first_(tmp.value().c_text());
    char const* const destination = ::foundation::core::detail::c_text_first_(target.value().c_text());
    return std::is_same_v<Atomicity, atomicity::Rename> ? detail::rename_replacing_at_(here, source, destination)
                                                        : detail::rename_no_replace_at_(here, source, destination);
}

template <eff::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] ::foundation::core::Result<DirectoryHandle, ::foundation::os::Errno>
open_directory(Ctx const&, Path<tags::source::Sanitized> const& dir) noexcept {
    return FileDoor::own_<DirectoryHandle>(
        FileDoor::open_directory_(::foundation::core::detail::c_text_first_(dir.value().c_text())));
}

// THE CALLS UNDER A DIRECTORY
//   Each call below takes the borrow of a directory and the path of an entry
//   under it, as a CText: a literal, or the c_text() of a
//   foundation::os::PathText.  Each call asks for the rights of its work on
//   the directory: an open asks for lookup, and also for create when its mode
//   makes a missing file.  A remove asks for remove, a make for create, a
//   rename for create and remove, and a check of an entry for lookup.  The
//   kernel reads the path relative to the directory.  Each open folds in
//   O_NOFOLLOW and O_CLOEXEC, so a symbolic link at the last part of the
//   path gives eloop.  A call that a signal interrupts runs again, and each
//   other error returns in the Result.  Each call takes the context that the
//   other calls of this header take, because each can park the caller on
//   the disk.
//
//   Each call reads its path beneath the directory.  An open gives the path
//   to detail::open_beneath_.  A remove, a make, a rename and a check of an
//   entry open the directory part of the path beneath the directory, and the
//   kernel acts on one final name under it (THE FINAL NAME of the detail
//   calls).  A path that leaves the directory gives exdev: an absolute path,
//   a ".." part that climbs above the directory, or a symbolic link whose
//   target is outside it.  A magic link of procfs, such as /proc/self/root,
//   gives eloop.  A ".." part and a symbolic link in a middle part that stay
//   inside the directory are permitted.
//
//   The borrow of a directory handle that holds no descriptor, such as a
//   handle that a move emptied, gives ebadf, and the call makes no system
//   call.  The kernel reads an absolute path with no regard to the
//   descriptor, so the descriptor -1 of an empty handle must not reach it.

// Opens path under dir with the mode and the flags of Atoms, as mint_file
// does.  The kernel reads perms only for a mode that creates the file.
template <typename... Atoms, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsFileMintAt<Ctx, Rights, Atoms...>
[[nodiscard]] inline ::foundation::core::Result<Linear<FileOf<Atoms...>>, ::foundation::os::Errno>
mint_file_at(Ctx const&, DirectoryRef<Rights> dir, ::foundation::core::CText path, FileMode perms) noexcept {
    if (!dir.is_open()) {
        return ::foundation::core::err(::foundation::os::Errno::ebadf);
    }
    constexpr int flags = detail::fold_open_flags<Atoms...>() | ::foundation::os::kOpenNoFollow;
    return FileDoor::own_linear_<FileOf<Atoms...>>(
        FileDoor::open_path_at_(::fixy::os::detail::HandleDoor::descriptor_of(dir),
                                ::foundation::core::detail::c_text_first_(path), flags, perms));
}

// Opens the directory at path under dir.
template <::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsHandleCall<Ctx, Rights, ::fixy::os::RightSet::lookup>
[[nodiscard]] ::foundation::core::Result<DirectoryHandle, ::foundation::os::Errno>
open_directory_at(Ctx const&, DirectoryRef<Rights> dir, ::foundation::core::CText path) noexcept {
    if (!dir.is_open()) {
        return ::foundation::core::err(::foundation::os::Errno::ebadf);
    }
    return FileDoor::own_<DirectoryHandle>(FileDoor::open_directory_at_(
        ::fixy::os::detail::HandleDoor::descriptor_of(dir), ::foundation::core::detail::c_text_first_(path)));
}

// THE TEMPORARY FILE
//   mint_temporary_file_at opens a regular file with no name in the
//   directory of dir (O_TMPFILE), for reading and writing, with the
//   permission bits perms.  No other process can open the file by a path,
//   and the kernel frees it at the last close of its descriptor, also when
//   the process ends with no close.  The file system of the directory must
//   give O_TMPFILE: a file system that does not gives eopnotsupp.  The borrow
//   of an empty handle gives ebadf, and the call makes no system call.
template <::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsTemporaryMint<Ctx, Rights>
[[nodiscard]] inline ::foundation::core::Result<Linear<TemporaryFile>, ::foundation::os::Errno>
mint_temporary_file_at(Ctx const&, DirectoryRef<Rights> dir, FileMode perms) noexcept {
    if (!dir.is_open()) {
        return ::foundation::core::err(::foundation::os::Errno::ebadf);
    }
    return FileDoor::own_linear_<TemporaryFile>(
        FileDoor::open_temporary_at_(::fixy::os::detail::HandleDoor::descriptor_of(dir), perms));
}

// A rename under a directory with the rights Held.  The rename makes the
// entry of the target and removes the entry of the source, so it needs the
// create and the remove rights.
template <typename Ctx, typename Atomicity, ::fixy::os::RightSet Held>
concept CtxFitsCommitAtomicAt =
    CtxFitsCommitAtomic<Ctx, Atomicity>
    && ::fixy::os::HoldsRights<Held, ::fixy::os::RightSet::create | ::fixy::os::RightSet::remove>;

// Renames from to to, the two under dir, with the atomicity of
// commit_atomic.  Not a mint.
template <typename Atomicity, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsCommitAtomicAt<Ctx, Atomicity, Rights>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
commit_atomic_at(Ctx const&, DirectoryRef<Rights> dir, ::foundation::core::CText from,
                 ::foundation::core::CText to) noexcept {
    if (!dir.is_open()) {
        return ::foundation::core::err(::foundation::os::Errno::ebadf);
    }
    int const directory = ::fixy::os::detail::HandleDoor::descriptor_of(dir);
    return std::is_same_v<Atomicity, atomicity::Rename> ? detail::rename_replacing_beneath_(directory, from, to)
                                                        : detail::rename_no_replace_beneath_(directory, from, to);
}

// Removes the file at path under dir.  A directory at path gives eisdir.
template <::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsHandleCall<Ctx, Rights, ::fixy::os::RightSet::remove>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
unlink_at(Ctx const&, DirectoryRef<Rights> dir, ::foundation::core::CText path) noexcept {
    if (!dir.is_open()) {
        return ::foundation::core::err(::foundation::os::Errno::ebadf);
    }
    return detail::unlink_beneath_(::fixy::os::detail::HandleDoor::descriptor_of(dir), path);
}

// Makes the directory at path under dir, with the permission bits perms.
// An entry at path gives eexist.
template <::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsHandleCall<Ctx, Rights, ::fixy::os::RightSet::create>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
make_directory_at(Ctx const&, DirectoryRef<Rights> dir, ::foundation::core::CText path,
                  FileMode perms = FileMode::owner_all_others_read_execute) noexcept {
    if (!dir.is_open()) {
        return ::foundation::core::err(::foundation::os::Errno::ebadf);
    }
    return detail::make_directory_beneath_(::fixy::os::detail::HandleDoor::descriptor_of(dir), path,
                                           std::to_underlying(perms));
}

// True when an entry is at path under dir, and false when no entry is
// there.  A symbolic link is an entry, and the call does not follow it.
template <::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsHandleCall<Ctx, Rights, ::fixy::os::RightSet::lookup>
[[nodiscard]] inline ::foundation::core::Result<bool, ::foundation::os::Errno>
has_entry_at(Ctx const&, DirectoryRef<Rights> dir, ::foundation::core::CText path) noexcept {
    if (!dir.is_open()) {
        return ::foundation::core::err(::foundation::os::Errno::ebadf);
    }
    return detail::has_entry_beneath_(::fixy::os::detail::HandleDoor::descriptor_of(dir), path);
}

// True when an entry is at path, and false when no entry is there.  The
// kernel reads a relative path from the current directory.  The status read
// judges each directory of the path with the effective ids and the
// capabilities of the process, as an open does.  A symbolic link at the last
// part of path is an entry, and the call does not follow it.
template <eff::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] inline ::foundation::core::Result<bool, ::foundation::os::Errno>
has_entry(Ctx const&, ::foundation::core::CText path) noexcept {
    return detail::has_entry_at_(::foundation::os::kAtCurrentDirectory,
                                 ::foundation::core::detail::c_text_first_(path));
}

// The calls below read and write through a handle that is open.  A signal
// that interrupts a read or a write restarts it, and every other error
// returns.  read_full stops at the end of the file, so it returns fewer
// bytes than the View holds only there.  A write_full to a pipe or a socket
// with no reader gives epipe, and the process gets no SIGPIPE.  The bytes
// are a View of the base, or the characters of a TextView.
//
// Each takes the borrow of a file, and the context that the other calls in
// this header take, because each call can park the caller on the disk.  A
// read asks for the read right of the file, and a write for the write right.
// The door gives EBADF for the borrow of an empty handle.
//
// Not mints: these act on an existing handle and synthesize nothing.
template <::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] inline ::foundation::core::Result<std::int64_t, ::foundation::os::Errno>
file_size(Ctx const&, FileRef<Rights> file) noexcept {
    return detail::file_size_(::fixy::os::detail::HandleDoor::descriptor_of(file));
}

// Reads into buffer from the byte offset of the file, until the buffer is
// full or the file ends, and gives the count of the bytes.  The read does not
// move the offset of the file, so a reader of one part of a file needs no
// state of its position.  A read at an offset is a positioned access, so it
// asks for the read and the seek rights.  A file of an append open holds no
// seek right, and a reader of such a file opens it again for reading.
template <::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsHandleCall<Ctx, Rights, ::fixy::os::RightSet::read | ::fixy::os::RightSet::seek>
[[nodiscard]] inline ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
read_full_at(Ctx const&, FileRef<Rights> file, ::foundation::core::View<std::byte> buffer, FileOffset offset) noexcept {
    return detail::read_full_at_(::fixy::os::detail::HandleDoor::descriptor_of(file),
                                 first_of_(::foundation::core::detail::ViewDoor::key(), buffer), buffer.size(),
                                 offset.raw());
}

// Sets the size of the file to length bytes: the file loses each byte at
// length or after it, and a shorter file gets zero bytes up to length.  The
// call changes the bytes of the file, so it asks for the write right.  A file
// of an append open holds it, and the next append writes at the new end.
template <::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsHandleCall<Ctx, Rights, ::fixy::os::RightSet::write>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_file_size(Ctx const&, FileRef<Rights> file, FileOffset length) noexcept {
    return detail::set_size_(::fixy::os::detail::HandleDoor::descriptor_of(file), length.raw());
}

// A read and a write take the borrow of a handle of each kind that holds the
// right: a file, a socket or a perf event.  A directory holds neither right.
template <class Kind, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsHandleCall<Ctx, Rights, ::fixy::os::RightSet::read>
[[nodiscard]] inline ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
read_full(Ctx const&, ::fixy::os::HandleRef<Kind, Rights> handle, ::foundation::core::View<std::byte> buffer) noexcept {
    return detail::read_full_(::fixy::os::detail::HandleDoor::descriptor_of(handle),
                              first_of_(::foundation::core::detail::ViewDoor::key(), buffer), buffer.size());
}

template <class Kind, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsHandleCall<Ctx, Rights, ::fixy::os::RightSet::write>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
write_full(Ctx const&, ::fixy::os::HandleRef<Kind, Rights> handle,
           ::foundation::core::View<std::byte const> buffer) noexcept {
    return detail::write_full_(::fixy::os::detail::HandleDoor::descriptor_of(handle),
                               first_of_(::foundation::core::detail::ViewDoor::key(), buffer), buffer.size());
}

template <class Kind, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsHandleCall<Ctx, Rights, ::fixy::os::RightSet::write>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
write_full(Ctx const&, ::fixy::os::HandleRef<Kind, Rights> handle, ::foundation::core::TextView text) noexcept {
    return detail::write_full_(::fixy::os::detail::HandleDoor::descriptor_of(handle),
                               ::foundation::core::detail::text_first_(text), text.size());
}

// The read-only open, spelled once.
using read_only = ::fixy::atom::fs::mode<open_mode::ReadOnly>;

// Reads the whole file at path under dir, and puts its bytes after the
// characters of text.  It gives the count of the bytes, or efbig when they
// do not fit below the maximum of text.  The allocation capability shows
// the growth of text at the call.  A failed read leaves the bytes that it
// put before the failure in text.  Complexity: linear in the size of the
// file.
template <class Marker, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsHandleCall<Ctx, Rights, ::fixy::os::RightSet::lookup>
          && ::foundation::core::AllocationCapability<Marker>
[[nodiscard]] inline ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
read_whole_at(Ctx const& ctx, Marker allocation, DirectoryRef<Rights> dir, ::foundation::core::CText path,
              ::foundation::core::GrowText& text) noexcept {
    return mint_file_at<read_only>(ctx, dir, path)
        .and_then([allocation, &text](Linear<FileOf<read_only>> opened) noexcept {
            FileOf<read_only> const file = std::move(opened).consume();
            return detail::read_all_into_(::fixy::os::detail::HandleDoor::descriptor_of(file.ref()), allocation, text);
        });
}

// The durable and atomic_write atoms only declare intent.  The mint
// opens the file.  The caller still calls sync<Fsync> after writing and
// then commit_atomic<Rename> to move the result into place.
template <eff::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, ::fixy::atom::fs::mode<open_mode::WriteTruncate>,
                             ::fixy::atom::fs::durable<sync_op::Fsync>,
                             ::fixy::atom::fs::atomic_write<atomicity::Rename>>
[[nodiscard]] inline ::foundation::core::Result<Linear<FileOf<::fixy::atom::fs::mode<open_mode::WriteTruncate>>>,
                                                ::foundation::os::Errno>
mint_durable_truncate_file(Ctx const& ctx, Path<tags::source::Sanitized> const& path,
                           FileMode perms = FileMode::owner_read_write_others_read) noexcept {
    return mint_file<::fixy::atom::fs::mode<open_mode::WriteTruncate>, ::fixy::atom::fs::durable<sync_op::Fsync>,
                     ::fixy::atom::fs::atomic_write<atomicity::Rename>>(ctx, path, perms);
}

// O_DSYNC makes each write(2) return only once the data is on stable
// storage, but not the metadata.  The durable<Fdatasync> atom declares
// that the caller still calls sync<Fdatasync> between batches to cover
// the metadata.
//
// There is no atomic_write atom.  An append-only file has no
// tmp-to-target rename phase.  The file is the target.
template <eff::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, ::fixy::atom::fs::mode<open_mode::WriteAppend>,
                             ::fixy::atom::fs::durable<sync_op::Fdatasync>, ::fixy::atom::fs::with_flag<flag::DataSync>>
[[nodiscard]] inline ::foundation::core::Result<Linear<FileOf<::fixy::atom::fs::mode<open_mode::WriteAppend>>>,
                                                ::foundation::os::Errno>
mint_durable_append_file(Ctx const& ctx, Path<tags::source::Sanitized> const& path,
                         FileMode perms = FileMode::owner_read_write_others_read) noexcept {
    return mint_file<::fixy::atom::fs::mode<open_mode::WriteAppend>, ::fixy::atom::fs::durable<sync_op::Fdatasync>,
                     ::fixy::atom::fs::with_flag<flag::DataSync>>(ctx, path, perms);
}

}  // namespace fixy::fs

namespace fixy::fs::detail::fs_surface_invariants {

// Every tag the four fixy::fs namespaces declare is known to the concept
// that gates it.  Each concept reads a closed table or a closed list, so
// this walk is the check that each declared tag has a row.
//
// These four walks stay in this header, and the other checks are in its
// check file.  Each walk calls the lookup of its table or its list in each
// translation unit that includes this header.  fixy/os/AtomPack.h says
// why that call must come before the code of the includer.
static_assert(::fixy::atom_pack::every_tag_in_satisfies<^^::fixy::fs::open_mode,
                                                        [](std::meta::info mode_tag) consteval {
                                                          return ::fixy::atom_pack::has_row(open_mode_table, mode_tag);
                                                        }>(),
              "fixy/os/Fs.h: a tag in fixy::fs::open_mode has no row in open_mode_table.");
static_assert(::fixy::atom_pack::every_tag_in_satisfies<^^::fixy::fs::open_mode,
                                                        [](std::meta::info mode_tag) consteval {
                                                          return ::fixy::atom_pack::has_row(open_mode_rights_table,
                                                                                            mode_tag);
                                                        }>(),
              "fixy/os/Fs.h: a tag in fixy::fs::open_mode has no row in open_mode_rights_table.");
static_assert(::fixy::atom_pack::every_tag_in_satisfies<^^::fixy::fs::flag,
                                                        [](std::meta::info flag_tag) consteval {
                                                          return ::fixy::atom_pack::has_row(flag_table, flag_tag);
                                                        }>(),
              "fixy/os/Fs.h: a tag in fixy::fs::flag has no row in flag_table.");
static_assert(::fixy::atom_pack::every_tag_in_satisfies<^^::fixy::fs::sync_op,
                                                        [](std::meta::info sync_tag) consteval {
                                                          return ::fixy::atom_pack::names_tag(sync_op_tags, sync_tag);
                                                        }>(),
              "fixy/os/Fs.h: a tag in fixy::fs::sync_op is missing from sync_op_tags.");
static_assert(::fixy::atom_pack::every_tag_in_satisfies<^^::fixy::fs::atomicity,
                                                        [](std::meta::info commit_tag) consteval {
                                                          return ::fixy::atom_pack::names_tag(atomicity_tags,
                                                                                              commit_tag);
                                                        }>(),
              "fixy/os/Fs.h: a tag in fixy::fs::atomicity is missing from atomicity_tags.");

}  // namespace fixy::fs::detail::fs_surface_invariants

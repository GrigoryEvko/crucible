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
//  4. The descriptor handle OwnedFd is in fixy/os/Fd.h, with the gate
//     CtxAdmitsFs.  It has a private constructor, and three door classes
//     make the ::open and ::socket calls: FileDoor here, SocketDoor in
//     fixy/os/Socket.h and PtpDeviceDoor in fixy/os/Time.h.  The members
//     of each door are private, and its friends are the gated mints that
//     use it.  So a descriptor that is owned is a descriptor the kernel
//     handed out to a gated mint.
//
//  5. open_dirfd takes a context and a sanitized path, so the path
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
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/os/Errno.h>
#include <foundation/os/OpenFlags.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>
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
// kernel in its Result.  A read or a write that a signal interrupts starts
// again, and each other error returns.  A descriptor below zero, the
// descriptor of a closed handle, gives EBADF and makes no system call.

// Flushes the data of DESCRIPTOR, with the metadata that a read of the data
// needs (fdatasync).
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sync_data_(int descriptor) noexcept;

// Flushes the data and all the metadata of DESCRIPTOR (fsync).  On a
// descriptor of a directory, the flush writes the entries of the directory.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sync_all_(int descriptor) noexcept;

// Renames FROM to TO, and replaces a file at TO (renameat).
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
rename_replacing_(char const* from, char const* to) noexcept;

// Renames FROM to TO, and gives eexist when a file is at TO (renameat2 with
// RENAME_NOREPLACE).  A file system with no such rename gives einval, and a
// kernel older than renameat2 gives enosys.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
rename_no_replace_(char const* from, char const* to) noexcept;

// Reads into the COUNT bytes at FIRST until they are full or the file ends,
// and gives the count of the bytes that it read.
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
read_full_(int descriptor, std::byte* first, std::size_t count) noexcept;

// Writes the COUNT bytes at FIRST.
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
write_full_(int descriptor, std::byte const* first, std::size_t count) noexcept;

// The size in bytes of the file of DESCRIPTOR (fstat).
[[nodiscard]] ::foundation::core::Result<std::int64_t, ::foundation::os::Errno> file_size_(int descriptor) noexcept;

}  // namespace detail

class FileDoor;

// A descriptor that is known to be a directory, opened O_DIRECTORY and
// O_NOFOLLOW.  sync<FsyncParentDir> takes this rather than any OwnedFd,
// because fsync on a directory descriptor is what flushes an entry, and
// on a file descriptor it is a different operation wearing the same
// name.
//
// The constructor is private and FileDoor is its sole friend: an OwnedFd
// is evidence of a descriptor the caller owns, not of a directory, so
// consuming one is not enough.  The door opens the directory, and
// open_dirfd is the gated caller of the door.  A Result of a Dirfd needs
// the complete class, so the door, which comes after the class, gives it.
class [[nodiscard]] Dirfd {
    OwnedFd fd_;

    explicit Dirfd(OwnedFd&& fd) noexcept : fd_{std::move(fd)} {}

    friend class FileDoor;

public:
    Dirfd() noexcept = default;

    Dirfd(const Dirfd&) = delete("Dirfd holds a descriptor; copy would double-close");
    Dirfd& operator=(const Dirfd&) = delete("Dirfd holds a descriptor; copy would double-close");
    Dirfd(Dirfd&&) noexcept = default;
    Dirfd& operator=(Dirfd&&) noexcept = default;
    ~Dirfd() noexcept = default;

    [[nodiscard]] int get() const noexcept { return fd_.get(); }
    [[nodiscard]] bool is_open() const noexcept { return fd_.is_open(); }
    [[nodiscard]] const OwnedFd& handle() const noexcept { return fd_; }
};

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

// O_CLOEXEC is folded in unconditionally.  A descriptor that survives
// execve leaks into every child process.
template <typename... Atoms>
[[nodiscard]] consteval int fold_open_flags() noexcept {
    int acc = ::foundation::os::kOpenCloseOnExec;
    ((acc |= atom_open_flags_v<Atoms>), ...);
    return acc;
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

// The first clause reads the row off the pack, and the mode clause asks
// for exactly one mode atom.
template <typename Ctx, typename... Atoms>
concept CtxFitsFileMint = ::fixy::atom_pack::CtxAdmitsAtomRow<Ctx, Atoms...> && detail::all_atom_tags_known_v<Atoms...>
                       && ::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::fs::mode, Atoms...>;

// sync_op::None is refused.  A caller that needs no durability does not
// call sync at all, and a tag with no syscall behind it must not reach
// the switch below and fall out of it.
template <typename Ctx, typename SyncOp>
concept CtxFitsSync = CtxAdmitsFs<Ctx> && KnownSyncOp<SyncOp> && !std::is_same_v<SyncOp, sync_op::None>;

// atomicity::None is refused.  It renames nothing, so a success from it
// would say that a commit was made when none was.  A caller that needs
// no commit does not call commit_atomic.
template <typename Ctx, typename Atomicity>
concept CtxFitsCommitAtomic =
    CtxAdmitsFs<Ctx> && KnownAtomicity<Atomicity> && !std::is_same_v<Atomicity, atomicity::None>;

// The Atoms pack precedes Ctx because every explicit template argument
// fills the pack and Ctx deduces from the first function argument.
//
// Declared here and defined below FileDoor, because FileDoor names it as
// a friend, and a friend must already have been declared.  The default
// argument belongs to this declaration.
template <typename... Atoms, eff::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, Atoms...>
[[nodiscard]] inline ::foundation::core::Result<Linear<OwnedFd>, ::foundation::os::Errno>
mint_file(Ctx const&, Path<tags::source::Sanitized> sanitized_path, ::mode_t perms = 0644) noexcept;

// Declared here and defined below FileDoor for the same reason.
template <eff::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] ::foundation::core::Result<Dirfd, ::foundation::os::Errno>
open_dirfd(Ctx const&, Path<tags::source::Sanitized> dir) noexcept;

// The door to the open of a path.  No object of it exists.  Its members
// are private, and its two friends are the gated mints that open a path,
// mint_file and open_dirfd.  OwnedFd and Dirfd befriend this class, so a
// handle over a path comes only from one of those two mints, after their
// gates.
//
// The trailing return types are necessary: fixy/os/CpuPinned.h gives the
// parse reason.
class FileDoor final : ::foundation::NoObject<FileDoor> {
    template <typename... FriendAtoms, eff::IsExecCtx FriendCtx>
        requires CtxFitsFileMint<FriendCtx, FriendAtoms...>
    friend auto mint_file(FriendCtx const&, Path<tags::source::Sanitized>, ::mode_t) noexcept
        -> ::foundation::core::Result<Linear<OwnedFd>, ::foundation::os::Errno>;

    template <eff::IsExecCtx FriendCtx>
        requires ::fixy::fs::CtxAdmitsFs<FriendCtx>
    friend auto open_dirfd(FriendCtx const&, Path<tags::source::Sanitized>) noexcept
        -> ::foundation::core::Result<Dirfd, ::foundation::os::Errno>;

    // The handle of a descriptor that the open gave, or the error of the
    // open.
    [[nodiscard]] static ::foundation::core::Result<OwnedFd, ::foundation::os::Errno>
    own_(::foundation::core::Result<int, ::foundation::os::Errno> opened) noexcept {
        return std::move(opened).match(
            [](int descriptor) -> ::foundation::core::Result<OwnedFd, ::foundation::os::Errno> {
                return OwnedFd{descriptor};
            },
            [](::foundation::os::Errno error) -> ::foundation::core::Result<OwnedFd, ::foundation::os::Errno> {
                return ::foundation::core::err(error);
            });
    }

    // The open reads perms only when flags holds O_CREAT and ignores it
    // otherwise, so it is passed in each case.
    [[nodiscard]] static ::foundation::core::Result<OwnedFd, ::foundation::os::Errno>
    open_path_(const char* path, int flags, ::mode_t perms) noexcept {
        return own_(detail::open_descriptor_(path, flags, perms));
    }

    // O_DIRECTORY refuses anything else, O_NOFOLLOW refuses a symlink
    // that stands in for the directory, and the descriptor is what a later
    // fsync flushes the entry through.
    [[nodiscard]] static ::foundation::core::Result<Dirfd, ::foundation::os::Errno>
    open_directory_(const char* dir_path) noexcept {
        using Opened = ::foundation::core::Result<Dirfd, ::foundation::os::Errno>;
        return own_(detail::open_descriptor_(dir_path,
                                             ::foundation::os::kOpenDirectory | ::foundation::os::kOpenNoFollow
                                                 | ::foundation::os::kOpenCloseOnExec | ::foundation::os::kOpenReadOnly,
                                             0))
            .match([](OwnedFd descriptor) -> Opened { return Dirfd{std::move(descriptor)}; },
                   [](::foundation::os::Errno error) -> Opened { return ::foundation::core::err(error); });
    }
};

template <typename... Atoms, eff::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, Atoms...>
[[nodiscard]] inline ::foundation::core::Result<Linear<OwnedFd>, ::foundation::os::Errno>
mint_file(Ctx const&, Path<tags::source::Sanitized> sanitized_path, ::mode_t perms) noexcept {
    using Minted = ::foundation::core::Result<Linear<OwnedFd>, ::foundation::os::Errno>;
    constexpr int flags = detail::fold_open_flags<Atoms...>();
    return FileDoor::open_path_(sanitized_path.value().c_str(), flags, perms)
        .match([](OwnedFd descriptor) -> Minted { return mint_linear<OwnedFd>(std::move(descriptor)); },
               [](::foundation::os::Errno error) -> Minted { return ::foundation::core::err(error); });
}

// Not a mint: this acts on an existing handle and synthesizes nothing.
template <typename SyncOp, eff::IsExecCtx Ctx>
    requires CtxFitsSync<Ctx, SyncOp>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sync(Ctx const&, const OwnedFd& handle) noexcept {
    // Fsync, and FsyncParentDir on a directory descriptor, which is the call
    // that flushes the directory entry.  The door gives EBADF for a closed
    // handle.
    return std::is_same_v<SyncOp, sync_op::Fdatasync> ? detail::sync_data_(handle.get())
                                                      : detail::sync_all_(handle.get());
}

// FsyncParentDir takes the directory handle by its own type, so a file
// descriptor cannot be handed in where an entry flush was meant.
template <typename SyncOp, eff::IsExecCtx Ctx>
    requires CtxFitsSync<Ctx, SyncOp> && std::is_same_v<SyncOp, sync_op::FsyncParentDir>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
sync(Ctx const& ctx, const Dirfd& dir) noexcept {
    return sync<SyncOp>(ctx, dir.handle());
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
commit_atomic(Ctx const&, Path<tags::source::Sanitized> tmp, Path<tags::source::Sanitized> target) noexcept {
    return std::is_same_v<Atomicity, atomicity::Rename>
             ? detail::rename_replacing_(tmp.value().c_str(), target.value().c_str())
             : detail::rename_no_replace_(tmp.value().c_str(), target.value().c_str());
}

template <eff::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] ::foundation::core::Result<Dirfd, ::foundation::os::Errno>
open_dirfd(Ctx const&, Path<tags::source::Sanitized> dir) noexcept {
    return FileDoor::open_directory_(dir.value().c_str());
}

// The three calls below read and write through a handle that is open.
// A signal that interrupts a read or a write restarts it, and every other
// error returns.  read_full stops at the end of the file, so it returns
// fewer bytes than the span holds only there.
//
// Each takes an OwnedFd, and the context that the other calls in this
// header take, because each call can park the caller on the disk.
//
// Not mints: these act on an existing handle and synthesize nothing.
template <eff::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] inline ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
read_full(Ctx const&, const OwnedFd& handle, std::span<std::byte> buffer) noexcept {
    // The door gives EBADF for a closed handle.
    return detail::read_full_(handle.get(), buffer.data(), buffer.size());
}

template <eff::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
write_full(Ctx const&, const OwnedFd& handle, std::span<const std::byte> buffer) noexcept {
    // The door gives EBADF for a closed handle.
    return detail::write_full_(handle.get(), buffer.data(), buffer.size());
}

template <eff::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] inline ::foundation::core::Result<std::int64_t, ::foundation::os::Errno>
file_size(Ctx const&, const OwnedFd& handle) noexcept {
    // The door gives EBADF for a closed handle.
    return detail::file_size_(handle.get());
}

// The read-only open, spelled once.
using read_only = ::fixy::atom::fs::mode<open_mode::ReadOnly>;

// The durable and atomic_write atoms only declare intent.  The mint
// opens the file.  The caller still calls sync<Fsync> after writing and
// then commit_atomic<Rename> to move the result into place.
template <eff::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, ::fixy::atom::fs::mode<open_mode::WriteTruncate>,
                             ::fixy::atom::fs::durable<sync_op::Fsync>,
                             ::fixy::atom::fs::atomic_write<atomicity::Rename>>
[[nodiscard]] inline ::foundation::core::Result<Linear<OwnedFd>, ::foundation::os::Errno>
mint_durable_truncate_file(Ctx const& ctx, Path<tags::source::Sanitized> path, ::mode_t perms = 0644) noexcept {
    return mint_file<::fixy::atom::fs::mode<open_mode::WriteTruncate>, ::fixy::atom::fs::durable<sync_op::Fsync>,
                     ::fixy::atom::fs::atomic_write<atomicity::Rename>>(ctx, std::move(path), perms);
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
[[nodiscard]] inline ::foundation::core::Result<Linear<OwnedFd>, ::foundation::os::Errno>
mint_durable_append_file(Ctx const& ctx, Path<tags::source::Sanitized> path, ::mode_t perms = 0644) noexcept {
    return mint_file<::fixy::atom::fs::mode<open_mode::WriteAppend>, ::fixy::atom::fs::durable<sync_op::Fdatasync>,
                     ::fixy::atom::fs::with_flag<flag::DataSync>>(ctx, std::move(path), perms);
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

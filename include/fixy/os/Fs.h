#pragma once

// The file syscalls: open, the two syncs, the atomic commit, and the
// directory descriptor that flushes an entry.
//
// An atom pack says how a file is opened — its mode, its flags, and the
// durability and commit it is opened FOR — and the pack is read twice.
// Once for the O_* word the syscall takes, and once for the effect row
// the calling context has to admit.
//
// Old spelling: include/crucible/fixy/Fs.h and src/fixy/Fs.cpp.  The
// five syscall bodies lived in that translation unit so the templates
// would share one wrapper; they are inline here because fixy is
// header-only and the old unit is frozen.  Each body is a few lines
// around one call, so the instantiation cost the split was for is not
// worth a second file.
//
// Deviations, each deliberate:
//
//  1. Grants became atoms.  fixy::atom::fs::{mode, with_flag, durable,
//     atomic_write} carry the axis and the row, so the which_dim
//     specializations are gone with the grant system that needed them.
//
//  2. The context gate reads the row off the pack rather than naming IO
//     and Block by hand.  Same answer today, pinned below.
//
//  3. Six tags did not come across, because fixy/atoms/Os.h ported only
//     the tags that reach an operation that works.  open_mode::TmpFile
//     was a placeholder the open gate refused.  flag::{Directory,
//     NonBlock, Path} folded bits nothing gated or consumed.
//     sync_op::Msync was a hard EINVAL.  atomicity::LinkAtomic was a
//     hard ENOSYS — and cold_writer_stance pinned it, so every cold
//     commit in the old tree failed before a filesystem was consulted.
//     That is the ENOSYS commit path, and it is closed by not existing:
//     fixy/os/CipherDurable.h pins Rename, and mint_durable_truncate_file
//     below hands out atomic_write<Rename> where it handed out the one
//     atomicity that could not succeed.
//
//  4. The bit maps have fail-open primaries — an unknown mode folds to
//     O_RDONLY, an unknown flag to nothing — so the gates read known-tag
//     predicates instead, and walks over the four fixy::fs namespaces
//     check the hand lists cover every tag declared.
//
//  5. The descriptor handle.  The old tree returned its FileHandle,
//     whose constructor took an int and whose destructor closed it: the
//     shape three doors this cycle closed.  OwnedFd below has a private constructor and two static
//     factories that perform the ::open themselves, so a descriptor that
//     is owned is a descriptor the kernel handed out.  Nothing to forge.
//
//  6. open_dirfd takes a context and a sanitized path.  It took a bare
//     std::filesystem::path and no context, which made it the one call
//     in the family that neither the path discipline nor the effect row
//     reached, and its ::open carried no marker.
//
//  7. The two durable mints carry a requires-clause of their own rather
//     than relying on the one inside mint_file.  A constraint failure a
//     layer down is a hard error at the call site, and the guard flagged
//     the missing clause as a defect the old tree allowlisted.

#include <fixy/Path.h>
#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <expected>
#include <meta>
#include <system_error>
#include <type_traits>
#include <utility>

namespace fixy::fs {

namespace eff = ::foundation::effects;

template <typename Mode>
struct open_mode_flags : std::integral_constant<int, 0> {};
template <>
struct open_mode_flags<open_mode::ReadOnly> : std::integral_constant<int, O_RDONLY> {};
template <>
struct open_mode_flags<open_mode::WriteCreate> : std::integral_constant<int, O_WRONLY | O_CREAT> {};
template <>
struct open_mode_flags<open_mode::WriteAppend> : std::integral_constant<int, O_WRONLY | O_CREAT | O_APPEND> {};
template <>
struct open_mode_flags<open_mode::WriteTruncate> : std::integral_constant<int, O_WRONLY | O_CREAT | O_TRUNC> {};
template <>
struct open_mode_flags<open_mode::ReadWrite> : std::integral_constant<int, O_RDWR | O_CREAT> {};

template <typename Mode>
inline constexpr int open_mode_flags_v = open_mode_flags<Mode>::value;

template <typename Flag>
struct flag_bits : std::integral_constant<int, 0> {};
template <>
struct flag_bits<flag::CloseOnExec> : std::integral_constant<int, O_CLOEXEC> {};
template <>
struct flag_bits<flag::NoFollow> : std::integral_constant<int, O_NOFOLLOW> {};
template <>
struct flag_bits<flag::DataSync> : std::integral_constant<int, O_DSYNC> {};
template <>
struct flag_bits<flag::FullSync> : std::integral_constant<int, O_SYNC> {};
template <>
struct flag_bits<flag::Direct> : std::integral_constant<int, O_DIRECT> {};

template <typename Flag>
inline constexpr int flag_bits_v = flag_bits<Flag>::value;

// The bit maps answer zero for a tag they do not know, and zero is a
// value the kernel accepts: O_RDONLY is zero, so an unmapped mode opens
// for reading, and an unmapped flag opens without the flag the caller
// asked for.  These four are what the gates read instead of the bits.
// They are hand lists, and the walks at the foot of this header read
// the four fixy::fs namespaces and fail if a declared tag is missing.
template <typename Mode>
inline constexpr bool is_known_open_mode_v =
    std::is_same_v<Mode, open_mode::ReadOnly> || std::is_same_v<Mode, open_mode::WriteCreate>
    || std::is_same_v<Mode, open_mode::WriteAppend> || std::is_same_v<Mode, open_mode::WriteTruncate>
    || std::is_same_v<Mode, open_mode::ReadWrite>;

template <typename Flag>
inline constexpr bool is_known_flag_v = std::is_same_v<Flag, flag::CloseOnExec> || std::is_same_v<Flag, flag::NoFollow>
                                     || std::is_same_v<Flag, flag::DataSync> || std::is_same_v<Flag, flag::FullSync>
                                     || std::is_same_v<Flag, flag::Direct>;

template <typename SyncOp>
inline constexpr bool is_known_sync_op_v =
    std::is_same_v<SyncOp, sync_op::None> || std::is_same_v<SyncOp, sync_op::Fdatasync>
    || std::is_same_v<SyncOp, sync_op::Fsync> || std::is_same_v<SyncOp, sync_op::FsyncParentDir>;

template <typename Atomicity>
inline constexpr bool is_known_atomicity_v = std::is_same_v<Atomicity, atomicity::None>
                                          || std::is_same_v<Atomicity, atomicity::Rename>
                                          || std::is_same_v<Atomicity, atomicity::RenameAt2NoReplace>;

// Exclusive ownership of one descriptor, closed on destruction.
//
// The constructor that claims a descriptor is private.  A public one
// took an int and the destructor closed it, so a caller could hand it
// any small integer — stdin, a descriptor another object still owns —
// and have it closed on scope exit.  The two factories perform the
// ::open themselves and build a handle only from what the kernel
// returned.  Same shape as OwnedMmap::map_region, for the same reason.
class [[nodiscard]] OwnedFd {
    int fd_ = -1;

    explicit OwnedFd(int fd) noexcept : fd_{fd} {}

public:
    // The empty handle owns nothing and closes nothing, so it stays
    // public: it claims no descriptor.
    OwnedFd() noexcept = default;

    // The one door for a file.  ::open consults perms only when flags
    // carries O_CREAT and ignores it otherwise, so it is passed
    // unconditionally.  Returns the errno on failure and no handle.
    [[nodiscard]] static std::expected<OwnedFd, int> open_path(const char* path, int flags, ::mode_t perms) noexcept {
        const int fd = ::open(path, flags, perms);  // SYSCALL-CAP-OK: OwnedFd::open_path, sole caller mint_file ctx-gate (CtxFitsFileMint)
        if (fd < 0) {
            return std::unexpected{errno};
        }
        return OwnedFd{fd};
    }

    // The one door for a directory.  O_DIRECTORY refuses anything else,
    // O_NOFOLLOW refuses a symlink standing in for it, and the
    // descriptor is what a later fsync flushes the entry through.
    [[nodiscard]] static std::expected<OwnedFd, int> open_directory(const char* dir_path) noexcept {
        const int fd = ::open(dir_path, O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_RDONLY);  // SYSCALL-CAP-OK: OwnedFd::open_directory, sole caller open_dirfd ctx-gate (CtxAdmitsFs)
        if (fd < 0) {
            return std::unexpected{errno};
        }
        return OwnedFd{fd};
    }

    OwnedFd(const OwnedFd&) = delete("a descriptor is unique; copy would double-close on destruction");
    OwnedFd& operator=(const OwnedFd&) = delete("a descriptor is unique; copy would double-close on destruction");

    OwnedFd(OwnedFd&& other) noexcept : fd_{std::exchange(other.fd_, -1)} {}

    OwnedFd& operator=(OwnedFd&& other) noexcept {
        if (this != &other) {
            close_();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    ~OwnedFd() noexcept { close_(); }

    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

    // A borrow.  Ownership stays here, so the caller must not close what
    // this returns.
    [[nodiscard]] int get() const noexcept { return fd_; }

private:
    void close_() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);  // SYSCALL-CAP-OK: OwnedFd destructor, releases what a factory acquired
            fd_ = -1;
        }
    }
};

class Dirfd;

// Every call here can park the caller until the disk answers, so the
// context must admit IO and Block.  This is the gate for the three
// operations that take no atom pack.
template <typename Ctx>
concept CtxAdmitsFs = eff::CtxOwnsAllOf<Ctx, eff::Effect::IO, eff::Effect::Block>;

// Declared before Dirfd so the class can name it as the sole friend of
// its only constructor; defined after it.
template <eff::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] std::expected<Dirfd, std::error_code> open_dirfd(Ctx const&,
                                                              Path<tags::source::Sanitized> dir) noexcept;

// A descriptor that is known to be a directory, opened O_DIRECTORY and
// O_NOFOLLOW.  sync<FsyncParentDir> takes this rather than any OwnedFd,
// because fsync on a directory descriptor is what flushes an entry, and
// on a file descriptor it is a different operation wearing the same
// name.
//
// The constructor is private and open_dirfd is its sole friend: an
// OwnedFd is evidence of a descriptor the caller owns, not of a
// directory, so consuming one is not enough.
class [[nodiscard]] Dirfd {
    OwnedFd fd_;

    explicit Dirfd(OwnedFd&& fd) noexcept : fd_{std::move(fd)} {}

    // Trailing return type on purpose: the leading form ends in `>` and
    // the parser takes `>::` as a nested-name-specifier.  Documented at
    // fixy/os/CpuPinned.h.
    template <eff::IsExecCtx FriendCtx>
        requires ::fixy::fs::CtxAdmitsFs<FriendCtx>
    friend auto open_dirfd(FriendCtx const&, Path<tags::source::Sanitized>) noexcept
        -> std::expected<Dirfd, std::error_code>;

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
inline constexpr bool is_mode_atom_v = ::foundation::reflect::is_instance_of_v<A, ^^::fixy::atom::fs::mode>;
template <typename A>
inline constexpr bool is_flag_atom_v = ::foundation::reflect::is_instance_of_v<A, ^^::fixy::atom::fs::with_flag>;
template <typename A>
inline constexpr bool is_durable_atom_v = ::foundation::reflect::is_instance_of_v<A, ^^::fixy::atom::fs::durable>;
template <typename A>
inline constexpr bool is_atomic_write_atom_v =
    ::foundation::reflect::is_instance_of_v<A, ^^::fixy::atom::fs::atomic_write>;

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
template <typename A>
inline constexpr int atom_open_flags_v = is_mode_atom_v<A>   ? open_mode_flags_v<extract_mode_t<A>>
                                       : is_flag_atom_v<A> ? flag_bits_v<extract_flag_t<A>>
                                                           : 0;

// O_CLOEXEC is folded in unconditionally.  A descriptor that survives
// execve leaks into every child process.
template <typename... Atoms>
[[nodiscard]] consteval int fold_open_flags() noexcept {
    int acc = O_CLOEXEC;
    ((acc |= atom_open_flags_v<Atoms>), ...);
    return acc;
}

template <typename... Atoms>
inline constexpr bool has_mode_v = (is_mode_atom_v<Atoms> || ...);

template <typename... Atoms>
inline constexpr bool has_duplicate_mode_v = (static_cast<int>(is_mode_atom_v<Atoms>) + ... + 0) > 1;

// An atom of another kind answers true, so the fold is a conjunction
// over the whole pack.
template <typename A>
inline constexpr bool atom_tag_is_known_v =
    (!is_mode_atom_v<A> || is_known_open_mode_v<extract_mode_t<A>>)
    && (!is_flag_atom_v<A> || is_known_flag_v<extract_flag_t<A>>)
    && (!is_durable_atom_v<A> || is_known_sync_op_v<extract_sync_op_t<A>>)
    && (!is_atomic_write_atom_v<A> || is_known_atomicity_v<extract_atomicity_t<A>>);

template <typename... Atoms>
inline constexpr bool all_atom_tags_known_v = (atom_tag_is_known_v<Atoms> && ... && true);

// The row a pack exercises is the union of the rows its atoms lift to.
template <typename... Atoms>
struct atoms_row {
    using type = eff::Row<>;
};
template <typename First, typename... Rest>
struct atoms_row<First, Rest...> {
    using type = eff::row_union_t<eff::lift_row_t<std::remove_cvref_t<First>>, typename atoms_row<Rest...>::type>;
};
template <typename... Atoms>
using atoms_row_t = typename atoms_row<Atoms...>::type;

}  // namespace detail

template <typename Ctx, typename... Atoms>
concept CtxAdmitsAtomRow = eff::IsExecCtx<Ctx> && (eff::LiftsToRow<std::remove_cvref_t<Atoms>> && ...)
                        && eff::CtxAdmits<Ctx, detail::atoms_row_t<Atoms...>>;

template <typename Ctx, typename... Atoms>
concept CtxFitsFileMint = CtxAdmitsAtomRow<Ctx, Atoms...> && detail::all_atom_tags_known_v<Atoms...>
                       && detail::has_mode_v<Atoms...> && !detail::has_duplicate_mode_v<Atoms...>;

// sync_op::None is refused.  A caller that needs no durability does not
// call sync at all, and a tag with no syscall behind it must not reach
// the switch below and fall out of it.
template <typename Ctx, typename SyncOp>
concept CtxFitsSync = CtxAdmitsFs<Ctx> && is_known_sync_op_v<SyncOp> && !std::is_same_v<SyncOp, sync_op::None>;

template <typename Ctx, typename Atomicity>
concept CtxFitsCommitAtomic = CtxAdmitsFs<Ctx> && is_known_atomicity_v<Atomicity>;

// The Atoms pack precedes Ctx because every explicit template argument
// fills the pack and Ctx deduces from the first function argument.
//
// §XXI carve-out: cx=alloc — opening a file invokes the kernel.
template <typename... Atoms, eff::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, Atoms...>
[[nodiscard]] inline std::expected<Linear<OwnedFd>, std::error_code>
mint_file(Ctx const&, Path<tags::source::Sanitized> sanitized_path, ::mode_t perms = 0644) noexcept {
    constexpr int flags = detail::fold_open_flags<Atoms...>();
    auto fd = OwnedFd::open_path(sanitized_path.value().c_str(), flags, perms);
    if (!fd) {
        return std::unexpected{std::error_code{fd.error(), std::system_category()}};
    }
    return mint_linear<OwnedFd>(std::move(*fd));
}

// Not a mint: this acts on an existing handle and synthesizes nothing.
template <typename SyncOp, eff::IsExecCtx Ctx>
    requires CtxFitsSync<Ctx, SyncOp>
[[nodiscard]] inline std::expected<void, std::error_code> sync(Ctx const&, const OwnedFd& handle) noexcept {
    if (!handle.is_open()) {
        return std::unexpected{std::error_code{EBADF, std::system_category()}};
    }
    int rc = 0;
    if constexpr (std::is_same_v<SyncOp, sync_op::Fdatasync>) {
        rc = ::fdatasync(handle.get());  // SYSCALL-CAP-OK: sync<SyncOp> ctx-gate (CtxFitsSync)
    } else {
        // Fsync, and FsyncParentDir on a directory descriptor, which is
        // the call that flushes the directory entry.
        rc = ::fsync(handle.get());  // SYSCALL-CAP-OK: sync<SyncOp> ctx-gate (CtxFitsSync)
    }
    if (rc < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

// FsyncParentDir takes the directory handle by its own type, so a file
// descriptor cannot be handed in where an entry flush was meant.
template <typename SyncOp, eff::IsExecCtx Ctx>
    requires CtxFitsSync<Ctx, SyncOp> && std::is_same_v<SyncOp, sync_op::FsyncParentDir>
[[nodiscard]] inline std::expected<void, std::error_code> sync(Ctx const& ctx, const Dirfd& dir) noexcept {
    return sync<SyncOp>(ctx, dir.handle());
}

// Rename uses ::rename, where the last writer wins.  RenameAt2NoReplace
// uses renameat2(RENAME_NOREPLACE), which fails with EEXIST when the
// target exists instead of overwriting it.  A filesystem without that
// flag reports EINVAL, and that is passed through rather than replaced
// by a plain rename: the caller chose the no-replace semantics, and a
// silent fallback would be a commit that overwrote what it promised not
// to.  None succeeds without a syscall.
//
// Not a mint: this renames existing paths and synthesizes nothing.
template <typename Atomicity, eff::IsExecCtx Ctx>
    requires CtxFitsCommitAtomic<Ctx, Atomicity>
[[nodiscard]] inline std::expected<void, std::error_code>
commit_atomic(Ctx const&, Path<tags::source::Sanitized> tmp, Path<tags::source::Sanitized> target) noexcept {
    int rc = 0;
    if constexpr (std::is_same_v<Atomicity, atomicity::None>) {
        return {};
    } else if constexpr (std::is_same_v<Atomicity, atomicity::Rename>) {
        rc = ::rename(tmp.value().c_str(),
                      target.value().c_str());  // SYSCALL-CAP-OK: commit_atomic<Atomicity> ctx-gate (CtxFitsCommitAtomic)
    } else {
        rc = ::renameat2(AT_FDCWD, tmp.value().c_str(), AT_FDCWD, target.value().c_str(),
                         RENAME_NOREPLACE);  // SYSCALL-CAP-OK: commit_atomic<Atomicity> ctx-gate (CtxFitsCommitAtomic)
    }
    if (rc < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

template <eff::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] std::expected<Dirfd, std::error_code> open_dirfd(Ctx const&, Path<tags::source::Sanitized> dir) noexcept {
    auto fd = OwnedFd::open_directory(dir.value().c_str());
    if (!fd) {
        return std::unexpected{std::error_code{fd.error(), std::system_category()}};
    }
    return Dirfd{std::move(*fd)};
}

// The read-only open, spelled once.  The old header exported the same
// alias over its grant, and include/crucible/fixy/Wrap.h re-exports it,
// so a port that dropped it would leave that re-export with no target at
// Stage C.
using read_only = ::fixy::atom::fs::mode<open_mode::ReadOnly>;

// The durable and atomic_write atoms only declare intent.  The mint
// opens the file.  The caller still calls sync<Fsync> after writing and
// then commit_atomic<Rename> to move the result into place.
//
// This handed out atomic_write<LinkAtomic> and told the caller to commit
// through it, and LinkAtomic was a hard ENOSYS: the canonical durable
// open named the one commit that could not succeed.
//
// §XXI carve-out: cx=alloc — opening a file invokes the kernel.
template <eff::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, ::fixy::atom::fs::mode<open_mode::WriteTruncate>,
                             ::fixy::atom::fs::durable<sync_op::Fsync>, ::fixy::atom::fs::atomic_write<atomicity::Rename>>
[[nodiscard]] inline std::expected<Linear<OwnedFd>, std::error_code>
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
//
// §XXI carve-out: cx=alloc — opening a file invokes the kernel.
template <eff::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, ::fixy::atom::fs::mode<open_mode::WriteAppend>,
                             ::fixy::atom::fs::durable<sync_op::Fdatasync>, ::fixy::atom::fs::with_flag<flag::DataSync>>
[[nodiscard]] inline std::expected<Linear<OwnedFd>, std::error_code>
mint_durable_append_file(Ctx const& ctx, Path<tags::source::Sanitized> path, ::mode_t perms = 0644) noexcept {
    return mint_file<::fixy::atom::fs::mode<open_mode::WriteAppend>, ::fixy::atom::fs::durable<sync_op::Fdatasync>,
                     ::fixy::atom::fs::with_flag<flag::DataSync>>(ctx, std::move(path), perms);
}

}  // namespace fixy::fs

namespace fixy::fs::detail::fs_surface_invariants {

static_assert(open_mode_flags_v<open_mode::ReadOnly> == O_RDONLY);
static_assert(open_mode_flags_v<open_mode::WriteTruncate> == (O_WRONLY | O_CREAT | O_TRUNC));
static_assert(open_mode_flags_v<open_mode::WriteAppend> == (O_WRONLY | O_CREAT | O_APPEND));
static_assert(flag_bits_v<flag::CloseOnExec> == O_CLOEXEC);
static_assert(flag_bits_v<flag::NoFollow> == O_NOFOLLOW);
static_assert(flag_bits_v<flag::FullSync> == O_SYNC);

using A_RO = ::fixy::atom::fs::mode<open_mode::ReadOnly>;
using A_Trunc = ::fixy::atom::fs::mode<open_mode::WriteTruncate>;
using A_NoFollow = ::fixy::atom::fs::with_flag<flag::NoFollow>;
using A_Fsync = ::fixy::atom::fs::durable<sync_op::Fsync>;
using A_Rename = ::fixy::atom::fs::atomic_write<atomicity::Rename>;

static_assert(has_mode_v<A_RO>);
static_assert(!has_mode_v<A_NoFollow>);
static_assert(!has_mode_v<>);
static_assert(has_duplicate_mode_v<A_RO, A_Trunc>);
static_assert(!has_duplicate_mode_v<A_RO, A_NoFollow>);

static_assert(fold_open_flags<A_RO>() == (O_RDONLY | O_CLOEXEC));
static_assert(fold_open_flags<A_Trunc, A_NoFollow>() == (O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC));
static_assert(fold_open_flags<A_Trunc, A_Fsync, A_Rename>() == (O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC),
              "durable and atomic_write atoms declare intent and add no open flag.");

// The bit maps answer zero for a tag they do not know, and for a mode
// zero is O_RDONLY: an unmapped mode would open for reading with no
// diagnostic.  The gate reads the known-tag predicates instead.
struct NotAMode final {};
struct NotAFlag final {};
static_assert(open_mode_flags_v<NotAMode> == O_RDONLY, "an unmapped mode folds to O_RDONLY, which the kernel accepts.");
static_assert(!is_known_open_mode_v<NotAMode>);
static_assert(!is_known_flag_v<NotAFlag>);
static_assert(all_atom_tags_known_v<A_RO, A_NoFollow, A_Fsync, A_Rename>);
static_assert(!all_atom_tags_known_v<::fixy::atom::fs::mode<NotAMode>>);
static_assert(!all_atom_tags_known_v<A_RO, ::fixy::atom::fs::with_flag<NotAFlag>>);

// The derived row and the row the old header named by hand agree, and
// this is the pin on that.
using ExpectedFsRow = eff::Row<eff::Effect::IO, eff::Effect::Block>;
static_assert(std::is_same_v<atoms_row_t<A_RO>, ExpectedFsRow>);
static_assert(std::is_same_v<atoms_row_t<A_Trunc, A_Fsync, A_Rename>, ExpectedFsRow>);

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;

static_assert(CtxFitsFileMint<IoBlockCtx, A_RO>);
static_assert(!CtxFitsFileMint<IoOnlyCtx, A_RO>, "a context without Block must not open a file: open can park.");
static_assert(!CtxFitsFileMint<IoBlockCtx>, "an empty atom pack names no mode.");
static_assert(!CtxFitsFileMint<IoBlockCtx, A_RO, A_Trunc>, "two modes in one pack.");
static_assert(!CtxFitsFileMint<IoBlockCtx, ::fixy::atom::fs::mode<NotAMode>>,
              "a mode with no O_* mapping must be refused, not opened O_RDONLY.");

static_assert(CtxFitsSync<IoBlockCtx, sync_op::Fdatasync>);
static_assert(CtxFitsSync<IoBlockCtx, sync_op::FsyncParentDir>);
static_assert(!CtxFitsSync<IoBlockCtx, sync_op::None>, "None has no syscall behind it.");
static_assert(!CtxFitsSync<IoOnlyCtx, sync_op::Fsync>);
struct NotASyncOp final {};
static_assert(!CtxFitsSync<IoBlockCtx, NotASyncOp>);

static_assert(CtxFitsCommitAtomic<IoBlockCtx, atomicity::Rename>);
static_assert(CtxFitsCommitAtomic<IoBlockCtx, atomicity::RenameAt2NoReplace>);
static_assert(CtxFitsCommitAtomic<IoBlockCtx, atomicity::None>);
static_assert(!CtxFitsCommitAtomic<IoOnlyCtx, atomicity::Rename>);
struct NotAnAtomicity final {};
static_assert(!CtxFitsCommitAtomic<IoBlockCtx, NotAnAtomicity>);

static_assert(CtxAdmitsFs<IoBlockCtx>);
static_assert(!CtxAdmitsFs<IoOnlyCtx>);

// The descriptor handle has one door per kind of descriptor, and the
// constructor that claims an int is private.  Checked from a scope the
// class does not befriend.
static_assert(!std::is_constructible_v<OwnedFd, int>,
              "The constructor over a descriptor must not be public: a caller could hand it any small integer and "
              "the destructor would close it.  Take a handle from mint_file or open_dirfd.");
static_assert(std::is_default_constructible_v<OwnedFd>, "The empty handle claims nothing, so it stays reachable.");
static_assert(!std::is_copy_constructible_v<OwnedFd>);
static_assert(std::is_nothrow_move_constructible_v<OwnedFd>);
static_assert(sizeof(OwnedFd) == sizeof(int), "OwnedFd is exactly the descriptor.");
static_assert(!std::is_constructible_v<Dirfd, OwnedFd&&>,
              "An OwnedFd is evidence of a descriptor, not of a directory; open_dirfd is the only door.");
static_assert(std::is_default_constructible_v<Dirfd>);
static_assert(!std::is_copy_constructible_v<Dirfd>);

// Every tag the four fixy::fs namespaces declare is known to the
// predicate that gates it.  The predicates are hand lists; this is what
// notices a tag they omit.
template <std::meta::info Ns, auto Predicate>
[[nodiscard]] consteval bool every_tag_in_satisfies_() noexcept {
    static constexpr auto members = std::define_static_array(std::meta::members_of(Ns, std::meta::access_context::unchecked()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member) && !std::meta::is_type_alias(member)
                      && std::meta::is_class_type(member)) {
            // A splice cannot stand directly as a template argument, so
            // the type is named first, the shape fixy/atoms/Os.h uses.
            using T = [:member:];
            if (!Predicate.template operator()<T>()) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(every_tag_in_satisfies_<^^::fixy::fs::open_mode, []<typename T>() { return is_known_open_mode_v<T>; }>(),
              "fixy/os/Fs.h: a tag in fixy::fs::open_mode is missing from is_known_open_mode_v.");
static_assert(every_tag_in_satisfies_<^^::fixy::fs::flag, []<typename T>() { return is_known_flag_v<T>; }>(),
              "fixy/os/Fs.h: a tag in fixy::fs::flag is missing from is_known_flag_v.");
static_assert(every_tag_in_satisfies_<^^::fixy::fs::sync_op, []<typename T>() { return is_known_sync_op_v<T>; }>(),
              "fixy/os/Fs.h: a tag in fixy::fs::sync_op is missing from is_known_sync_op_v.");
static_assert(every_tag_in_satisfies_<^^::fixy::fs::atomicity, []<typename T>() { return is_known_atomicity_v<T>; }>(),
              "fixy/os/Fs.h: a tag in fixy::fs::atomicity is missing from is_known_atomicity_v.");

}  // namespace fixy::fs::detail::fs_surface_invariants

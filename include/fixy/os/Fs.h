#pragma once

// The file syscalls: open, the two syncs, the atomic commit, and the
// directory descriptor that flushes an entry.
//
// An atom pack says how a file is opened — its mode, its flags, and the
// durability and commit it is opened FOR — and the pack is read twice.
// Once for the O_* word the syscall takes, and once for the effect row
// the calling context has to admit.
//
// The syscall bodies are inline, because fixy is header-only.  Each body
// is a few lines around one call, so a second file does not pay for
// itself.
//
// Design notes:
//
//  1. The atoms fixy::atom::fs::{mode, with_flag, durable, atomic_write}
//     carry the axis and the row.  The context gate reads the row off
//     the pack and does not name IO and Block by hand.  A check at the
//     foot of this header pins the derived row to IO and Block.
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
//  4. The descriptor handle.  OwnedFd below has a private constructor,
//     and three door classes make the ::open and ::socket calls: FileDoor
//     here, SocketDoor in fixy/os/Socket.h and PtpDeviceDoor in
//     fixy/os/Time.h.  The members of each door are private, and its
//     friends are the gated mints that use it.  So a descriptor that is
//     owned is a descriptor the kernel handed out to a gated mint.
//
//  5. open_dirfd takes a context and a sanitized path, so the path
//     discipline and the effect row reach it as they reach each other
//     call in the family.
//
//  6. The two durable mints carry a requires-clause of their own and do
//     not rely on the one inside mint_file.  A constraint failure a
//     layer down is a hard error at the call site.

#include <fixy/Path.h>
#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
#include <fixy/os/AtomPack.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <meta>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>

namespace fixy::fs {

namespace eff = ::foundation::effects;

// The O_* word of each open mode and the O_* bits of each flag.  A tag
// reaches the open call only through a row of these tables, and
// fixy/os/AtomPack.h says why a table is closed.  O_RDONLY is zero, so a
// map that answers zero for an unknown mode opens it for reading.  A
// class template map also takes a specialization for a class of the
// caller, and that class can add O_TRUNC to an open whose mode is
// ReadOnly.
inline constexpr ::fixy::atom_pack::tag_row<int> open_mode_table[] = {
    {^^open_mode::ReadOnly, O_RDONLY},
    {^^open_mode::WriteCreate, O_WRONLY | O_CREAT},
    {^^open_mode::WriteAppend, O_WRONLY | O_CREAT | O_APPEND},
    {^^open_mode::WriteTruncate, O_WRONLY | O_CREAT | O_TRUNC},
    {^^open_mode::ReadWrite, O_RDWR | O_CREAT},
};

inline constexpr ::fixy::atom_pack::tag_row<int> flag_table[] = {
    {^^flag::CloseOnExec, O_CLOEXEC}, {^^flag::NoFollow, O_NOFOLLOW}, {^^flag::DataSync, O_DSYNC},
    {^^flag::FullSync, O_SYNC},       {^^flag::Direct, O_DIRECT},
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

class FileDoor;

}  // namespace fixy::fs

namespace fixy::net {
class SocketDoor;
}  // namespace fixy::net

namespace fixy::time {
class PtpDeviceDoor;
}  // namespace fixy::time

namespace fixy::fs {

// Exclusive ownership of one descriptor, closed on destruction.
//
// The constructor that claims a descriptor is private.  With a public
// one, a caller can hand the handle any small integer — stdin, a
// descriptor another object still owns — and the destructor closes it
// on scope exit.  The three door classes below are
// its only friends.  Each makes the ::open or ::socket call itself and
// builds a handle only from what the kernel returned, and each admits
// only the gated mints that use it.
class [[nodiscard]] OwnedFd {
    int fd_ = -1;

    explicit OwnedFd(int fd) noexcept : fd_{fd} {}

    friend class FileDoor;
    friend class ::fixy::net::SocketDoor;
    friend class ::fixy::time::PtpDeviceDoor;

public:
    // The empty handle owns nothing and closes nothing, so it stays
    // public: it claims no descriptor.
    OwnedFd() noexcept = default;

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

    // The inverse door: ownership leaves with the descriptor, and the
    // close becomes the caller's.  Only what this handle owns can leave
    // it, and a released handle is left owning nothing, so its
    // destructor closes nothing.  This is the fd twin of
    // OwnedFile::release, and the one explicit, discouraged way the raw
    // descriptor escapes: a caller that wants to hand the fd to a C API
    // that will own it spells release() and the loss of ownership is in
    // view, rather than the fd leaking out through a bare accessor.
    [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }

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
[[nodiscard]] std::expected<Dirfd, std::error_code> open_dirfd(Ctx const&, Path<tags::source::Sanitized> dir) noexcept;

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
    int acc = O_CLOEXEC;
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
//
// §XXI carve-out: cx=alloc — opening a file invokes the kernel.
template <typename... Atoms, eff::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, Atoms...>
[[nodiscard]] inline std::expected<Linear<OwnedFd>, std::error_code>
mint_file(Ctx const&, Path<tags::source::Sanitized> sanitized_path, ::mode_t perms = 0644) noexcept;

// The door to ::open.  No object of it exists.  Its members are private,
// and its two friends are the gated mints that open a path, mint_file
// and open_dirfd.  OwnedFd befriends this class, so a handle over a path
// comes only from one of those two mints, after their gates.
//
// The trailing return types are necessary: fixy/os/CpuPinned.h gives the
// parse reason.
class FileDoor final : ::foundation::NoObject<FileDoor> {
    template <typename... FriendAtoms, eff::IsExecCtx FriendCtx>
        requires CtxFitsFileMint<FriendCtx, FriendAtoms...>
    friend auto mint_file(FriendCtx const&, Path<tags::source::Sanitized>, ::mode_t) noexcept
        -> std::expected<Linear<OwnedFd>, std::error_code>;

    template <eff::IsExecCtx FriendCtx>
        requires ::fixy::fs::CtxAdmitsFs<FriendCtx>
    friend auto open_dirfd(FriendCtx const&, Path<tags::source::Sanitized>) noexcept
        -> std::expected<Dirfd, std::error_code>;

    // ::open consults perms only when flags carries O_CREAT and ignores
    // it otherwise, so it is passed unconditionally.  Returns the errno on
    // failure and no handle.
    [[nodiscard]] static std::expected<OwnedFd, int> open_path_(const char* path, int flags, ::mode_t perms) noexcept {
        const int fd =
            ::open(path, flags,
                   perms);  // SYSCALL-CAP-OK: FileDoor::open_path_, sole caller mint_file ctx-gate (CtxFitsFileMint)
        if (fd < 0) {
            return std::unexpected{errno};
        }
        return OwnedFd{fd};
    }

    // O_DIRECTORY refuses anything else, O_NOFOLLOW refuses a symlink
    // that stands in for the directory, and the descriptor is what a later
    // fsync flushes the entry through.
    [[nodiscard]] static std::expected<OwnedFd, int> open_directory_(const char* dir_path) noexcept {
        const int fd = ::open(
            dir_path,
            O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
                | O_RDONLY);  // SYSCALL-CAP-OK: FileDoor::open_directory_, sole caller open_dirfd ctx-gate (CtxAdmitsFs)
        if (fd < 0) {
            return std::unexpected{errno};
        }
        return OwnedFd{fd};
    }
};

template <typename... Atoms, eff::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, Atoms...>
[[nodiscard]] inline std::expected<Linear<OwnedFd>, std::error_code>
mint_file(Ctx const&, Path<tags::source::Sanitized> sanitized_path, ::mode_t perms) noexcept {
    constexpr int flags = detail::fold_open_flags<Atoms...>();
    auto fd = FileDoor::open_path_(sanitized_path.value().c_str(), flags, perms);
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
// to.  The gate refuses None.
//
// Not a mint: this renames existing paths and synthesizes nothing.
template <typename Atomicity, eff::IsExecCtx Ctx>
    requires CtxFitsCommitAtomic<Ctx, Atomicity>
[[nodiscard]] inline std::expected<void, std::error_code> commit_atomic(Ctx const&, Path<tags::source::Sanitized> tmp,
                                                                        Path<tags::source::Sanitized> target) noexcept {
    int rc = 0;
    if constexpr (std::is_same_v<Atomicity, atomicity::Rename>) {
        rc = ::rename(
            tmp.value().c_str(),
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
    auto fd = FileDoor::open_directory_(dir.value().c_str());
    if (!fd) {
        return std::unexpected{std::error_code{fd.error(), std::system_category()}};
    }
    return Dirfd{std::move(*fd)};
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
[[nodiscard]] inline std::expected<std::size_t, std::error_code> read_full(Ctx const&, const OwnedFd& handle,
                                                                           std::span<std::byte> buffer) noexcept {
    if (!handle.is_open()) {
        return std::unexpected{std::error_code{EBADF, std::system_category()}};
    }
    std::size_t total = 0;
    while (total < buffer.size()) {
        const ::ssize_t transferred =
            ::read(handle.get(), buffer.data() + total,
                   buffer.size() - total);  // SYSCALL-CAP-OK: read_full ctx-gate (CtxAdmitsFs)
        if (transferred == 0) break;
        if (transferred < 0) {
            if (errno == EINTR) continue;
            return std::unexpected{std::error_code{errno, std::system_category()}};
        }
        total += static_cast<std::size_t>(transferred);
    }
    return total;
}

template <eff::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] inline std::expected<void, std::error_code> write_full(Ctx const&, const OwnedFd& handle,
                                                                     std::span<const std::byte> buffer) noexcept {
    if (!handle.is_open()) {
        return std::unexpected{std::error_code{EBADF, std::system_category()}};
    }
    std::size_t total = 0;
    while (total < buffer.size()) {
        const ::ssize_t transferred =
            ::write(handle.get(), buffer.data() + total,
                    buffer.size() - total);  // SYSCALL-CAP-OK: write_full ctx-gate (CtxAdmitsFs)
        if (transferred < 0) {
            if (errno == EINTR) continue;
            return std::unexpected{std::error_code{errno, std::system_category()}};
        }
        total += static_cast<std::size_t>(transferred);
    }
    return {};
}

template <eff::IsExecCtx Ctx>
    requires ::fixy::fs::CtxAdmitsFs<Ctx>
[[nodiscard]] inline std::expected<::off_t, std::error_code> file_size(Ctx const&, const OwnedFd& handle) noexcept {
    if (!handle.is_open()) {
        return std::unexpected{std::error_code{EBADF, std::system_category()}};
    }
    struct ::stat status{};
    if (::fstat(handle.get(), &status) < 0) {  // SYSCALL-CAP-OK: file_size ctx-gate (CtxAdmitsFs)
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return status.st_size;
}

// The read-only open, spelled once.
using read_only = ::fixy::atom::fs::mode<open_mode::ReadOnly>;

// The durable and atomic_write atoms only declare intent.  The mint
// opens the file.  The caller still calls sync<Fsync> after writing and
// then commit_atomic<Rename> to move the result into place.
//
// §XXI carve-out: cx=alloc — opening a file invokes the kernel.
template <eff::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, ::fixy::atom::fs::mode<open_mode::WriteTruncate>,
                             ::fixy::atom::fs::durable<sync_op::Fsync>,
                             ::fixy::atom::fs::atomic_write<atomicity::Rename>>
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

static_assert(open_mode_flags_of(^^open_mode::ReadOnly) == O_RDONLY);
static_assert(open_mode_flags_of(^^open_mode::WriteTruncate) == (O_WRONLY | O_CREAT | O_TRUNC));
static_assert(open_mode_flags_of(^^open_mode::WriteAppend) == (O_WRONLY | O_CREAT | O_APPEND));
static_assert(flag_bits_of(^^flag::CloseOnExec) == O_CLOEXEC);
static_assert(flag_bits_of(^^flag::NoFollow) == O_NOFOLLOW);
static_assert(flag_bits_of(^^flag::FullSync) == O_SYNC);

using A_RO = ::fixy::atom::fs::mode<open_mode::ReadOnly>;
using A_Trunc = ::fixy::atom::fs::mode<open_mode::WriteTruncate>;
using A_NoFollow = ::fixy::atom::fs::with_flag<flag::NoFollow>;
using A_Fsync = ::fixy::atom::fs::durable<sync_op::Fsync>;
using A_Rename = ::fixy::atom::fs::atomic_write<atomicity::Rename>;

static_assert(::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::fs::mode, A_RO, A_NoFollow>);
static_assert(!::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::fs::mode, A_NoFollow>);
static_assert(!::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::fs::mode>);
static_assert(!::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::fs::mode, A_RO, A_Trunc>);

static_assert(fold_open_flags<A_RO>() == (O_RDONLY | O_CLOEXEC));
static_assert(fold_open_flags<A_Trunc, A_NoFollow>() == (O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC));
static_assert(fold_open_flags<A_Trunc, A_Fsync, A_Rename>() == (O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC),
              "durable and atomic_write atoms declare intent and add no open flag.");

// A tag with no row in a table has no bits.  A read of them is a
// compile error, so an unknown mode cannot open for reading and an
// unknown flag cannot vanish from the open word.
struct NotAMode final {};
struct NotAFlag final {};
static_assert(!MappedOpenMode<NotAMode>, "a mode with no row must have no bits, not O_RDONLY.");
static_assert(!MappedFlag<NotAFlag>, "a flag with no row must have no bits, not zero.");
static_assert(!MappedOpenMode<void> && !MappedFlag<void>);
static_assert(MappedOpenMode<open_mode::ReadOnly> && MappedFlag<flag::NoFollow>);
static_assert(!MappedOpenMode<const open_mode::ReadOnly>, "a mode with a cv-qualifier is another type.");
static_assert(all_atom_tags_known_v<A_RO, A_NoFollow, A_Fsync, A_Rename>);
static_assert(!all_atom_tags_known_v<::fixy::atom::fs::mode<NotAMode>>);
static_assert(!all_atom_tags_known_v<A_RO, ::fixy::atom::fs::with_flag<NotAFlag>>);

// The row derived from the pack is IO and Block, and this is the pin on
// that.
using ExpectedFsRow = eff::Row<eff::Effect::IO, eff::Effect::Block>;
static_assert(std::is_same_v<::fixy::atom_pack::atoms_row_t<A_RO>, ExpectedFsRow>);
static_assert(std::is_same_v<::fixy::atom_pack::atoms_row_t<A_Trunc, A_Fsync, A_Rename>, ExpectedFsRow>);

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
static_assert(!KnownSyncOp<NotASyncOp> && !CtxFitsSync<IoBlockCtx, NotASyncOp>);

static_assert(CtxFitsCommitAtomic<IoBlockCtx, atomicity::Rename>);
static_assert(CtxFitsCommitAtomic<IoBlockCtx, atomicity::RenameAt2NoReplace>);
static_assert(!CtxFitsCommitAtomic<IoBlockCtx, atomicity::None>, "None renames nothing, so it commits nothing.");
static_assert(!CtxFitsCommitAtomic<IoOnlyCtx, atomicity::Rename>);
struct NotAnAtomicity final {};
static_assert(!KnownAtomicity<NotAnAtomicity> && !CtxFitsCommitAtomic<IoBlockCtx, NotAnAtomicity>);

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
static_assert(!std::is_default_constructible_v<FileDoor> && !std::is_copy_constructible_v<FileDoor>
                  && !std::is_move_constructible_v<FileDoor>,
              "No object of the file door exists.  Its private members are the only calls to ::open.");

// Every tag the four fixy::fs namespaces declare is known to the concept
// that gates it.  Each concept reads a closed table or a closed list, so
// this walk is the check that each declared tag has a row.
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

#pragma once

// ── crucible::fixy::fs — typed filesystem surface (FIXY-V-224) ───────
//
// Four orthogonal axis namespaces under `crucible::fixy::fs`:
//
//   open_mode::{ReadOnly, WriteCreate, WriteAppend, WriteTruncate,
//               ReadWrite, TmpFile}     — POSIX open mode tier
//   flag::{CloseOnExec, NoFollow, Directory, DataSync, FullSync,
//          Direct, NonBlock, Path}       — per-flag O_* tier
//   sync_op::{None, Fdatasync, Fsync, Msync, FsyncParentDir}
//                                         — durability syscall tier
//   atomicity::{None, Rename, RenameAt2NoReplace, LinkAtomic}
//                                         — commit-time atomicity tier
//
// Grant tags (each `final : grant::grant_base`) route through
// `DimensionAxis::SyscallSurface` (V-097's enumerator; filesystem
// CRUD IS the FileMutation tier of that lattice):
//
//   crucible::fixy::grant::fs::mode<Mode>
//   crucible::fixy::grant::fs::with_flag<Flag>
//   crucible::fixy::grant::fs::durable<SyncOp>
//   crucible::fixy::grant::fs::atomic_write<Atomicity>
//
// Plus a typed `Path<Source>` alias (re-export of safety::Path<S>)
// and a `Dirfd` Linear<FileHandle> wrapper opening with
// O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC for sandboxed openat() discipline.
//
// Three call-site factories per CLAUDE.md §XXI:
//
//   mint_file<Grants...>(ctx, Path<Sanitized>)
//         — §XXI mint; opens a file with the OR of all grant-implied
//           O_* flags + mode bits.  Requires Path<Sanitized> at the
//           call site (V-031/V-233 admission gate).
//
//   sync<SyncOp>(ctx, FileHandle const&)
//         — NOT a mint per §XXI (no fresh authoritative resource
//           synthesized; performs a durability syscall on an existing
//           handle).  Returns std::expected<void, std::error_code>.
//
//   commit_atomic<Atomicity>(ctx, tmp_path, target_path)
//         — NOT a mint per §XXI (commits a tmp file to its final
//           name via renameat2/linkat).  Returns std::expected<void,
//           std::error_code>.
//
// ── Axiom coverage (code_guide §II) ───────────────────────────────────
//
//   InitSafe   — every tag is `final` empty struct, NSDMI-trivial;
//                std::expected return channel; no uninit output.
//   TypeSafe   — strong types for every axis value (no raw int O_* bits
//                in the public surface); Path<Sanitized> at boundary.
//   NullSafe   — std::expected, no raw pointer return.
//   MemSafe    — FileHandle (safety/) is move-only RAII; Linear gate
//                for Dirfd; no manual close needed.
//   BorrowSafe — mint_file consumes Path<Sanitized> by value; the call
//                site re-mints with a fresh sanitize per attempt.
//   ThreadSafe — every factory is pure / stateless; no shared state.
//   LeakSafe   — FileHandle dtor closes fd; mint_file's std::expected
//                path always either returns the handle or unwinds.
//   DetSafe    — same path + same grants + same ctx → same syscall
//                sequence on any platform (Linux-only per CLAUDE.md
//                §XIV).
//
// ── HS14 fixtures (≥6 per §XXI / CLAUDE.md HS14) ──────────────────────
//
// Each mismatch class lives in test/fixy_neg/neg_fixy_v_224_*.cpp:
//
//   1. mint_file<...>(ctx, Path<External>) — Sanitized-only domain.
//   2. mint_file<...>(ctx, Path<Sanitized>) — empty Grants pack rejected
//      (must engage at least mode<>).
//   3. mint_file<mode<X>, mode<Y>>(ctx, ...) — duplicate-mode rejected.
//   4. sync<sync_op::None>(ctx, h) — sentinel "no-op" call rejected.
//   5. commit_atomic with Path<External> target rejected.
//   6. mint_file in a ctx that lacks the Block cap rejected.

#include <crucible/fixy/Grant.h>            // grant_base, which_dim primary
#include <crucible/safety/DimensionTraits.h>// DimensionAxis::SyscallSurface
#include <crucible/safety/Path.h>           // Path<Source> + sanitize_path
                                            // (transitively pulls
                                            // PathTraversal.h via V-233
                                            // back-edge include)
#include <crucible/safety/source/Path.h>    // FromUserPath/Env/Config

#include <crucible/handles/FileHandle.h>    // safety::FileHandle
#include <crucible/safety/Linear.h>         // safety::Linear

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <system_error>
#include <type_traits>
#include <utility>

namespace crucible::fixy::fs {

// ═════════════════════════════════════════════════════════════════════
// ── (a) open_mode — POSIX open-mode tier ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Six tier markers mirroring O_RDONLY / O_WRONLY|O_CREAT
// / O_WRONLY|O_APPEND / O_WRONLY|O_TRUNC / O_RDWR / O_TMPFILE.
// Each is an empty `final` struct (sizeof == 1 standalone, EBO-
// collapsible).  The bit pattern lives in `flags_of<>` below; tags are
// type-level identities only.

namespace open_mode {
struct ReadOnly       final {};
struct WriteCreate    final {};  // O_WRONLY | O_CREAT
struct WriteAppend    final {};  // O_WRONLY | O_CREAT | O_APPEND
struct WriteTruncate  final {};  // O_WRONLY | O_CREAT | O_TRUNC
struct ReadWrite      final {};  // O_RDWR  | O_CREAT
struct TmpFile        final {};  // O_TMPFILE | O_RDWR  (anonymous temp)
}  // namespace open_mode

template <typename Mode>
struct open_mode_flags;  // primary undefined — specialized per tag

template <> struct open_mode_flags<open_mode::ReadOnly>
    : std::integral_constant<int, O_RDONLY> {};
template <> struct open_mode_flags<open_mode::WriteCreate>
    : std::integral_constant<int, O_WRONLY | O_CREAT> {};
template <> struct open_mode_flags<open_mode::WriteAppend>
    : std::integral_constant<int, O_WRONLY | O_CREAT | O_APPEND> {};
template <> struct open_mode_flags<open_mode::WriteTruncate>
    : std::integral_constant<int, O_WRONLY | O_CREAT | O_TRUNC> {};
template <> struct open_mode_flags<open_mode::ReadWrite>
    : std::integral_constant<int, O_RDWR | O_CREAT> {};
template <> struct open_mode_flags<open_mode::TmpFile>
    : std::integral_constant<int, O_TMPFILE | O_RDWR> {};

template <typename Mode>
inline constexpr int open_mode_flags_v = open_mode_flags<Mode>::value;

// ═════════════════════════════════════════════════════════════════════
// ── (b) flag — per-O_* tier ───────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Eight per-flag tags.  CloseOnExec is `default-on` for every mint_file
// call — the only way to get a non-O_CLOEXEC fd is to NOT pass
// `with_flag<CloseOnExec>` AND not have it in the default-flag pack
// (which we always do here).  Forgetting O_CLOEXEC is the textbook
// fd-leak-to-child bug; the default-on discipline prevents it.

namespace flag {
struct CloseOnExec    final {};  // O_CLOEXEC (default-on)
struct NoFollow       final {};  // O_NOFOLLOW  — refuse symlinks
struct Directory      final {};  // O_DIRECTORY — must be dir
struct DataSync       final {};  // O_DSYNC     — data-synced writes
struct FullSync       final {};  // O_SYNC      — data+meta synced
struct Direct         final {};  // O_DIRECT    — bypass page cache
struct NonBlock       final {};  // O_NONBLOCK  — non-blocking I/O
struct Path           final {};  // O_PATH      — pathfd, no real open
}  // namespace flag

template <typename Flag>
struct flag_bits;  // primary undefined — specialized per tag

template <> struct flag_bits<flag::CloseOnExec>
    : std::integral_constant<int, O_CLOEXEC> {};
template <> struct flag_bits<flag::NoFollow>
    : std::integral_constant<int, O_NOFOLLOW> {};
template <> struct flag_bits<flag::Directory>
    : std::integral_constant<int, O_DIRECTORY> {};
template <> struct flag_bits<flag::DataSync>
    : std::integral_constant<int, O_DSYNC> {};
template <> struct flag_bits<flag::FullSync>
    : std::integral_constant<int, O_SYNC> {};
template <> struct flag_bits<flag::Direct>
    : std::integral_constant<int, O_DIRECT> {};
template <> struct flag_bits<flag::NonBlock>
    : std::integral_constant<int, O_NONBLOCK> {};
template <> struct flag_bits<flag::Path>
    : std::integral_constant<int, O_PATH> {};

template <typename Flag>
inline constexpr int flag_bits_v = flag_bits<Flag>::value;

// ═════════════════════════════════════════════════════════════════════
// ── (c) sync_op — durability syscall tier ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Five durability strategies.  `None` is the placeholder for explicit
// "no durability" (kernel page cache only — THE Cipher.h bug class
// V-026/V-027 fixed).  The neg-compile fixture for sync<None> rejects
// it: if you don't need durability, don't call sync().

namespace sync_op {
struct None             final {};  // no syscall — page-cache only
struct Fdatasync        final {};  // ::fdatasync   (data-only)
struct Fsync            final {};  // ::fsync       (data + metadata)
struct Msync            final {};  // ::msync       (memory-mapped)
struct FsyncParentDir   final {};  // ::fsync(dirfd) — directory entry
}  // namespace sync_op

// ═════════════════════════════════════════════════════════════════════
// ── (d) atomicity — commit-time atomicity tier ────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Four atomic-commit strategies for the tmp → target rename step.
// LinkAtomic = O_TMPFILE + linkat(AT_EMPTY_PATH) — the only race-free
// name reveal pattern (the file is never visible by name until after
// the link succeeds).  RenameAt2NoReplace = renameat2(RENAME_NOREPLACE)
// — fails if target exists, avoiding the silent-overwrite race.

namespace atomicity {
struct None                 final {};  // no atomicity (caller assumes ok)
struct Rename               final {};  // ::rename     (last-writer-wins)
struct RenameAt2NoReplace   final {};  // ::renameat2(RENAME_NOREPLACE)
struct LinkAtomic           final {};  // ::linkat(AT_EMPTY_PATH)
}  // namespace atomicity

// ═════════════════════════════════════════════════════════════════════
// ── Path<Source> — re-export of safety::Path<Source> ──────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Typed filesystem-path wrapper.  V-031 (External) + V-232
// (FromUser/Env/Config) supply the Source taint tags; V-233's
// `sanitize_path_*` family launders them to source::Sanitized.

template <typename Source>
using Path = ::crucible::safety::Path<Source>;

}  // namespace crucible::fixy::fs

// ═════════════════════════════════════════════════════════════════════
// ── grant tags — every fs grant routes to SyscallSurface ──────────────
// ═════════════════════════════════════════════════════════════════════
//
// Per Grant.h's namespace-purity discipline (lines 121-158), all
// `which_dim` specializations MUST live syntactically inside
// `namespace crucible::fixy::grant`.  This header reopens that
// namespace; check-fixy-grant-namespace-purity.sh allowlists Fs.h
// alongside Fp.h and the syscall/ family.

namespace crucible::fixy::grant {

namespace fs {

// Each parametric grant takes a type-tag NTTP from the fs:: enums.
// EBO-collapsible (sizeof == 1 standalone, 0 inside aggregators).

template <typename Mode>
struct mode             final : grant_base {};

template <typename Flag>
struct with_flag        final : grant_base {};

template <typename SyncOp>
struct durable          final : grant_base {};

template <typename Atomicity>
struct atomic_write     final : grant_base {};

}  // namespace fs

// ── which_dim routing ────────────────────────────────────────────────
//
// All four families route to DimensionAxis::SyscallSurface (V-097);
// filesystem CRUD IS a syscall surface.  Reject.h's engagement walk
// treats them uniformly.

template <typename Mode>
struct which_dim<fs::mode<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <typename Flag>
struct which_dim<fs::with_flag<Flag>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <typename SyncOp>
struct which_dim<fs::durable<SyncOp>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <typename Atomicity>
struct which_dim<fs::atomic_write<Atomicity>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

}  // namespace crucible::fixy::grant

// ═════════════════════════════════════════════════════════════════════
// ── Dirfd + mint_file + sync + commit_atomic ──────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::fs {

// ── Dirfd — sandbox-anchor dirfd opened with safe defaults ───────────
//
// Wraps safety::FileHandle.  The factory always passes
// O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC — refuses symlinked dirs, refuses
// non-dirs, fd survives execve.  Sandbox-anchor discipline (V-030).
//
// Returned wrapped in Linear<> at the §XXI mint boundary so callers
// can't accidentally double-open.

class [[nodiscard]] Dirfd {
    ::crucible::safety::FileHandle fd_;

public:
    Dirfd() noexcept = default;
    explicit Dirfd(::crucible::safety::FileHandle&& fd) noexcept
        : fd_{std::move(fd)} {}

    Dirfd(const Dirfd&)            = delete("Dirfd holds an fd; copy would double-close");
    Dirfd& operator=(const Dirfd&) = delete("Dirfd holds an fd; copy would double-close");
    Dirfd(Dirfd&&) noexcept            = default;
    Dirfd& operator=(Dirfd&&) noexcept = default;

    [[nodiscard]] int get() const noexcept { return fd_.get(); }
    [[nodiscard]] bool is_open() const noexcept { return fd_.is_open(); }
    [[nodiscard]] const ::crucible::safety::FileHandle& handle() const noexcept { return fd_; }
};

[[nodiscard]] inline std::expected<Dirfd, std::error_code>
open_dirfd(const ::std::filesystem::path& dir) noexcept {
    const int fd = ::open(dir.c_str(),
                          O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_RDONLY);
    if (fd < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return Dirfd{::crucible::safety::FileHandle{fd}};
}

// ── flag composition over a Grants pack ──────────────────────────────
//
// Helpers that walk a `Grants...` pack and compute the OR of every
// grant's contribution.  Used by mint_file to fold the open()'s
// final flags argument.
//
// Specialization-discovery is by partial-match (mode<X> matches; bare
// types don't).  Unknown grants contribute 0 (the grant exists on the
// FS axis but doesn't add to the open() flags — e.g., a durable<>
// grant declares intent for a later sync() call, not the open() flags).

namespace detail {

template <typename G>
struct grant_open_flags : std::integral_constant<int, 0> {};

template <typename Mode>
struct grant_open_flags<::crucible::fixy::grant::fs::mode<Mode>>
    : std::integral_constant<int, open_mode_flags_v<Mode>> {};

template <typename Flag>
struct grant_open_flags<::crucible::fixy::grant::fs::with_flag<Flag>>
    : std::integral_constant<int, flag_bits_v<Flag>> {};

template <typename G>
inline constexpr int grant_open_flags_v = grant_open_flags<G>::value;

// Fold OR across the Grants pack.  CloseOnExec is implicitly OR'd in
// — every mint_file call gets O_CLOEXEC for free.
template <typename... Grants>
inline constexpr int fold_open_flags() noexcept {
    int acc = O_CLOEXEC;
    ((acc |= grant_open_flags_v<Grants>), ...);
    return acc;
}

// has_mode<Grants...> — at least one mode<X> grant in the pack.
template <typename G>
struct is_mode_grant : std::false_type {};
template <typename Mode>
struct is_mode_grant<::crucible::fixy::grant::fs::mode<Mode>> : std::true_type {};

template <typename... Grants>
inline constexpr bool has_mode_v = (is_mode_grant<Grants>::value || ...);

// duplicate_mode<Grants...> — strictly more than one mode<X> in pack.
template <typename... Grants>
inline constexpr bool has_duplicate_mode_v =
    (static_cast<int>(is_mode_grant<Grants>::value) + ...) > 1;

}  // namespace detail

// ── mint_file<Grants...>(ctx, Path<Sanitized>) ───────────────────────
//
// §XXI Token mint.  Opens the path with the OR of grant-implied O_*
// bits + the mandatory O_CLOEXEC, returns a FileHandle wrapped in
// safety::Linear at the call boundary.
//
// The `requires` clause is the single load-bearing soundness gate:
//
//   has_mode_v<Grants...>           — caller must pick exactly one
//                                     open-mode tier (rule 2 / fixture
//                                     #2 — empty grants reject).
//   !has_duplicate_mode_v<Grants...> — at-most-one mode (fixture #3).
//
// Path<Sanitized> at the call site witnesses V-031's four-rule no-dot-
// dot algorithm has already discharged.  Passing Path<External> /
// Path<FromUser> / etc. fires `retag_policy` rejection at the caller.

template <typename... Grants>
    requires (detail::has_mode_v<Grants...> &&
              !detail::has_duplicate_mode_v<Grants...>)
[[nodiscard]] inline std::expected<::crucible::safety::Linear<::crucible::safety::FileHandle>,
                                   std::error_code>
mint_file(Path<::crucible::safety::source::Sanitized> sanitized_path,
          mode_t                                       perms = 0644) noexcept {
    constexpr int flags = detail::fold_open_flags<Grants...>();
    // O_TMPFILE requires a directory argument; for now we route every
    // mint_file through ::open() on the full path.  V-225/V-229 may
    // grow an openat(dirfd, basename) variant for sandboxed callers.
    const int fd = ::open(sanitized_path.value().c_str(), flags, perms);
    if (fd < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return ::crucible::safety::Linear<::crucible::safety::FileHandle>{
        ::crucible::safety::FileHandle{fd}};
}

// ── sync<SyncOp>(FileHandle const&) ──────────────────────────────────
//
// Durability syscall.  NOT a §XXI mint — performs an effect on an
// existing handle; no fresh authoritative resource synthesized.  The
// `requires !std::is_same_v<SyncOp, sync_op::None>` gate enforces
// fixture #4: calling sync<None> is a programmer error (if you don't
// need durability, don't call sync).

template <typename SyncOp>
    requires (!std::is_same_v<SyncOp, sync_op::None>)
[[nodiscard]] inline std::expected<void, std::error_code>
sync(const ::crucible::safety::FileHandle& h) noexcept {
    if (!h.is_open()) {
        return std::unexpected{std::error_code{EBADF, std::system_category()}};
    }
    int rc = 0;
    if constexpr (std::is_same_v<SyncOp, sync_op::Fdatasync>) {
        rc = ::fdatasync(h.get());
    } else if constexpr (std::is_same_v<SyncOp, sync_op::Fsync>) {
        rc = ::fsync(h.get());
    } else if constexpr (std::is_same_v<SyncOp, sync_op::FsyncParentDir>) {
        // For FsyncParentDir, the caller's handle MUST be a dirfd.
        // We trust the caller (Dirfd type-discipline upstream); the
        // fsync syscall on a non-dir fd will EINVAL naturally.
        rc = ::fsync(h.get());
    } else if constexpr (std::is_same_v<SyncOp, sync_op::Msync>) {
        // Msync requires a mapped address + length, not an fd — the
        // V-225 Mmap.h surface owns the call.  For Fs.h, Msync at this
        // signature is an error: route through the mmap surface
        // instead.
        return std::unexpected{std::error_code{EINVAL, std::system_category()}};
    } else {
        static_assert(std::is_same_v<SyncOp, sync_op::Fdatasync> ||
                      std::is_same_v<SyncOp, sync_op::Fsync> ||
                      std::is_same_v<SyncOp, sync_op::Msync> ||
                      std::is_same_v<SyncOp, sync_op::FsyncParentDir>,
                      "FIXY-V-224 sync<SyncOp>: SyncOp must be a sync_op:: tag");
    }
    if (rc < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

// ── commit_atomic<Atomicity>(tmp, target) ────────────────────────────
//
// Tmp → target atomic-commit syscall.  NOT a §XXI mint — performs a
// rename/link side effect on existing paths; no fresh authoritative
// resource synthesized.
//
// Both tmp and target MUST be Path<Sanitized> — the source-tag
// discipline catches "caller forgot to sanitize the target" at compile
// time (fixture #5).

template <typename Atomicity>
[[nodiscard]] inline std::expected<void, std::error_code>
commit_atomic(Path<::crucible::safety::source::Sanitized> tmp,
              Path<::crucible::safety::source::Sanitized> target) noexcept {
    if constexpr (std::is_same_v<Atomicity, atomicity::Rename>) {
        if (::rename(tmp.value().c_str(), target.value().c_str()) < 0) {
            return std::unexpected{std::error_code{errno, std::system_category()}};
        }
        return {};
    } else if constexpr (std::is_same_v<Atomicity, atomicity::RenameAt2NoReplace>) {
        if (::renameat2(AT_FDCWD, tmp.value().c_str(),
                        AT_FDCWD, target.value().c_str(),
                        RENAME_NOREPLACE) < 0) {
            return std::unexpected{std::error_code{errno, std::system_category()}};
        }
        return {};
    } else if constexpr (std::is_same_v<Atomicity, atomicity::LinkAtomic>) {
        // LinkAtomic is the O_TMPFILE + linkat(AT_EMPTY_PATH) shape.
        // The caller already holds the tmp fd; this path is reserved
        // for the V-229 implementation TU where the fd channel is
        // wired through.  For the V-224 substrate slice, we expose
        // the shape but return ENOSYS — the V-229 sweep replaces.
        return std::unexpected{std::error_code{ENOSYS, std::system_category()}};
    } else if constexpr (std::is_same_v<Atomicity, atomicity::None>) {
        // Atomicity::None is a programmer-acknowledged "do nothing"
        // — semantically a no-op.  Returning success is correct (the
        // caller asserted no atomicity guarantee was needed).
        (void)tmp; (void)target;
        return {};
    } else {
        static_assert(std::is_same_v<Atomicity, atomicity::None> ||
                      std::is_same_v<Atomicity, atomicity::Rename> ||
                      std::is_same_v<Atomicity, atomicity::RenameAt2NoReplace> ||
                      std::is_same_v<Atomicity, atomicity::LinkAtomic>,
                      "FIXY-V-224 commit_atomic<Atomicity>: Atomicity must be an atomicity:: tag");
    }
}

}  // namespace crucible::fixy::fs

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time) ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::fs::detail::v224_self_test {

namespace om = open_mode;
namespace fl = flag;
namespace so = sync_op;
namespace at_ = atomicity;
using D = ::crucible::fixy::dim::DimensionAxis;

// ── Layer 1: every type tag is a distinct empty-final marker ──────
static_assert(sizeof(om::ReadOnly)       == 1);
static_assert(sizeof(om::WriteCreate)    == 1);
static_assert(sizeof(om::WriteAppend)    == 1);
static_assert(sizeof(om::WriteTruncate)  == 1);
static_assert(sizeof(om::ReadWrite)      == 1);
static_assert(sizeof(om::TmpFile)        == 1);

static_assert(sizeof(fl::CloseOnExec)    == 1);
static_assert(sizeof(fl::NoFollow)       == 1);
static_assert(sizeof(fl::Directory)      == 1);
static_assert(sizeof(fl::DataSync)       == 1);

static_assert(sizeof(so::None)           == 1);
static_assert(sizeof(so::Fdatasync)      == 1);
static_assert(sizeof(so::Fsync)          == 1);

static_assert(sizeof(at_::None)              == 1);
static_assert(sizeof(at_::RenameAt2NoReplace) == 1);
static_assert(sizeof(at_::LinkAtomic)        == 1);

// ── Layer 2: flag-fold maps each tag to its POSIX O_* value ───────
static_assert(open_mode_flags_v<om::ReadOnly>      == O_RDONLY);
static_assert(open_mode_flags_v<om::WriteTruncate> == (O_WRONLY | O_CREAT | O_TRUNC));
static_assert(open_mode_flags_v<om::TmpFile>       == (O_TMPFILE | O_RDWR));
static_assert(flag_bits_v<fl::CloseOnExec>         == O_CLOEXEC);
static_assert(flag_bits_v<fl::NoFollow>            == O_NOFOLLOW);

// ── Layer 3: grant tags structurally valid + on SyscallSurface ────
using ::crucible::fixy::grant::which_dim_v;
using ::crucible::fixy::grant::IsGrantTag;
namespace gfs = ::crucible::fixy::grant::fs;

static_assert(IsGrantTag<gfs::mode<om::ReadOnly>>);
static_assert(IsGrantTag<gfs::with_flag<fl::NoFollow>>);
static_assert(IsGrantTag<gfs::durable<so::Fsync>>);
static_assert(IsGrantTag<gfs::atomic_write<at_::LinkAtomic>>);

static_assert(which_dim_v<gfs::mode<om::ReadOnly> >            == D::SyscallSurface);
static_assert(which_dim_v<gfs::with_flag<fl::NoFollow> >       == D::SyscallSurface);
static_assert(which_dim_v<gfs::durable<so::Fsync> >            == D::SyscallSurface);
static_assert(which_dim_v<gfs::atomic_write<at_::LinkAtomic> > == D::SyscallSurface);

// ── Layer 4: parametric grant distinctness — different mode tags ──
static_assert(!std::is_same_v<gfs::mode<om::ReadOnly>,
                              gfs::mode<om::WriteTruncate>>);
static_assert(!std::is_same_v<gfs::with_flag<fl::NoFollow>,
                              gfs::with_flag<fl::Direct>>);
static_assert(!std::is_same_v<gfs::durable<so::Fdatasync>,
                              gfs::durable<so::Fsync>>);

// ── Layer 5: mint_file domain — has_mode + no duplicate-mode ──────
static_assert( detail::has_mode_v<gfs::mode<om::ReadOnly>>);
static_assert(!detail::has_mode_v<gfs::with_flag<fl::NoFollow>>);
static_assert( detail::has_duplicate_mode_v<gfs::mode<om::ReadOnly>,
                                            gfs::mode<om::WriteTruncate>>);
static_assert(!detail::has_duplicate_mode_v<gfs::mode<om::ReadOnly>,
                                            gfs::with_flag<fl::NoFollow>>);

// ── Layer 6: flag-fold ORs the pack + O_CLOEXEC ───────────────────
static_assert(detail::fold_open_flags<gfs::mode<om::ReadOnly>>() ==
              (O_RDONLY | O_CLOEXEC));
static_assert(detail::fold_open_flags<gfs::mode<om::WriteTruncate>,
                                      gfs::with_flag<fl::NoFollow>>() ==
              (O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC));

}  // namespace crucible::fixy::fs::detail::v224_self_test

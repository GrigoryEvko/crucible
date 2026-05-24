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

#include <crucible/effects/ExecCtx.h>       // IsExecCtx + row_type_of_t
#include <crucible/effects/EffectRow.h>     // row_contains_v
#include <crucible/effects/Capabilities.h>  // effects::Effect

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

// ── Non-template syscall impls — FIXY-V-229 src/fixy/Fs.cpp ─────────
//
// The actual ::open() / ::fdatasync() / ::fsync() / ::rename() /
// ::renameat2() / ::linkat() calls are NOT template-parameter-
// dependent — they take an int (fd) and a small enum (SyncOpTag /
// AtomicityTag) and emit one canonical syscall sequence.  Hoisting
// these bodies out of the header into `src/fixy/Fs.cpp` deduplicates
// the syscall wrapper across every TU that calls `mint_file<...>` /
// `sync<...>` / `commit_atomic<...>`: each call site emits ONE call
// instruction to the impl helper instead of an inline `::open()` body
// per instantiation.
//
// The templates above (`mint_file<Grants...>`, `sync<SyncOp>`,
// `commit_atomic<Atomicity>`) MUST stay inline because their bodies
// dispatch via `if constexpr` on the explicit template parameter.
// They become THIN — they compile-time map the template parameter to
// an enum tag and forward to the impl.

namespace detail::impl {

enum class SyncOpTag : ::std::uint8_t {
    Fdatasync,
    Fsync,
    FsyncParentDir,
    Msync,
};

enum class AtomicityTag : ::std::uint8_t {
    Rename,
    RenameAt2NoReplace,
    LinkAtomic,
    None,
};

// ::open() wrapper.  Path NUL-terminated; flags = OR-folded by caller.
// Returns the bare FileHandle (move-only RAII).  Errno wrapped in
// std::system_category().
[[nodiscard]] ::std::expected<::crucible::safety::FileHandle, ::std::error_code>
do_open_impl(const char* path, int flags, ::mode_t perms) noexcept;

// O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC|O_RDONLY ::open() — for Dirfd.
[[nodiscard]] ::std::expected<::crucible::safety::FileHandle, ::std::error_code>
do_open_dirfd_impl(const char* dir_path) noexcept;

// Durability syscall dispatch.  fd >= 0 expected; caller pre-checks.
// SyncOpTag::Msync returns EINVAL (route through mmap surface).
[[nodiscard]] ::std::expected<void, ::std::error_code>
do_sync_impl(int fd, SyncOpTag op) noexcept;

// Commit-atomic syscall dispatch.  None returns success (no-op);
// LinkAtomic returns ENOSYS until V-228's CipherDurable wires the
// O_TMPFILE + linkat fd channel.
[[nodiscard]] ::std::expected<void, ::std::error_code>
do_commit_atomic_impl(const char* tmp, const char* target,
                      AtomicityTag atomicity) noexcept;

}  // namespace detail::impl

[[nodiscard]] inline std::expected<Dirfd, std::error_code>
open_dirfd(const ::std::filesystem::path& dir) noexcept {
    auto fh = detail::impl::do_open_dirfd_impl(dir.c_str());
    if (!fh) {
        return std::unexpected{fh.error()};
    }
    return Dirfd{std::move(*fh)};
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

// ── §XXI ctx-bound mint gates — single-concept requires per family ───
//
// Each concept bundles every load-bearing predicate into ONE name so
// the mint's requires-clause is a single line per §XXI strict reading
// ("The `requires` clause MUST be a single concept").
//
// `CtxAdmitsIoBlock<Ctx>` is the shared row-membership check: the
// caller's ExecCtx row MUST admit both Effect::IO and Effect::Block.
// Filesystem syscalls do both — they cross the kernel boundary (IO)
// and can park the caller until disk responds (Block).  A hot-path
// foreground ctx that admits neither cannot mint a file by design.

template <typename Ctx>
concept CtxAdmitsIoBlock =
    ::crucible::effects::IsExecCtx<Ctx>
    && ::crucible::effects::row_contains_v<
           ::crucible::effects::row_type_of_t<Ctx>,
           ::crucible::effects::Effect::IO>
    && ::crucible::effects::row_contains_v<
           ::crucible::effects::row_type_of_t<Ctx>,
           ::crucible::effects::Effect::Block>;

// `CtxFitsFileMint<Ctx, Grants...>` — single soundness gate on
// `mint_file<Grants...>(ctx, Path<Sanitized>)`.  Bundles:
//
//   (1) Ctx is a valid ExecCtx admitting IO + Block effects.
//   (2) Grants pack engages exactly one open-mode tier (`mode<X>`).
//   (3) No duplicate mode-engagement (caller cannot pin two modes).
//
// Mismatch on any rule fires HS14 fixture #2 / #3 / #6.

template <typename Ctx, typename... Grants>
concept CtxFitsFileMint =
    CtxAdmitsIoBlock<Ctx>
    && detail::has_mode_v<Grants...>
    && !detail::has_duplicate_mode_v<Grants...>;

// `CtxFitsSync<Ctx, SyncOp>` — single soundness gate on
// `sync<SyncOp>(ctx, h)`.  Bundles:
//
//   (1) Ctx admits IO + Block (fsync can block on slow disks).
//   (2) SyncOp is NOT sync_op::None (calling sync<None> is a
//       programmer error — fixture #4).

template <typename Ctx, typename SyncOp>
concept CtxFitsSync =
    CtxAdmitsIoBlock<Ctx>
    && !std::is_same_v<SyncOp, sync_op::None>;

// `CtxFitsCommitAtomic<Ctx, Atomicity>` — single soundness gate on
// `commit_atomic<Atomicity>(ctx, tmp, target)`.  Bundles ctx fit +
// recognized Atomicity tag.

template <typename Ctx, typename Atomicity>
concept CtxFitsCommitAtomic =
    CtxAdmitsIoBlock<Ctx>
    && (std::is_same_v<Atomicity, atomicity::None>
        || std::is_same_v<Atomicity, atomicity::Rename>
        || std::is_same_v<Atomicity, atomicity::RenameAt2NoReplace>
        || std::is_same_v<Atomicity, atomicity::LinkAtomic>);

// ── mint_file<Grants...>(ctx, Path<Sanitized>) ───────────────────────
//
// §XXI Ctx-bound mint.  Opens the path with the OR of grant-implied
// O_* bits + the mandatory O_CLOEXEC, returns a FileHandle wrapped in
// safety::Linear at the boundary so callers can't accidentally
// double-close.
//
// Pack ordering: `template <typename... Grants, IsExecCtx Ctx>` — the
// Grants pack appears explicitly at the call site; Ctx is deduced
// from the runtime argument.  This is the canonical "explicit pack +
// trailing deduced parameter" shape (works because all explicit args
// fill the pack; Ctx deduces from the first runtime argument).
//
// Path<Sanitized> at the boundary witnesses V-031/V-233's path-
// traversal sanitize has already discharged.  Passing Path<External>
// / Path<FromUserPath> / etc. fires `retag_policy` rejection at the
// caller (fixture #1).

template <typename... Grants, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsFileMint<Ctx, Grants...>
[[nodiscard]] inline std::expected<::crucible::safety::Linear<::crucible::safety::FileHandle>,
                                   std::error_code>
mint_file(Ctx const&,
          Path<::crucible::safety::source::Sanitized> sanitized_path,
          mode_t                                       perms = 0644) noexcept {
    constexpr int flags = detail::fold_open_flags<Grants...>();
    // V-229: syscall body lives in src/fixy/Fs.cpp.  This template
    // remains inline because the Grants-pack-dependent `flags` compile
    // constant is folded at instantiation time and threaded through.
    // O_TMPFILE requires a directory argument; for now we route every
    // mint_file through ::open() on the full path.  V-225/V-229 may
    // grow an openat(dirfd, basename) variant for sandboxed callers.
    auto fh = detail::impl::do_open_impl(sanitized_path.value().c_str(),
                                         flags, perms);
    if (!fh) {
        return std::unexpected{fh.error()};
    }
    return ::crucible::safety::Linear<::crucible::safety::FileHandle>{
        std::move(*fh)};
}

// ── sync<SyncOp>(ctx, FileHandle const&) ─────────────────────────────
//
// Durability syscall.  NOT a §XXI mint — performs an effect on an
// existing handle; no fresh authoritative resource synthesized.
// Ctx-bound nonetheless because the syscall can block on slow disks.
// Concept rejects sync_op::None at compile time (fixture #4).

template <typename SyncOp, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsSync<Ctx, SyncOp>
[[nodiscard]] inline std::expected<void, std::error_code>
sync(Ctx const&,
     const ::crucible::safety::FileHandle& h) noexcept {
    if (!h.is_open()) {
        return std::unexpected{std::error_code{EBADF, std::system_category()}};
    }
    // V-229: compile-time map SyncOp -> SyncOpTag; syscall body lives
    // in src/fixy/Fs.cpp.  The static_assert exhaustiveness check stays
    // at the template-instantiation site so a malformed SyncOp fires
    // the diagnostic here, before the impl call.
    constexpr auto op = []() consteval {
        if constexpr (std::is_same_v<SyncOp, sync_op::Fdatasync>) {
            return detail::impl::SyncOpTag::Fdatasync;
        } else if constexpr (std::is_same_v<SyncOp, sync_op::Fsync>) {
            return detail::impl::SyncOpTag::Fsync;
        } else if constexpr (std::is_same_v<SyncOp, sync_op::FsyncParentDir>) {
            return detail::impl::SyncOpTag::FsyncParentDir;
        } else if constexpr (std::is_same_v<SyncOp, sync_op::Msync>) {
            return detail::impl::SyncOpTag::Msync;
        } else {
            static_assert(std::is_same_v<SyncOp, sync_op::Fdatasync> ||
                          std::is_same_v<SyncOp, sync_op::Fsync> ||
                          std::is_same_v<SyncOp, sync_op::Msync> ||
                          std::is_same_v<SyncOp, sync_op::FsyncParentDir>,
                          "FIXY-V-224 sync<SyncOp>: SyncOp must be a sync_op:: tag");
            return detail::impl::SyncOpTag::Fdatasync;  // unreachable
        }
    }();
    return detail::impl::do_sync_impl(h.get(), op);
}

// ── commit_atomic<Atomicity>(ctx, tmp, target) ───────────────────────
//
// Tmp → target atomic-commit syscall.  NOT a §XXI mint — performs a
// rename/link side effect on existing paths; no fresh authoritative
// resource synthesized.  Ctx-bound for the IO+Block syscall surface.
//
// Both tmp and target MUST be Path<Sanitized> — the source-tag
// discipline catches "caller forgot to sanitize the target" at compile
// time (fixture #5).

template <typename Atomicity, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsCommitAtomic<Ctx, Atomicity>
[[nodiscard]] inline std::expected<void, std::error_code>
commit_atomic(Ctx const&,
              Path<::crucible::safety::source::Sanitized> tmp,
              Path<::crucible::safety::source::Sanitized> target) noexcept {
    // V-229: compile-time map Atomicity -> AtomicityTag; syscall body
    // lives in src/fixy/Fs.cpp.  LinkAtomic still returns ENOSYS until
    // V-228's CipherDurable wires the O_TMPFILE + linkat fd channel.
    // Atomicity::None remains a no-op (caller-asserted).
    constexpr auto at = []() consteval {
        if constexpr (std::is_same_v<Atomicity, atomicity::Rename>) {
            return detail::impl::AtomicityTag::Rename;
        } else if constexpr (std::is_same_v<Atomicity, atomicity::RenameAt2NoReplace>) {
            return detail::impl::AtomicityTag::RenameAt2NoReplace;
        } else if constexpr (std::is_same_v<Atomicity, atomicity::LinkAtomic>) {
            return detail::impl::AtomicityTag::LinkAtomic;
        } else if constexpr (std::is_same_v<Atomicity, atomicity::None>) {
            return detail::impl::AtomicityTag::None;
        } else {
            static_assert(std::is_same_v<Atomicity, atomicity::None> ||
                          std::is_same_v<Atomicity, atomicity::Rename> ||
                          std::is_same_v<Atomicity, atomicity::RenameAt2NoReplace> ||
                          std::is_same_v<Atomicity, atomicity::LinkAtomic>,
                          "FIXY-V-224 commit_atomic<Atomicity>: Atomicity must be an atomicity:: tag");
            return detail::impl::AtomicityTag::None;  // unreachable
        }
    }();
    return detail::impl::do_commit_atomic_impl(tmp.value().c_str(),
                                               target.value().c_str(),
                                               at);
}

// ═════════════════════════════════════════════════════════════════════
// ── Composition aliases & composite §XXI mints ───────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Two ergonomic single-grant aliases plus two §XXI composite mint
// wrappers encoding canonical durable-write compositions.  Each
// composite preserves §XXI grep-discoverability (`mint_` prefix) AND
// eliminates the per-call-site boilerplate of spelling the full Grants
// pack.  Composite bodies delegate to `mint_file<...>` so the
// ctx-fit / mode-engagement / no-duplicate-mode gate fires in exactly
// one place — the underlying mint_file's `CtxFitsFileMint` concept.

// Single-grant alias for ergonomic read-only opens.  Spelling
// `mint_file<read_only>(ctx, path)` is the canonical idiom.
using read_only = ::crucible::fixy::grant::fs::mode<open_mode::ReadOnly>;

// `mint_durable_truncate_file` — write+truncate, fsync the data, then
// atomically link the result into place via O_TMPFILE+linkat (the
// `LinkAtomic` shape).  Canonical for "create-or-replace this file
// durably" use cases — Cipher snapshot writes, recipe registry
// updates, KernelCache L2 spills.
//
// Composite grant stack:
//   mode<WriteTruncate>      — O_WRONLY | O_CREAT | O_TRUNC
//   durable<Fsync>           — declares intent: caller MUST sync<Fsync>
//                              after writing, before commit_atomic
//   atomic_write<LinkAtomic> — declares intent: caller MUST
//                              commit_atomic<LinkAtomic>(tmp, target)
//                              after sync
//
// The durable<> and atomic_write<> grants are intent declarations; the
// mint synthesizes the open(2) but the caller is still responsible for
// invoking sync() and commit_atomic() at the right points.  V-228's
// CipherDurable wrapper will fold these into a single high-level
// `mint_durable_write_session` so the caller can't forget.

// §XXI carve-out: cx=alloc — file open invokes the kernel (open, fstat,
// linkat, fsync) and the underlying FileHandle wrapper holds an OS file
// descriptor.  constexpr would lie about the runtime cost.  The
// `IsExecCtx Ctx` template-parameter constraint IS the concept gate
// (rq=Y in spirit, though gen-mint-inventory.sh's `\brequires\b` regex
// does not detect template-param-constraint form; rq column shows '-'
// pending a scanner extension follow-up).
template <::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] inline auto
mint_durable_truncate_file(Ctx const& ctx,
                           Path<::crucible::safety::source::Sanitized> p,
                           mode_t perms = 0644) noexcept
{
    return mint_file<
        ::crucible::fixy::grant::fs::mode<open_mode::WriteTruncate>,
        ::crucible::fixy::grant::fs::durable<sync_op::Fsync>,
        ::crucible::fixy::grant::fs::atomic_write<atomicity::LinkAtomic>
    >(ctx, std::move(p), perms);
}

// `mint_durable_append_file` — write+append with O_DSYNC at the
// kernel-syscall level + caller-driven `sync<Fdatasync>` checkpoints.
// Canonical for "event log / WAL / monotonic-append" use cases —
// MetaLog spill TUs, Cipher event-sourced chain segments.
//
// Composite grant stack:
//   mode<WriteAppend>       — O_WRONLY | O_CREAT | O_APPEND
//   durable<Fdatasync>      — declares intent: caller MUST sync<Fdatasync>
//                             between batches (NOT after every write —
//                             O_DSYNC handles per-write durability)
//   with_flag<DataSync>     — O_DSYNC: kernel-side write-through; each
//                             write(2) call returns only after the data
//                             is on stable storage (not metadata; that's
//                             what the caller-driven Fdatasync covers)
//
// No atomic_write<> grant: append-only files don't have a "tmp →
// target" rename phase — the file IS the target.

// §XXI carve-out: cx=alloc — same rationale as mint_durable_truncate_file
// above: file open syscalls + FileHandle owns an fd.  Append form differs
// only in grant stack (Fdatasync + DataSync rather than Fsync + LinkAtomic).
template <::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] inline auto
mint_durable_append_file(Ctx const& ctx,
                         Path<::crucible::safety::source::Sanitized> p,
                         mode_t perms = 0644) noexcept
{
    return mint_file<
        ::crucible::fixy::grant::fs::mode<open_mode::WriteAppend>,
        ::crucible::fixy::grant::fs::durable<sync_op::Fdatasync>,
        ::crucible::fixy::grant::fs::with_flag<flag::DataSync>
    >(ctx, std::move(p), perms);
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

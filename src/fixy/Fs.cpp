// SPDX-License-Identifier: BUSL-1.1
// ============================================================================
// FIXY-V-229 — src/fixy/Fs.cpp — impl TU for fixy::fs syscall bodies
// ============================================================================
//
// Hosts the non-template syscall wrapper bodies declared at
// `crucible::fixy::fs::detail::impl::` in include/crucible/fixy/Fs.h.
// The header's mint_file<Grants...> / sync<SyncOp> /
// commit_atomic<Atomicity> templates compile-time map their template
// parameters to a small enum tag and forward into the impls defined
// here; the actual ::open() / ::fdatasync() / ::fsync() / ::rename() /
// ::renameat2() calls live in this single translation unit.
//
// Why this split:
//   * Every TU that calls mint_file<...> would otherwise emit a fresh
//     inline ::open() wrapper body per instantiation.  Hoisting the
//     syscall body to one .cpp deduplicates the wrapper across the
//     whole binary — every call site collapses to a CALL into the impl
//     instead of an inlined ::open() body + errno-to-error_code dance.
//   * The TU is bench-discoverable: future microbench / profiling can
//     pin syscall hotspots to src/fixy/Fs.cpp without crawling the
//     header expansion graph.
//   * The header templates remain inline because their `if constexpr`
//     dispatch IS template-parameter-dependent and must happen at the
//     call site; only the syscall body is the non-template hot path.
//
// ── Axiom coverage (code_guide §II) ──────────────────────────────────
//
//   InitSafe   — all locals NSDMI-initialized via std::expected ctor.
//   TypeSafe   — typed SyncOpTag / AtomicityTag enums; no raw POSIX
//                ints in the public-impl signature except `fd`.
//   NullSafe   — every ::open()/::fdatasync()/etc. failure produces
//                std::unexpected with errno wrapped in
//                std::system_category(); never returns silently.
//   MemSafe    — FileHandle is move-only RAII; the impl returns by
//                value (NRVO).  No raw fd leaks: every error path
//                discards the fd before constructing FileHandle.
//   BorrowSafe — pure function bodies; no shared state, no aliased
//                mutation.
//   ThreadSafe — every syscall is thread-safe by POSIX guarantee.
//   LeakSafe   — FileHandle ctor takes ownership of the fd; on the
//                error path we never reach the ctor.
//   DetSafe    — identical (path, flags, perms) produce identical
//                syscall sequences on any Linux build.

#include <crucible/fixy/Fs.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <expected>
#include <system_error>
#include <utility>

namespace crucible::fixy::fs::detail::impl {

// ── do_open_impl ────────────────────────────────────────────────────
//
// Single ::open() call site.  `flags` is the OR-folded grant-implied
// O_* bits + the implicit O_CLOEXEC (folded by the template caller).
// `perms` is the file-creation mode argument (only consulted when
// O_CREAT is in `flags`, but ::open() ignores it harmlessly otherwise).
//
// On success: returns the bare FileHandle (move-only RAII).
// On failure: std::unexpected wrapping errno in std::system_category().

std::expected<::crucible::safety::FileHandle, std::error_code>
do_open_impl(const char* path, int flags, ::mode_t perms) noexcept {
    const int fd = ::open(path, flags, perms);  // SYSCALL-CAP-OK: do_open_impl, sole caller mint_file ctx-gate (CtxFitsFileMint)
    if (fd < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return ::crucible::safety::FileHandle{fd};
}

// ── do_open_dirfd_impl ──────────────────────────────────────────────
//
// O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_RDONLY ::open().  Used by
// open_dirfd() to mint a Dirfd for openat() sandboxing.  Trailing
// component MUST be a directory; symlinks refused (O_NOFOLLOW).

std::expected<::crucible::safety::FileHandle, std::error_code>
do_open_dirfd_impl(const char* dir_path) noexcept {
    const int fd = ::open(dir_path,
                          O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_RDONLY);
    if (fd < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return ::crucible::safety::FileHandle{fd};
}

// ── do_sync_impl ────────────────────────────────────────────────────
//
// Durability syscall dispatch.  Caller pre-checks fd >= 0.
//
//   Fdatasync       → ::fdatasync(fd)
//   Fsync           → ::fsync(fd)
//   FsyncParentDir  → ::fsync(fd)   (caller's handle is a dirfd; the
//                                     fsync call flushes the directory
//                                     entry)
//   Msync           → EINVAL (route through V-225 mmap surface)
//
// The Msync branch returns EINVAL because Fs.h has no mapped-address
// + length signature; the caller asked for the wrong surface.  Routing
// the diagnostic at the impl boundary keeps the template surface
// uniform (one return type) while still failing-loud on misuse.

std::expected<void, std::error_code>
do_sync_impl(int fd, SyncOpTag op) noexcept {
    int rc = 0;
    switch (op) {
        case SyncOpTag::Fdatasync:
            rc = ::fdatasync(fd);  // SYSCALL-CAP-OK: do_sync_impl, sole caller sync<SyncOp> ctx-gate (CtxFitsSync)
            break;
        case SyncOpTag::Fsync:
            rc = ::fsync(fd);  // SYSCALL-CAP-OK: do_sync_impl, sole caller sync<SyncOp> ctx-gate (CtxFitsSync)
            break;
        case SyncOpTag::FsyncParentDir:
            // The handle MUST be a dirfd (Dirfd type-discipline
            // upstream).  ::fsync() on a non-dir fd returns EINVAL
            // naturally — we let the kernel speak.
            rc = ::fsync(fd);  // SYSCALL-CAP-OK: do_sync_impl, sole caller sync<SyncOp> ctx-gate (CtxFitsSync)
            break;
        case SyncOpTag::Msync:
            // Mmap surface owns the syscall — Fs.h's signature
            // doesn't carry (address, length).  Reject loudly.
            return std::unexpected{
                std::error_code{EINVAL, std::system_category()}};
        default:
            // SyncOpTag is a closed enum — any other value is UB.
            // The template caller's static_assert chain already
            // rejected unknown tags at the call site; reaching here
            // means an out-of-range cast was injected.
            std::unreachable();
    }
    if (rc < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

// ── do_commit_atomic_impl ───────────────────────────────────────────
//
// Tmp → target atomic commit syscall.
//
//   Rename              → ::rename(tmp, target)            (clobbers)
//   RenameAt2NoReplace  → ::renameat2(AT_FDCWD, tmp,
//                                     AT_FDCWD, target,
//                                     RENAME_NOREPLACE)   (refuses
//                                                          clobber)
//   LinkAtomic          → ENOSYS (V-228's CipherDurable will wire the
//                                  O_TMPFILE + linkat AT_EMPTY_PATH fd
//                                  channel here)
//   None                → success (no-op, caller-asserted)
//
// The LinkAtomic ENOSYS branch is intentional placeholder: the shape
// IS exposed, but the fd-channel wiring waits on V-228's higher-level
// CipherDurableHandle to thread the tmp fd through to linkat.  Bare
// Fs.h commit_atomic doesn't have the fd argument shape yet.

std::expected<void, std::error_code>
do_commit_atomic_impl(const char* tmp,
                      const char* target,
                      AtomicityTag atomicity) noexcept {
    int rc = 0;
    switch (atomicity) {
        case AtomicityTag::Rename:
            rc = ::rename(tmp, target);  // SYSCALL-CAP-OK: do_commit_atomic_impl, sole caller commit_atomic<Atomicity> ctx-gate (CtxFitsCommitAtomic)
            break;
        case AtomicityTag::RenameAt2NoReplace:
            rc = ::renameat2(AT_FDCWD, tmp,
                             AT_FDCWD, target,
                             RENAME_NOREPLACE);
            break;
        case AtomicityTag::LinkAtomic:
            // V-228 CipherDurable owns the fd-channel wiring; the bare
            // path-pair signature here cannot do linkat(AT_EMPTY_PATH).
            return std::unexpected{
                std::error_code{ENOSYS, std::system_category()}};
        case AtomicityTag::None:
            // Caller-asserted no-op.  Both path arguments are
            // intentionally unused.
            (void)tmp;
            (void)target;
            return {};
        default:
            // AtomicityTag is a closed enum — any other value is UB.
            // The template caller's static_assert chain already
            // rejected unknown tags at the call site; reaching here
            // means an out-of-range cast was injected.
            std::unreachable();
    }
    if (rc < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

}  // namespace crucible::fixy::fs::detail::impl

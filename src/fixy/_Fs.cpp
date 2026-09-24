// SPDX-License-Identifier: BUSL-1.1
//
// The syscall bodies live in one translation unit so that the header
// templates forwarding here share a single wrapper, instead of emitting an
// inline copy of it per instantiation.

#include <crucible/fixy/_Fs.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <expected>
#include <system_error>
#include <utility>

namespace crucible::fixy::fs::detail::impl {

// ::open() consults perms only when flags carries O_CREAT and ignores it
// otherwise, so perms is passed unconditionally.

std::expected<::crucible::safety::FileHandle, std::error_code> do_open_impl(const char* path, int flags,
                                                                            ::mode_t perms) noexcept {
    const int fd =
        ::open(path, flags, perms);  // SYSCALL-CAP-OK: do_open_impl, sole caller mint_file ctx-gate (CtxFitsFileMint)
    if (fd < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return ::crucible::safety::FileHandle{fd};
}

std::expected<::crucible::safety::FileHandle, std::error_code> do_open_dirfd_impl(const char* dir_path) noexcept {
    const int fd = ::open(dir_path, O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_RDONLY);
    if (fd < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return ::crucible::safety::FileHandle{fd};
}

// The caller has already checked that fd is non-negative.

std::expected<void, std::error_code> do_sync_impl(int fd, SyncOpTag op) noexcept {
    int rc = 0;
    switch (op) {
        case SyncOpTag::Fdatasync:
            rc = ::fdatasync(fd);  // SYSCALL-CAP-OK: do_sync_impl, sole caller sync<SyncOp> ctx-gate (CtxFitsSync)
            break;
        case SyncOpTag::Fsync:
            rc = ::fsync(fd);  // SYSCALL-CAP-OK: do_sync_impl, sole caller sync<SyncOp> ctx-gate (CtxFitsSync)
            break;
        case SyncOpTag::FsyncParentDir:
            // The handle is a directory fd, and fsync on it flushes the
            // directory entry.
            rc = ::fsync(fd);  // SYSCALL-CAP-OK: do_sync_impl, sole caller sync<SyncOp> ctx-gate (CtxFitsSync)
            break;
        case SyncOpTag::Msync:
            // msync needs an address and a length, which this signature does
            // not carry.  The mapped-memory surface owns that call.
            return std::unexpected{std::error_code{EINVAL, std::system_category()}};
        default:
            // The tag enum is closed and the caller rejects unknown tags
            // before the call, so any other value can only come from an
            // out-of-range cast.
            std::unreachable();
    }
    if (rc < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

std::expected<void, std::error_code> do_commit_atomic_impl(const char* tmp, const char* target,
                                                           AtomicityTag atomicity) noexcept {
    int rc = 0;
    switch (atomicity) {
        case AtomicityTag::Rename:
            rc = ::rename(
                tmp,
                target);  // SYSCALL-CAP-OK: do_commit_atomic_impl, sole caller commit_atomic<Atomicity> ctx-gate (CtxFitsCommitAtomic)
            break;
        case AtomicityTag::RenameAt2NoReplace:
            rc = ::renameat2(AT_FDCWD, tmp, AT_FDCWD, target, RENAME_NOREPLACE);
            break;
        case AtomicityTag::LinkAtomic:
            // linkat with AT_EMPTY_PATH needs the fd of the temporary file,
            // which this path-pair signature does not carry.
            return std::unexpected{std::error_code{ENOSYS, std::system_category()}};
        case AtomicityTag::None:
            (void)tmp;
            (void)target;
            return {};
        default:
            // The tag enum is closed and the caller rejects unknown tags
            // before the call, so any other value can only come from an
            // out-of-range cast.
            std::unreachable();
    }
    if (rc < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return {};
}

}  // namespace crucible::fixy::fs::detail::impl

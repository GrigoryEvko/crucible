#pragma once

// ── crucible::safety::FileHandle ────────────────────────────────────
//
// RAII posix file-descriptor wrapper.  Move-only; destructor closes
// the fd.  Factories return FileHandle; callers check .is_open().
// Errors flow through negative-errno return codes, matching posix
// conventions (no exceptions — -fno-exceptions is the project default).
//
//   Axiom coverage: MemSafe, LeakSafe (code_guide §II).
//   Runtime cost:   sizeof(FileHandle) == sizeof(int); one syscall on
//                   destruction when the fd is live.
//
// ── Composition ─────────────────────────────────────────────────────
//
// FileHandle itself is already move-only RAII — Linear<FileHandle>
// wrapping it adds the [[nodiscard]] at construction + explicit
// consume() semantics for APIs that want to spell "this function
// takes ownership" in the signature.  Bare FileHandle is fine for
// local scopes where RAII-close is the whole lifecycle; reach for
// Linear<FileHandle> when the handle crosses method boundaries.
//
// LinearRefined<is_open_fd, int> is a related composition where the
// invariant "fd >= 0" is part of the type — useful for inner helpers
// that shouldn't have to recheck openness.  FileHandle carries that
// invariant behind its .is_open() method; prefer FileHandle unless
// the caller specifically wants the refinement shape.

#include <crucible/Platform.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Pre.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace crucible::safety {

// ── Fd newtype (fixy-A1-012) ────────────────────────────────────────
//
// TypeSafe distinguishes a POSIX file descriptor from any other int.
// Pre-fix, `FileHandle{int}` accepted arbitrary negative values; a
// caller mistakenly passing `-EBADF == -9` (an errno encoded as a
// negative-int sentinel) constructed an apparently-closed FileHandle
// that silently swallowed the error.  Two layers close the gap:
//
//   1. `Fd` newtype — int-sized wrapper.  `Fd{-9}` fires CRUCIBLE_PRE
//      at construction; only the sentinel `-1` (closed) and the POSIX
//      non-negative range pass.
//   2. `FileHandle::FileHandle(int)` ctor also gates on the same
//      pattern (kept as a backward-compatible boundary).
//
// `is_valid_pattern(v) := v == -1 || v >= 0` — the POSIX-fd shape.
// Anything else is the caller smuggling an errno-shaped value through
// the fd channel; we abort, not store-and-pretend-closed.
//
// sizeof(Fd) == sizeof(int): zero runtime cost.

class [[nodiscard]] Fd {
    int value_ = -1;

public:
    constexpr Fd() noexcept = default;  // sentinel: closed/invalid

    explicit constexpr Fd(int value) noexcept : value_{value} {
        CRUCIBLE_PRE(value == -1 || value >= 0);
    }

    [[nodiscard]] constexpr int  raw()      const noexcept { return value_; }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ >= 0; }

    [[nodiscard]] static constexpr Fd invalid() noexcept { return Fd{}; }

    [[nodiscard]] static constexpr bool is_valid_pattern(int v) noexcept {
        return v == -1 || v >= 0;
    }

    constexpr auto operator<=>(const Fd&) const noexcept = default;
};

static_assert(sizeof(Fd) == sizeof(int),
              "Fd must be a zero-cost int wrapper");

class [[nodiscard]] FileHandle {
    int fd_ = -1;

public:
    FileHandle() noexcept = default;
    explicit FileHandle(int fd) noexcept : fd_{fd} {
        // fixy-A1-012: reject errno-shaped negatives (e.g. -EBADF = -9).
        // Only the sentinel -1 (closed) and non-negative POSIX fds pass.
        CRUCIBLE_PRE(Fd::is_valid_pattern(fd));
    }

    explicit FileHandle(Fd fd) noexcept : fd_{fd.raw()} {}

    ~FileHandle() noexcept {
        if (fd_ >= 0) { (void)::close(fd_); }
    }

    FileHandle(const FileHandle&)            = delete("fd is unique; copy would double-close on destruction");
    FileHandle& operator=(const FileHandle&) = delete("fd is unique; copy would double-close on destruction");

    FileHandle(FileHandle&& other) noexcept : fd_{std::exchange(other.fd_, -1)} {}

    FileHandle& operator=(FileHandle&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) (void)::close(fd_);
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }
    [[nodiscard]] int  get()     const noexcept { return fd_; }

    // fixy-A1-012: typed accessor for new code.  `get()` survives for
    // backward compatibility with read_full/write_full and external
    // POSIX-int call sites; `fd()` is the §II.2 TypeSafe-aligned form
    // when handing the descriptor across a Crucible API boundary.
    [[nodiscard]] Fd fd() const noexcept { return Fd{fd_}; }

    // Release the fd without closing — caller takes ownership of cleanup.
    // Useful when handing the fd to a C API that will close it later.
    [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }

    // Explicit close — returns 0 on success, errno on error.  The dtor
    // ignores errors; callers who need the close() return code (e.g.
    // for writeback-sync diagnostics) call this explicitly.
    [[nodiscard]] int close_explicit() noexcept {
        int fd = std::exchange(fd_, -1);
        if (fd < 0) return 0;
        return ::close(fd) == 0 ? 0 : errno;
    }
};

static_assert(sizeof(FileHandle) == sizeof(int),
              "FileHandle must be a zero-cost int wrapper");

// ── Factories ───────────────────────────────────────────────────────
//
// O_CLOEXEC is always set — fd survival across exec() is never wanted
// in the Crucible runtime; a forgotten fd-leak to a child would be a
// FD exhaustion bug in production.

[[nodiscard]] inline FileHandle open_read(const char* path) noexcept {
    return FileHandle{::open(path, O_RDONLY | O_CLOEXEC)};
}

[[nodiscard]] inline FileHandle open_write_truncate(
    const char* path, mode_t mode = 0644) noexcept
{
    return FileHandle{::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode)};
}

[[nodiscard]] inline FileHandle open_write_append(
    const char* path, mode_t mode = 0644) noexcept
{
    return FileHandle{::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, mode)};
}

// ── I/O helpers ─────────────────────────────────────────────────────
//
// Read-all / write-all loops that survive EINTR.  Return bytes-read
// (read_full) or 0-on-success/negative-errno (write_full).  Inline
// so the hot-path compiles to a tight loop with no call overhead.

[[nodiscard]] inline ssize_t read_full(
    const FileHandle& h, std::span<std::byte> buf) noexcept
{
    if (!h.is_open()) return -EBADF;
    size_t total = 0;
    while (total < buf.size()) {
        ssize_t n = ::read(h.get(),
                           buf.data() + total,
                           buf.size() - total);
        if (n == 0) break;            // EOF
        if (n < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        total += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(total);
}

[[nodiscard]] inline int write_full(
    const FileHandle& h, std::span<const std::byte> buf) noexcept
{
    if (!h.is_open()) return -EBADF;
    size_t total = 0;
    while (total < buf.size()) {
        ssize_t n = ::write(h.get(),
                            buf.data() + total,
                            buf.size() - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        total += static_cast<size_t>(n);
    }
    return 0;
}

[[nodiscard]] inline off_t file_size(const FileHandle& h) noexcept {
    if (!h.is_open()) return -EBADF;
    struct stat st;
    if (::fstat(h.get(), &st) < 0) return -errno;
    return st.st_size;
}

} // namespace crucible::safety

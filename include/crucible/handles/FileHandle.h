#pragma once

// ── crucible::safety::FileHandle ────────────────────────────────────
//
// RAII posix file-descriptor wrapper.  Move-only; destructor closes
// the fd.  Factories return std::expected<FileHandle, std::error_code>
// (fixy-A1-013), and the I/O helpers (read_full / write_full /
// file_size) return std::expected<T, std::error_code> per §XII —
// fixy-A1-030 retired the legacy "negative result encodes errno"
// sentinel overload that conflicted with §X TypeSafe (negative
// off_t aliases real EOF reads and zero-byte writes).
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
#include <expected>
#include <span>
#include <system_error>
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
//
// fixy-A1-013: factories return `std::expected<FileHandle,
// std::error_code>`.  Per CLAUDE.md §XII, OS-side open() failure is
// expected-but-rare — the canonical `std::expected` shape — not a
// contract violation.  Pre-fix the factories returned a bare
// FileHandle whose `is_open()` was false on failure, with the real
// errno discarded: callers couldn't tell ENOENT from EACCES from
// EMFILE.  Now the error_code carries the POSIX errno via
// `std::system_category()`, callable-on-the-spot via `.message()`.

[[nodiscard]] inline std::expected<FileHandle, std::error_code>
open_read(const char* path) noexcept {
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return FileHandle{fd};
}

[[nodiscard]] inline std::expected<FileHandle, std::error_code>
open_write_truncate(const char* path, mode_t mode = 0644) noexcept {
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return FileHandle{fd};
}

[[nodiscard]] inline std::expected<FileHandle, std::error_code>
open_write_append(const char* path, mode_t mode = 0644) noexcept {
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, mode);
    if (fd < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return FileHandle{fd};
}

// ── I/O helpers ─────────────────────────────────────────────────────
//
// Read-all / write-all / fstat loops that survive EINTR.  Per CLAUDE.md
// §XII the expected-but-rare failure shape (closed fd, EIO, ENOSPC,
// fstat denied) returns std::expected<T, std::error_code> with the
// POSIX errno wrapped in std::system_category().  Pre-fix the
// signatures returned ssize_t/int/off_t with negative-result-encodes-
// errno: that sentinel collided with real ssize_t-positive bytes-read
// values on 32-bit builds where ssize_t was 32-bit signed, and made
// callers branch on `n < 0` while still risking implicit conversion
// to size_t (-EBADF would convert to a huge size).  std::expected
// closes the gap structurally — there is no positive ssize_t value
// that aliases an errno, and the success type is now size_t directly.
//
// Inline so the hot-path compiles to a tight loop with no call
// overhead.  Cost: one branch on .has_value() at the call site
// (~1 ns, predictable on the success path).

[[nodiscard]] inline std::expected<std::size_t, std::error_code>
read_full(const FileHandle& h, std::span<std::byte> buf) noexcept {
    if (!h.is_open()) {
        return std::unexpected{
            std::error_code{EBADF, std::system_category()}};
    }
    std::size_t total = 0;
    while (total < buf.size()) {
        const ssize_t n = ::read(h.get(),
                                 buf.data() + total,
                                 buf.size() - total);
        if (n == 0) break;            // EOF
        if (n < 0) {
            if (errno == EINTR) continue;
            return std::unexpected{
                std::error_code{errno, std::system_category()}};
        }
        total += static_cast<std::size_t>(n);
    }
    return total;
}

[[nodiscard]] inline std::expected<void, std::error_code>
write_full(const FileHandle& h, std::span<const std::byte> buf) noexcept {
    if (!h.is_open()) {
        return std::unexpected{
            std::error_code{EBADF, std::system_category()}};
    }
    std::size_t total = 0;
    while (total < buf.size()) {
        const ssize_t n = ::write(h.get(),
                                  buf.data() + total,
                                  buf.size() - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return std::unexpected{
                std::error_code{errno, std::system_category()}};
        }
        total += static_cast<std::size_t>(n);
    }
    return {};
}

[[nodiscard]] inline std::expected<off_t, std::error_code>
file_size(const FileHandle& h) noexcept {
    if (!h.is_open()) {
        return std::unexpected{
            std::error_code{EBADF, std::system_category()}};
    }
    struct stat st;
    if (::fstat(h.get(), &st) < 0) {
        return std::unexpected{
            std::error_code{errno, std::system_category()}};
    }
    return st.st_size;
}

} // namespace crucible::safety

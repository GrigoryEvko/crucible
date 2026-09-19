#pragma once

#include <crucible/Platform.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/_Pre.h>

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

// A POSIX descriptor is either the closed sentinel -1 or a non-negative
// number. Any other negative value is an errno that a caller has
// smuggled through the descriptor channel. Storing it would present as
// a closed handle and discard the error, so the precondition rejects it
// rather than accept it as closed.

class [[nodiscard]] Fd {
    int value_ = -1;

public:
    constexpr Fd() noexcept = default;

    explicit constexpr Fd(int value) noexcept : value_{value} { CRUCIBLE_PRE(value == -1 || value >= 0); }

    [[nodiscard]] constexpr int raw() const noexcept { return value_; }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ >= 0; }

    [[nodiscard]] static constexpr Fd invalid() noexcept { return Fd{}; }

    [[nodiscard]] static constexpr bool is_valid_pattern(int v) noexcept { return v == -1 || v >= 0; }

    constexpr auto operator<=>(const Fd&) const noexcept = default;
};

static_assert(sizeof(Fd) == sizeof(int), "Fd must be a zero-cost int wrapper");

class [[nodiscard]] FileHandle {
    int fd_ = -1;

public:
    FileHandle() noexcept = default;
    explicit FileHandle(int fd) noexcept : fd_{fd} { CRUCIBLE_PRE(Fd::is_valid_pattern(fd)); }

    explicit FileHandle(Fd fd) noexcept : fd_{fd.raw()} {}

    ~FileHandle() noexcept {
        // close can fail while flushing writeback, and a destructor has
        // nowhere to report it. A caller that needs the result calls
        // close_explicit first.
        if (fd_ >= 0) {
            (void)::close(fd_);
        }
    }

    FileHandle(const FileHandle&) = delete("fd is unique; copy would double-close on destruction");
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
    [[nodiscard]] int get() const noexcept { return fd_; }

    [[nodiscard]] Fd fd() const noexcept { return Fd{fd_}; }

    [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }

    [[nodiscard]] int close_explicit() noexcept {
        int fd = std::exchange(fd_, -1);
        if (fd < 0) return 0;
        return ::close(fd) == 0 ? 0 : errno;
    }
};

static_assert(sizeof(FileHandle) == sizeof(int), "FileHandle must be a zero-cost int wrapper");

// O_CLOEXEC is unconditional and is not offered as a caller option. A
// descriptor that survives exec into a child process is never wanted
// here, and one forgotten descriptor per child exhausts the table.

[[nodiscard]] inline std::expected<FileHandle, std::error_code> open_read(const char* path) noexcept {
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return FileHandle{fd};
}

[[nodiscard]] inline std::expected<FileHandle, std::error_code> open_write_truncate(const char* path,
                                                                                    mode_t mode = 0644) noexcept {
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return FileHandle{fd};
}

[[nodiscard]] inline std::expected<FileHandle, std::error_code> open_write_append(const char* path,
                                                                                  mode_t mode = 0644) noexcept {
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, mode);
    if (fd < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return FileHandle{fd};
}

[[nodiscard]] inline std::expected<std::size_t, std::error_code> read_full(const FileHandle& h,
                                                                           std::span<std::byte> buf) noexcept {
    if (!h.is_open()) {
        return std::unexpected{std::error_code{EBADF, std::system_category()}};
    }
    std::size_t total = 0;
    while (total < buf.size()) {
        const ssize_t n = ::read(h.get(), buf.data() + total, buf.size() - total);
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            return std::unexpected{std::error_code{errno, std::system_category()}};
        }
        total += static_cast<std::size_t>(n);
    }
    return total;
}

[[nodiscard]] inline std::expected<void, std::error_code> write_full(const FileHandle& h,
                                                                     std::span<const std::byte> buf) noexcept {
    if (!h.is_open()) {
        return std::unexpected{std::error_code{EBADF, std::system_category()}};
    }
    std::size_t total = 0;
    while (total < buf.size()) {
        const ssize_t n = ::write(h.get(), buf.data() + total, buf.size() - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return std::unexpected{std::error_code{errno, std::system_category()}};
        }
        total += static_cast<std::size_t>(n);
    }
    return {};
}

[[nodiscard]] inline std::expected<off_t, std::error_code> file_size(const FileHandle& h) noexcept {
    if (!h.is_open()) {
        return std::unexpected{std::error_code{EBADF, std::system_category()}};
    }
    struct stat st;
    if (::fstat(h.get(), &st) < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return st.st_size;
}

}  // namespace crucible::safety

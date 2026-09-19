#pragma once

#include <crucible/Platform.h>
#include <crucible/safety/_Pre.h>

#include <cerrno>
#include <cstdio>
#include <utility>

namespace crucible::safety {

class [[nodiscard]] OwnedFile {
    std::FILE* fp_ = nullptr;

public:
    OwnedFile() noexcept = default;

    // A null handle is accepted rather than rejected, so a caller can hand the
    // result of a failed open straight in and ask is_open about it.
    explicit OwnedFile(std::FILE* fp) noexcept : fp_{fp} {}

    ~OwnedFile() noexcept {
        // A close failure here is unactionable, so the result is discarded.
        if (fp_ != nullptr) {
            (void)std::fclose(fp_);
        }
    }

    OwnedFile(const OwnedFile&) = delete("FILE* is unique; copy would double-close on destruction");
    OwnedFile& operator=(const OwnedFile&) = delete("FILE* is unique; copy would double-close on destruction");

    OwnedFile(OwnedFile&& other) noexcept : fp_{std::exchange(other.fp_, nullptr)} {}

    OwnedFile& operator=(OwnedFile&& other) noexcept {
        if (this != &other) {
            if (fp_ != nullptr) (void)std::fclose(fp_);
            fp_ = std::exchange(other.fp_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] bool is_open() const noexcept { return fp_ != nullptr; }
    explicit operator bool() const noexcept { return fp_ != nullptr; }

    // A borrow.  Ownership stays here, so the caller must not close what this
    // returns.
    [[nodiscard]] std::FILE* get() const noexcept { return fp_; }

    [[nodiscard]] std::FILE* release() noexcept { return std::exchange(fp_, nullptr); }

    // Close early when the flush result matters, since the destructor cannot
    // report one.
    [[nodiscard]] int close_explicit() noexcept {
        std::FILE* fp = std::exchange(fp_, nullptr);
        if (fp == nullptr) return 0;
        return std::fclose(fp) == 0 ? 0 : errno;
    }
};

static_assert(sizeof(OwnedFile) == sizeof(std::FILE*), "OwnedFile must be a zero-cost FILE* wrapper");

}  // namespace crucible::safety

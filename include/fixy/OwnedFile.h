#pragma once

// Exclusive ownership of one stdio stream, closed on destruction.
//
// A null handle is accepted rather than rejected, so a caller can hand
// the result of a failed open straight in and ask is_open about it.
// That is why there is no precondition on the constructor: refusing the
// null would force every caller to branch before it could ask.
//
// Old spelling: include/crucible/safety/OwnedFile.h.

#include <foundation/Platform.h>

#include <cerrno>
#include <cstdio>
#include <utility>

namespace fixy {

class [[nodiscard]] OwnedFile {
    std::FILE* fp_ = nullptr;

public:
    OwnedFile() noexcept = default;

    explicit OwnedFile(std::FILE* fp) noexcept : fp_{fp} {}

    ~OwnedFile() noexcept {
        // A close failure here is unactionable, so the result is discarded.
        // close_explicit is the door for a caller that needs the answer.
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

    // A borrow.  Ownership stays here, so the caller must not close what
    // this returns.
    [[nodiscard]] std::FILE* get() const noexcept { return fp_; }

    [[nodiscard]] std::FILE* release() noexcept { return std::exchange(fp_, nullptr); }

    // Close early when the flush result matters, since the destructor
    // cannot report one.  Returns 0 on success, otherwise errno.
    [[nodiscard]] int close_explicit() noexcept {
        std::FILE* fp = std::exchange(fp_, nullptr);
        if (fp == nullptr) return 0;
        return std::fclose(fp) == 0 ? 0 : errno;
    }
};

static_assert(sizeof(OwnedFile) == sizeof(std::FILE*), "OwnedFile must be a zero-cost FILE* wrapper");

namespace detail::owned_file_self_test {

static_assert(!std::is_copy_constructible_v<OwnedFile>, "OwnedFile must be move-only — copy would double-close");
static_assert(!std::is_copy_assignable_v<OwnedFile>);
static_assert(std::is_nothrow_move_constructible_v<OwnedFile>);
static_assert(std::is_nothrow_move_assignable_v<OwnedFile>);
static_assert(std::is_nothrow_default_constructible_v<OwnedFile>);
static_assert(std::is_nothrow_destructible_v<OwnedFile>);

// The empty state is the only one reachable without a real stream, and
// every query answers on it.  Closing an empty handle reports success,
// because there was nothing whose flush could fail.
inline void runtime_smoke_test() {
    OwnedFile empty{};
    if (empty.is_open()) std::abort();
    if (static_cast<bool>(empty)) std::abort();
    if (empty.get() != nullptr) std::abort();
    if (empty.release() != nullptr) std::abort();
    if (empty.close_explicit() != 0) std::abort();

    OwnedFile moved = std::move(empty);
    if (moved.is_open()) std::abort();

    OwnedFile assigned{};
    assigned = std::move(moved);
    if (assigned.is_open()) std::abort();
}

}  // namespace detail::owned_file_self_test

}  // namespace fixy

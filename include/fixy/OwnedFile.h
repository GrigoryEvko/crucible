#pragma once

// Exclusive ownership of one stdio stream, closed on destruction.
//
// Old spelling: include/crucible/safety/OwnedFile.h.  One deviation: the
// construction door.
//
// The old constructor took a FILE* and was public, and the destructor
// calls std::fclose on whatever it holds.  So `OwnedFile f{stdin};` was
// one line of ordinary-looking code that closed the process's standard
// input on scope exit, and a stream owned by some other handle could be
// closed twice.  That is the same defect fixy/OwnedMmap.h had over an
// address, and it takes the same repair: the constructor is private, and
// the two doors below perform the open themselves, so a stream that
// exists is one std::fopen or std::tmpfile returned to this class.
//
// There is no token in that arrangement, which is the point.  A passkey
// would have had to travel through fixy::mint_linear, a generic variadic
// forwarder that Qtt befriends for every T, so the key's reach would be
// every type in the tree.  Putting the open next to the constructor
// leaves nothing to forward to: is_constructible_v<OwnedFile, FILE*> is
// false, and mint_linear's own requires-clause refuses the call.
//
// The empty handle stays default-constructible for the reason the empty
// region does: it claims nothing and closes nothing.  A failed open is
// not an empty handle; it is the errno, handed back with no handle built.
//
// What the doors do NOT decide is who may open.  They are reachable
// without a context, like OwnedMmap::map_region and OwnedFd::open_path,
// and reaching one mints no false claim, because what it hands back is
// what libc returned.  A caller that wants a gate on the open stands one
// layer up, where fixy/os/Fs.h gates OwnedFd; nothing there mints an
// OwnedFile today.  The stdio calls are not in the syscall-capability
// guard's name set, so they carry no allowlist line.

#include <foundation/Platform.h>

#include <cerrno>
#include <cstdio>
#include <expected>
#include <type_traits>
#include <utility>

namespace fixy {

class [[nodiscard]] OwnedFile {
    std::FILE* fp_ = nullptr;

    // Private, and the two doors below are its only callers.  A public
    // one let any stream be claimed, and the destructor closes whatever
    // was claimed.
    explicit OwnedFile(std::FILE* fp) noexcept : fp_{fp} {}

public:
    // The empty handle owns nothing and closes nothing, so it stays
    // public.
    OwnedFile() noexcept = default;

    // The door for a named path.  The mode string is std::fopen's own,
    // passed through unchanged; a typed spelling of it belongs to the
    // layer that would gate the open.  Returns the errno on failure and
    // no handle.
    [[nodiscard]] static std::expected<OwnedFile, int> open_path(const char* path, const char* mode) noexcept {
        std::FILE* const fp = std::fopen(path, mode);
        if (fp == nullptr) {
            return std::unexpected{errno};
        }
        return OwnedFile{fp};
    }

    // The door for an anonymous stream, which std::tmpfile creates and
    // the OS unlinks, so it has no path to open by name.
    [[nodiscard]] static std::expected<OwnedFile, int> open_temporary() noexcept {
        std::FILE* const fp = std::tmpfile();
        if (fp == nullptr) {
            return std::unexpected{errno};
        }
        return OwnedFile{fp};
    }

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

    // The inverse door: ownership leaves with the pointer, and the close
    // becomes the caller's.  Only what this handle owns can leave it.
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

// The construction door, checked from a scope the class does not
// befriend.  A public constructor over a FILE* let a caller claim a
// stream it never opened, and the destructor closes whatever it holds,
// so `OwnedFile{stdin}` closed standard input on scope exit.  The two
// doors are the only way to a live handle, and each builds one only
// from what libc returned.
static_assert(!std::is_constructible_v<OwnedFile, std::FILE*>,
              "The constructor that claims a stream must not be public.  A caller could hand it stdin, or a "
              "stream another handle owns, and the destructor would fclose it.  Take a handle from open_path "
              "or open_temporary.");
static_assert(!std::is_constructible_v<OwnedFile, int>,
              "There is no descriptor form either: fdopen over a descriptor owned elsewhere would fclose that "
              "descriptor.  A descriptor is OwnedFd's business, in fixy/os/Fs.h.");
static_assert(std::is_default_constructible_v<OwnedFile>, "The empty handle claims nothing, so it stays reachable.");

}  // namespace detail::owned_file_self_test

}  // namespace fixy

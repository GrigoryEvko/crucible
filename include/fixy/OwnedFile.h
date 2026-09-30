#pragma once

// Exclusive ownership of one stdio stream, closed on destruction.
//
// The destructor calls std::fclose on the stream that the handle holds.
// A public constructor from a FILE* lets the line `OwnedFile f{stdin};`
// close the standard input of the process at the end of its scope, and
// nothing in that line shows the risk.  It also lets two handles hold one
// stream, and the stream then closes two times.  So the constructor is
// private, as the constructor of fixy/OwnedMmap.h from an address is.
// OwnedFileDoor below is its one caller.  The door opens the stream
// itself, so each stream that this class holds came from std::fopen or
// std::tmpfile.
//
// The opens are in a door class, and no passkey gates them.  A passkey
// would have to go through fixy::mint_linear, a variadic forwarder that
// Qtt befriends for every T, so the key would reach every type in the
// tree.  is_constructible_v<OwnedFile, FILE*> is false, and the
// constraint of mint_linear refuses the call.
//
// An open enters the kernel, and it can wait there on the file system,
// so each open is a mint that asks for a context that owns IO and Block.
// The two mints are mint_owned_file for a named path and
// mint_temporary_file for an anonymous stream.  The door class is not a
// template, and only the two mints are its friends.  An explicit
// specialization of a mint still has to satisfy the context gate, and
// through the door it reaches only a real open.
//
// The release has the two gates of OwnedMmap::release: a leak atom that
// names why the stream leaves without a close, and an rvalue handle.
//
// The empty handle stays default-constructible for the reason the empty
// region does: it claims nothing and closes nothing.  A failed open is
// not an empty handle.  It is the errno, handed back with no handle built.
//
// The stdio calls are not in the syscall-capability guard's name set, so
// they carry no allowlist line.

#include <fixy/atoms/Os.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <cerrno>
#include <cstdio>
#include <expected>
#include <type_traits>
#include <utility>

namespace fixy {

// The gate of the two opening mints below.  An open enters the kernel, and
// it can wait there on the file system, so the context must own IO and
// Block.  fixy/os/Fs.h asks for the same two atoms when it opens a
// descriptor.
template <typename Ctx>
concept CtxFitsFileOpen =
    ::foundation::effects::CtxOwnsAllOf<Ctx, ::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;

class OwnedFileDoor;

class [[nodiscard]] OwnedFile {
    std::FILE* fp_ = nullptr;

    // Private, and OwnedFileDoor is its one caller.  A public one lets any
    // stream be claimed, and the destructor closes whatever was claimed.
    explicit OwnedFile(std::FILE* fp) noexcept : fp_{fp} {}

    friend class OwnedFileDoor;

public:
    // The empty handle owns nothing and closes nothing, so it stays
    // public.
    OwnedFile() noexcept = default;

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
    //
    // The witness has to be a leak atom, whose tag names why the stream
    // leaves without a close here, so each such site is in the source and
    // a search finds it.  The method binds only to an rvalue, so a second
    // release needs a second explicit move of the same handle.
    template <atom::IsLeakAtom LeakAtom>
    [[nodiscard]] std::FILE* release(LeakAtom) && noexcept {
        return std::exchange(fp_, nullptr);
    }

    // Close early when the flush result matters, since the destructor
    // cannot report one.  Returns 0 on success, otherwise errno.
    [[nodiscard]] int close_explicit() noexcept {
        std::FILE* fp = std::exchange(fp_, nullptr);
        if (fp == nullptr) return 0;
        return std::fclose(fp) == 0 ? 0 : errno;
    }
};

static_assert(sizeof(OwnedFile) == sizeof(std::FILE*), "OwnedFile must be a zero-cost FILE* wrapper");

// The two opens.  No object of the door exists, and its members are
// private, so the mints below are the only callers.
class OwnedFileDoor final : ::foundation::NoObject<OwnedFileDoor> {
    // The mode string is std::fopen's own, passed through unchanged.
    [[nodiscard]] static std::expected<OwnedFile, int> open_path_(const char* path, const char* mode) noexcept {
        std::FILE* const fp = std::fopen(path, mode);
        if (fp == nullptr) {
            return std::unexpected{errno};
        }
        return OwnedFile{fp};
    }

    // std::tmpfile creates the stream and the OS unlinks it, so it has no
    // path to open by name.
    [[nodiscard]] static std::expected<OwnedFile, int> open_temporary_() noexcept {
        std::FILE* const fp = std::tmpfile();
        if (fp == nullptr) {
            return std::unexpected{errno};
        }
        return OwnedFile{fp};
    }

    template <typename Ctx>
        requires CtxFitsFileOpen<Ctx>
    friend std::expected<OwnedFile, int> mint_owned_file(Ctx const& ctx, const char* path, const char* mode) noexcept;

    template <typename Ctx>
        requires CtxFitsFileOpen<Ctx>
    friend std::expected<OwnedFile, int> mint_temporary_file(Ctx const& ctx) noexcept;
};

// Opens the named path with the std::fopen mode.  Returns the errno on
// failure and no handle.
// §XXI carve-out: cx=alloc — opening a stream invokes the kernel.
template <typename Ctx>
    requires CtxFitsFileOpen<Ctx>
[[nodiscard]] std::expected<OwnedFile, int> mint_owned_file(Ctx const&, const char* path, const char* mode) noexcept {
    return OwnedFileDoor::open_path_(path, mode);
}

// Opens an anonymous stream that the OS unlinks.  Returns the errno on
// failure and no handle.
// §XXI carve-out: cx=alloc — opening a stream invokes the kernel.
template <typename Ctx>
    requires CtxFitsFileOpen<Ctx>
[[nodiscard]] std::expected<OwnedFile, int> mint_temporary_file(Ctx const&) noexcept {
    return OwnedFileDoor::open_temporary_();
}

namespace detail::owned_file_self_test {

static_assert(!std::is_copy_constructible_v<OwnedFile>, "OwnedFile must be move-only — copy would double-close");
static_assert(!std::is_copy_assignable_v<OwnedFile>);
static_assert(std::is_nothrow_move_constructible_v<OwnedFile>);
static_assert(std::is_nothrow_move_assignable_v<OwnedFile>);
static_assert(std::is_nothrow_default_constructible_v<OwnedFile>);
static_assert(std::is_nothrow_destructible_v<OwnedFile>);

// The construction door, checked from a scope the class does not
// befriend.  A public constructor over a FILE* lets a caller claim a
// stream it never opened, and the destructor closes whatever it holds,
// so `OwnedFile{stdin}` closes standard input on scope exit.  The two
// mints are the only way to a live handle, and each builds one only from
// what libc returned.
static_assert(!std::is_constructible_v<OwnedFile, std::FILE*>,
              "The constructor that claims a stream must not be public.  A caller could hand it stdin, or a "
              "stream another handle owns, and the destructor would fclose it.  Take a handle from "
              "mint_owned_file or mint_temporary_file.");
static_assert(!std::is_constructible_v<OwnedFile, int>,
              "There is no descriptor form either: fdopen over a descriptor owned elsewhere would fclose that "
              "descriptor.  A descriptor is OwnedFd's business, in fixy/os/Fs.h.");
static_assert(std::is_default_constructible_v<OwnedFile>, "The empty handle claims nothing, so it stays reachable.");

// The release door admits a leak atom on an rvalue handle and nothing
// else.  An unrelated type is not a witness, and an lvalue handle keeps
// its stream.
struct NotALeakAtom final {};
struct release_rationale final {};
using SampleLeak = atom::leak::resource<release_rationale>;

template <typename Witness>
concept can_release = requires(OwnedFile handle, Witness witness) { std::move(handle).release(witness); };
template <typename Witness>
concept can_release_lvalue = requires(OwnedFile& handle, Witness witness) { handle.release(witness); };

static_assert(can_release<SampleLeak>);
static_assert(!can_release<NotALeakAtom>);
static_assert(!can_release<int>);
static_assert(!can_release<std::FILE*>);
static_assert(!can_release_lvalue<SampleLeak>, "A release binds only to an rvalue handle.");

namespace fe = ::foundation::effects;
static_assert(CtxFitsFileOpen<fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO, fe::Effect::Block>>>);
static_assert(!CtxFitsFileOpen<fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>>>,
              "A context that owns IO but not Block cannot wait on the file system.");
static_assert(!CtxFitsFileOpen<fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Alloc>>>,
              "The drain context owns no IO.");
static_assert(!CtxFitsFileOpen<fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>>, "The foreground context owns no IO.");
static_assert(!CtxFitsFileOpen<int>, "Only an execution context passes the gate.");

// The door has no object, and its opens are not reachable from here.
static_assert(!std::is_default_constructible_v<OwnedFileDoor>);
template <typename Door>
concept CanOpenThroughTheDoor = requires { Door::open_temporary_(); };
static_assert(!CanOpenThroughTheDoor<OwnedFileDoor>, "Only the two mints reach the opens of the door.");

}  // namespace detail::owned_file_self_test

}  // namespace fixy

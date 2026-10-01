#pragma once

// The descriptor handle OwnedFd, and the context gate CtxAdmitsFs of the
// file calls.
//
// fixy/os/Fs.h holds the file calls, and its path type includes
// <filesystem>.  A header that only holds a descriptor, or only asks for
// the gate, includes this header and does not pay for that parse.
//
// The handle has a private constructor, and three door classes make the
// ::open and ::socket calls: FileDoor in fixy/os/Fs.h, SocketDoor in
// fixy/os/Socket.h and PtpDeviceDoor in fixy/os/Time.h.  The members of
// each door are private, and its friends are the gated mints that use
// it.  So a descriptor that is owned is a descriptor the kernel handed
// out to a gated mint.

#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>

#include <unistd.h>

#include <utility>

namespace fixy::fs {

namespace eff = ::foundation::effects;

class FileDoor;

}  // namespace fixy::fs

namespace fixy::net {
class SocketDoor;
}  // namespace fixy::net

namespace fixy::time {
class PtpDeviceDoor;
}  // namespace fixy::time

namespace fixy::fs {

// Exclusive ownership of one descriptor, closed on destruction.
//
// The constructor that claims a descriptor is private.  With a public
// one, a caller can hand the handle any small integer — stdin, a
// descriptor another object still owns — and the destructor closes it
// on scope exit.  The three door classes above are
// its only friends.  Each makes the ::open or ::socket call itself and
// builds a handle only from what the kernel returned, and each admits
// only the gated mints that use it.
class [[nodiscard]] OwnedFd {
    int fd_ = -1;

    explicit OwnedFd(int fd) noexcept : fd_{fd} {}

    friend class FileDoor;
    friend class ::fixy::net::SocketDoor;
    friend class ::fixy::time::PtpDeviceDoor;

public:
    // The empty handle owns nothing and closes nothing, so it stays
    // public: it claims no descriptor.
    OwnedFd() noexcept = default;

    OwnedFd(const OwnedFd&) = delete("a descriptor is unique; copy would double-close on destruction");
    OwnedFd& operator=(const OwnedFd&) = delete("a descriptor is unique; copy would double-close on destruction");

    OwnedFd(OwnedFd&& other) noexcept : fd_{std::exchange(other.fd_, -1)} {}

    OwnedFd& operator=(OwnedFd&& other) noexcept {
        if (this != &other) {
            close_();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    ~OwnedFd() noexcept { close_(); }

    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

    // A borrow.  Ownership stays here, so the caller must not close what
    // this returns.
    [[nodiscard]] int get() const noexcept { return fd_; }

    // The inverse door: ownership leaves with the descriptor, and the
    // close becomes the caller's.  Only what this handle owns can leave
    // it, and a released handle is left owning nothing, so its
    // destructor closes nothing.  This is the fd twin of
    // OwnedFile::release, and the one explicit, discouraged way the raw
    // descriptor escapes: a caller that wants to hand the fd to a C API
    // that will own it spells release() and the loss of ownership is in
    // view, rather than the fd leaking out through a bare accessor.
    [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }

private:
    void close_() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);  // SYSCALL-CAP-OK: OwnedFd destructor, releases what a factory acquired
            fd_ = -1;
        }
    }
};

// Each file call of fixy/os/Fs.h can park the caller until the disk
// answers, so the context must admit IO and Block.  This is the gate of
// the calls there that take no atom pack.
template <typename Ctx>
concept CtxAdmitsFs = eff::CtxOwnsAllOf<Ctx, eff::Effect::IO, eff::Effect::Block>;

}  // namespace fixy::fs

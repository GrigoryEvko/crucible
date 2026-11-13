#pragma once

// The descriptor handle OwnedFd, and the context gate CtxAdmitsFs of the
// file calls.
//
// fixy/os/Fs.h holds the file calls, and its path type includes
// <filesystem>.  A header that only holds a descriptor, or only asks for
// the gate, includes this header and does not pay for that parse.
//
// The handle has a private constructor, and four door classes make the
// open, socket and perf_event_open calls: FileDoor in fixy/os/Fs.h,
// SocketDoor in fixy/os/Socket.h, PtpDeviceDoor in fixy/os/Time.h and
// PerfEventDoor in fixy/os/PerfEvent.h.  The members of
// each door are private, and its friends are the gated mints that use
// it.  So a descriptor that is owned is a descriptor the kernel handed
// out to a gated mint.
//
// The door file src/fixy/os/Fd.cpp holds the open, the close and the query
// of a descriptor.  It issues them with the raw primitive of
// foundation/os/Syscall.h, so this header includes no system header.

#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>

#include <cstdint>
#include <utility>

namespace fixy::fs {

namespace eff = ::foundation::effects;

class FileDoor;

namespace detail {

// Opens PATH, which a zero ends, relative to the current directory with the
// open flags FLAGS of foundation/os/OpenFlags.h.  The kernel reads MODE, the
// permission bits of a file that the open makes, only when FLAGS holds
// kOpenCreate.  The result is the descriptor, or the error of the kernel.  A
// door class of OwnedFd is the one caller that keeps the descriptor.
[[nodiscard]] ::foundation::core::Result<int, ::foundation::os::Errno> open_descriptor_(char const* path, int flags,
                                                                                        std::uint32_t mode) noexcept;

// Closes DESCRIPTOR.  The kernel releases the descriptor also when the close
// reports an error, so the caller has no error that it can act on, and the
// door does not report one.
void close_descriptor_(int descriptor) noexcept;

// Sends the ioctl request REQUEST to DESCRIPTOR, with the address ARGUMENT
// of the memory that the request reads or writes.  The door is only for a
// request that the kernel answers at once, such as a query of the
// capabilities of a device, because the primitive tells ThreadSanitizer of
// no wait for an ioctl (THE SANITIZERS of foundation/os/Syscall.h).
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
query_descriptor_(int descriptor, std::uint64_t request, void* argument) noexcept;

// The error number of the error of FAILED, a Result of a door that holds an
// error.  The calls of fixy/os give it to a std::error_code of the system
// category.  Each caller reads only a Result that holds an error, so the EIO
// of a Result with a value is not a path of the tree.
template <class T>
[[nodiscard]] constexpr int
error_number_of_(::foundation::core::Result<T, ::foundation::os::Errno> const& failed) noexcept {
    return std::to_underlying(failed.err().value_or(::foundation::os::Errno::eio));
}

// The error number of a call on a handle that holds no descriptor.
inline constexpr int closed_handle_error = std::to_underlying(::foundation::os::Errno::ebadf);

}  // namespace detail

}  // namespace fixy::fs

namespace fixy::net {
class SocketDoor;
}  // namespace fixy::net

namespace fixy::time {
class PtpDeviceDoor;
}  // namespace fixy::time

namespace fixy::perf {
class PerfEventDoor;
}  // namespace fixy::perf

namespace fixy::fs {

// Exclusive ownership of one descriptor, closed on destruction.
//
// The constructor that claims a descriptor is private.  With a public
// one, a caller can hand the handle any small integer — stdin, a
// descriptor another object still owns — and the destructor closes it
// on scope exit.  The four door classes above are
// its only friends.  Each makes the open, socket or perf_event_open call
// itself and builds a handle only from what the kernel returned, and each
// admits only the gated mints that use it.
class [[nodiscard]] OwnedFd {
    int fd_ = -1;

    explicit OwnedFd(int fd) noexcept : fd_{fd} {}

    friend class FileDoor;
    friend class ::fixy::net::SocketDoor;
    friend class ::fixy::time::PtpDeviceDoor;
    friend class ::fixy::perf::PerfEventDoor;

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
            detail::close_descriptor_(fd_);
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

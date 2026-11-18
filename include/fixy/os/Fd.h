#pragma once

// The words of the file calls: the offset in a file, the permission bits of
// a file, the doors of the open, the close and the query of a descriptor,
// and the context gate CtxAdmitsFs of the file calls.
//
// fixy/os/Fs.h holds the file calls, and its path type includes
// <filesystem>.  A header that only needs these words, or only asks for
// the gate, includes this header and does not pay for that parse.  The
// owner of a descriptor is the Handle of fixy/os/Handle.h.
//
// The door file src/fixy/os/Fd.cpp holds the open, the close and the query
// of a descriptor.  It issues them with the raw primitive of
// foundation/os/Syscall.h, so this header includes no system header.

#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Quantity.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>

#include <cstdint>

namespace fixy::fs {

namespace eff = ::foundation::effects;

// A position in a file: the count of bytes from the start of the file.  The
// representation is the signed 64-bit loff_t of the kernel, so a door gives
// the value to the kernel with no conversion.  The kernel refuses a negative
// offset with einval.
using FileOffset = ::foundation::core::Quantity<::foundation::core::unit::Byte, std::int64_t>;

// The permission bits of a file or a directory that an open or a make
// creates, with the octal values of chmod(2).  The kernel removes the bits of
// the umask of the process from the value.  A named value states the set of
// bits, so no call gives a number that a reader takes for octal and the
// compiler takes for decimal.
enum class FileMode : std::uint32_t {
    owner_read = 0400,
    owner_read_write = 0600,
    owner_all = 0700,
    owner_read_write_others_read = 0644,
    owner_all_others_read_execute = 0755,
};

namespace detail {

// Opens PATH, which a zero ends, relative to the current directory with the
// open flags FLAGS of foundation/os/OpenFlags.h.  The kernel reads MODE, the
// permission bits of a file that the open makes, only when FLAGS holds
// kOpenCreate.  The result is the descriptor, or the error of the kernel.  A
// door class of a Handle is the one caller that keeps the descriptor.
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

}  // namespace detail

}  // namespace fixy::fs

namespace fixy::fs {

// Each file call of fixy/os/Fs.h can park the caller until the disk
// answers, so the context must admit IO and Block.  This is the gate of
// the calls there that take no atom pack.
template <typename Ctx>
concept CtxAdmitsFs = eff::CtxOwnsAllOf<Ctx, eff::Effect::IO, eff::Effect::Block>;

}  // namespace fixy::fs

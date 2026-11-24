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
// of a descriptor, and the generation of each number of a descriptor.  It
// issues the calls with the raw primitive of foundation/os/Syscall.h, so this
// header includes no system header.

#include <foundation/ByteSeal.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Quantity.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>
#include <utility>

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

// Opens path, which a zero ends, relative to the current directory with the
// open flags of foundation/os/OpenFlags.h.  The kernel reads mode, the
// permission bits of a file that the open makes, only when flags holds
// kOpenCreate.  The result is the descriptor, or the error of the kernel.  A
// door class of a Handle is the one caller that keeps the descriptor.
[[nodiscard]] ::foundation::core::Result<int, ::foundation::os::Errno> open_descriptor_(char const* path, int flags,
                                                                                        std::uint32_t mode) noexcept;

// Closes descriptor.  The kernel releases the descriptor also when the close
// reports an error, so the caller has no error that it can act on, and the
// door does not report one.
void close_descriptor_(int descriptor) noexcept;

// Sends the ioctl request to descriptor, with the address argument
// of the memory that the request reads or writes.  The door is only for a
// request that the kernel answers at once, such as a query of the
// capabilities of a device, because the primitive tells ThreadSanitizer of
// no wait for an ioctl (the sanitizers of foundation/os/Syscall.h).
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
query_descriptor_(int descriptor, std::uint64_t request, void* argument) noexcept;

// Generation of a descriptor
//   Each number of a descriptor below descriptor_generation_slots has a
//   generation: a count that a Handle of fixy/os/Handle.h advances after the
//   open that gives it the number, and before the close or the release that
//   ends its ownership.  A move of a Handle keeps the generation.  The borrow
//   of a Handle and a ForeignSocketRef of fixy/os/Socket.h keep the
//   generation of their number in a DescriptorStamp (the stamp of a
//   descriptor below), and each door compares it with the generation now, so
//   a borrow that outlives its Handle fails closed.  The table is in the door file
//   src/fixy/os/Fd.cpp, and one table serves the process
//   (CRUCIBLE_PROCESS_WIDE of foundation/Platform.h), as one table of
//   descriptors does in the kernel.
//
// The count of the numbers that have a generation: 2^20, the default of
// fs.nr_open, past which the kernel gives no number.  A number at or past it
// has no generation, so a ref of it is never current.
inline constexpr std::size_t descriptor_generation_slots = std::size_t{1} << 20;

// The generation of the number of descriptor, or zero for a number with no
// generation.
[[nodiscard]] std::uint32_t descriptor_generation_(int descriptor) noexcept;

// True when the number of descriptor has a generation and it is generation.
[[nodiscard]] bool is_descriptor_generation_(int descriptor, std::uint32_t generation) noexcept;

// Advances the generation of the number of descriptor.  A negative number and
// a number with no generation change nothing.
void advance_descriptor_generation_(int descriptor) noexcept;

// The door that makes an opened descriptor (below).
struct OpenedDescriptorDoor;

}  // namespace detail

// Stamp of a descriptor
//   A DescriptorStamp is the number of a descriptor and the generation of the
//   number when a door read it.  The borrow of a Handle (fixy/os/Handle.h)
//   and a ForeignSocketRef (fixy/os/Socket.h) each hold one by value, and no
//   address of a Handle.  A Handle advances the generation at its close and
//   at its release to an ABI, so a stamp of a closed Handle is not current
//   (is_current), and a door gives ebadf for it and makes no call on the
//   number.  So a borrow that outlives its Handle, a borrow of a Handle that
//   a move assignment closed, and a number that the kernel gave to a later
//   open all fail closed.  A move of a Handle keeps the generation, so a
//   stamp of a Handle that moved stays current while the descriptor is open.
//
//   Two routes stay open.  A close of the Handle on a second thread between
//   the check of the generation and the call of the door lets the call act
//   on the number, which the kernel can give to a later open in that time.
//   A C library that closes a number that a Handle owns changes no
//   generation, so a later open of the number by the C library keeps each
//   stamp current.
class DescriptorStamp final {
    // The number of the descriptor, or -1 for the empty stamp.
    int number_ = -1;
    // The generation of the number when a door read it.
    std::uint32_t generation_ = 0;
    // No copy of bytes builds a stamp, so none builds a borrow that holds one
    // (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr DescriptorStamp(int number, std::uint32_t generation) noexcept
        : number_{number}, generation_{generation} {}

public:
    // The empty stamp, which names no descriptor.
    constexpr DescriptorStamp() noexcept = default;

    // The stamp of descriptor with the generation of its number now.  A
    // negative number gives the empty stamp and reads no generation.
    // Complexity: one acquire load.
    [[nodiscard]] static DescriptorStamp read_now(int descriptor) noexcept {
        if (descriptor < 0) {
            return DescriptorStamp{};
        }
        return DescriptorStamp{descriptor, detail::descriptor_generation_(descriptor)};
    }

    // False for the empty stamp.  A stamp that is open can still be stale.
    [[nodiscard]] constexpr bool is_open() const noexcept { return number_ >= 0; }

    // True when the number has a generation and it is the generation of the
    // stamp: no Handle opened, closed or released the number since the read.
    // False for the empty stamp.  Complexity: one acquire load.
    [[nodiscard]] bool is_current() const noexcept {
        return number_ >= 0 && detail::is_descriptor_generation_(number_, generation_);
    }

    // The number of a current stamp, or -1, so a door that gets it gives
    // ebadf and makes no system call.  Complexity: one acquire load.
    [[nodiscard]] int current_number() const noexcept { return is_current() ? number_ : -1; }

    // The number with no check of the generation, for a constant evaluation
    // and for an identity that names a socket only while it is open.
    [[nodiscard]] constexpr int number() const noexcept { return number_; }

    // Two stamps are equal when they hold one number of one generation.
    [[nodiscard]] friend constexpr bool operator==(DescriptorStamp left, DescriptorStamp right) noexcept {
        return left.number_ == right.number_ && left.generation_ == right.generation_;
    }
};

// Opened descriptor
//   An OpenedDescriptor is the number that an open gave to the base, below the
//   limit Slots of the slots of the generations, so the number has a
//   generation.  The private constructor of a Handle takes one, so each number
//   that a Handle owns has a generation.  A number at or past the limit, which
//   a host with a raised fs.nr_open and a raised limit of open files can
//   give, has no generation: admit_opened_descriptor_ closes it and gives
//   emfile.  The value moves and does not copy, so no two Handles take one
//   number.  detail::OpenedDescriptorDoor is the one friend that makes one.
class OpenedDescriptor final {
    int number_ = -1;

    explicit constexpr OpenedDescriptor(int number) noexcept : number_{number} {}

    friend struct detail::OpenedDescriptorDoor;

public:
    OpenedDescriptor(OpenedDescriptor const&) =
        delete("an opened number has one owner: a copy gives it to two Handles");
    OpenedDescriptor&
    operator=(OpenedDescriptor const&) = delete("an opened number has one owner: a copy gives it to two Handles");

    // A move takes the number, and the source holds -1.
    constexpr OpenedDescriptor(OpenedDescriptor&& other) noexcept : number_{other.number_} { other.number_ = -1; }
    OpenedDescriptor& operator=(OpenedDescriptor&&) = delete("a Handle takes an opened number one time");
    ~OpenedDescriptor() = default;

    // The number, for the constructor of a Handle.
    [[nodiscard]] constexpr int number() const noexcept { return number_; }
};

namespace detail {

// The door that makes an opened descriptor.  No object of it exists.
struct OpenedDescriptorDoor final : ::foundation::NoObject<OpenedDescriptorDoor> {
    // The opened descriptor of the number descriptor that an open gave, when
    // it is below Slots.  A number at or past Slots is closed, and the result
    // is emfile (the opened descriptor).  The limit is a parameter, so a test
    // can give a low limit to a real open.
    template <std::size_t Slots>
    [[nodiscard]] static ::foundation::core::Result<OpenedDescriptor, ::foundation::os::Errno>
    admit_below(int descriptor) noexcept {
        if (static_cast<std::size_t>(descriptor) >= Slots) {
            close_descriptor_(descriptor);
            return ::foundation::core::err(::foundation::os::Errno::emfile);
        }
        return OpenedDescriptor{descriptor};
    }
};

// The opened descriptor of the result of an open, or the error of the open,
// or emfile for a number with no generation (the opened descriptor).  A door
// of an open gives the result to the constructor of a Handle.
[[nodiscard]] inline ::foundation::core::Result<OpenedDescriptor, ::foundation::os::Errno>
admit_opened_descriptor_(::foundation::core::Result<int, ::foundation::os::Errno> opened) noexcept {
    return std::move(opened).and_then([](int descriptor) noexcept {
        return OpenedDescriptorDoor::admit_below<descriptor_generation_slots>(descriptor);
    });
}

}  // namespace detail

}  // namespace fixy::fs

namespace fixy::fs {

// Each file call of fixy/os/Fs.h can park the caller until the disk
// answers, so the context must admit IO and Block.  This is the gate of
// the calls there that take no atom pack.
template <typename Ctx>
concept CtxAdmitsFs = eff::CtxOwnsAllOf<Ctx, eff::Effect::IO, eff::Effect::Block>;

}  // namespace fixy::fs

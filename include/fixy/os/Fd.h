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
// of a descriptor, the read of the type of an opened file, and the generation
// of each number of a descriptor.  It issues the calls with the raw primitive
// of foundation/os/Syscall.h, so this header includes no system header.

#include <foundation/ByteSeal.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Quantity.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>
#include <foundation/os/OpenFlags.h>

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

// Gives a second number to the open file of descriptor, which closes when a
// thread of the process starts a program (F_DUPFD_CLOEXEC).  The result is the
// number, or the error of the kernel.  The call can wait, because the growth of
// the table of the numbers of a process with threads waits for each reader of
// the old table.  The duplicate of fixy/os/Handle.h and the split of a stream
// socket into its halves (fixy/os/Socket.h) are the callers.
[[nodiscard]] ::foundation::core::Result<int, ::foundation::os::Errno> duplicate_descriptor_(int descriptor) noexcept;

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

// True when the number of descriptor has a generation and it is generation.
[[nodiscard]] bool is_descriptor_generation_(int descriptor, std::uint32_t generation) noexcept;

// The number of descriptor when is_descriptor_generation_ is true, or -1.
// The check and the choice are in the door file, so a door that takes a borrow
// holds one call in place of them.
[[nodiscard]] int current_descriptor_(int descriptor, std::uint32_t generation) noexcept;

// Advances the generation of the number of descriptor.  A negative number and
// a number with no generation change nothing.
void advance_descriptor_generation_(int descriptor) noexcept;

// Ends the ownership of descriptor: the generation of its number advances
// first, while the owner still holds the number, and then the descriptor
// closes.  The order keeps an open of another thread, which can take the
// number after the close, from a generation of the old one.
void close_owned_descriptor_(int descriptor) noexcept;

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
//   Both borrows stay on one thread (HandleRef and ForeignSocketRef), and so
//   does the Handle, so a door that checks a stamp and then makes its call has
//   no close of the Handle between the two.  A Handle goes to another thread
//   only through the transit of fixy/os/Handle.h, which advances the generation
//   on the first thread before the move.  A table of another thread holds a
//   Handle of its own from mint_duplicate of fixy/os/Handle.h, and no borrow.
//
//   One route stays open.  A C library that closes a number that a Handle owns
//   changes no generation, so a later open of the number by the C library keeps
//   each stamp current.
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
    // negative number gives the empty stamp and reads no generation.  The body
    // is in the door file, so a borrow of a Handle is one call.
    // Complexity: one acquire load.
    [[nodiscard]] static DescriptorStamp read_now(int descriptor) noexcept;

    // False for the empty stamp.  A stamp that is open can still be stale.
    [[nodiscard]] constexpr bool is_open() const noexcept { return number_ >= 0; }

    // True when the number has a generation and it is the generation of the
    // stamp: no Handle opened, closed or released the number since the read.
    // False for the empty stamp.  Complexity: one acquire load.
    [[nodiscard]] bool is_current() const noexcept { return detail::is_descriptor_generation_(number_, generation_); }

    // The number of a current stamp, or -1, so a door that gets it gives
    // ebadf and makes no system call.  Complexity: one acquire load.
    [[nodiscard]] int current_number() const noexcept { return detail::current_descriptor_(number_, generation_); }

    // The number with no check of the generation, for a constant evaluation
    // and for an identity that names a socket only while it is open.
    [[nodiscard]] constexpr int number() const noexcept { return number_; }

    // Two stamps are equal when they hold one number of one generation.
    [[nodiscard]] friend constexpr bool operator==(DescriptorStamp left, DescriptorStamp right) noexcept {
        return left.number_ == right.number_ && left.generation_ == right.generation_;
    }
};

// The type of the file of an opened number, and how the base knows it (the
// opened descriptor below).
enum class OpenedType : std::uint8_t {
    // The call that gave the number gives no file of another kind: a socket, an
    // event of perf_event_open, a memory file, or an open with O_DIRECTORY.
    of_call,
    // A regular file.  The door of the open reads the type, unless the flags
    // of the open make the file.
    regular_file,
    // A character device.  The door of the open reads the type.
    character_device,
};

// An open with Flags makes its file, so the file is a regular file that no
// other open made: O_CREAT with O_EXCL, or O_TMPFILE.
template <int Flags>
concept MakesItsFile = (Flags & (::foundation::os::kOpenCreate | ::foundation::os::kOpenExclusive))
                        == (::foundation::os::kOpenCreate | ::foundation::os::kOpenExclusive)
                    || (Flags & ::foundation::os::kOpenTemporary) == ::foundation::os::kOpenTemporary;

// Opened descriptor
//   An OpenedDescriptor is the number that an open gave to the base, with the
//   type of its file, Type.  The private constructor of a Handle takes one
//   with the type of its kind (opened_type_of of fixy/os/Handle.h), so the
//   kind of a Handle states the type of its file.  The value moves and does
//   not copy, so no two Handles take one number.
//   detail::OpenedDescriptorDoor is the one friend that makes one.
//
//   The number is below the slots of the generations, so each number that a
//   Handle owns has a generation.  A number at or past them, which a host with
//   a raised fs.nr_open and a raised limit of open files can give, has no
//   generation: the door of an open closes it and gives emfile
//   (admit_opened_number_).
//
//   A path can name each type of file: a regular file, a directory, a device,
//   a FIFO or a socket.  The door of an open that has no other proof reads the
//   type of the opened file (admit_regular_file_), and it closes a file of
//   another type and gives eftype.  The type of a file never changes, so the
//   answer stays true for the life of the Handle.  The read asks for the type
//   alone, and it asks no server of a network file system.  The open itself
//   comes before the read.  So the open of a FIFO with no writer waits for a
//   writer before the refusal, and the open of a device runs its driver.
template <OpenedType Type>
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

// The number of the result of an open when it has a generation.  A number
// with no generation is closed, and the result is emfile (the opened
// descriptor).  The error of the open stays.
[[nodiscard]] ::foundation::core::Result<int, ::foundation::os::Errno>
admit_opened_number_(::foundation::core::Result<int, ::foundation::os::Errno> opened) noexcept;

// The number of admit_opened_number_ when its file is a regular file.  A file
// of another type is closed, and the result is eftype (the opened
// descriptor).  An error of the type read closes the file and is the result.
[[nodiscard]] ::foundation::core::Result<int, ::foundation::os::Errno>
admit_regular_file_(::foundation::core::Result<int, ::foundation::os::Errno> opened) noexcept;

// The number of admit_opened_number_ when its file is a character device, with
// the refusals of admit_regular_file_.
[[nodiscard]] ::foundation::core::Result<int, ::foundation::os::Errno>
admit_character_device_(::foundation::core::Result<int, ::foundation::os::Errno> opened) noexcept;

// The door that makes an opened descriptor.  No object of it exists.
struct OpenedDescriptorDoor final : ::foundation::NoObject<OpenedDescriptorDoor> {
    // The number descriptor that an open gave, when it is below Slots.  A
    // number at or past Slots is closed, and the result is emfile (the opened
    // descriptor).  The limit is a parameter, so a test can give a low limit to
    // a real open.
    template <std::size_t Slots>
    [[nodiscard]] static ::foundation::core::Result<int, ::foundation::os::Errno> admit_below(int descriptor) noexcept {
        if (static_cast<std::size_t>(descriptor) >= Slots) {
            close_descriptor_(descriptor);
            return ::foundation::core::err(::foundation::os::Errno::emfile);
        }
        return descriptor;
    }

    // Gives make(opened descriptor) for the result of an open, or the error of
    // the open, or the refusal of the number or of its file (the opened
    // descriptor).  A door of an open gives the opened descriptor to the
    // constructor of a Handle in make.  The checks and the close of a refused
    // number are one call in the door file, whose result is in registers, so
    // each open holds that call and the body of make.
    template <OpenedType Type, class Make>
    [[nodiscard]] static auto admit(::foundation::core::Result<int, ::foundation::os::Errno> opened,
                                    Make make) noexcept {
        return checked_<Type>(opened).map(
            [&make](int descriptor) noexcept { return make(OpenedDescriptor<Type>{descriptor}); });
    }

    // Gives make(opened descriptor) for the result of an open with Flags,
    // which makes its regular file, so the door reads no type.
    template <int Flags, class Make>
        requires MakesItsFile<Flags>
    [[nodiscard]] static auto admit_new_file(::foundation::core::Result<int, ::foundation::os::Errno> opened,
                                             Make make) noexcept {
        return admit_opened_number_(opened).map(
            [&make](int descriptor) noexcept { return make(OpenedDescriptor<OpenedType::regular_file>{descriptor}); });
    }

    // Gives make(opened descriptor) for the second number of an open file whose
    // type Type a Handle states, so the door reads no type.
    template <OpenedType Type, class Make>
    [[nodiscard]] static auto admit_duplicate(::foundation::core::Result<int, ::foundation::os::Errno> duplicated,
                                              Make make) noexcept {
        return admit_opened_number_(duplicated).map([&make](int descriptor) noexcept {
            return make(OpenedDescriptor<Type>{descriptor});
        });
    }

private:
    // The number of an open whose file has the type Type, or the refusal.  A
    // type with no arm does not compile.
    template <OpenedType Type>
    [[nodiscard]] static ::foundation::core::Result<int, ::foundation::os::Errno>
    checked_(::foundation::core::Result<int, ::foundation::os::Errno> opened) noexcept {
        if constexpr (Type == OpenedType::regular_file) {
            return admit_regular_file_(opened);
        } else if constexpr (Type == OpenedType::character_device) {
            return admit_character_device_(opened);
        } else {
            static_assert(Type == OpenedType::of_call,
                          "OpenedDescriptorDoor::admit: the type has no check.  Give it an arm that reads it.");
            return admit_opened_number_(opened);
        }
    }
};

}  // namespace detail

}  // namespace fixy::fs

namespace fixy::fs {

// Each file call of fixy/os/Fs.h can park the caller until the disk
// answers, so the context must admit IO and Block.  This is the gate of
// the calls there that take no atom pack.
template <typename Ctx>
concept CtxAdmitsFs = eff::CtxOwnsAllOf<Ctx, eff::Effect::IO, eff::Effect::Block>;

}  // namespace fixy::fs

#pragma once

// The resource handle of the OS doors: one descriptor of the kernel, with
// its kind and its rights in its type.
//
//   Handle<Kind, Rights>     owns one descriptor, and closes it at its end
//   HandleRef<Kind, Rights>  the borrow of the descriptor of a Handle
//   RightSet                 the rights of a handle: a set of named bits
//   kind::*                  the kinds of a handle: a closed list of tags
//
// Rights
//   RightSet is an enum class of named bits in 64 bits.  A class type that
//   is a template argument must have public members, so a class with a
//   private mask cannot be one, and an enum class is the strong type that
//   can.  No number converts to it.  The bits combine with |, and
//   HoldsRights<Held, Needed> is the subset test.  A door asks for the bits
//   of its call in its constraint, so a call through a handle that lacks a
//   right does not compile.  The low 32 bits are the rights of the calls,
//   and the high 32 bits are the families of the closed table of ioctls, one
//   bit for each family.
//
// Kinds
//   A kind is a tag of namespace kind, and HandleKind names each one, so a
//   class of a caller is no kind.  A socket kind takes the tag of its socket
//   (fixy/os/Socket.h), because the doors of a socket read its triple.
//
// Borrow
//   HandleRef is the one borrow of a descriptor.  It holds the number of the
//   descriptor and the generation of the number by value (the stamp of a
//   descriptor of fixy/os/Fd.h), and no address of its Handle.  Each Handle
//   advances the generation at its open, at its close and at its release to
//   an ABI, so a borrow that outlives its Handle, or whose Handle a move
//   assignment closed, is not current.  A door then gives ebadf and makes no
//   call on the number, which the kernel can have given to a later open.  A
//   move of a Handle keeps the generation, so a borrow taken before the move
//   stays current while the descriptor is open.  The borrow of an empty
//   Handle, such as one that a move emptied, holds -1.
//
//   The borrow states refers{} of foundation/Transfer.h over its kind, so the
//   rule view-escape refuses a return or a store of the borrow of a local
//   Handle, and the frame doors of the base read it as a borrow (HoldsNoBorrow
//   of foundation/core/Choice.h).  It also states thread_bound{} and
//   not_shareable{}: a door on a second thread can act on the number between
//   the check of the generation and the call, while the thread of the Handle
//   closes it, so the borrow stays on its thread.  A table that stores a
//   socket holds a ForeignSocketRef of fixy/os/Socket.h, which holds the same
//   stamp and is no borrow.
//   restrict<Fewer>() gives a borrow with fewer rights, and no form gives
//   more rights.  A borrow of a temporary Handle does not compile.
//
// Doors
//   The constructor of a Handle from a descriptor is private.  Its friends
//   are the door classes that make the open, socket, perf_event_open and
//   PTP open calls, and each door admits only its gated mints.  The
//   constructor takes an OpenedDescriptor of fixy/os/Fd.h, so each number of
//   a Handle has a generation.  The doors of the base read the descriptor of
//   a borrow, and an owner reads the descriptor of its own Handle, through
//   detail::HandleDoor.  A use of a namespace detail outside the base is a
//   finding of the quarantine plugin.
//
// ABI descriptor
//   A call of a C ABI, such as a call of libbpf that takes a descriptor,
//   reads the raw descriptor of a borrow with abi_descriptor().  A C ABI that
//   takes the ownership of a descriptor gets it with release_to_abi(), which
//   empties the Handle.  A socket that a C library opened comes in through
//   admit_foreign_socket of fixy/os/Socket.h.  Each such call is inside an
//   opt-out region of the class ABI, or of the class PROBE for a test of raw
//   behavior, which states the reason.  No rule of the tree keeps these
//   members and admit_foreign_socket inside such a region, so a review of
//   each new use keeps that limit.  Each other caller uses a door of the base.

#include <fixy/os/Fd.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/Choice.h>
#include <foundation/os/Errno.h>
#include <foundation/os/PathText.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy::fs {
class FileDoor;
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

namespace fixy::os {

// The rights of a handle.  Each named value is one bit, and none is the
// empty set.
enum class RightSet : std::uint64_t {
    none = 0,
    read = std::uint64_t{1} << 0U,
    write = std::uint64_t{1} << 1U,
    seek = std::uint64_t{1} << 2U,
    map = std::uint64_t{1} << 3U,
    map_exec = std::uint64_t{1} << 4U,
    sync = std::uint64_t{1} << 5U,
    lookup = std::uint64_t{1} << 6U,
    create = std::uint64_t{1} << 7U,
    remove = std::uint64_t{1} << 8U,
    transfer = std::uint64_t{1} << 9U,
    ioctl_ptp = std::uint64_t{1} << 32U,
    ioctl_socket = std::uint64_t{1} << 33U,
    ioctl_perf = std::uint64_t{1} << 34U,
};

// The union of two sets of rights.  The union is commutative, so the two
// arguments can swap with no change of meaning.
[[nodiscard]] constexpr RightSet operator|(RightSet left, RightSet right) noexcept {
    return static_cast<RightSet>(std::to_underlying(left) | std::to_underlying(right));
}

// True when the set Held holds each right of the set Needed.
template <RightSet Held, RightSet Needed>
concept HoldsRights = (std::to_underlying(Held) & std::to_underlying(Needed)) == std::to_underlying(Needed);

// The kinds of a handle.
namespace kind {
// A regular file of a file system.
struct File final {};
// A directory, the root of the paths beneath it.
struct Directory final {};
// A socket of the kind SocketKind of fixy/os/Socket.h.
template <class SocketKind>
struct Socket final {};
// An event or a counter of perf_event_open.
struct PerfEvent final {};
// A ring of io_uring.
struct IoRing final {};
// A PTP hardware clock of a /dev/ptpN node.
struct PtpClock final {};
// A memory file whose size never decreases (mint_sealed_memory of
// fixy/os/Fs.h).
struct SealedMemory final {};
}  // namespace kind

// A kind of namespace kind.  The concept names each kind, so a class of a
// different header is no kind.
template <class Kind>
concept HandleKind =
    std::same_as<Kind, kind::File> || std::same_as<Kind, kind::Directory> || std::same_as<Kind, kind::PerfEvent>
    || std::same_as<Kind, kind::IoRing> || std::same_as<Kind, kind::PtpClock> || std::same_as<Kind, kind::SealedMemory>
    || ::foundation::reflect::IsInstanceOf<Kind, ^^kind::Socket>;

template <class Kind, RightSet Rights>
    requires HandleKind<Kind>
class Handle;

template <class Kind, RightSet Rights>
    requires HandleKind<Kind>
class HandleRef;

namespace detail {

// The door of the calls of the base to the descriptor of a borrow.  No
// object of it exists.  descriptor_of gives the descriptor of a current
// borrow, or -1 for an empty or a stale borrow, so the door of the call gives
// ebadf and makes no system call.  owned_descriptor_of gives the descriptor
// of a Handle to its owner, which keeps the Handle alive, so it reads no
// generation.  stamp_of gives the stamp of a borrow, for a ForeignSocketRef
// of the same socket.
struct HandleDoor final : ::foundation::NoObject<HandleDoor> {
    template <class Kind, RightSet Rights>
    [[nodiscard]] static int descriptor_of(HandleRef<Kind, Rights> borrow) noexcept;

    template <class Kind, RightSet Rights>
    [[nodiscard]] static int owned_descriptor_of(Handle<Kind, Rights> const& owner) noexcept;

    template <class Kind, RightSet Rights>
    [[nodiscard]] static ::fixy::fs::DescriptorStamp stamp_of(HandleRef<Kind, Rights> borrow) noexcept;
};

}  // namespace detail

template <class Kind, RightSet Rights>
    requires HandleKind<Kind>
class [[nodiscard]][[= ::foundation::transfer::refers{}]][[= ::foundation::transfer::thread_bound{}]]
                   [[= ::foundation::transfer::not_shareable{}]] HandleRef final {
    // The number of the descriptor of the Handle and its generation, or the
    // empty stamp for the borrow of an empty Handle.  The stamp holds the
    // seal of foundation/ByteSeal.h, so no copy of bytes builds a borrow.
    ::fixy::fs::DescriptorStamp stamp_{};

    explicit constexpr HandleRef(::fixy::fs::DescriptorStamp stamp) noexcept : stamp_{stamp} {}

    template <class OtherKind, RightSet OtherRights>
        requires HandleKind<OtherKind>
    friend class Handle;

    template <class OtherKind, RightSet OtherRights>
        requires HandleKind<OtherKind>
    friend class HandleRef;

    friend struct detail::HandleDoor;

public:
    using kind_type = Kind;
    static constexpr RightSet rights = Rights;

    // A borrow with the rights Fewer, which this borrow holds.  It keeps the
    // stamp of this borrow.
    template <RightSet Fewer>
        requires HoldsRights<Rights, Fewer>
    [[nodiscard]] constexpr HandleRef<Kind, Fewer> restrict() const noexcept {
        return HandleRef<Kind, Fewer>{stamp_};
    }

    // True when the Handle still owns the descriptor of the borrow.  False
    // for the borrow of an empty Handle, and for a borrow whose Handle closed
    // or released the descriptor since the borrow.  Complexity: one acquire
    // load.
    [[nodiscard]] bool is_open() const noexcept { return stamp_.is_current(); }

    // The raw descriptor, for a call of a C ABI only (the ABI descriptor).
    // The descriptor of the borrow of an empty Handle, and of a borrow that
    // is not open, is -1, so a C ABI gets no number of a later open.  A
    // constant evaluation reads no generation and gives the number.
    [[nodiscard]] constexpr int abi_descriptor() const noexcept {
        if consteval {
            return stamp_.number();
        }
        return stamp_.current_number();
    }
};

template <class Kind, RightSet Rights>
[[nodiscard]] int detail::HandleDoor::descriptor_of(HandleRef<Kind, Rights> borrow) noexcept {
    return borrow.stamp_.current_number();
}

template <class Kind, RightSet Rights>
[[nodiscard]] ::fixy::fs::DescriptorStamp detail::HandleDoor::stamp_of(HandleRef<Kind, Rights> borrow) noexcept {
    return borrow.stamp_;
}

// Appends the number of the descriptor of borrow to path, in decimal, for a
// path under /proc/self/fd or /dev/fd that names the open file of the
// descriptor.  A borrow that is not open gives ebadf, and a number that does
// not fit gives enametoolong.  Each error leaves the path as it was.
template <class Kind, RightSet Rights, std::size_t Capacity>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
append_descriptor_number(::foundation::os::PathText<Capacity>& path, HandleRef<Kind, Rights> borrow) noexcept {
    int const descriptor = detail::HandleDoor::descriptor_of(borrow);
    if (descriptor < 0) {
        return ::foundation::core::err(::foundation::os::Errno::ebadf);
    }
    return path.append_decimal(static_cast<std::uint64_t>(descriptor));
}

// Exclusive ownership of one descriptor of the kind Kind with the rights
// Rights, closed at the end of the Handle.
template <class Kind, RightSet Rights>
    requires HandleKind<Kind>
class [[nodiscard]] Handle final {
    int descriptor_ = -1;

    // The open gave the number to this Handle, so the generation of the
    // number advances (the generation of a descriptor of fixy/os/Fd.h).  The
    // number is below the slots of the generations (the opened descriptor of
    // fixy/os/Fd.h), so it has a generation.
    explicit Handle(::fixy::fs::OpenedDescriptor&& opened) noexcept : descriptor_{opened.number()} {
        ::fixy::fs::detail::advance_descriptor_generation_(descriptor_);
    }

    friend class ::fixy::fs::FileDoor;
    friend class ::fixy::net::SocketDoor;
    friend class ::fixy::time::PtpDeviceDoor;
    friend class ::fixy::perf::PerfEventDoor;
    friend struct detail::HandleDoor;

public:
    using kind_type = Kind;
    static constexpr RightSet rights = Rights;

    // The empty handle owns nothing and closes nothing, so it stays public:
    // it claims no descriptor.
    Handle() noexcept = default;

    Handle(Handle const&) = delete("a descriptor has one owner: a copy would close it two times");
    Handle& operator=(Handle const&) = delete("a descriptor has one owner: a copy would close it two times");

    // A move takes the descriptor, and the source holds none.
    Handle(Handle&& other) noexcept : descriptor_{std::exchange(other.descriptor_, -1)} {}

    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            close_();
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }

    ~Handle() noexcept { close_(); }

    [[nodiscard]] bool is_open() const noexcept { return descriptor_ >= 0; }

    // The borrow of the descriptor with each right of this Handle.  The
    // borrow holds the stamp of the descriptor now, so it fails closed when
    // this Handle closes or releases the descriptor.  The borrow of an empty
    // Handle holds the empty stamp.  Complexity: one acquire load.
    [[nodiscard]] HandleRef<Kind, Rights> ref() const& noexcept {
        return HandleRef<Kind, Rights>{::fixy::fs::DescriptorStamp::read_now(descriptor_)};
    }
    void ref() const&& = delete("the borrow of a temporary handle dangles at the end of the full expression");

    // Gives the descriptor to the caller, which closes it from then on, and
    // leaves this Handle empty (the ABI descriptor).  The one caller is a C
    // ABI that takes the ownership of a descriptor, such as a link of libbpf,
    // inside an opt-out region of the class ABI.  An empty Handle gives -1.
    // The base gives up the number, so its generation advances, and no
    // ForeignSocketRef of the number is current after the release.
    [[nodiscard]] int release_to_abi() && noexcept {
        ::fixy::fs::detail::advance_descriptor_generation_(descriptor_);
        return std::exchange(descriptor_, -1);
    }

private:
    // The generation advances before the close (close_owned_descriptor_ of
    // fixy/os/Fd.h), so no open of another thread takes the number first.
    void close_() noexcept {
        if (descriptor_ >= 0) {
            ::fixy::fs::detail::close_owned_descriptor_(descriptor_);
            descriptor_ = -1;
        }
    }
};

template <class Kind, RightSet Rights>
[[nodiscard]] int detail::HandleDoor::owned_descriptor_of(Handle<Kind, Rights> const& owner) noexcept {
    return owner.descriptor_;
}

}  // namespace fixy::os

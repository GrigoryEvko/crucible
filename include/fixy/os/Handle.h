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
//   HandleRef is the one borrow of a descriptor.  It holds the address of the
//   descriptor of its Handle, so it is a borrow for the frame doors of the
//   base (HoldsNoBorrow of foundation/core/Choice.h), and no result of a
//   frame carries it out.  A Handle that a move emptied gives its borrows the
//   descriptor -1, so a door makes no call through them and gives ebadf.  A
//   borrow that outlives its Handle dangles, as each borrow does.  A table
//   that stores a socket holds a ForeignSocketRef of fixy/os/Socket.h by
//   value, with the generation of its number (the generation of a
//   descriptor of fixy/os/Fd.h), which each Handle advances at its open, at
//   its close and at its release to an ABI.
//   restrict<Fewer>() gives a borrow with fewer rights, and no form gives
//   more rights.  A borrow of a temporary Handle does not compile.
//
// Doors
//   The constructor of a Handle from a descriptor is private.  Its friends
//   are the door classes that make the open, socket, perf_event_open and
//   PTP open calls, and each door admits only its gated mints.  The doors of
//   the base read the descriptor of a borrow through detail::HandleDoor.  A
//   use of a namespace detail outside the base is a finding of the
//   quarantine plugin.
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
#include <foundation/ByteSeal.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
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
}  // namespace kind

// A kind of namespace kind.  The concept names each kind, so a class of a
// different header is no kind.
template <class Kind>
concept HandleKind = std::same_as<Kind, kind::File> || std::same_as<Kind, kind::Directory>
                  || std::same_as<Kind, kind::PerfEvent> || std::same_as<Kind, kind::IoRing>
                  || std::same_as<Kind, kind::PtpClock> || ::foundation::reflect::IsInstanceOf<Kind, ^^kind::Socket>;

template <class Kind, RightSet Rights>
    requires HandleKind<Kind>
class Handle;

template <class Kind, RightSet Rights>
    requires HandleKind<Kind>
class HandleRef;

namespace detail {

// The door of the calls of the base to the descriptor of a borrow.  No
// object of it exists, and its member gives the descriptor of a borrow.
struct HandleDoor final : ::foundation::NoObject<HandleDoor> {
    template <class Kind, RightSet Rights>
    [[nodiscard]] static int descriptor_of(HandleRef<Kind, Rights> borrow) noexcept;
};

}  // namespace detail

template <class Kind, RightSet Rights>
    requires HandleKind<Kind>
class [[nodiscard]] HandleRef final {
    // The descriptor of the Handle.  It is never null.
    int const* descriptor_ = nullptr;
    // No copy of bytes builds a borrow (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    explicit constexpr HandleRef(int const& descriptor) noexcept : descriptor_{__builtin_addressof(descriptor)} {}

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

    // A borrow with the rights Fewer, which this borrow holds.
    template <RightSet Fewer>
        requires HoldsRights<Rights, Fewer>
    [[nodiscard]] constexpr HandleRef<Kind, Fewer> restrict() const noexcept {
        return HandleRef<Kind, Fewer>{*descriptor_};
    }

    // False for the borrow of an empty Handle, such as one that a move
    // emptied.
    [[nodiscard]] constexpr bool is_open() const noexcept { return *descriptor_ >= 0; }

    // The raw descriptor, for a call of a C ABI only (the ABI descriptor).
    // The descriptor of an empty Handle is -1.
    [[nodiscard]] constexpr int abi_descriptor() const noexcept { return *descriptor_; }
};

template <class Kind, RightSet Rights>
[[nodiscard]] int detail::HandleDoor::descriptor_of(HandleRef<Kind, Rights> borrow) noexcept {
    return *borrow.descriptor_;
}

// Appends the number of the descriptor of borrow to path, in decimal, for a
// path under /proc/self/fd or /dev/fd that names the open file of the
// descriptor.  The borrow of an empty handle gives ebadf, and a number that
// does not fit gives enametoolong.  Each error leaves the path as it was.
template <class Kind, RightSet Rights, std::size_t Capacity>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
append_descriptor_number(::foundation::os::PathText<Capacity>& path, HandleRef<Kind, Rights> borrow) noexcept {
    if (!borrow.is_open()) {
        return ::foundation::core::err(::foundation::os::Errno::ebadf);
    }
    return path.append_decimal(static_cast<std::uint64_t>(detail::HandleDoor::descriptor_of(borrow)));
}

// Exclusive ownership of one descriptor of the kind Kind with the rights
// Rights, closed at the end of the Handle.
template <class Kind, RightSet Rights>
    requires HandleKind<Kind>
class [[nodiscard]] Handle final {
    int descriptor_ = -1;

    // The open gave the number to this Handle, so the generation of the
    // number advances (the generation of a descriptor of fixy/os/Fd.h).
    explicit Handle(int descriptor) noexcept : descriptor_{descriptor} {
        ::fixy::fs::detail::advance_descriptor_generation_(descriptor_);
    }

    friend class ::fixy::fs::FileDoor;
    friend class ::fixy::net::SocketDoor;
    friend class ::fixy::time::PtpDeviceDoor;
    friend class ::fixy::perf::PerfEventDoor;

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
    // borrow names the descriptor of this object, so it must not outlive it.
    [[nodiscard]] HandleRef<Kind, Rights> ref() const& noexcept { return HandleRef<Kind, Rights>{descriptor_}; }
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
    // The generation advances before the close, while this Handle still owns
    // the number, so no open of another thread takes the number first.
    void close_() noexcept {
        if (descriptor_ >= 0) {
            ::fixy::fs::detail::advance_descriptor_generation_(descriptor_);
            ::fixy::fs::detail::close_descriptor_(descriptor_);
            descriptor_ = -1;
        }
    }
};

}  // namespace fixy::os

#pragma once

// The socket syscall.  A kind tag names one socket, and the tag is read
// twice: once for the domain, type and protocol that socket(2) takes, and
// once for the effect row that the calling context must admit.
//
// mint_socket below makes the call through SocketDoor, the one door to
// socket(2) and socketpair(2), so a socket handle holds a descriptor that the
// kernel returned to a gated mint.  mint_socket_pair gives the two connected
// ends of one stream socket of the local domain.  The door file
// src/fixy/os/Socket.cpp issues the calls with the raw primitive of
// foundation/os/Syscall.h.  The words of the calls come from
// fixy/os/SocketCalls.h (the kernel words), so this header includes no
// system header.
//
// Three kinds are declared: the netlink socket of the qdisc query, the TCP
// stream socket whose options the test of the socket calls reads back from
// the kernel, and the stream socket of the local domain, whose pair carries
// bytes between two ends in one process.  A new kind is a tag in
// fixy::net::socket_kind and a row of socket_table.
// The walk at the foot of this header fails if the tag has no row.

#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/atoms/Syscall.h>
#include <fixy/os/AtomPack.h>
#include <fixy/os/Fd.h>
#include <fixy/os/Handle.h>
#include <fixy/os/SocketCalls.h>
#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Id.h>
#include <foundation/core/Record.h>
#include <foundation/core/Region.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>
#include <foundation/os/Errno.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace fixy::net {

namespace eff = ::foundation::effects;

namespace socket_kind {

// A netlink socket to the routing subsystem of the kernel.  It sends a
// dump request and reads the reply, for example the queueing discipline
// of an interface.  Each call is one message, so a duplicate of it keeps
// each right (the shape of a socket kind of fixy/os/Handle.h).  The two
// stream kinds below state no shape.
struct[[= ::fixy::os::message_socket{}]] NetlinkRoute final {};

// A TCP stream socket of IPv4.
struct TcpStream final {};

// A stream socket of the local domain (AF_UNIX).  mint_socket_pair gives the
// two connected ends of one such connection in one process.
struct UnixStream final {};

}  // namespace socket_kind

namespace detail {

// The three arguments of socket(2) for one kind.
struct socket_triple final {
    int domain = -1;
    int type = -1;
    int protocol = -1;
};

}  // namespace detail

// The domain, type and protocol of each kind.  A kind reaches socket(2)
// only through a row of this table, and fixy/os/AtomPack.h says why a
// table is closed: a class template map takes a specialization for a
// class of the caller, and the gated mint can then open a raw packet
// socket as a known kind.  A kind with no row has no triple, so the mint refuses
// it and socket(2) never sees a guessed triple.
inline constexpr ::fixy::atom_pack::tag_row<detail::socket_triple> socket_table[] = {
    {^^socket_kind::NetlinkRoute,
     {detail::address_family_netlink, detail::socket_type_raw, detail::netlink_route_protocol}},
    {^^socket_kind::TcpStream, {detail::address_family_inet, detail::socket_type_stream, 0}},
    {^^socket_kind::UnixStream, {detail::address_family_unix, detail::socket_type_stream, 0}},
};

template <typename Kind>
concept MappedSocketKind = ::fixy::atom_pack::has_row(socket_table, ^^Kind);

// The triple of a kind that has a row.  The lookup is a function and not
// a variable template, because a caller can specialize a variable
// template for one kind and give it a triple of its own.
[[nodiscard]] consteval detail::socket_triple socket_triple_of(std::meta::info kind_tag) noexcept {
    return ::fixy::atom_pack::value_for(socket_table, kind_tag);
}

// A kind of the local domain, whose two connected ends socketpair(2) can
// give.  The kernel refuses a pair of each other domain of the table.
template <typename Kind>
concept PairedSocketKind = MappedSocketKind<Kind> && socket_triple_of(^^Kind).domain == detail::address_family_unix;

// The row comes from the syscall atom, so the gate and the catalog in
// fixy/atoms/Syscall.h cannot disagree.  The catalog puts socket in the
// network family, which lifts to IO and Block.
using socket_row_t = eff::lift_row_t<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::socket>>;

template <typename Ctx, typename Kind>
concept CtxFitsSocketMint = eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, socket_row_t> && MappedSocketKind<Kind>;

// The row of socketpair(2), from its syscall atom, as the row of socket(2).
using socket_pair_row_t = eff::lift_row_t<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::socketpair>>;

// The gate of the pair mint: the context admits the row of socketpair(2), and
// the kind is of the local domain.
template <typename Ctx, typename Kind>
concept CtxFitsSocketPairMint = eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, socket_pair_row_t> && PairedSocketKind<Kind>;

// The rights of a socket: a read and a write of its bytes, and the control
// calls of a socket (ioctl_socket): its ioctls and its options.
inline constexpr ::fixy::os::RightSet socket_rights =
    ::fixy::os::RightSet::read | ::fixy::os::RightSet::write | ::fixy::os::RightSet::ioctl_socket;

// The handle of a socket of the kind Kind, and its borrow.
template <typename Kind>
using SocketHandle = ::fixy::os::Handle<::fixy::os::kind::Socket<Kind>, socket_rights>;

template <typename Kind, ::fixy::os::RightSet Rights>
using SocketRef = ::fixy::os::HandleRef<::fixy::os::kind::Socket<Kind>, Rights>;

// The two connected ends of one connection of the kind Kind, from
// mint_socket_pair: a byte that one end writes, the other end reads.  Each end
// is a handle of its own with its own number, and the close of one end is the
// end of the stream for the other.
template <typename Kind>
using SocketPair = ::foundation::core::Record<Linear<SocketHandle<Kind>>, Linear<SocketHandle<Kind>>>;

// The identity of a socket: the number of its descriptor.  A table that only
// keys or compares sockets holds the identity, and no door takes it.  The
// kernel gives the number of a closed socket to its next socket, so an
// identity names a socket only while the socket is open.
using SocketIdentity = ::foundation::core::Id<struct SocketIdentityTag, std::uint32_t>;

// Foreign socket
//   A ForeignSocketRef is the borrow of a socket by value.  A table that
//   stores, compares or keys a socket that it gives back to a door holds
//   one, because a HandleRef is a borrow, which the rule view-escape keeps
//   inside the frame of its Handle.  The ref stays on its thread
//   (thread_bound{} and not_shareable{} of foundation/Transfer.h), as a
//   HandleRef does, and so does each class that holds one.  A table of another
//   thread holds a Handle of its own, from mint_duplicate of
//   fixy/os/Handle.h.
//
//   Two doors make a ref: share_socket, from the borrow of a Handle of the
//   base, and admit_foreign_socket, from the descriptor of a socket that a C
//   library opened.  Each ref carries one promise of the code that made it: the
//   owner of the socket keeps it open for the life of each copy of the ref,
//   on the thread of the ref.  The ref holds the stamp of its
//   descriptor (the stamp of a descriptor of fixy/os/Fd.h), as a HandleRef
//   does, and a Handle of the base advances the generation of the number at
//   its open, at its close and at its release to an ABI.  So a ref that
//   outlives its Handle is not current, and each door gives ebadf for it and
//   makes no call on the number, which the kernel can have given to a later
//   descriptor.  abi_descriptor() gives -1 for such a ref.  The departure of a
//   Handle to another thread (the transit of fixy/os/Handle.h) makes each ref
//   of its number stale too.  The route that the stamp leaves open stays open
//   here: a foreign socket that a C library closes and a C library opens again
//   on its number with no Handle between.  A number at or past
//   descriptor_generation_slots has no generation, so its ref is never
//   current, and each door refuses it.  The default ref is empty, and a door
//   gives ebadf for it.
template <typename Kind, ::fixy::os::RightSet Rights>
    requires MappedSocketKind<Kind>
class ForeignSocketRef;

namespace detail {

// The door of the base to the descriptor of a foreign socket.  No object of
// it exists.  admit makes a ref with the stamp of its number now, share makes
// a ref with the stamp of a borrow of a Handle, and descriptor_of gives the
// descriptor of a current ref, or -1.
struct ForeignSocketDoor final : ::foundation::NoObject<ForeignSocketDoor> {
    template <typename Kind, ::fixy::os::RightSet Rights>
    [[nodiscard]] static ForeignSocketRef<Kind, Rights> admit(int descriptor) noexcept;

    template <typename Kind, ::fixy::os::RightSet Rights>
    [[nodiscard]] static ForeignSocketRef<Kind, Rights>
    share(::fixy::os::HandleRef<::fixy::os::kind::Socket<Kind>, Rights> socket) noexcept;

    template <typename Kind, ::fixy::os::RightSet Rights>
    [[nodiscard]] static int descriptor_of(ForeignSocketRef<Kind, Rights> borrow) noexcept;
};

}  // namespace detail

template <typename Kind, ::fixy::os::RightSet Rights>
    requires MappedSocketKind<Kind>
class [[nodiscard]][[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]]
    ForeignSocketRef final {
    // The number of the socket and its generation when a door made the ref,
    // or the empty stamp for the empty ref.  The stamp holds the seal of
    // foundation/ByteSeal.h, so a number becomes a ref only through a door.
    ::fixy::fs::DescriptorStamp stamp_{};

    explicit constexpr ForeignSocketRef(::fixy::fs::DescriptorStamp stamp) noexcept : stamp_{stamp} {}

    template <typename OtherKind, ::fixy::os::RightSet OtherRights>
        requires MappedSocketKind<OtherKind>
    friend class ForeignSocketRef;

    friend struct detail::ForeignSocketDoor;

public:
    // The kind of the borrow, the same kind as that of a SocketRef.
    using kind_type = ::fixy::os::kind::Socket<Kind>;
    static constexpr ::fixy::os::RightSet rights = Rights;

    // The empty ref.
    constexpr ForeignSocketRef() noexcept = default;

    // A ref with the rights Fewer, which this ref holds.  It keeps the
    // stamp of this ref.
    template <::fixy::os::RightSet Fewer>
        requires ::fixy::os::HoldsRights<Rights, Fewer>
    [[nodiscard]] constexpr ForeignSocketRef<Kind, Fewer> restrict() const noexcept {
        return ForeignSocketRef<Kind, Fewer>{stamp_};
    }

    // False for the empty ref.  A ref that is open can still be stale: ask
    // is_current() for that.
    [[nodiscard]] constexpr bool is_open() const noexcept { return stamp_.is_open(); }

    // True when no Handle of the base opened, closed or released the number
    // of this ref since a door made it, so the number still names the socket
    // of the ref (the foreign socket).  False for the empty ref.  Complexity:
    // one acquire load.
    [[nodiscard]] bool is_current() const noexcept { return stamp_.is_current(); }

    // The identity of the socket of an open ref: the number of its descriptor.
    // A stale ref and the ref of a later socket of the number have one
    // identity, so a table that keys by identity also asks is_current().
    [[nodiscard]] constexpr SocketIdentity identity() const noexcept {
        CRUCIBLE_PRE(is_open());
        return SocketIdentity{static_cast<std::uint32_t>(stamp_.number())};
    }

    // The raw descriptor, for a call of a C ABI only (the ABI descriptor of
    // fixy/os/Handle.h).  The descriptor of the empty ref, and of a ref that
    // is not current, is -1, so a C ABI gets no number of a later socket.  A
    // constant evaluation reads no generation and gives the descriptor.
    [[nodiscard]] constexpr int abi_descriptor() const noexcept {
        if consteval {
            return stamp_.number();
        }
        return stamp_.current_number();
    }

    // Two refs are equal when they hold one descriptor of one generation.
    [[nodiscard]] friend constexpr bool operator==(ForeignSocketRef left, ForeignSocketRef right) noexcept {
        return left.stamp_ == right.stamp_;
    }
};

// The ref of descriptor with the generation of its number now.  The empty ref
// has no number, so it reads no generation.
template <typename Kind, ::fixy::os::RightSet Rights>
ForeignSocketRef<Kind, Rights> detail::ForeignSocketDoor::admit(int descriptor) noexcept {
    return ForeignSocketRef<Kind, Rights>{::fixy::fs::DescriptorStamp::read_now(descriptor)};
}

// The ref with the stamp of the borrow of a Handle, so a borrow that is not
// current gives a ref that is not current.
template <typename Kind, ::fixy::os::RightSet Rights>
ForeignSocketRef<Kind, Rights>
detail::ForeignSocketDoor::share(::fixy::os::HandleRef<::fixy::os::kind::Socket<Kind>, Rights> socket) noexcept {
    return ForeignSocketRef<Kind, Rights>{::fixy::os::detail::HandleDoor::stamp_of(socket)};
}

template <typename Kind, ::fixy::os::RightSet Rights>
int detail::ForeignSocketDoor::descriptor_of(ForeignSocketRef<Kind, Rights> borrow) noexcept {
    return borrow.stamp_.current_number();
}

// The ref of a socket that a C library opened, with the kind Kind and the
// rights Rights that the caller states.  The caller keeps the socket open for
// the life of each copy of the ref (the foreign socket).  The ref keeps the
// generation of the number now, so a later open or close of the number by a
// Handle of the base makes it stale.  The base does not see a close of the C
// library, so a reuse of the number by the C library keeps the ref current.
// Each call is inside an opt-out region of the class ABI (the ABI descriptor
// of fixy/os/Handle.h).
template <typename Kind, ::fixy::os::RightSet Rights>
    requires MappedSocketKind<Kind>
[[nodiscard]] ForeignSocketRef<Kind, Rights> admit_foreign_socket(NonNegative<int> descriptor) noexcept {
    return detail::ForeignSocketDoor::admit<Kind, Rights>(descriptor.value());
}

// The ref by value of the socket of a Handle, for a table that stores the
// socket.  The caller keeps the Handle open for the life of each copy of the
// ref (the foreign socket), and a ref that outlives the Handle is stale.  The
// ref keeps the stamp of the borrow, so the borrow of an empty Handle gives
// the empty ref, and a borrow that is not current gives a stale ref.
template <typename Kind, ::fixy::os::RightSet Rights>
    requires MappedSocketKind<Kind>
[[nodiscard]] ForeignSocketRef<Kind, Rights> share_socket(SocketRef<Kind, Rights> const& socket) noexcept {
    return detail::ForeignSocketDoor::share<Kind, Rights>(socket);
}

// A borrow of a socket: a SocketRef or a ForeignSocketRef.
template <typename Borrow>
concept SocketBorrow = ::foundation::reflect::IsInstanceOf<Borrow, ^^ForeignSocketRef>
                    || (::foundation::reflect::IsInstanceOf<Borrow, ^^::fixy::os::HandleRef>
                        && ::foundation::reflect::IsInstanceOf<typename Borrow::kind_type, ^^::fixy::os::kind::Socket>);

// A borrow of a socket that holds the rights Needed.
template <typename Borrow, ::fixy::os::RightSet Needed>
concept SocketBorrowWith = SocketBorrow<Borrow> && ::fixy::os::HoldsRights<Borrow::rights, Needed>;

// A borrow of a TCP stream socket that holds the rights Needed.
template <typename Borrow, ::fixy::os::RightSet Needed>
concept TcpBorrowWith = SocketBorrowWith<Borrow, Needed>
                     && std::same_as<typename Borrow::kind_type, ::fixy::os::kind::Socket<socket_kind::TcpStream>>;

namespace detail {

// The descriptor of a borrow of a socket, for a door of the base.
template <typename Kind, ::fixy::os::RightSet Rights>
[[nodiscard]] inline int socket_descriptor_of(SocketRef<Kind, Rights> socket) noexcept {
    return ::fixy::os::detail::HandleDoor::descriptor_of(socket);
}

// The descriptor of a current foreign ref, or -1 for a stale or empty ref, so
// the call of the door gives ebadf and names no later socket of the number.
template <typename Kind, ::fixy::os::RightSet Rights>
[[nodiscard]] inline int socket_descriptor_of(ForeignSocketRef<Kind, Rights> socket) noexcept {
    return ForeignSocketDoor::descriptor_of(socket);
}

// The part of the run of buffer that a call of the kernel filled, with the
// brand of the frame: the first count bytes when the call gave the count, or
// the error of the call.  The kernel writes at most the size of the run, and
// take_prefix_up_to holds that bound with one compare.  The part borrows the
// run, so the match reads the named count of the caller.
template <class Brand, std::size_t Extent>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::ViewMut<std::byte, Brand>, ::foundation::os::Errno>
filled_part_(::foundation::core::ViewMut<std::byte, Brand, Extent>&& buffer,
             ::foundation::core::Result<std::size_t, ::foundation::os::Errno> const& counted) noexcept {
    using Filled = ::foundation::core::Result<::foundation::core::ViewMut<std::byte, Brand>, ::foundation::os::Errno>;
    return counted.match(
        [run = std::move(buffer)](std::size_t count) mutable noexcept -> Filled {
            return ::foundation::core::take_prefix_up_to(std::move(run), count);
        },
        [](::foundation::os::Errno error) noexcept -> Filled { return ::foundation::core::err(error); });
}

}  // namespace detail

// Halves of a stream
//   Two threads divide a stream socket by direction (the shape of a socket kind
//   of fixy/os/Handle.h).  with_stream_halves takes the Handle of a stream
//   socket with the rights read and write, and gives its body a read half and
//   a write half of the socket.  The read half keeps the number of the Handle
//   with no write right, and the write half is a duplicate of it with no read
//   right.  The door advances the generation of the number first, so each
//   borrow and each ForeignSocketRef of the Handle from before the split gives
//   ebadf: none of them writes beside the write half.
//
//   Each half is linear and owes a consume, and it stays on its thread.  It
//   moves to another thread through a transit (depart() and arrive(), the
//   parts stream_read_part and stream_write_part).  It ends at a named door or
//   at the end of the frame.  close() of the read half closes its number.
//   shut_down_and_close() of the write half ends the write direction of the
//   socket first, so the peer reads the end of the stream while the read half
//   lives.  The end of a write half that no door ended does the same, as a
//   backstop of a path that the rule must-consume reports.  Each half owns its
//   number (owns_by_number{} of foundation/Transfer.h), so the quarantine
//   plugin refuses a read of a HandleRef of a half after a door that takes the
//   half by an rvalue reference.
//
//   The body gives back a Record of the read part, the write part and at most
//   one value.  A part is the half or the token of its end (StreamReadEnded,
//   StreamWriteEnded), each of the brand of the frame.  The value names no
//   brand of the frame and is no linear value, so it holds no half.  The door
//   reads the shape at compile time.  Two halves are the join: the door closes
//   the number of the write half, advances the generation of the first number,
//   and gives the Handle with its rights.  One half gives the Handle of its
//   number with its rights.  Two ends give Unit.  The value follows in a
//   Record.  A duplicate that the kernel refuses gives the Handle back with
//   the error (StreamHalvesRefused).
//
//   The brand of a frame is a scope brand (CRUCIBLE_SCOPE_BRAND of
//   foundation/Brand.h) in the last template parameter, after a pack that
//   takes each explicit argument, so no caller names it.  So a part of another
//   frame does not fit the body of this frame, and a join takes only the two
//   halves of its own split.  A recursion that gives a half of an outer frame
//   to an inner frame at the same site names the type of the half in its
//   parameters, so the inner frame is in another instantiation, with another
//   brand.  The halves can go to two threads inside the body, as run_joined
//   and the fork mints join each thread before the body returns.

template <class Kind, ::fixy::os::RightSet Rights, class Brand>
    requires ::fixy::os::StreamSocketKind<Kind> && ::foundation::brand::IsBrand<Brand>
class StreamReadHalf;

template <class Kind, ::fixy::os::RightSet Rights, class Brand>
    requires ::fixy::os::StreamSocketKind<Kind> && ::foundation::brand::IsBrand<Brand>
class StreamWriteHalf;

// The parts of a transit of a half (the transit of fixy/os/Handle.h).
template <class Brand>
struct stream_read_part final {
    template <class Kind, ::fixy::os::RightSet Rights>
    using owner = StreamReadHalf<Kind, Rights, Brand>;
};

template <class Brand>
struct stream_write_part final {
    template <class Kind, ::fixy::os::RightSet Rights>
    using owner = StreamWriteHalf<Kind, Rights, Brand>;
};

// The token of the end of the read half of a frame.  Only close() of the
// read half makes one.
template <class Brand>
class [[nodiscard]] StreamReadEnded final {
    // No copy of bytes builds a token (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr StreamReadEnded() noexcept = default;

    template <class Kind, ::fixy::os::RightSet Rights, class HalfBrand>
        requires ::fixy::os::StreamSocketKind<Kind> && ::foundation::brand::IsBrand<HalfBrand>
    friend class StreamReadHalf;

public:
    using brand_type = Brand;
};

// The token of the end of the write half of a frame.  Only
// shut_down_and_close() of the write half makes one.
template <class Brand>
class [[nodiscard]] StreamWriteEnded final {
    // No copy of bytes builds a token (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr StreamWriteEnded() noexcept = default;

    template <class Kind, ::fixy::os::RightSet Rights, class HalfBrand>
        requires ::fixy::os::StreamSocketKind<Kind> && ::foundation::brand::IsBrand<HalfBrand>
    friend class StreamWriteHalf;

public:
    using brand_type = Brand;
};

// The read half of a stream socket of a frame (halves of a stream).
template <class Kind, ::fixy::os::RightSet Rights, class Brand>
    requires ::fixy::os::StreamSocketKind<Kind> && ::foundation::brand::IsBrand<Brand>
class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::linear::owes_consume{}]]
                   [[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::owns_by_number{}]]
    StreamReadHalf final {
    // The number of the socket, or -1 for a half that a move emptied.
    int descriptor_ = -1;
    // No start of a lifetime over bytes builds a half (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    explicit StreamReadHalf(int descriptor) noexcept : descriptor_{descriptor} {}

    // The end of a number that no door ended: the close.
    static void close_number_(int descriptor) noexcept {
        if (descriptor >= 0) ::fixy::fs::detail::close_owned_descriptor_(descriptor);
    }

    // Gives the number to the door of the frame, which owns it from then on.
    [[nodiscard]] int release_number_() && noexcept {
        int const descriptor = descriptor_;
        descriptor_ = -1;
        return descriptor;
    }

    friend class SocketDoor;

public:
    // The half of a number that a transit carried.  The generation stays.
    // Only a transit has the key (TransitKey of fixy/os/Handle.h).
    [[nodiscard]] static StreamReadHalf from_number_(::fixy::os::TransitKey, int descriptor) noexcept {
        return StreamReadHalf{descriptor};
    }

    // The end of a number that a transit carried and that did not arrive.
    // Only a transit has the key.
    static void end_number_(::fixy::os::TransitKey, int descriptor) noexcept { close_number_(descriptor); }

    using kind_type = Kind;
    using brand_type = Brand;
    static constexpr ::fixy::os::RightSet rights = ::fixy::os::rights_without<Rights, ::fixy::os::RightSet::write>;

    StreamReadHalf(StreamReadHalf const&) = delete("a half carries one descriptor: a copy would give it two owners");
    StreamReadHalf&
    operator=(StreamReadHalf const&) = delete("a half carries one descriptor: a copy would give it two owners");

    // A move takes the descriptor, and the source holds none.
    StreamReadHalf(StreamReadHalf&& other) noexcept : descriptor_{other.descriptor_} { other.descriptor_ = -1; }
    StreamReadHalf&
    operator=(StreamReadHalf&&) = delete("a half ends at its door: an assignment would end the half that it replaces");

    ~StreamReadHalf() noexcept { close_number_(descriptor_); }

    // The borrow of the socket with the rights of the read half, so each door
    // of a write refuses it at compile time.  Complexity: one acquire load.
    [[nodiscard]] ::fixy::os::HandleRef<Kind, rights> ref() const& noexcept {
        return ::fixy::os::detail::HandleDoor::borrow_of_owned_number<Kind, rights>(descriptor_);
    }
    void ref() const&& = delete("the borrow of a temporary half dangles at the end of the full expression");

    // Closes the number of the half, and gives the token of its end.
    [[nodiscard]] StreamReadEnded<Brand> close() && noexcept {
        close_number_(descriptor_);
        descriptor_ = -1;
        return StreamReadEnded<Brand>{};
    }

    // Gives the number to a transit for another thread (the transit of
    // fixy/os/Handle.h).  The generation advances first, so each borrow of the
    // half on this thread is stale from then on.  Complexity: one atomic step.
    [[nodiscard]] ::fixy::os::HandleInTransit<Kind, Rights, stream_read_part<Brand>> depart() && noexcept {
        ::fixy::fs::detail::advance_descriptor_generation_(descriptor_);
        int const descriptor = descriptor_;
        descriptor_ = -1;
        return ::fixy::os::HandleInTransit<Kind, Rights, stream_read_part<Brand>>{descriptor};
    }
};

// The write half of a stream socket of a frame (halves of a stream).
template <class Kind, ::fixy::os::RightSet Rights, class Brand>
    requires ::fixy::os::StreamSocketKind<Kind> && ::foundation::brand::IsBrand<Brand>
class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::linear::owes_consume{}]]
                   [[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::owns_by_number{}]]
    StreamWriteHalf final {
    // The number of the duplicate, or -1 for a half that a move emptied.
    int descriptor_ = -1;
    // No start of a lifetime over bytes builds a half (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    explicit StreamWriteHalf(int descriptor) noexcept : descriptor_{descriptor} {}

    // The end of a number that no door ended: the end of the write direction
    // of the socket, so the peer reads the end of the stream, and the close.
    // A socket with no connection refuses the shutdown, and the close follows.
    static void shut_down_number_(int descriptor) noexcept {
        if (descriptor < 0) return;
        static_cast<void>(detail::shut_down_writes_(descriptor));
        ::fixy::fs::detail::close_owned_descriptor_(descriptor);
    }

    // Gives the number to the door of the frame, which owns it from then on.
    [[nodiscard]] int release_number_() && noexcept {
        int const descriptor = descriptor_;
        descriptor_ = -1;
        return descriptor;
    }

    friend class SocketDoor;

public:
    // The half of a number that a transit carried.  The generation stays.
    // Only a transit has the key (TransitKey of fixy/os/Handle.h).
    [[nodiscard]] static StreamWriteHalf from_number_(::fixy::os::TransitKey, int descriptor) noexcept {
        return StreamWriteHalf{descriptor};
    }

    // The end of a number that a transit carried and that did not arrive: the
    // end of the write direction and the close.  Only a transit has the key.
    static void end_number_(::fixy::os::TransitKey, int descriptor) noexcept { shut_down_number_(descriptor); }

    using kind_type = Kind;
    using brand_type = Brand;
    static constexpr ::fixy::os::RightSet rights = ::fixy::os::rights_without<Rights, ::fixy::os::RightSet::read>;

    StreamWriteHalf(StreamWriteHalf const&) = delete("a half carries one descriptor: a copy would give it two owners");
    StreamWriteHalf&
    operator=(StreamWriteHalf const&) = delete("a half carries one descriptor: a copy would give it two owners");

    // A move takes the descriptor, and the source holds none.
    StreamWriteHalf(StreamWriteHalf&& other) noexcept : descriptor_{other.descriptor_} { other.descriptor_ = -1; }
    StreamWriteHalf&
    operator=(StreamWriteHalf&&) = delete("a half ends at its door: an assignment would end the half that it replaces");

    // The end of a live write half ends the write direction, as
    // shut_down_and_close() does, so no path closes it in silence.
    ~StreamWriteHalf() noexcept { shut_down_number_(descriptor_); }

    // The borrow of the socket with the rights of the write half, so each
    // door of a read refuses it at compile time.  Complexity: one acquire load.
    [[nodiscard]] ::fixy::os::HandleRef<Kind, rights> ref() const& noexcept {
        return ::fixy::os::detail::HandleDoor::borrow_of_owned_number<Kind, rights>(descriptor_);
    }
    void ref() const&& = delete("the borrow of a temporary half dangles at the end of the full expression");

    // Ends the write direction of the socket and closes the number, and gives
    // the token of the end.  The peer then reads the end of the stream, also
    // while the read half lives.
    [[nodiscard]] StreamWriteEnded<Brand> shut_down_and_close() && noexcept {
        shut_down_number_(descriptor_);
        descriptor_ = -1;
        return StreamWriteEnded<Brand>{};
    }

    // Gives the number to a transit for another thread (the transit of
    // fixy/os/Handle.h).  The generation advances first, so each borrow of the
    // half on this thread is stale from then on.  Complexity: one atomic step.
    [[nodiscard]] ::fixy::os::HandleInTransit<Kind, Rights, stream_write_part<Brand>> depart() && noexcept {
        ::fixy::fs::detail::advance_descriptor_generation_(descriptor_);
        int const descriptor = descriptor_;
        descriptor_ = -1;
        return ::fixy::os::HandleInTransit<Kind, Rights, stream_write_part<Brand>>{descriptor};
    }
};

// The refusal of a split (halves of a stream): the error of the duplicate,
// and the Handle back.  It is a linear error of a Result, which gives the
// resource back to the caller.  The end of its scope closes the Handle, so it
// is the door of the refusal.
template <class Kind, ::fixy::os::RightSet Rights>
struct [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::linear::scope_end_door{}]]
    StreamHalvesRefused final {
    ::foundation::os::Errno error{};
    ::fixy::os::Handle<Kind, Rights> socket{};
    // No start of a lifetime over bytes builds a refusal (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// The gate of a split: the context admits a call that can wait (the duplicate
// of fixy/os/Handle.h), the kind is a stream socket, and the Handle holds the
// two directions.
template <class Ctx, class Kind, ::fixy::os::RightSet Rights>
concept CtxFitsStreamHalves = ::fixy::fs::CtxAdmitsFs<Ctx> && ::fixy::os::StreamSocketKind<Kind>
                           && ::fixy::os::HoldsRights<Rights, ::fixy::os::RightSet::read | ::fixy::os::RightSet::write>;

namespace detail {

// The read part, the write part and the value of the Record that a body of a
// frame gives back (halves of a stream).  A type that is no Record of two or
// three parts has no shape.
template <class Back>
struct halves_back_ {
    static constexpr bool is_record = false;
};

template <class ReadPart, class WritePart>
struct halves_back_<::foundation::core::Record<ReadPart, WritePart>> {
    static constexpr bool is_record = true;
    static constexpr bool has_value = false;
    using read_part = ReadPart;
    using write_part = WritePart;
};

template <class ReadPart, class WritePart, class Value>
struct halves_back_<::foundation::core::Record<ReadPart, WritePart, Value>> {
    static constexpr bool is_record = true;
    static constexpr bool has_value = true;
    using read_part = ReadPart;
    using write_part = WritePart;
    using value = Value;
};

// True when Back is the Record of a frame of the brand Brand: a read part, a
// write part, each the half or the token of its end, and at most one value
// that names no brand of the frame and is no linear value.
template <class Back, class Kind, ::fixy::os::RightSet Rights, class Brand>
[[nodiscard]] consteval bool is_halves_back_() noexcept {
    using Shape = halves_back_<Back>;
    if constexpr (!Shape::is_record) {
        return false;
    } else {
        using ReadPart = typename Shape::read_part;
        using WritePart = typename Shape::write_part;
        bool const has_parts = (std::same_as<ReadPart, StreamReadHalf<Kind, Rights, Brand>>
                                || std::same_as<ReadPart, StreamReadEnded<Brand>>)
                            && (std::same_as<WritePart, StreamWriteHalf<Kind, Rights, Brand>>
                                || std::same_as<WritePart, StreamWriteEnded<Brand>>);
        if constexpr (Shape::has_value) {
            using Value = typename Shape::value;
            return has_parts && !::foundation::core::detail::names_brand(^^Value, ^^Brand)
                && !::foundation::linear::is_linear_type(^^Value);
        } else {
            return has_parts;
        }
    }
}

}  // namespace detail

// A body of a frame of halves (halves of a stream): it takes the two halves
// with no throw, holds no linear value by value, and stores no borrow outside
// the frame.  The door reads the shape of the Record that the body gives back
// in a static_assert, so a wrong shape gives its reason.
template <class Body, class Kind, ::fixy::os::RightSet Rights, class Brand>
concept StreamHalvesBody =
    ::foundation::linear::HoldsNoLinearValue<Body> && ::foundation::core::StoresNoBorrowOutside<Body>
    && std::is_nothrow_invocable_v<Body&&, StreamReadHalf<Kind, Rights, Brand>&&,
                                   StreamWriteHalf<Kind, Rights, Brand>&&>;

// The frame of the two halves of a stream socket (halves of a stream).
// Declared here and defined below SocketDoor, because SocketDoor names it as
// its friend.
template <class... Refused, class Kind, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0
             && CtxFitsStreamHalves<Ctx, Kind, Rights> && StreamHalvesBody<Body, Kind, Rights, Fresh>)
[[nodiscard]] auto with_stream_halves(Ctx const&, ::fixy::os::Handle<Kind, Rights>&& socket, Body&& body) noexcept;

// The Kind parameter precedes Ctx because the caller names it explicitly
// and Ctx deduces from the argument.
//
// Declared here and defined below SocketDoor, because SocketDoor names it
// as its friend, and a friend must already have been declared.
template <typename Kind, eff::IsExecCtx Ctx>
    requires CtxFitsSocketMint<Ctx, Kind>
[[nodiscard]] inline ::foundation::core::Result<Linear<SocketHandle<Kind>>, ::foundation::os::Errno>
mint_socket(Ctx const&) noexcept;

// Declared here and defined below SocketDoor, for the reason of mint_socket.
template <typename Kind, eff::IsExecCtx Ctx>
    requires CtxFitsSocketPairMint<Ctx, Kind>
[[nodiscard]] inline ::foundation::core::Result<SocketPair<Kind>, ::foundation::os::Errno>
mint_socket_pair(Ctx const&) noexcept;

// The door to socket(2) and socketpair(2).  No object of it exists.  Its
// members are private, and its friends are mint_socket and mint_socket_pair.
// A Handle befriends this class, so a socket handle comes only from one of
// the two mints, after its gate.  The door reads the triple of the kind, so
// no caller gives it a domain, a type or a protocol.
class SocketDoor final : ::foundation::NoObject<SocketDoor> {
    template <typename FriendKind, eff::IsExecCtx FriendCtx>
        requires CtxFitsSocketMint<FriendCtx, FriendKind>
    friend auto mint_socket(FriendCtx const&) noexcept
        -> ::foundation::core::Result<Linear<SocketHandle<FriendKind>>, ::foundation::os::Errno>;

    template <typename FriendKind, eff::IsExecCtx FriendCtx>
        requires CtxFitsSocketPairMint<FriendCtx, FriendKind>
    friend auto mint_socket_pair(FriendCtx const&) noexcept
        -> ::foundation::core::Result<SocketPair<FriendKind>, ::foundation::os::Errno>;

    template <class... Refused, class FriendKind, ::fixy::os::RightSet FriendRights, eff::IsExecCtx FriendCtx,
              class FriendBody, ::foundation::brand::IsBrand FriendFresh>
        requires(sizeof...(Refused) == 0 && CtxFitsStreamHalves<FriendCtx, FriendKind, FriendRights>
                 && StreamHalvesBody<FriendBody, FriendKind, FriendRights, FriendFresh>)
    friend auto with_stream_halves(FriendCtx const&, ::fixy::os::Handle<FriendKind, FriendRights>&& socket,
                                   FriendBody&& body) noexcept;

    // The Handle of the parts that a body of a frame gives back (halves of a
    // stream).  Two halves are the join: the number of the write half closes,
    // and the generation of the first number advances, so no borrow of a half
    // is current after the frame.  One half gives the Handle of its number
    // with its rights, and its generation advances too.  Two ends give Unit.
    template <class Kind, ::fixy::os::RightSet Rights, class Brand, class ReadPart, class WritePart>
    [[nodiscard]] static auto handle_of_parts_(ReadPart&& read_part, WritePart&& write_part) noexcept {
        constexpr bool keeps_read = std::same_as<ReadPart, StreamReadHalf<Kind, Rights, Brand>>;
        constexpr bool keeps_write = std::same_as<WritePart, StreamWriteHalf<Kind, Rights, Brand>>;
        if constexpr (keeps_read && keeps_write) {
            int const read_number = std::move(read_part).release_number_();
            int const write_number = std::move(write_part).release_number_();
            ::fixy::fs::detail::close_owned_descriptor_(write_number);
            ::fixy::fs::detail::advance_descriptor_generation_(read_number);
            return ::fixy::os::Handle<Kind, Rights>::adopt_number_(read_number);
        } else if constexpr (keeps_read) {
            static_cast<void>(write_part);
            int const read_number = std::move(read_part).release_number_();
            ::fixy::fs::detail::advance_descriptor_generation_(read_number);
            return ::fixy::os::Handle<
                Kind, ::fixy::os::rights_without<Rights, ::fixy::os::RightSet::write>>::adopt_number_(read_number);
        } else if constexpr (keeps_write) {
            static_cast<void>(read_part);
            int const write_number = std::move(write_part).release_number_();
            ::fixy::fs::detail::advance_descriptor_generation_(write_number);
            return ::fixy::os::Handle<
                Kind, ::fixy::os::rights_without<Rights, ::fixy::os::RightSet::read>>::adopt_number_(write_number);
        } else {
            static_cast<void>(read_part);
            static_cast<void>(write_part);
            return ::foundation::core::Unit{};
        }
    }

    // The value of a frame: the Handle of the parts, and the value of the body
    // when the body gives one.
    template <class Kind, ::fixy::os::RightSet Rights, class Brand, class Back>
    [[nodiscard]] static auto value_of_back_(Back&& back) noexcept {
        if constexpr (detail::halves_back_<Back>::has_value) {
            auto [read_part, write_part, value] = std::move(back);
            auto handle = handle_of_parts_<Kind, Rights, Brand>(std::move(read_part), std::move(write_part));
            return ::foundation::core::Record<decltype(handle), typename detail::halves_back_<Back>::value>{
                std::move(handle), std::move(value)};
        } else {
            auto [read_part, write_part] = std::move(back);
            return handle_of_parts_<Kind, Rights, Brand>(std::move(read_part), std::move(write_part));
        }
    }

    // The split of a frame (halves of a stream).  The duplicate comes first,
    // so a refusal gives the Handle back as it was.  An empty Handle gives
    // ebadf and makes no call.  Then the generation of each number advances:
    // the first one ends each borrow of the Handle, and the second one starts
    // the ownership of the write half, as the open of a Handle does.  The body
    // gets the two halves.
    template <class Kind, ::fixy::os::RightSet Rights, class Brand, class Body>
    [[nodiscard]] static auto run_stream_halves_(::fixy::os::Handle<Kind, Rights>&& socket, Body&& body) noexcept {
        using ReadHalf = StreamReadHalf<Kind, Rights, Brand>;
        using WriteHalf = StreamWriteHalf<Kind, Rights, Brand>;
        using Back = std::invoke_result_t<Body&&, ReadHalf&&, WriteHalf&&>;
        using Value = decltype(value_of_back_<Kind, Rights, Brand>(std::declval<Back&&>()));
        using Refused = StreamHalvesRefused<Kind, Rights>;
        using Split = ::foundation::core::Result<Value, Refused>;
        int const first = socket.descriptor_;
        if (first < 0) {
            return Split{::foundation::core::err(Refused{::foundation::os::Errno::ebadf, std::move(socket)})};
        }
        ::foundation::core::Result<int, ::foundation::os::Errno> duplicated =
            ::fixy::fs::detail::admit_opened_number_(::fixy::fs::detail::duplicate_descriptor_(first));
        if (duplicated.is_err()) {
            ::foundation::os::Errno const error = std::move(duplicated).err().value_or(::foundation::os::Errno::ebadf);
            return Split{::foundation::core::err(Refused{error, std::move(socket)})};
        }
        int const second = std::move(duplicated).value_or(-1);
        ::fixy::fs::detail::advance_descriptor_generation_(first);
        ::fixy::fs::detail::advance_descriptor_generation_(second);
        socket.descriptor_ = -1;
        Back back = static_cast<Body&&>(body)(ReadHalf{first}, WriteHalf{second});
        return Split{value_of_back_<Kind, Rights, Brand>(std::move(back))};
    }

    // The open door of src/fixy/os/Socket.cpp adds SOCK_CLOEXEC, because a
    // descriptor that survives execve leaks into each child process.  It
    // gives the error of the kernel on failure and no handle.
    template <MappedSocketKind Kind>
    [[nodiscard]] static ::foundation::core::Result<Linear<SocketHandle<Kind>>, ::foundation::os::Errno>
    open_() noexcept {
        constexpr detail::socket_triple triple = socket_triple_of(^^Kind);
        return ::fixy::fs::detail::OpenedDescriptorDoor::admit<::fixy::fs::OpenedType::of_call>(
            detail::open_socket_(triple.domain, triple.type, triple.protocol),
            [](::fixy::fs::OpenedDescriptor<::fixy::fs::OpenedType::of_call> opened) noexcept {
                return mint_linear<SocketHandle<Kind>>(SocketHandle<Kind>{std::move(opened)});
            });
    }

    // The pair door of src/fixy/os/Socket.cpp adds SOCK_CLOEXEC to the type of
    // the two ends.  It gives the error of the kernel on failure and no handle.
    template <PairedSocketKind Kind>
    [[nodiscard]] static ::foundation::core::Result<SocketPair<Kind>, ::foundation::os::Errno> open_pair_() noexcept {
        using Opened = ::foundation::core::Result<SocketPair<Kind>, ::foundation::os::Errno>;
        constexpr detail::socket_triple triple = socket_triple_of(^^Kind);
        return detail::open_socket_pair_(triple.domain, triple.type, triple.protocol)
            .match([](detail::SocketPairNumbers numbers) noexcept -> Opened { return own_pair_<Kind>(numbers); },
                   [](::foundation::os::Errno error) noexcept -> Opened { return ::foundation::core::err(error); });
    }

    // The two Handles of the numbers of a pair.  Each number goes through the
    // door of an opened number (fixy/os/Fd.h), which closes a number past the
    // slots of the generations and gives emfile.  When one number is refused,
    // the Handle of the other one closes it at the end of this call, and the
    // error is the result.
    template <PairedSocketKind Kind>
    [[nodiscard]] static ::foundation::core::Result<SocketPair<Kind>, ::foundation::os::Errno>
    own_pair_(detail::SocketPairNumbers numbers) noexcept {
        using Opened = ::foundation::core::Result<SocketPair<Kind>, ::foundation::os::Errno>;
        using Owned = ::foundation::core::Result<SocketHandle<Kind>, ::foundation::os::Errno>;
        auto const own = [](::fixy::fs::OpenedDescriptor<::fixy::fs::OpenedType::of_call> opened) noexcept {
            return SocketHandle<Kind>{std::move(opened)};
        };
        Owned first = ::fixy::fs::detail::OpenedDescriptorDoor::admit<::fixy::fs::OpenedType::of_call>(
            ::foundation::core::Result<int, ::foundation::os::Errno>{numbers.first}, own);
        Owned second = ::fixy::fs::detail::OpenedDescriptorDoor::admit<::fixy::fs::OpenedType::of_call>(
            ::foundation::core::Result<int, ::foundation::os::Errno>{numbers.second}, own);
        return std::move(first).match(
            [&second](SocketHandle<Kind> first_end) noexcept -> Opened {
                return std::move(second).match(
                    [&first_end](SocketHandle<Kind> second_end) noexcept -> Opened {
                        return SocketPair<Kind>{mint_linear<SocketHandle<Kind>>(std::move(first_end)),
                                                mint_linear<SocketHandle<Kind>>(std::move(second_end))};
                    },
                    [](::foundation::os::Errno error) noexcept -> Opened { return ::foundation::core::err(error); });
            },
            [](::foundation::os::Errno error) noexcept -> Opened { return ::foundation::core::err(error); });
    }
};

template <typename Kind, eff::IsExecCtx Ctx>
    requires CtxFitsSocketMint<Ctx, Kind>
[[nodiscard]] inline ::foundation::core::Result<Linear<SocketHandle<Kind>>, ::foundation::os::Errno>
mint_socket(Ctx const&) noexcept {
    return SocketDoor::open_<Kind>();
}

// The frame of the two halves of a stream socket (halves of a stream).  The
// result is the value of the shape that the body gives back, or the refusal
// of the duplicate with the Handle.  Complexity: one duplicate call and one
// generation step for the split, one close and one generation step for the
// join, and the body.
template <class... Refused, class Kind, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx, class Body,
          ::foundation::brand::IsBrand Fresh>
    requires(sizeof...(Refused) == 0
             && CtxFitsStreamHalves<Ctx, Kind, Rights> && StreamHalvesBody<Body, Kind, Rights, Fresh>)
[[nodiscard]] auto with_stream_halves(Ctx const&, ::fixy::os::Handle<Kind, Rights>&& socket, Body&& body) noexcept {
    constexpr bool is_shaped = detail::is_halves_back_<
        std::invoke_result_t<Body&&, StreamReadHalf<Kind, Rights, Fresh>&&, StreamWriteHalf<Kind, Rights, Fresh>&&>,
        Kind, Rights, Fresh>();
    static_assert(is_shaped,
                  "with_stream_halves: the body gives back a Record of the read part, the write part and at most one "
                  "value.  Each part is the half of this frame or the token of its end, and the value names no brand "
                  "of the frame and is no linear value");
    // A body of another shape stops at the static_assert, and the door then
    // makes no split, so the compile gives no second error.
    if constexpr (is_shaped) {
        return SocketDoor::run_stream_halves_<Kind, Rights, Fresh>(std::move(socket), static_cast<Body&&>(body));
    }
}

// A body that holds a linear value by value binds to this overload, so the
// refusal gives its reason (StreamHalvesBody).
template <class... Refused, class Kind, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx, class Body>
    requires(sizeof...(Refused) == 0
             && CtxFitsStreamHalves<Ctx, Kind, Rights> && !::foundation::linear::HoldsNoLinearValue<Body>)
void with_stream_halves(Ctx const&, ::fixy::os::Handle<Kind, Rights>&& socket,
                        Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// A body that can store a borrow outside the frame binds to this overload, so
// the refusal gives its reason (StreamHalvesBody).
template <class... Refused, class Kind, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx, class Body>
    requires(sizeof...(Refused) == 0 && CtxFitsStreamHalves<Ctx, Kind, Rights>
             && ::foundation::linear::HoldsNoLinearValue<Body> && !::foundation::core::StoresNoBorrowOutside<Body>)
void with_stream_halves(Ctx const&, ::fixy::os::Handle<Kind, Rights>&& socket,
                        Body&& body) = delete(CRUCIBLE_STORING_BODY_REFUSAL);

// The two connected ends of one stream socket of the local domain (the socket
// pair).  The kernel answers socketpair(2) at once.  A process with no free
// number gives emfile, and each other error of the kernel is the result.
template <typename Kind, eff::IsExecCtx Ctx>
    requires CtxFitsSocketPairMint<Ctx, Kind>
[[nodiscard]] inline ::foundation::core::Result<SocketPair<Kind>, ::foundation::os::Errno>
mint_socket_pair(Ctx const&) noexcept {
    return SocketDoor::open_pair_<Kind>();
}

// Socket calls
//   Each call takes the borrow of a socket.  A call of the bytes takes a
//   SocketRef, and a call of the options or the stamps takes a SocketRef or a
//   ForeignSocketRef (SocketBorrow) with the right ioctl_socket, or read for
//   a receive.  A TCP option needs a borrow of a TcpStream socket.
//   The door file src/fixy/os/Socket.cpp issues each call
//   with the raw primitive (fixy/os/SocketCalls.h), and gives the error of
//   the kernel in a Result.  A call that a signal interrupts runs again.
//   send_bytes, receive_bytes and receive_with_timestamps pass MSG_DONTWAIT
//   and never wait: a netlink request of the kernel queues its reply before
//   the send returns, and the receive of a dump makes its next part.  A
//   receive with no bytes gives eagain.  The door adds MSG_NOSIGNAL to each
//   send, so a send to a socket with no connection gives epipe and raises no
//   SIGPIPE.  Each call takes a context that admits the row of the network
//   family of the syscall catalog.

using socket_call_row_t =
    eff::lift_row_t<::fixy::atom::syscall::family<::fixy::atom::syscall::SyscallFamily::NetworkIo>>;

template <typename Ctx>
concept CtxFitsSocketCall = eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, socket_call_row_t>;

// The longest name of a congestion control of the kernel, with no zero
// (TCP_CA_NAME_MAX of the kernel, less one).
inline constexpr std::size_t congestion_name_bytes = 15;

namespace detail {

// The address of a netlink socket that asks the kernel for a unique port:
// the bytes of struct sockaddr_nl with no port and no group.  The check file
// of this header compares the size with that struct.
struct NetlinkAddress final {
    std::uint16_t family = static_cast<std::uint16_t>(address_family_netlink);
    std::uint16_t pad = 0;
    std::uint32_t port = 0;
    std::uint32_t groups = 0;
};

}  // namespace detail

// Sets the congestion control of a TCP socket to name (TCP_CONGESTION).  A
// name of more than congestion_name_bytes characters gives enametoolong.
template <eff::IsExecCtx Ctx, typename Borrow>
    requires CtxFitsSocketCall<Ctx> && TcpBorrowWith<Borrow, ::fixy::os::RightSet::ioctl_socket>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_tcp_congestion(Ctx const&, Borrow socket, ::foundation::core::TextView name) noexcept {
    if (name.size() > congestion_name_bytes) return ::foundation::core::err(::foundation::os::Errno::enametoolong);
    ::foundation::core::FixedText<congestion_name_bytes + 1> bytes{};
    bytes.append(name).expect("the name fits below the capacity");
    bytes.append('\0').expect("the zero fits after the name");
    return detail::set_tcp_option_(detail::socket_descriptor_of(socket), TcpOption::congestion,
                                   ::foundation::core::detail::text_first_(bytes.view()),
                                   static_cast<std::uint32_t>(bytes.size()));
}

// The name of the congestion control of a TCP socket (TCP_CONGESTION).
template <eff::IsExecCtx Ctx, typename Borrow>
    requires CtxFitsSocketCall<Ctx> && TcpBorrowWith<Borrow, ::fixy::os::RightSet::ioctl_socket>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::FixedText<congestion_name_bytes>,
                                                ::foundation::os::Errno>
read_tcp_congestion(Ctx const&, Borrow socket) noexcept {
    char bytes[congestion_name_bytes + 1]{};
    return detail::read_tcp_option_(detail::socket_descriptor_of(socket), TcpOption::congestion, bytes, sizeof(bytes))
        .map([&bytes](std::size_t length) noexcept {
            std::size_t count = 0;
            while (count < length && count < congestion_name_bytes && bytes[count] != '\0') {
                ++count;
            }
            ::foundation::core::FixedText<congestion_name_bytes> name{};
            name.append(::foundation::core::mint_foreign_text(bytes, count).characters())
                .expect("the name holds at most congestion_name_bytes characters");
            return name;
        });
}

// Reads a TCP option of a socket into the run of a frame, and gives the
// ViewMut of the part that the kernel wrote, with the brand of the frame.
// The door takes the ViewMut of the frame by value, as fill does, so the read
// is the one writer of the run.  An option that the kernel gives as a fixed
// struct, such as tcp_info (TcpOption::info) or tcp_cc_info
// (TcpOption::congestion_info), reads into a run of its size, and a kernel
// with a shorter struct writes fewer bytes.
template <eff::IsExecCtx Ctx, typename Borrow, class Brand, std::size_t Extent>
    requires CtxFitsSocketCall<Ctx> && TcpBorrowWith<Borrow, ::fixy::os::RightSet::ioctl_socket>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::ViewMut<std::byte, Brand>, ::foundation::os::Errno>
read_tcp_option(Ctx const&, Borrow socket, TcpOption option,
                ::foundation::core::ViewMut<std::byte, Brand, Extent> buffer) noexcept {
    std::size_t const size = buffer.size();
    std::uint32_t const capacity = size < 0xFFFFFFFFU ? static_cast<std::uint32_t>(size) : 0xFFFFFFFFU;
    ::foundation::core::Result<std::size_t, ::foundation::os::Errno> const read =
        detail::read_tcp_option_(detail::socket_descriptor_of(socket), option,
                                 first_of_(::foundation::core::detail::BaseLayerRead{}, buffer), capacity);
    return detail::filled_part_(std::move(buffer), read);
}

// Sets the least retransmission timeout of a TCP socket in microseconds
// (TCP_RTO_MIN_US).  A kernel with no such option gives enoprotoopt.
template <eff::IsExecCtx Ctx, typename Borrow>
    requires CtxFitsSocketCall<Ctx> && TcpBorrowWith<Borrow, ::fixy::os::RightSet::ioctl_socket>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_tcp_rto_min(Ctx const&, Borrow socket, std::uint32_t microseconds) noexcept {
    return detail::set_tcp_option_(detail::socket_descriptor_of(socket), TcpOption::rto_min_us, &microseconds,
                                   sizeof(microseconds));
}

// Sets the largest pacing rate of a socket in bytes per second
// (SO_MAX_PACING_RATE).
template <eff::IsExecCtx Ctx, typename Borrow>
    requires CtxFitsSocketCall<Ctx> && SocketBorrowWith<Borrow, ::fixy::os::RightSet::ioctl_socket>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_max_pacing_rate(Ctx const&, Borrow socket, std::uint64_t bytes_per_second) noexcept {
    return detail::set_option_(detail::socket_descriptor_of(socket), detail::socket_level,
                               detail::max_pacing_rate_option, &bytes_per_second, sizeof(bytes_per_second));
}

// A socket call that needs the rights Needed of a socket with the rights
// Held.
template <typename Ctx, ::fixy::os::RightSet Held, ::fixy::os::RightSet Needed>
concept CtxFitsSocketCallWith = CtxFitsSocketCall<Ctx> && ::fixy::os::HoldsRights<Held, Needed>;

// Binds a netlink socket to a port that the kernel gives, with no group.
// Two threads of one process then get the replies of their own requests.
template <::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsSocketCall<Ctx>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
bind_netlink(Ctx const&, SocketRef<socket_kind::NetlinkRoute, Rights> socket) noexcept {
    detail::NetlinkAddress const address{};
    return detail::bind_address_(::fixy::os::detail::HandleDoor::descriptor_of(socket), &address, sizeof(address));
}

// Sends the bytes of buffer, and gives the count that the kernel took.  A
// socket with no connection gives epipe, and the process gets no SIGPIPE.
template <typename Kind, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsSocketCallWith<Ctx, Rights, ::fixy::os::RightSet::write>
[[nodiscard]] inline ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
send_bytes(Ctx const&, SocketRef<Kind, Rights> socket, ::foundation::core::View<std::byte const> buffer) noexcept {
    return detail::send_with_flags_(::fixy::os::detail::HandleDoor::descriptor_of(socket),
                                    first_of_(::foundation::core::detail::BaseLayerRead{}, buffer), buffer.size(),
                                    detail::no_wait_flag);
}

// Receives the bytes that the socket holds into the run of a frame, at most
// the size of the run, and gives the ViewMut of the part that they filled,
// with the brand of the frame.  The door takes the ViewMut of the frame by
// value, as fill does, so the receive is the one writer of the run.  The
// kernel writes at most the size of the run, and take_prefix_up_to holds
// that bound with one compare.  It does not wait: a socket with no bytes
// gives eagain.
template <typename Kind, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx, class Brand, std::size_t Extent>
    requires CtxFitsSocketCallWith<Ctx, Rights, ::fixy::os::RightSet::read>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::ViewMut<std::byte, Brand>, ::foundation::os::Errno>
receive_bytes(Ctx const&, SocketRef<Kind, Rights> socket,
              ::foundation::core::ViewMut<std::byte, Brand, Extent> buffer) noexcept {
    ::foundation::core::Result<std::size_t, ::foundation::os::Errno> const received = detail::receive_with_flags_(
        ::fixy::os::detail::HandleDoor::descriptor_of(socket),
        first_of_(::foundation::core::detail::BaseLayerRead{}, buffer), buffer.size(), detail::no_wait_flag);
    return detail::filled_part_(std::move(buffer), received);
}

// The longest name of a network interface of the kernel, with no zero
// (IFNAMSIZ of the kernel, less one).
inline constexpr std::size_t interface_name_bytes = detail::interface_name_capacity - 1;

// Turns on the receive stamps of a socket (SO_TIMESTAMPING): the software
// stamp of the kernel, and the raw stamp of the hardware clock of the NIC
// when the NIC takes one.  receive_with_timestamps gives the stamps of each
// datagram.
template <eff::IsExecCtx Ctx, typename Borrow>
    requires CtxFitsSocketCall<Ctx> && SocketBorrowWith<Borrow, ::fixy::os::RightSet::ioctl_socket>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_socket_timestamping(Ctx const&, Borrow socket) noexcept {
    std::uint32_t const flags = detail::receive_timestamping_flags;
    return detail::set_option_(detail::socket_descriptor_of(socket), detail::socket_level, detail::timestamping_option,
                               &flags, sizeof(flags));
}

// Turns on the hardware stamps of the NIC of the network interface
// interface_name (SIOCSHWTSTAMP): a stamp of each sent packet and of each
// received packet.  The kernel needs CAP_NET_ADMIN for the call, and a NIC
// with no hardware clock refuses it.  A name of more than
// interface_name_bytes characters gives enametoolong.
template <eff::IsExecCtx Ctx, typename Borrow>
    requires CtxFitsSocketCall<Ctx> && SocketBorrowWith<Borrow, ::fixy::os::RightSet::ioctl_socket>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_hardware_timestamping(Ctx const&, Borrow socket, ::foundation::core::TextView interface_name) noexcept {
    return detail::set_hardware_timestamping_(detail::socket_descriptor_of(socket), interface_name);
}

// Receives one datagram into buffer with the stamps of its control data
// (SCM_TIMESTAMPING).  The payload is the part of buffer that the kernel
// wrote, and it borrows buffer.  The kernel writes at most the size of
// buffer, and prefix_up_to holds that bound with one compare.  It does not
// wait: a socket with no datagram gives eagain.  The control data holds no
// stamp when the socket has no SO_TIMESTAMPING (set_socket_timestamping).
// This form takes a plain View that no frame lends, such as the View of an
// arena run.
template <eff::IsExecCtx Ctx, typename Borrow>
    requires CtxFitsSocketCall<Ctx> && SocketBorrowWith<Borrow, ::fixy::os::RightSet::read>
[[nodiscard]] inline ::foundation::core::Result<TimestampedReceive<::foundation::core::View<std::byte>>,
                                                ::foundation::os::Errno>
receive_with_timestamps(Ctx const&, Borrow socket, ::foundation::core::View<std::byte> buffer) noexcept {
    using Received =
        ::foundation::core::Result<TimestampedReceive<::foundation::core::View<std::byte>>, ::foundation::os::Errno>;
    ::foundation::core::Result<detail::TimestampedCount, ::foundation::os::Errno> const received =
        detail::receive_with_timestamps_(detail::socket_descriptor_of(socket),
                                         first_of_(::foundation::core::detail::BaseLayerRead{}, buffer), buffer.size(),
                                         detail::no_wait_flag);
    return received.match(
        [buffer](detail::TimestampedCount const& counted) noexcept -> Received {
            return TimestampedReceive<::foundation::core::View<std::byte>>{
                .payload = buffer.prefix_up_to(counted.count),
                .control = counted.control,
            };
        },
        [](::foundation::os::Errno error) noexcept -> Received { return ::foundation::core::err(error); });
}

// The receive with stamps into the run of a frame.  The payload is the
// ViewMut of the part that the kernel wrote, with the brand of the frame.
// The door takes the ViewMut of the frame by value, as fill does, so the
// receive is the one writer of the run.
template <eff::IsExecCtx Ctx, typename Borrow, class Brand, std::size_t Extent>
    requires CtxFitsSocketCall<Ctx> && SocketBorrowWith<Borrow, ::fixy::os::RightSet::read>
[[nodiscard]] inline ::foundation::core::Result<TimestampedReceive<::foundation::core::ViewMut<std::byte, Brand>>,
                                                ::foundation::os::Errno>
receive_with_timestamps(Ctx const&, Borrow socket,
                        ::foundation::core::ViewMut<std::byte, Brand, Extent> buffer) noexcept {
    using Payload = ::foundation::core::ViewMut<std::byte, Brand>;
    using Received = ::foundation::core::Result<TimestampedReceive<Payload>, ::foundation::os::Errno>;
    ::foundation::core::Result<detail::TimestampedCount, ::foundation::os::Errno> const received =
        detail::receive_with_timestamps_(detail::socket_descriptor_of(socket),
                                         first_of_(::foundation::core::detail::BaseLayerRead{}, buffer), buffer.size(),
                                         detail::no_wait_flag);
    return received.match(
        [run = std::move(buffer)](detail::TimestampedCount const& counted) mutable noexcept -> Received {
            return TimestampedReceive<Payload>{
                .payload = ::foundation::core::take_prefix_up_to(std::move(run), counted.count),
                .control = counted.control,
            };
        },
        [](::foundation::os::Errno error) noexcept -> Received { return ::foundation::core::err(error); });
}

}  // namespace fixy::net

namespace fixy::net::detail::socket_surface_invariants {

// Every tag in fixy::net::socket_kind has a row.  The walk is the check
// that a new tag also has its row.
//
// The walk stays in this header, and the other checks are in its check
// file.  The walk calls the lookup of the table in each translation unit
// that includes this header.  fixy/os/AtomPack.h says why that call must
// come before the code of the includer.
static_assert(::fixy::atom_pack::every_tag_in_satisfies<^^::fixy::net::socket_kind,
                                                        [](std::meta::info kind_tag) consteval {
                                                          return ::fixy::atom_pack::has_row(socket_table, kind_tag);
                                                        }>(),
              "fixy/os/Socket.h: a tag in fixy::net::socket_kind has no row in socket_table.");

}  // namespace fixy::net::detail::socket_surface_invariants

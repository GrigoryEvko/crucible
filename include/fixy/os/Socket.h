#pragma once

// The socket syscall.  A kind tag names one socket, and the tag is read
// twice: once for the domain, type and protocol that socket(2) takes, and
// once for the effect row that the calling context must admit.
//
// mint_socket below makes the call through SocketDoor, the one door to
// socket(2), so a socket handle holds a descriptor that the kernel returned
// to a gated mint.  The door file src/fixy/os/Socket.cpp issues the call with
// the raw primitive of foundation/os/Syscall.h.  The words of the calls come
// from fixy/os/SocketCalls.h (THE KERNEL WORDS), so this header includes no
// system header.
//
// Two kinds are declared: the netlink socket of the qdisc query, and the
// TCP stream socket whose options the test of THE SOCKET CALLS reads back
// from the kernel.  A new kind is a tag in fixy::net::socket_kind and a row
// of socket_table.
// The walk at the foot of this header fails if the tag has no row.

#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/atoms/Syscall.h>
#include <fixy/os/AtomPack.h>
#include <fixy/os/Fd.h>
#include <fixy/os/Handle.h>
#include <fixy/os/SocketCalls.h>
#include <foundation/ByteSeal.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Id.h>
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
// of an interface.
struct NetlinkRoute final {};

// A TCP stream socket of IPv4.
struct TcpStream final {};

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
};

template <typename Kind>
concept MappedSocketKind = ::fixy::atom_pack::has_row(socket_table, ^^Kind);

// The triple of a kind that has a row.  The lookup is a function and not
// a variable template, because a caller can specialize a variable
// template for one kind and give it a triple of its own.
[[nodiscard]] consteval detail::socket_triple socket_triple_of(std::meta::info kind_tag) noexcept {
    return ::fixy::atom_pack::value_for(socket_table, kind_tag);
}

// The row comes from the syscall atom, so the gate and the catalog in
// fixy/atoms/Syscall.h cannot disagree.  The catalog puts socket in the
// network family, which lifts to IO and Block.
using socket_row_t = eff::lift_row_t<::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::socket>>;

template <typename Ctx, typename Kind>
concept CtxFitsSocketMint = eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, socket_row_t> && MappedSocketKind<Kind>;

// The rights of a socket: a read and a write of its bytes, and the control
// calls of a socket (ioctl_socket): its ioctls and its options.
inline constexpr ::fixy::os::RightSet socket_rights =
    ::fixy::os::RightSet::read | ::fixy::os::RightSet::write | ::fixy::os::RightSet::ioctl_socket;

// The handle of a socket of the kind Kind, and its borrow.
template <typename Kind>
using SocketHandle = ::fixy::os::Handle<::fixy::os::kind::Socket<Kind>, socket_rights>;

template <typename Kind, ::fixy::os::RightSet Rights>
using SocketRef = ::fixy::os::HandleRef<::fixy::os::kind::Socket<Kind>, Rights>;

// The identity of a socket: the number of its descriptor.  A table that only
// keys or compares sockets holds the identity, and no door takes it.  The
// kernel gives the number of a closed socket to its next socket, so an
// identity names a socket only while the socket is open.
using SocketIdentity = ::foundation::core::Id<struct SocketIdentityTag, std::uint32_t>;

// THE FOREIGN SOCKET
//   A ForeignSocketRef is the borrow of a socket by value.  A table that
//   stores, compares or keys a socket that it gives back to a door holds
//   one, because a HandleRef holds the address of the descriptor of its
//   Handle and cannot outlive the frame of that Handle.  Two doors make one:
//   share_socket, from the borrow of a Handle of the base, and
//   admit_foreign_socket, from the descriptor of a socket that a C library
//   opened.  Each ref carries one promise of the code that made it: the
//   owner of the socket keeps it open for the life of each copy of the ref.
//   The ref also keeps the generation of its number (THE GENERATION OF A
//   DESCRIPTOR of fixy/os/Fd.h), and a Handle of the base advances that
//   generation at its open, at its close and at its release to an ABI.  So a
//   ref that outlives its Handle is not current, and each door gives ebadf
//   for it and makes no call on the number, which the kernel can have given
//   to a later descriptor.  abi_descriptor() gives -1 for such a ref.  Two
//   holes stay: a close of the Handle while a door of another thread makes
//   its call, and a foreign socket that a C library closes and a C library
//   opens again on its number with no Handle between.  A number at or past
//   descriptor_generation_slots has no generation, so its ref is never
//   current, and each door refuses it.  The default ref is empty, and a door
//   gives ebadf for it.
template <typename Kind, ::fixy::os::RightSet Rights>
    requires MappedSocketKind<Kind>
class ForeignSocketRef;

namespace detail {

// The door of the base to the descriptor of a foreign socket.  No object of
// it exists.  admit makes a ref with the generation of its number, and
// descriptor_of gives the descriptor of a current ref, or -1.
struct ForeignSocketDoor final : ::foundation::NoObject<ForeignSocketDoor> {
    template <typename Kind, ::fixy::os::RightSet Rights>
    [[nodiscard]] static ForeignSocketRef<Kind, Rights> admit(int descriptor) noexcept;

    template <typename Kind, ::fixy::os::RightSet Rights>
    [[nodiscard]] static int descriptor_of(ForeignSocketRef<Kind, Rights> borrow) noexcept;
};

}  // namespace detail

template <typename Kind, ::fixy::os::RightSet Rights>
    requires MappedSocketKind<Kind>
class [[nodiscard]] ForeignSocketRef final {
    // The descriptor of the socket, or -1 for the empty ref.
    int descriptor_ = -1;
    // The generation of the number of the descriptor when a door made the ref.
    std::uint32_t generation_ = 0;
    // No copy of bytes builds a ref, so a number becomes a ref only through
    // a door (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr ForeignSocketRef(int descriptor, std::uint32_t generation) noexcept
        : descriptor_{descriptor}, generation_{generation} {}

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
    // generation of this ref.
    template <::fixy::os::RightSet Fewer>
        requires ::fixy::os::HoldsRights<Rights, Fewer>
    [[nodiscard]] constexpr ForeignSocketRef<Kind, Fewer> restrict() const noexcept {
        return ForeignSocketRef<Kind, Fewer>{descriptor_, generation_};
    }

    // False for the empty ref.  A ref that is open can still be stale: ask
    // is_current() for that.
    [[nodiscard]] constexpr bool is_open() const noexcept { return descriptor_ >= 0; }

    // True when no Handle of the base opened, closed or released the number
    // of this ref since a door made it, so the number still names the socket
    // of the ref (THE FOREIGN SOCKET).  False for the empty ref.  Complexity:
    // one acquire load.
    [[nodiscard]] bool is_current() const noexcept {
        return descriptor_ >= 0 && ::fixy::fs::detail::is_descriptor_generation_(descriptor_, generation_);
    }

    // The identity of the socket of an open ref: the number of its descriptor.
    // A stale ref and the ref of a later socket of the number have one
    // identity, so a table that keys by identity also asks is_current().
    [[nodiscard]] constexpr SocketIdentity identity() const noexcept {
        CRUCIBLE_PRE(is_open());
        return SocketIdentity{static_cast<std::uint32_t>(descriptor_)};
    }

    // The raw descriptor, for a call of a C ABI only (THE ABI DESCRIPTOR of
    // fixy/os/Handle.h).  The descriptor of the empty ref, and of a ref that
    // is not current, is -1, so a C ABI gets no number of a later socket.  A
    // constant evaluation reads no generation and gives the descriptor.
    [[nodiscard]] constexpr int abi_descriptor() const noexcept {
        if consteval {
            return descriptor_;
        }
        return is_current() ? descriptor_ : -1;
    }

    // Two refs are equal when they hold one descriptor of one generation.
    [[nodiscard]] friend constexpr bool operator==(ForeignSocketRef left, ForeignSocketRef right) noexcept {
        return left.descriptor_ == right.descriptor_ && left.generation_ == right.generation_;
    }
};

// The ref of descriptor with the generation of its number now.  The empty ref
// has no number, so it reads no generation.
template <typename Kind, ::fixy::os::RightSet Rights>
ForeignSocketRef<Kind, Rights> detail::ForeignSocketDoor::admit(int descriptor) noexcept {
    if (descriptor < 0) {
        return ForeignSocketRef<Kind, Rights>{};
    }
    return ForeignSocketRef<Kind, Rights>{descriptor, ::fixy::fs::detail::descriptor_generation_(descriptor)};
}

template <typename Kind, ::fixy::os::RightSet Rights>
int detail::ForeignSocketDoor::descriptor_of(ForeignSocketRef<Kind, Rights> borrow) noexcept {
    return borrow.is_current() ? borrow.descriptor_ : -1;
}

// The ref of a socket that a C library opened, with the kind Kind and the
// rights Rights that the caller states.  The caller keeps the socket open for
// the life of each copy of the ref (THE FOREIGN SOCKET).  The ref keeps the
// generation of the number now, so a later open or close of the number by a
// Handle of the base makes it stale.  The base does not see a close of the C
// library, so a reuse of the number by the C library keeps the ref current.
// Each call is inside an opt-out region of the class ABI (THE ABI DESCRIPTOR
// of fixy/os/Handle.h).
template <typename Kind, ::fixy::os::RightSet Rights>
    requires MappedSocketKind<Kind>
[[nodiscard]] ForeignSocketRef<Kind, Rights> admit_foreign_socket(NonNegative<int> descriptor) noexcept {
    return detail::ForeignSocketDoor::admit<Kind, Rights>(descriptor.value());
}

// The ref by value of the socket of a Handle, for a table that stores the
// socket.  The caller keeps the Handle open for the life of each copy of the
// ref (THE FOREIGN SOCKET), and a ref that outlives the Handle is stale.  The
// borrow of an empty Handle gives the empty ref.
template <typename Kind, ::fixy::os::RightSet Rights>
    requires MappedSocketKind<Kind>
[[nodiscard]] ForeignSocketRef<Kind, Rights> share_socket(SocketRef<Kind, Rights> const& socket) noexcept {
    return detail::ForeignSocketDoor::admit<Kind, Rights>(::fixy::os::detail::HandleDoor::descriptor_of(socket));
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

}  // namespace detail

// The Kind parameter precedes Ctx because the caller names it explicitly
// and Ctx deduces from the argument.
//
// Declared here and defined below SocketDoor, because SocketDoor names it
// as its friend, and a friend must already have been declared.
template <typename Kind, eff::IsExecCtx Ctx>
    requires CtxFitsSocketMint<Ctx, Kind>
[[nodiscard]] inline ::foundation::core::Result<Linear<SocketHandle<Kind>>, ::foundation::os::Errno>
mint_socket(Ctx const&) noexcept;

// The door to socket(2).  No object of it exists.  Its member is private,
// and its one friend is mint_socket.  A Handle befriends this class, so a
// socket handle comes only from mint_socket, after its gate.  The door
// reads the triple of the kind, so no caller gives it a domain, a type or
// a protocol.
class SocketDoor final : ::foundation::NoObject<SocketDoor> {
    template <typename FriendKind, eff::IsExecCtx FriendCtx>
        requires CtxFitsSocketMint<FriendCtx, FriendKind>
    friend auto mint_socket(FriendCtx const&) noexcept
        -> ::foundation::core::Result<Linear<SocketHandle<FriendKind>>, ::foundation::os::Errno>;

    // The open door of src/fixy/os/Socket.cpp adds SOCK_CLOEXEC, because a
    // descriptor that survives execve leaks into each child process.  It
    // gives the error of the kernel on failure and no handle.
    template <MappedSocketKind Kind>
    [[nodiscard]] static ::foundation::core::Result<Linear<SocketHandle<Kind>>, ::foundation::os::Errno>
    open_() noexcept {
        constexpr detail::socket_triple triple = socket_triple_of(^^Kind);
        return detail::open_socket_(triple.domain, triple.type, triple.protocol).map([](int descriptor) noexcept {
            return mint_linear<SocketHandle<Kind>>(SocketHandle<Kind>{descriptor});
        });
    }
};

template <typename Kind, eff::IsExecCtx Ctx>
    requires CtxFitsSocketMint<Ctx, Kind>
[[nodiscard]] inline ::foundation::core::Result<Linear<SocketHandle<Kind>>, ::foundation::os::Errno>
mint_socket(Ctx const&) noexcept {
    return SocketDoor::open_<Kind>();
}

// THE SOCKET CALLS
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
            name.append(::foundation::core::detail::text_over_(bytes, count))
                .expect("the name holds at most congestion_name_bytes characters");
            return name;
        });
}

// Reads a TCP option of a socket into the bytes of buffer, and gives the
// count of the bytes that the kernel wrote.  An option that the kernel
// gives as a fixed struct, such as tcp_info (TcpOption::info) or
// tcp_cc_info (TcpOption::congestion_info), reads into a buffer of its
// size, and a kernel with a shorter struct writes fewer bytes.
template <eff::IsExecCtx Ctx, typename Borrow>
    requires CtxFitsSocketCall<Ctx> && TcpBorrowWith<Borrow, ::fixy::os::RightSet::ioctl_socket>
[[nodiscard]] inline ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
read_tcp_option(Ctx const&, Borrow socket, TcpOption option, ::foundation::core::View<std::byte> buffer) noexcept {
    std::size_t const size = buffer.size();
    std::uint32_t const capacity = size < 0xFFFFFFFFU ? static_cast<std::uint32_t>(size) : 0xFFFFFFFFU;
    return detail::read_tcp_option_(detail::socket_descriptor_of(socket), option,
                                    first_of_(::foundation::core::detail::ViewDoor::key(), buffer), capacity);
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
                                    first_of_(::foundation::core::detail::ViewDoor::key(), buffer), buffer.size(),
                                    detail::no_wait_flag);
}

// Receives the bytes that the socket holds, at most the size of buffer, and
// gives their count.  It does not wait: a socket with no bytes gives eagain.
template <typename Kind, ::fixy::os::RightSet Rights, eff::IsExecCtx Ctx>
    requires CtxFitsSocketCallWith<Ctx, Rights, ::fixy::os::RightSet::read>
[[nodiscard]] inline ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
receive_bytes(Ctx const&, SocketRef<Kind, Rights> socket, ::foundation::core::View<std::byte> buffer) noexcept {
    return detail::receive_with_flags_(::fixy::os::detail::HandleDoor::descriptor_of(socket),
                                       first_of_(::foundation::core::detail::ViewDoor::key(), buffer), buffer.size(),
                                       detail::no_wait_flag);
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
// (SCM_TIMESTAMPING).  The count is the count of the bytes that the kernel
// wrote into buffer, at most its size.  It does not wait: a socket with no
// datagram gives eagain.  The control data holds no stamp when the socket
// has no SO_TIMESTAMPING (set_socket_timestamping).
template <eff::IsExecCtx Ctx, typename Borrow>
    requires CtxFitsSocketCall<Ctx> && SocketBorrowWith<Borrow, ::fixy::os::RightSet::read>
[[nodiscard]] inline ::foundation::core::Result<TimestampedReceive, ::foundation::os::Errno>
receive_with_timestamps(Ctx const&, Borrow socket, ::foundation::core::View<std::byte> buffer) noexcept {
    return detail::receive_with_timestamps_(detail::socket_descriptor_of(socket),
                                            first_of_(::foundation::core::detail::ViewDoor::key(), buffer),
                                            buffer.size(), detail::no_wait_flag);
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

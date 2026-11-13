#pragma once

// The socket syscall.  A kind tag names one socket, and the tag is read
// twice: once for the domain, type and protocol that ::socket takes, and
// once for the effect row that the calling context must admit.
//
// mint_socket below makes the call through SocketDoor, the one door to
// ::socket, so a socket handle holds a descriptor that the kernel
// returned to a gated mint.
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
#include <fixy/os/SocketCalls.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>
#include <foundation/os/Errno.h>

#include <linux/netlink.h>
#include <sys/socket.h>

#include <cerrno>
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

// The three arguments of ::socket for one kind.
struct socket_triple final {
    int domain = -1;
    int type = -1;
    int protocol = -1;
};

}  // namespace detail

// The domain, type and protocol of each kind.  A kind reaches ::socket
// only through a row of this table, and fixy/os/AtomPack.h says why a
// table is closed: a class template map takes a specialization for a
// class of the caller, and the gated mint can then open a raw packet
// socket as a known kind.  A kind with no row has no triple, so the mint refuses
// it and ::socket never sees a guessed triple.
inline constexpr ::fixy::atom_pack::tag_row<detail::socket_triple> socket_table[] = {
    {^^socket_kind::NetlinkRoute, {AF_NETLINK, SOCK_RAW, NETLINK_ROUTE}},
    {^^socket_kind::TcpStream, {AF_INET, SOCK_STREAM, 0}},
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

// The Kind parameter precedes Ctx because the caller names it explicitly
// and Ctx deduces from the argument.
//
// Declared here and defined below SocketDoor, because SocketDoor names it
// as its friend, and a friend must already have been declared.
template <typename Kind, eff::IsExecCtx Ctx>
    requires CtxFitsSocketMint<Ctx, Kind>
[[nodiscard]] inline ::foundation::core::Result<Linear<::fixy::fs::OwnedFd>, ::foundation::os::Errno>
mint_socket(Ctx const&) noexcept;

// The door to ::socket.  No object of it exists.  Its member is private,
// and its one friend is mint_socket.  OwnedFd befriends this class, so a
// socket handle comes only from mint_socket, after its gate.  The door
// reads the triple of the kind, so no caller gives it a domain, a type or
// a protocol.
class SocketDoor final : ::foundation::NoObject<SocketDoor> {
    template <typename FriendKind, eff::IsExecCtx FriendCtx>
        requires CtxFitsSocketMint<FriendCtx, FriendKind>
    friend auto mint_socket(FriendCtx const&) noexcept
        -> ::foundation::core::Result<Linear<::fixy::fs::OwnedFd>, ::foundation::os::Errno>;

    // SOCK_CLOEXEC is folded in, because a descriptor that survives execve
    // leaks into every child process.  Returns the error of the kernel on
    // failure and no handle.
    template <MappedSocketKind Kind>
    [[nodiscard]] static ::foundation::core::Result<::fixy::fs::OwnedFd, ::foundation::os::Errno> open_() noexcept {
        constexpr detail::socket_triple triple = socket_triple_of(^^Kind);
        const int fd = ::socket(triple.domain, triple.type | SOCK_CLOEXEC, triple.protocol);
        if (fd < 0) {
            return ::foundation::core::err(static_cast<::foundation::os::Errno>(errno));
        }
        return ::fixy::fs::OwnedFd{fd};
    }
};

template <typename Kind, eff::IsExecCtx Ctx>
    requires CtxFitsSocketMint<Ctx, Kind>
[[nodiscard]] inline ::foundation::core::Result<Linear<::fixy::fs::OwnedFd>, ::foundation::os::Errno>
mint_socket(Ctx const&) noexcept {
    using Minted = ::foundation::core::Result<Linear<::fixy::fs::OwnedFd>, ::foundation::os::Errno>;
    return SocketDoor::open_<Kind>().match(
        [](::fixy::fs::OwnedFd descriptor) -> Minted {
            return mint_linear<::fixy::fs::OwnedFd>(std::move(descriptor));
        },
        [](::foundation::os::Errno error) -> Minted { return ::foundation::core::err(error); });
}

// THE SOCKET CALLS
//   Each call takes the descriptor of a socket: the NonNegative<int> of a
//   TCP socket that the caller holds, or the OwnedFd of a netlink socket of
//   mint_socket.  The door file src/fixy/os/Socket.cpp issues each call
//   with the raw primitive (fixy/os/SocketCalls.h), and gives the error of
//   the kernel in a Result.  A call that a signal interrupts runs again.
//   send_bytes and receive_bytes pass MSG_DONTWAIT and never wait: a
//   netlink request of the kernel queues its reply before the send returns,
//   and the receive of a dump makes its next part.  A receive with no bytes
//   gives eagain.  Each call takes a context that admits the row of the
//   network family of the syscall catalog.

using socket_call_row_t =
    eff::lift_row_t<::fixy::atom::syscall::family<::fixy::atom::syscall::SyscallFamily::NetworkIo>>;

template <typename Ctx>
concept CtxFitsSocketCall = eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, socket_call_row_t>;

// The longest name of a congestion control of the kernel, with no zero
// (TCP_CA_NAME_MAX of the kernel, less one).
inline constexpr std::size_t congestion_name_bytes = 15;

namespace detail {

// The address of a netlink socket that asks the kernel for a unique port:
// the bytes of struct sockaddr_nl with no port and no group.
struct NetlinkAddress final {
    std::uint16_t family = AF_NETLINK;
    std::uint16_t pad = 0;
    std::uint32_t port = 0;
    std::uint32_t groups = 0;
};

}  // namespace detail

// Sets the congestion control of a TCP socket to name (TCP_CONGESTION).  A
// name of more than congestion_name_bytes characters gives enametoolong.
template <eff::IsExecCtx Ctx>
    requires CtxFitsSocketCall<Ctx>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_tcp_congestion(Ctx const&, NonNegative<int> socket, ::foundation::core::TextView name) noexcept {
    if (name.size() > congestion_name_bytes) return ::foundation::core::err(::foundation::os::Errno::enametoolong);
    ::foundation::core::FixedText<congestion_name_bytes + 1> bytes{};
    bytes.append(name).expect("the name fits below the capacity");
    bytes.push('\0').expect("the zero fits after the name");
    return detail::set_tcp_option_(socket.value(), TcpOption::congestion,
                                   ::foundation::core::detail::text_first_(bytes.view()),
                                   static_cast<std::uint32_t>(bytes.size()));
}

// The name of the congestion control of a TCP socket (TCP_CONGESTION).
template <eff::IsExecCtx Ctx>
    requires CtxFitsSocketCall<Ctx>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::FixedText<congestion_name_bytes>,
                                                ::foundation::os::Errno>
read_tcp_congestion(Ctx const&, NonNegative<int> socket) noexcept {
    using Read =
        ::foundation::core::Result<::foundation::core::FixedText<congestion_name_bytes>, ::foundation::os::Errno>;
    char bytes[congestion_name_bytes + 1]{};
    return detail::read_tcp_option_(socket.value(), TcpOption::congestion, bytes, sizeof(bytes))
        .match(
            [&bytes](std::size_t length) -> Read {
                std::size_t count = 0;
                while (count < length && count < congestion_name_bytes && bytes[count] != '\0') {
                    ++count;
                }
                ::foundation::core::FixedText<congestion_name_bytes> name{};
                name.append(::foundation::core::detail::text_over_(bytes, count))
                    .expect("the name holds at most congestion_name_bytes characters");
                return name;
            },
            [](::foundation::os::Errno error) -> Read { return ::foundation::core::err(error); });
}

// Reads a TCP option of a socket into the bytes of buffer, and gives the
// count of the bytes that the kernel wrote.  An option that the kernel
// gives as a fixed struct, such as tcp_info (TcpOption::info) or
// tcp_cc_info (TcpOption::congestion_info), reads into a buffer of its
// size, and a kernel with a shorter struct writes fewer bytes.
template <eff::IsExecCtx Ctx>
    requires CtxFitsSocketCall<Ctx>
[[nodiscard]] inline ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
read_tcp_option(Ctx const&, NonNegative<int> socket, TcpOption option,
                ::foundation::core::View<std::byte> buffer) noexcept {
    std::size_t const size = buffer.size();
    std::uint32_t const capacity = size < 0xFFFFFFFFU ? static_cast<std::uint32_t>(size) : 0xFFFFFFFFU;
    return detail::read_tcp_option_(socket.value(), option,
                                    first_of_(::foundation::core::detail::ViewDoor::key(), buffer), capacity);
}

// Sets the least retransmission timeout of a TCP socket in microseconds
// (TCP_RTO_MIN_US).  A kernel with no such option gives enoprotoopt.
template <eff::IsExecCtx Ctx>
    requires CtxFitsSocketCall<Ctx>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_tcp_rto_min(Ctx const&, NonNegative<int> socket, std::uint32_t microseconds) noexcept {
    return detail::set_tcp_option_(socket.value(), TcpOption::rto_min_us, &microseconds, sizeof(microseconds));
}

// Sets the largest pacing rate of a socket in bytes per second
// (SO_MAX_PACING_RATE).
template <eff::IsExecCtx Ctx>
    requires CtxFitsSocketCall<Ctx>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
set_max_pacing_rate(Ctx const&, NonNegative<int> socket, std::uint64_t bytes_per_second) noexcept {
    return detail::set_option_(socket.value(), SOL_SOCKET, SO_MAX_PACING_RATE, &bytes_per_second,
                               sizeof(bytes_per_second));
}

// Binds a netlink socket to a port that the kernel gives, with no group.
// Two threads of one process then get the replies of their own requests.
template <eff::IsExecCtx Ctx>
    requires CtxFitsSocketCall<Ctx>
[[nodiscard]] inline ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
bind_netlink(Ctx const&, ::fixy::fs::OwnedFd const& socket) noexcept {
    static_assert(sizeof(detail::NetlinkAddress) == sizeof(::sockaddr_nl),
                  "NetlinkAddress has the bytes of struct sockaddr_nl");
    detail::NetlinkAddress const address{};
    return detail::bind_address_(socket.get(), &address, sizeof(address));
}

// Sends the bytes of buffer, and gives the count that the kernel took.
template <eff::IsExecCtx Ctx>
    requires CtxFitsSocketCall<Ctx>
[[nodiscard]] inline ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
send_bytes(Ctx const&, ::fixy::fs::OwnedFd const& socket, ::foundation::core::View<std::byte const> buffer) noexcept {
    return detail::send_with_flags_(socket.get(), first_of_(::foundation::core::detail::ViewDoor::key(), buffer),
                                    buffer.size(), MSG_DONTWAIT);
}

// Receives the bytes that the socket holds, at most the size of buffer, and
// gives their count.  It does not wait: a socket with no bytes gives eagain.
template <eff::IsExecCtx Ctx>
    requires CtxFitsSocketCall<Ctx>
[[nodiscard]] inline ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
receive_bytes(Ctx const&, ::fixy::fs::OwnedFd const& socket, ::foundation::core::View<std::byte> buffer) noexcept {
    return detail::receive_with_flags_(socket.get(), first_of_(::foundation::core::detail::ViewDoor::key(), buffer),
                                       buffer.size(), MSG_DONTWAIT);
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

// The socket mint and the socket calls, run against the kernel.
//
// The header pins the triple, the row and the gate at compile time.  This
// file checks what only a running kernel can answer: that the descriptor
// the mint returns is a socket of the kind the tag names, that it does not
// survive execve, that a netlink request gets its reply through the door,
// that each TCP option of the door reaches the TCP option of the kernel,
// and that the timestamp calls reach the kernel.  The check file of the
// header holds the walk of the control data of each kind of message.

#include <fixy/Core.h>
#include <fixy/os/Socket.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>

#include <fcntl.h>
#include <linux/net_tstamp.h>
#include <linux/netlink.h>
#include <sys/socket.h>

#include <cstdio>
#include <utility>
#include "../test_assert.h"

namespace eff = foundation::effects;

namespace {

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;

[[nodiscard]] bool socket_option_is(int fd, int option, int expected) noexcept {
    int value = -1;
    ::socklen_t length = sizeof(value);
    if (::getsockopt(fd, SOL_SOCKET, option, &value, &length) != 0) return false;
    return value == expected;
}

// The descriptor of a mint, or an empty handle when the open failed.
[[nodiscard]] fixy::fs::OwnedFd
descriptor_of(fixy::Result<fixy::Linear<fixy::fs::OwnedFd>, foundation::os::Errno> minted) noexcept {
    return std::move(minted).match(
        [](fixy::Linear<fixy::fs::OwnedFd> opened) -> fixy::fs::OwnedFd { return std::move(opened).consume(); },
        [](foundation::os::Errno) -> fixy::fs::OwnedFd { return fixy::fs::OwnedFd{}; });
}

[[nodiscard]] int check_netlink_route_socket() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    fixy::fs::OwnedFd const handle = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::NetlinkRoute>(ctx));
    if (!handle.is_open()) return 2;
    if (!socket_option_is(handle.get(), SO_DOMAIN, AF_NETLINK)) return 3;
    if (!socket_option_is(handle.get(), SO_TYPE, SOCK_RAW)) return 4;
    if (!socket_option_is(handle.get(), SO_PROTOCOL, NETLINK_ROUTE)) return 5;
    const int descriptor_flags = ::fcntl(handle.get(), F_GETFD);
    if (descriptor_flags < 0 || (descriptor_flags & FD_CLOEXEC) == 0) return 6;
    return 0;
}

// A dump request of the links: a header of 16 bytes and the family byte of
// struct rtgenmsg, with the pad of the header alignment.
struct LinkDumpRequest final {
    std::uint32_t length = 20;
    std::uint16_t type = 18;  // RTM_GETLINK
    std::uint16_t flags = 0x0301;  // NLM_F_REQUEST | NLM_F_DUMP
    std::uint32_t sequence = 1;
    std::uint32_t port = 0;
    std::uint8_t family = 0;  // AF_UNSPEC
    std::uint8_t pad_first = 0;
    std::uint8_t pad_second = 0;
    std::uint8_t pad_third = 0;
};

// The netlink door binds, sends a dump request and receives its reply.  The
// kernel queues the first part of the dump before the send returns, so the
// receive, which does not wait, gets at least one message header.
[[nodiscard]] int check_netlink_calls() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    fixy::fs::OwnedFd const socket = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::NetlinkRoute>(ctx));
    if (!socket.is_open()) return 11;
    if (fixy::net::bind_netlink(ctx, socket).is_err()) return 12;
    LinkDumpRequest const request{};
    if (fixy::net::send_bytes(ctx, socket, foundation::core::bytes(request)).value_or(0) != sizeof(request)) return 13;
    foundation::core::FixedArray<std::byte, 8192> reply{};
    if (fixy::net::receive_bytes(ctx, socket, reply.view()).value_or(0) < 16) return 14;
    return 0;
}

// The congestion control of a TCP socket reads back as the door set it, so
// the level and the name of the option agree with the kernel.  A name past
// the limit is refused before the kernel sees it.
[[nodiscard]] int check_tcp_options() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    fixy::fs::OwnedFd const tcp = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    if (!tcp.is_open()) return 21;
    fixy::NonNegative<int> const socket = fixy::mint_refined<fixy::non_negative>(tcp.get());
    if (fixy::net::set_tcp_congestion(ctx, socket, "reno").is_err()) return 22;
    auto read = fixy::net::read_tcp_congestion(ctx, socket);
    if (read.is_err()) return 23;
    auto const name = std::move(read).expect("the read of the congestion control gave its name");
    if (!(name.view() == fixy::TextView{"reno"})) return 24;
    if (fixy::net::set_tcp_congestion(ctx, socket, "a-name-of-sixteen").err().value_or(foundation::os::Errno{})
        != foundation::os::Errno::enametoolong) {
        return 25;
    }
    auto const rto = fixy::net::set_tcp_rto_min(ctx, socket, 10000);
    if (rto.is_err() && rto.err().value_or(foundation::os::Errno{}) != foundation::os::Errno::enoprotoopt) return 26;
    if (fixy::net::set_max_pacing_rate(ctx, socket, 1000000).is_err()) return 27;
    // The kernel struct tcp_info starts with eight one-byte fields, and the
    // kernel writes at least them into a buffer that holds them.
    foundation::core::FixedArray<std::byte, 256> info{};
    if (fixy::net::read_tcp_option(ctx, socket, fixy::net::TcpOption::info, info.view()).value_or(0) < 8) return 28;
    return 0;
}

// The door sets SO_TIMESTAMPING, and the kernel reads back the flags of
// <linux/net_tstamp.h>.
[[nodiscard]] int check_socket_timestamping() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    fixy::fs::OwnedFd const tcp = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    if (!tcp.is_open()) return 31;
    fixy::NonNegative<int> const socket = fixy::mint_refined<fixy::non_negative>(tcp.get());
    if (fixy::net::set_socket_timestamping(ctx, socket).is_err()) return 32;
    int const flags = SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE
                    | SOF_TIMESTAMPING_RAW_HARDWARE | SOF_TIMESTAMPING_OPT_CMSG | SOF_TIMESTAMPING_OPT_TSONLY;
    if (!socket_option_is(tcp.get(), SO_TIMESTAMPING, flags)) return 33;
    return 0;
}

// The loopback interface has no hardware clock.  The kernel refuses
// SIOCSHWTSTAMP on it with eperm for a caller with no CAP_NET_ADMIN, and
// with eopnotsupp for a caller with it.  A name past the limit is refused
// before the kernel sees it, and a name at the limit is not.
[[nodiscard]] int check_hardware_timestamping() noexcept {
    using foundation::os::Errno;
    IoBlockCtx ctx{eff::testing::test()};
    fixy::fs::OwnedFd const tcp = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    if (!tcp.is_open()) return 41;
    fixy::NonNegative<int> const socket = fixy::mint_refined<fixy::non_negative>(tcp.get());
    auto const loopback = fixy::net::set_hardware_timestamping(ctx, socket, "lo");
    if (loopback.is_ok()) return 42;
    Errno const refusal = loopback.err().value_or(Errno::eio);
    if (refusal != Errno::eperm && refusal != Errno::eopnotsupp) return 43;
    if (fixy::net::set_hardware_timestamping(ctx, socket, "sixteen-chars-xx").err().value_or(Errno::eio)
        != Errno::enametoolong) {
        return 44;
    }
    if (fixy::net::set_hardware_timestamping(ctx, socket, "fifteen-chars-x").err().value_or(Errno::enametoolong)
        == Errno::enametoolong) {
        return 45;
    }
    return 0;
}

// A netlink socket with no message gives eagain, and the kernel puts no
// stamp on a netlink message, so its control data holds no stamp message.
[[nodiscard]] int check_receive_with_timestamps() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    fixy::fs::OwnedFd const netlink = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::NetlinkRoute>(ctx));
    if (!netlink.is_open()) return 51;
    if (fixy::net::bind_netlink(ctx, netlink).is_err()) return 52;
    fixy::NonNegative<int> const socket = fixy::mint_refined<fixy::non_negative>(netlink.get());
    foundation::core::FixedArray<std::byte, 8192> reply{};
    if (fixy::net::receive_with_timestamps(ctx, socket, reply.view()).err().value_or(foundation::os::Errno::eio)
        != foundation::os::Errno::eagain) {
        return 53;
    }
    LinkDumpRequest const request{};
    if (fixy::net::send_bytes(ctx, netlink, foundation::core::bytes(request)).value_or(0) != sizeof(request)) return 54;
    auto received = fixy::net::receive_with_timestamps(ctx, socket, reply.view());
    if (received.is_err()) return 55;
    fixy::net::TimestampedReceive const message = std::move(received).expect("the receive gave a message");
    if (message.count < 16 || message.count > reply.size()) return 56;
    if (!message.control.is<fixy::net::NoTimestampMessage>()) return 57;
    return 0;
}

}  // namespace

int main() {
    int failed = check_netlink_route_socket();
    if (failed == 0) failed = check_netlink_calls();
    if (failed == 0) failed = check_tcp_options();
    if (failed == 0) failed = check_socket_timestamping();
    if (failed == 0) failed = check_hardware_timestamping();
    if (failed == 0) failed = check_receive_with_timestamps();
    if (failed != 0) {
        std::fprintf(stderr, "test_os_socket: check %d failed\n", failed);
        return failed;
    }
    crucible::test::pass("test_os_socket: passed\n");
    return 0;
}

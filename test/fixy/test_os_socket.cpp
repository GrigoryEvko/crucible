// The socket mint and the socket calls, run against the kernel.
//
// The header pins the triple, the row and the gate at compile time.  This
// file checks what only a running kernel can answer: that the descriptor
// the mint returns is a socket of the kind the tag names, that it does not
// survive execve, that a netlink request gets its reply through the door,
// that each TCP option of the door reaches the TCP option of the kernel,
// that the timestamp calls reach the kernel, that a shared ref of a socket
// reaches the same socket, that a duplicate of a socket is a second handle of
// it, and that a send and a full write of the file door to a socket with no
// connection give an error and no signal.  The check file of the header holds
// the walk of the control data of each kind of message.

#include <fixy/Core.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Socket.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>

#include <fcntl.h>
#include <linux/net_tstamp.h>
#include <linux/netlink.h>
#include <sys/socket.h>

#include <cstdio>
#include <utility>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a send or a write that raises SIGPIPE reaches a handler that marks its arrival")
#include <pthread.h>
#include <signal.h>

namespace {

// One when a SIGPIPE reached mark_pipe_signal.
volatile sig_atomic_t pipe_signal_arrived = 0;

// The action of SIGPIPE that install_pipe_marker replaced, and the signal
// mask of the thread before it.
struct ::sigaction replaced_pipe_action{};
::sigset_t replaced_mask{};

void mark_pipe_signal(int) noexcept { pipe_signal_arrived = 1; }

// Puts mark_pipe_signal on SIGPIPE, and unblocks SIGPIPE in the thread.  The
// process can get SIG_IGN for SIGPIPE and a mask that blocks it from its
// parent, and each of the two hides the signal.
[[nodiscard]] bool install_pipe_marker() noexcept {
    pipe_signal_arrived = 0;
    struct ::sigaction marker{};
    marker.sa_handler = &mark_pipe_signal;
    static_cast<void>(::sigemptyset(&marker.sa_mask));
    ::sigset_t pipe{};
    static_cast<void>(::sigemptyset(&pipe));
    static_cast<void>(::sigaddset(&pipe, SIGPIPE));
    return ::sigaction(SIGPIPE, &marker, &replaced_pipe_action) == 0
        && ::pthread_sigmask(SIG_UNBLOCK, &pipe, &replaced_mask) == 0;
}

// Puts back the action and the mask that install_pipe_marker replaced.
[[nodiscard]] bool restore_pipe_action() noexcept {
    return ::pthread_sigmask(SIG_SETMASK, &replaced_mask, nullptr) == 0
        && ::sigaction(SIGPIPE, &replaced_pipe_action, nullptr) == 0;
}

[[nodiscard]] bool has_pipe_signal_arrived() noexcept { return pipe_signal_arrived != 0; }

// True when the mask of the thread blocks SIGPIPE.
[[nodiscard]] bool is_pipe_signal_blocked() noexcept {
    ::sigset_t current{};
    return ::pthread_sigmask(SIG_SETMASK, nullptr, &current) == 0 && ::sigismember(&current, SIGPIPE) == 1;
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace eff = foundation::effects;

namespace {

// A socket call can park the caller and reads the live state of a peer, so
// its row holds IO, Block and Nondet.
using IoBlockCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block, eff::Effect::Nondet>>;

// True when the test runs under the emulator of a cross build, qemu user mode.
constexpr bool kIsUnderEmulator = CRUCIBLE_TEST_UNDER_EMULATOR != 0;

using NetlinkSocket = fixy::net::SocketHandle<fixy::net::socket_kind::NetlinkRoute>;
using TcpSocket = fixy::net::SocketHandle<fixy::net::socket_kind::TcpStream>;
using TcpRef = fixy::net::SocketRef<fixy::net::socket_kind::TcpStream, fixy::net::socket_rights>;
using SharedTcp = fixy::net::ForeignSocketRef<fixy::net::socket_kind::TcpStream, fixy::net::socket_rights>;

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the test reads the state that the doors gave a socket from the kernel")
namespace {

// True when the option of the level SOL_SOCKET of the socket of borrow holds
// expected.
template <class Kind, fixy::os::RightSet Rights>
[[nodiscard]] bool socket_option_is(fixy::net::SocketRef<Kind, Rights> borrow, int option, int expected) noexcept {
    int value = -1;
    ::socklen_t length = sizeof(value);
    if (::getsockopt(borrow.abi_descriptor(), SOL_SOCKET, option, &value, &length) != 0) return false;
    return value == expected;
}

// True when the descriptor of borrow has the flag FD_CLOEXEC.
template <class Kind, fixy::os::RightSet Rights>
[[nodiscard]] bool is_close_on_exec(fixy::net::SocketRef<Kind, Rights> borrow) noexcept {
    const int descriptor_flags = ::fcntl(borrow.abi_descriptor(), F_GETFD);
    return descriptor_flags >= 0 && (descriptor_flags & FD_CLOEXEC) != 0;
}

// The raw descriptor that a shared ref gives to a C ABI.
[[nodiscard]] int abi_descriptor_of(SharedTcp shared) noexcept { return shared.abi_descriptor(); }

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

// The socket of a mint, or an empty handle when the open failed.
template <class Socket>
[[nodiscard]] Socket descriptor_of(fixy::Result<fixy::Linear<Socket>, foundation::os::Errno> minted) noexcept {
    return std::move(minted).match([](fixy::Linear<Socket> opened) -> Socket { return std::move(opened).consume(); },
                                   [](foundation::os::Errno) -> Socket { return Socket{}; });
}

[[nodiscard]] int check_netlink_route_socket() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    NetlinkSocket const handle = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::NetlinkRoute>(ctx));
    if (!handle.is_open()) return 2;
    if (!socket_option_is(handle.ref(), SO_DOMAIN, AF_NETLINK)) return 3;
    if (!socket_option_is(handle.ref(), SO_TYPE, SOCK_RAW)) return 4;
    if (!socket_option_is(handle.ref(), SO_PROTOCOL, NETLINK_ROUTE)) return 5;
    if (!is_close_on_exec(handle.ref())) return 6;
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
    NetlinkSocket const handle = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::NetlinkRoute>(ctx));
    if (!handle.is_open()) return 11;
    auto const socket = handle.ref();
    if (fixy::net::bind_netlink(ctx, socket).is_err()) return 12;
    LinkDumpRequest const request{};
    if (fixy::net::send_bytes(ctx, socket, foundation::core::bytes(request)).value_or(0) != sizeof(request)) return 13;
    // The part that the receive filled names the brand of the frame, so the
    // check reads it in the frame.
    auto const [status, reply] = fixy::build_fixed_array<std::byte, 8192>([&ctx, &socket](auto run) noexcept -> int {
        auto const received = fixy::net::receive_bytes(ctx, socket, std::move(run));
        return received.match([](auto const& part) noexcept { return part.size() < 16 ? 14 : 0; },
                              [](foundation::os::Errno) noexcept { return 14; });
    });
    return status;
}

// The congestion control of a TCP socket reads back as the door set it, so
// the level and the name of the option agree with the kernel.  A name past
// the limit is refused before the kernel sees it.
[[nodiscard]] int check_tcp_options() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    TcpSocket const tcp = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    if (!tcp.is_open()) return 21;
    TcpRef const socket = tcp.ref();
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
    // qemu user mode has no SO_MAX_PACING_RATE, and the door gives its
    // refusal.  A real kernel takes the option.
    auto const pacing = fixy::net::set_max_pacing_rate(ctx, socket, 1000000);
    bool const is_emulator_refusal =
        kIsUnderEmulator && pacing.err().value_or(foundation::os::Errno{}) == foundation::os::Errno::enoprotoopt;
    if (pacing.is_err() && !is_emulator_refusal) return 27;
    // The kernel struct tcp_info starts with eight one-byte fields, and the
    // kernel writes at least them into a buffer that holds them.  qemu user
    // mode answers TCP_INFO with the four bytes of one int.
    // The part that the read filled names the brand of the frame, so the
    // check reads it in the frame.
    auto const [status, info] = fixy::build_fixed_array<std::byte, 256>([&ctx, &socket](auto run) noexcept -> int {
        auto const info_read = fixy::net::read_tcp_option(ctx, socket, fixy::net::TcpOption::info, std::move(run));
        return info_read.match(
            [](auto const& written) noexcept { return written.size() < 8 && !kIsUnderEmulator ? 28 : 0; },
            [](foundation::os::Errno) noexcept { return 28; });
    });
    return status;
}

// The door sets SO_TIMESTAMPING, and the kernel reads back the flags of
// <linux/net_tstamp.h>.
[[nodiscard]] int check_socket_timestamping() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    TcpSocket const tcp = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    if (!tcp.is_open()) return 31;
    // qemu user mode has no SO_TIMESTAMPING, and the door gives its refusal.
    auto const stamping = fixy::net::set_socket_timestamping(ctx, tcp.ref());
    if (kIsUnderEmulator && stamping.err().value_or(foundation::os::Errno{}) == foundation::os::Errno::enoprotoopt) {
        return 0;
    }
    if (stamping.is_err()) return 32;
    int const flags = SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE
                    | SOF_TIMESTAMPING_RAW_HARDWARE | SOF_TIMESTAMPING_OPT_CMSG | SOF_TIMESTAMPING_OPT_TSONLY;
    if (!socket_option_is(tcp.ref(), SO_TIMESTAMPING, flags)) return 33;
    return 0;
}

// share_socket gives a ref by value of the socket of a Handle.  The doors take
// it as they take the borrow of the Handle, so an option that the shared ref
// sets reads back through the borrow.  Two shares of one socket are equal and
// have one identity.  The empty ref and the share of an empty Handle are
// equal, and a door gives ebadf for them.
[[nodiscard]] int check_shared_socket() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    TcpSocket const tcp = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    TcpSocket const other = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    if (!tcp.is_open() || !other.is_open()) return 61;
    SharedTcp const shared = fixy::net::share_socket(tcp.ref());
    if (!shared.is_open()) return 62;
    if (fixy::net::set_tcp_congestion(ctx, shared, "reno").is_err()) return 63;
    auto read = fixy::net::read_tcp_congestion(ctx, tcp.ref());
    if (read.is_err()) return 64;
    auto const name = std::move(read).expect("the read gave the name");
    if (!(name.view() == fixy::TextView{"reno"})) return 65;
    if (!(fixy::net::share_socket(tcp.ref()) == shared)) return 66;
    if (fixy::net::share_socket(other.ref()) == shared) return 67;
    if (!(fixy::net::share_socket(tcp.ref()).identity() == shared.identity())) return 68;
    if (fixy::net::share_socket(other.ref()).identity() == shared.identity()) return 69;
    SharedTcp const empty{};
    TcpSocket const closed{};
    if (empty.is_open() || !(fixy::net::share_socket(closed.ref()) == empty)) return 70;
    if (fixy::net::set_tcp_congestion(ctx, empty, "reno").err().value_or(foundation::os::Errno::eio)
        != foundation::os::Errno::ebadf) {
        return 71;
    }
    if (!fixy::net::share_socket(tcp.ref()).restrict<fixy::os::RightSet::ioctl_socket>().is_open()) return 72;
    return 0;
}

// The name of the congestion control of a socket, or the empty name when the
// read fails.
[[nodiscard]] fixy::FixedText<fixy::net::congestion_name_bytes> congestion_of(IoBlockCtx const& ctx,
                                                                              TcpRef socket) noexcept {
    return fixy::net::read_tcp_congestion(ctx, socket).value_or(fixy::FixedText<fixy::net::congestion_name_bytes>{});
}

// A shared ref that outlives its Handle fails closed.  The close of the Handle
// advances the generation of its number, so the ref is not current after it.  The
// kernel gives the lowest free number to the next socket, so a second socket
// takes the number of the first one.  A door of the stale ref then gives
// ebadf and makes no call on the second socket, which keeps its congestion
// control, and the raw descriptor of the stale ref is -1.
[[nodiscard]] int check_stale_shared_socket() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    SharedTcp stale{};
    {
        TcpSocket const first = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
        if (!first.is_open()) return 81;
        stale = fixy::net::share_socket(first.ref());
        if (fixy::net::set_tcp_congestion(ctx, stale, "reno").is_err()) return 82;
        if (!stale.is_current() || abi_descriptor_of(stale) < 0) return 88;
    }
    if (stale.is_current() || !stale.is_open() || abi_descriptor_of(stale) != -1) return 89;
    TcpSocket const second = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    if (!second.is_open()) return 83;
    SharedTcp const fresh = fixy::net::share_socket(second.ref());
    if (!(fresh.identity() == stale.identity())) return 84;
    if (fresh == stale || !fresh.is_current() || stale.is_current()) return 90;
    if (stale.restrict<fixy::os::RightSet::ioctl_socket>().is_current()) return 91;
    auto const before = congestion_of(ctx, second.ref());
    if (before.size() == 0) return 85;
    if (fixy::net::set_tcp_congestion(ctx, stale, "reno").err().value_or(foundation::os::Errno::eio)
        != foundation::os::Errno::ebadf) {
        return 86;
    }
    auto const after = congestion_of(ctx, second.ref());
    if (!(after.view() == before.view())) return 87;
    return 0;
}

// A duplicate of a socket is a second handle of the one socket, with a number
// of its own.  Its number closes when a thread starts a program.  An option
// that one handle sets reads back through the other.  After the first handle
// closes, its number goes to a later socket, the ref of the first handle gives
// ebadf, and the duplicate still names the first socket.
[[nodiscard]] int check_duplicate_socket() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    TcpSocket first = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    if (!first.is_open()) return 111;
    TcpSocket const second = fixy::os::mint_duplicate<fixy::net::socket_rights>(ctx, first.ref()).value_or(TcpSocket{});
    if (!second.is_open()) return 112;
    SharedTcp const first_shared = fixy::net::share_socket(first.ref());
    SharedTcp const second_shared = fixy::net::share_socket(second.ref());
    if (first_shared.identity() == second_shared.identity()) return 113;
    if (!is_close_on_exec(second.ref())) return 114;
    if (fixy::net::set_tcp_congestion(ctx, first.ref(), "reno").is_err()) return 115;
    auto const name = congestion_of(ctx, second.ref());
    if (!(name.view() == fixy::TextView{"reno"})) return 116;
    first = TcpSocket{};
    TcpSocket const later = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    if (!later.is_open()) return 117;
    if (!(fixy::net::share_socket(later.ref()).identity() == first_shared.identity())) return 118;
    if (first_shared.is_current() || !second_shared.is_current()) return 119;
    if (fixy::net::read_tcp_congestion(ctx, first_shared).err().value_or(foundation::os::Errno::eio)
        != foundation::os::Errno::ebadf) {
        return 120;
    }
    auto const kept = congestion_of(ctx, second.ref());
    if (!(kept.view() == fixy::TextView{"reno"})) return 121;
    // A duplicate takes the rights that the caller names, and no right more.
    return fixy::os::mint_duplicate<fixy::os::RightSet::ioctl_socket>(ctx, second.ref()).is_ok() ? 0 : 122;
}

// The loopback interface has no hardware clock.  The kernel refuses
// SIOCSHWTSTAMP on it with eperm for a caller with no CAP_NET_ADMIN, and
// with eopnotsupp for a caller with it.  A name past the limit is refused
// before the kernel sees it, and a name at the limit is not.
[[nodiscard]] int check_hardware_timestamping() noexcept {
    using foundation::os::Errno;
    IoBlockCtx ctx{eff::testing::test()};
    TcpSocket const tcp = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    if (!tcp.is_open()) return 41;
    TcpRef const socket = tcp.ref();
    auto const loopback = fixy::net::set_hardware_timestamping(ctx, socket, "lo");
    if (loopback.is_ok()) return 42;
    Errno const refusal = loopback.err().value_or(Errno::eio);
    // qemu user mode has no SIOCSHWTSTAMP and refuses it with enotty.
    bool const is_emulator_refusal = kIsUnderEmulator && refusal == Errno::enotty;
    if (refusal != Errno::eperm && refusal != Errno::eopnotsupp && !is_emulator_refusal) return 43;
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
    NetlinkSocket const handle = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::NetlinkRoute>(ctx));
    if (!handle.is_open()) return 51;
    auto const netlink = handle.ref();
    if (fixy::net::bind_netlink(ctx, netlink).is_err()) return 52;
    auto [reply, reply_key] = fixy::mint_keyed_owner(fixy::fixed_array_owner<std::byte, 8192>);
    foundation::os::Errno const first_error = reply.with_mut(reply_key, [&ctx, &netlink](auto run) noexcept {
        return fixy::net::receive_with_timestamps(ctx, netlink, std::move(run))
            .err()
            .value_or(foundation::os::Errno::eio);
    });
    if (first_error != foundation::os::Errno::eagain) {
        return 53;
    }
    LinkDumpRequest const request{};
    if (fixy::net::send_bytes(ctx, netlink, foundation::core::bytes(request)).value_or(0) != sizeof(request)) return 54;
    // The payload names the brand of the frame, so the check reads the
    // message in the frame.
    return reply.with_mut(reply_key, [&ctx, &netlink](auto run) noexcept -> int {
        auto const received = fixy::net::receive_with_timestamps(ctx, netlink, std::move(run));
        return received.match(
            [](auto const& message) noexcept -> int {
                if (message.payload.size() < 16) return 56;
                if (!message.control.template is<fixy::net::NoTimestampMessage>()) return 57;
                return 0;
            },
            [](foundation::os::Errno) noexcept -> int { return 55; });
    });
}

// The View form of the receive with stamps gives the part of a plain View that
// the kernel wrote: the reply of a dump request, which holds at least one
// message header and fits the View.  The View is the run of an arena, which
// no frame lends.
[[nodiscard]] int check_receive_with_timestamps_into_view() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    NetlinkSocket const handle = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::NetlinkRoute>(ctx));
    if (!handle.is_open()) return 101;
    auto const netlink = handle.ref();
    if (fixy::net::bind_netlink(ctx, netlink).is_err()) return 102;
    LinkDumpRequest const request{};
    if (fixy::net::send_bytes(ctx, netlink, foundation::core::bytes(request)).value_or(0) != sizeof(request)) {
        return 103;
    }
    constexpr std::size_t kReplyBytes = 8192;
    return ::fixy::with_bump_arena(
        eff::testing::test().alloc, ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{2 * kReplyBytes},
        [&ctx, &netlink](auto& arena) noexcept -> int {
            auto reply_run = arena.array(eff::testing::test().alloc, kReplyBytes, std::byte{0});
            fixy::View<std::byte> const reply = reply_run.view();
            auto const received = fixy::net::receive_with_timestamps(ctx, netlink, reply);
            return received.match(
                [](fixy::net::TimestampedReceive<fixy::View<std::byte>> const& message) noexcept -> int {
                    if (message.payload.size() < 16 || message.payload.size() > kReplyBytes) return 105;
                    if (!message.control.is<fixy::net::NoTimestampMessage>()) return 106;
                    return 0;
                },
                [](foundation::os::Errno) noexcept -> int { return 104; });
        });
}

// A send to a TCP socket with no connection gives epipe, and the kernel
// raises no SIGPIPE.  The case puts a handler on SIGPIPE that marks the
// arrival of the signal, so a send that raises it fails the case and does not
// end the process.  The process can get SIG_IGN for SIGPIPE from the runner of
// the tests, and the handler replaces it.  send_bytes is the one send door of
// the header.
[[nodiscard]] int check_send_raises_no_signal() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    TcpSocket const tcp = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    if (!tcp.is_open()) return 61;
    if (!install_pipe_marker()) return 62;
    LinkDumpRequest const request{};
    auto const sent = fixy::net::send_bytes(ctx, tcp.ref(), foundation::core::bytes(request));
    bool const is_restored = restore_pipe_action();
    if (sent.err().value_or(foundation::os::Errno::eio) != foundation::os::Errno::epipe) return 63;
    if (has_pipe_signal_arrived()) return 64;
    if (!is_restored) return 65;
    return 0;
}

// A full write to a TCP socket with no connection gives epipe, and the kernel
// raises SIGPIPE in the thread, because write(2) has no MSG_NOSIGNAL.  The
// file door blocks the signal for the write and takes the signal of the
// write, so the handler of the case marks no arrival.  The mask of the thread
// after the write is the mask before it, which does not block SIGPIPE.
[[nodiscard]] int check_write_raises_no_signal() noexcept {
    IoBlockCtx ctx{eff::testing::test()};
    TcpSocket const tcp = descriptor_of(fixy::net::mint_socket<fixy::net::socket_kind::TcpStream>(ctx));
    if (!tcp.is_open()) return 71;
    if (!install_pipe_marker()) return 72;
    LinkDumpRequest const request{};
    auto const written = fixy::fs::write_full(ctx, tcp.ref(), foundation::core::bytes(request));
    bool const is_blocked = is_pipe_signal_blocked();
    bool const is_restored = restore_pipe_action();
    if (written.err().value_or(foundation::os::Errno::eio) != foundation::os::Errno::epipe) return 73;
    if (has_pipe_signal_arrived()) return 74;
    if (is_blocked) return 75;
    if (!is_restored) return 76;
    return 0;
}

}  // namespace

int main() {
    int failed = check_netlink_route_socket();
    if (failed == 0) failed = check_netlink_calls();
    if (failed == 0) failed = check_tcp_options();
    if (failed == 0) failed = check_socket_timestamping();
    if (failed == 0) failed = check_shared_socket();
    if (failed == 0) failed = check_stale_shared_socket();
    if (failed == 0) failed = check_duplicate_socket();
    if (failed == 0) failed = check_hardware_timestamping();
    if (failed == 0) failed = check_receive_with_timestamps();
    if (failed == 0) failed = check_receive_with_timestamps_into_view();
    if (failed == 0) failed = check_send_raises_no_signal();
    if (failed == 0) failed = check_write_raises_no_signal();
    if (failed != 0) {
        std::fprintf(stderr, "test_os_socket: check %d failed\n", failed);
        return failed;
    }
    ::fixy::check::pass(kTest, "test_os_socket: passed\n");
    return 0;
}

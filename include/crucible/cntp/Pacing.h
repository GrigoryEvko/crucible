#pragma once

// BBR paces from its own rate estimate, so the throughput claim holds only
// when the root qdisc paces as well.  That is why only fq and fq_codel count
// as compatible.
//
// Nothing here installs a qdisc.  ensure_fq_active reads the live one and
// reports whether it matches.  allow_auto_config only selects which error
// comes back, and the fq parameters are declared values that nothing applies.

#include <crucible/cntp/CongestionControl.h>
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/os/Fd.h>
#include <fixy/os/Socket.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/PathText.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::cntp {

enum class Qdisc : std::uint8_t {
    Fq = 0,
    FqCodel = 1,
    Pfifo = 2,
    Mq = 3,
    Noqueue = 4,
    Other = 5,
};

enum class PacingError : std::uint8_t {
    InvalidInterfaceName,
    InvalidSocketFd,
    InvalidPacingRate,
    NetlinkOpenFailed,
    NetlinkSendFailed,
    NetlinkReceiveFailed,
    InterfaceNotFound,
    QdiscKindMissing,
    FqRequired,
    AutoConfigDeferred,
    SetSockOptFailed,
};

[[nodiscard]] ::fixy::TextView qdisc_name(Qdisc qdisc) noexcept;

using PositivePacingRate = ::fixy::Positive<std::uint64_t>;
using PositiveFqParam = ::fixy::Positive<std::uint32_t>;

// The name of a network interface.  from() checks each character and is
// the one writer.  The name holds at most max_bytes - 1 characters, so a
// kernel field of IFNAMSIZ bytes has room for the zero after the name.
// The name is not "." and not "..", and it holds no '/', so a path that
// puts the name after a directory stays in that directory.
//
// The class is not an aggregate, so no caller sets the characters or the
// count without from().
class NicInterfaceName {
public:
    // IFNAMSIZ of the kernel: the characters of a name and its zero.
    static constexpr std::size_t max_bytes = 16;

    // A default-constructed name is empty, which reads as "no interface
    // selected".
    constexpr NicInterfaceName() noexcept = default;

    // The characters of the name.  The view borrows this name, so a view of
    // a temporary name is refused.
    [[nodiscard]] constexpr ::fixy::TextView view() const& noexcept { return characters_.view(); }
    ::fixy::TextView view() const&& =
        delete("a view of a temporary NicInterfaceName points into an object that the full expression ends");

    // A Result<NicInterfaceName, PacingError>.  The type of a Result needs a
    // complete name, so the definition stands after the class.
    [[nodiscard]] static constexpr auto from(::fixy::TextView name) noexcept;

private:
    ::fixy::FixedText<max_bytes - 1> characters_{};
};

// A name of max_bytes characters or more does not fit, and append() refuses
// it with one compare.  Then each character takes one check.
constexpr auto NicInterfaceName::from(::fixy::TextView name) noexcept {
    using Admitted = ::fixy::Result<NicInterfaceName, PacingError>;
    NicInterfaceName out{};
    bool const is_dot_name = name == "." || name == "..";
    if (name.size() == 0 || is_dot_name || out.characters_.append(name).is_err()) {
        return Admitted{::fixy::err(PacingError::InvalidInterfaceName)};
    }
    for (char const character : name) {
        bool const is_name_character = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
                                    || (character >= '0' && character <= '9') || character == '_' || character == '-'
                                    || character == '.' || character == ':';
        if (!is_name_character) {
            return Admitted{::fixy::err(PacingError::InvalidInterfaceName)};
        }
    }
    return Admitted{out};
}

namespace detail {

// The directory of the network interfaces in sysfs.
inline constexpr ::fixy::TextView sysfs_net_root = "/sys/class/net/";

// The most characters of a path under the directory of an interface.
inline constexpr std::size_t interface_leaf_bytes = 32;

using InterfacePath =
    ::foundation::os::PathText<sysfs_net_root.size() + (NicInterfaceName::max_bytes - 1) + 1 + interface_leaf_bytes>;

// The path of the file leaf under the sysfs directory of iface.  The name
// is not "." or ".." and holds no '/', so the path stays in the directory
// of the interface.  leaf holds at most interface_leaf_bytes characters and
// no zero.
[[nodiscard]] constexpr InterfacePath interface_path_of(NicInterfaceName iface, ::fixy::TextView leaf) noexcept {
    CRUCIBLE_PRE(leaf.size() <= interface_leaf_bytes);
    InterfacePath path{};
    path.append(sysfs_net_root).expect("the sysfs root fits the path");
    path.append(iface.view()).expect("an interface name fits the path");
    path.append('/').expect("the separator fits the path");
    path.append(leaf).expect("a leaf of interface_leaf_bytes characters fits the path");
    return path;
}

// The index of the interface iface, from the file ifindex of its sysfs
// directory.  An interface that has no such file gives InterfaceNotFound.
[[nodiscard]] ::fixy::Result<std::int32_t, PacingError> read_interface_index(NicInterfaceName iface) noexcept;

}  // namespace detail

struct FqConfig {
    PositiveFqParam max_quantum = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{8192});
    PositiveFqParam flow_limit = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{100});
    PositiveFqParam low_rate_threshold_kbps = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{50});
};

struct QdiscConfig {
    NicInterfaceName interface{};
    Qdisc required = Qdisc::Fq;
    FqConfig fq{};
    bool allow_auto_config = false;
};

using DeclaredQdiscConfig = ::fixy::Tagged<QdiscConfig, ::fixy::tags::source::QdiscConfig>;

template <Qdisc Q>
concept BbrCompatibleQdisc = Q == Qdisc::Fq || Q == Qdisc::FqCodel;

[[nodiscard]] constexpr ::fixy::Result<PositivePacingRate, PacingError>
admit_pacing_rate(std::uint64_t bytes_per_second) noexcept {
    if (bytes_per_second == 0) {
        return ::fixy::err(PacingError::InvalidPacingRate);
    }
    return ::fixy::mint_refined<::fixy::positive>(bytes_per_second);
}

template <Qdisc Required>
    requires BbrCompatibleQdisc<Required>
[[nodiscard]] constexpr DeclaredQdiscConfig mint_bbr_qdisc_config(NicInterfaceName iface, FqConfig fq = {},
                                                                  bool allow_auto_config = false) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::QdiscConfig>(QdiscConfig{
        .interface = iface,
        .required = Required,
        .fq = fq,
        .allow_auto_config = allow_auto_config,
    });
}

[[nodiscard]] ::fixy::Result<Qdisc, PacingError> qdisc_from_kernel_name(::fixy::TextView name) noexcept;

[[nodiscard]] ::fixy::Result<Qdisc, PacingError> parse_tc_qdisc_show(::fixy::TextView text) noexcept;

namespace detail {

// The records of the qdisc dump.  Each one has the bytes of a struct of
// the netlink headers of the kernel, and Pacing.cpp checks each size and
// each constant below against those headers.

// The header of each netlink message (struct nlmsghdr).
struct NetlinkHeader final {
    std::uint32_t length = 0;
    std::uint16_t type = 0;
    std::uint16_t flags = 0;
    std::uint32_t sequence = 0;
    std::uint32_t port = 0;
};

// The traffic control message after the header of a qdisc message (struct
// tcmsg).  The family zero is AF_UNSPEC.
struct TrafficControlMessage final {
    std::uint8_t family = 0;
    std::uint8_t pad_byte = 0;
    std::uint16_t pad_word = 0;
    std::int32_t interface_index = 0;
    std::uint32_t handle = 0;
    std::uint32_t parent = 0;
    std::uint32_t info = 0;
};

// The header of a route attribute (struct rtattr).
struct RouteAttribute final {
    std::uint16_t length = 0;
    std::uint16_t type = 0;
};

// The dump request of the queueing disciplines of each interface.
struct QdiscDumpRequest final {
    NetlinkHeader header{};
    TrafficControlMessage message{};
};

inline constexpr std::uint16_t netlink_error = 2;  // NLMSG_ERROR
inline constexpr std::uint16_t netlink_done = 3;  // NLMSG_DONE
inline constexpr std::uint16_t route_new_qdisc = 36;  // RTM_NEWQDISC
inline constexpr std::uint16_t route_get_qdisc = 38;  // RTM_GETQDISC
inline constexpr std::uint16_t netlink_dump_request = 0x0301;  // NLM_F_REQUEST | NLM_F_DUMP
inline constexpr std::uint16_t qdisc_kind_attribute = 1;  // TCA_KIND
inline constexpr std::uint32_t root_parent = 0xFFFFFFFFU;  // TC_H_ROOT

// The sequence number of the dump request.  The scan reads only the
// replies that carry it.
inline constexpr std::uint32_t qdisc_dump_sequence = 1;

// The bytes of one receive of the dump reply.
inline constexpr std::size_t qdisc_reply_bytes = 16 * 1024;

// The answer of one part of the dump reply: the root qdisc of the interface
// of interface_index, or the error that ends the dump.  No value means that
// the part holds no answer, and the next part follows.
[[nodiscard]] ::fixy::Option<::fixy::Result<Qdisc, PacingError>>
scan_qdisc_reply(::fixy::View<std::byte const> part, std::int32_t interface_index) noexcept;

}  // namespace detail

// The query opens a netlink socket through the socket door, which counts
// the socket family as IO and Block, so only a context that carries both
// may ask which qdisc is live.  No call of the door waits: the kernel
// queues each part of the dump before the call that asks for it returns,
// and a socket with no reply gives NetlinkReceiveFailed.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::net::CtxFitsSocketMint<Ctx, ::fixy::net::socket_kind::NetlinkRoute>
[[nodiscard]] ::fixy::Result<Qdisc, PacingError> query_active_qdisc(Ctx const& ctx, NicInterfaceName iface) noexcept {
    CRUCIBLE_TRY(interface_index, detail::read_interface_index(iface));
    const ::fixy::fs::OwnedFd nl = ::fixy::net::mint_socket<::fixy::net::socket_kind::NetlinkRoute>(ctx).match(
        [](::fixy::Linear<::fixy::fs::OwnedFd> opened) -> ::fixy::fs::OwnedFd { return std::move(opened).consume(); },
        [](::foundation::os::Errno) -> ::fixy::fs::OwnedFd { return ::fixy::fs::OwnedFd{}; });
    if (!nl.is_open()) {
        return ::fixy::err(PacingError::NetlinkOpenFailed);
    }

    // The bind asks the kernel for a unique port.  With no bind, the kernel
    // takes the port from the process id, so two threads share it and can
    // read the replies of each other.
    if (::fixy::net::bind_netlink(ctx, nl).is_err()) {
        return ::fixy::err(PacingError::NetlinkOpenFailed);
    }

    detail::QdiscDumpRequest const request{
        .header =
            {
                .length = sizeof(detail::QdiscDumpRequest),
                .type = detail::route_get_qdisc,
                .flags = detail::netlink_dump_request,
                .sequence = detail::qdisc_dump_sequence,
                .port = 0,
            },
        .message = {},
    };
    if (::fixy::net::send_bytes(ctx, nl, ::fixy::bytes(request)).value_or(0) != sizeof(request)) {
        return ::fixy::err(PacingError::NetlinkSendFailed);
    }

    ::foundation::core::FixedArray<std::byte, detail::qdisc_reply_bytes> reply{};
    for (;;) {
        std::size_t const received = ::fixy::net::receive_bytes(ctx, nl, reply.view()).value_or(0);
        if (received == 0) {
            return ::fixy::err(PacingError::NetlinkReceiveFailed);
        }
        ::fixy::Option<::fixy::Result<Qdisc, PacingError>> answer = detail::scan_qdisc_reply(
            reply.view().window(0, received).expect("the kernel gives at most the bytes of the buffer"),
            interface_index);
        if (answer.is_some()) {
            return std::move(answer).expect("the part holds the answer of the dump");
        }
    }
}

template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::net::CtxFitsSocketMint<Ctx, ::fixy::net::socket_kind::NetlinkRoute>
[[nodiscard]] ::fixy::Result<::fixy::Unit, PacingError> ensure_fq_active(Ctx const& ctx,
                                                                         DeclaredQdiscConfig config) noexcept {
    auto const& raw = config.value();
    CRUCIBLE_TRY(active, query_active_qdisc(ctx, raw.interface));
    if (active == Qdisc::Fq || active == Qdisc::FqCodel) {
        return ::fixy::Unit{};
    }
    if (raw.allow_auto_config) {
        return ::fixy::err(PacingError::AutoConfigDeferred);
    }
    return ::fixy::err(PacingError::FqRequired);
}

// The rate is a socket option (SO_MAX_PACING_RATE), so the call takes a
// context that admits the socket option row.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSocketOption<Ctx>
[[nodiscard]] ::fixy::Result<::fixy::Unit, PacingError>
set_socket_pacing_rate(Ctx const& ctx, SocketFd fd, PositivePacingRate bytes_per_second) noexcept {
    using Set = ::fixy::Result<::fixy::Unit, PacingError>;
    return ::fixy::net::set_max_pacing_rate(ctx, fd, bytes_per_second.value())
        .match([](::fixy::Unit) -> Set { return ::fixy::Unit{}; },
               [](::foundation::os::Errno) -> Set { return ::fixy::err(PacingError::SetSockOptFailed); });
}

}  // namespace crucible::cntp

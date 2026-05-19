#include <crucible/cntp/Pacing.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <memory>

#include <asm-generic/socket.h>
#include <linux/pkt_sched.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace crucible::cntp {

namespace {

class LocalFd {
public:
    explicit LocalFd(int fd) noexcept : fd_{fd} {}
    LocalFd(LocalFd const&) = delete;
    LocalFd& operator=(LocalFd const&) = delete;
    LocalFd(LocalFd&& other) noexcept : fd_{other.fd_} { other.fd_ = -1; }
    LocalFd& operator=(LocalFd&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    ~LocalFd() noexcept { close(); }

    [[nodiscard]] int raw() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

private:
    int fd_ = -1;

    void close() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
};

struct QdiscDumpRequest {
    nlmsghdr header{};
    tcmsg message{};
};

[[nodiscard]] constexpr std::size_t align4(std::size_t bytes) noexcept {
    return (bytes + 3u) & ~std::size_t{3u};
}

[[nodiscard]] bool token_eq(std::string_view lhs,
                            std::string_view rhs) noexcept {
    return lhs == rhs;
}

[[nodiscard]] std::expected<Qdisc, PacingError>
qdisc_from_attrs(tcmsg const* msg, std::size_t payload_len) noexcept {
    auto const* cursor =
        static_cast<std::byte const*>(static_cast<void const*>(msg + 1));
    std::size_t remaining = payload_len;

    while (remaining >= sizeof(rtattr)) {
        auto const* attr = std::start_lifetime_as<rtattr>(cursor);
        if (attr->rta_len < sizeof(rtattr) || attr->rta_len > remaining) {
            return std::unexpected(PacingError::QdiscKindMissing);
        }
        if (attr->rta_type == TCA_KIND) {
            auto const* data = static_cast<char const*>(
                static_cast<void const*>(cursor + sizeof(rtattr)));
            const std::size_t cap = attr->rta_len - sizeof(rtattr);
            std::size_t len = 0;
            while (len < cap && data[len] != '\0') {
                ++len;
            }
            return qdisc_from_kernel_name(std::string_view{data, len});
        }

        const std::size_t step = align4(attr->rta_len);
        if (step > remaining) {
            return std::unexpected(PacingError::QdiscKindMissing);
        }
        cursor += step;
        remaining -= step;
    }
    return std::unexpected(PacingError::QdiscKindMissing);
}

}  // namespace

std::string_view qdisc_name(Qdisc qdisc) noexcept {
    switch (qdisc) {
        case Qdisc::Fq:      return "fq";
        case Qdisc::FqCodel: return "fq_codel";
        case Qdisc::Pfifo:   return "pfifo_fast";
        case Qdisc::Mq:      return "mq";
        case Qdisc::Noqueue: return "noqueue";
        case Qdisc::Other:   return "other";
        default:             return "unknown";
    }
}

std::expected<Qdisc, PacingError>
qdisc_from_kernel_name(std::string_view name) noexcept {
    if (token_eq(name, "fq")) {
        return Qdisc::Fq;
    }
    if (token_eq(name, "fq_codel")) {
        return Qdisc::FqCodel;
    }
    if (token_eq(name, "pfifo_fast") || token_eq(name, "pfifo")) {
        return Qdisc::Pfifo;
    }
    if (token_eq(name, "mq")) {
        return Qdisc::Mq;
    }
    if (token_eq(name, "noqueue")) {
        return Qdisc::Noqueue;
    }
    if (name.empty()) {
        return std::unexpected(PacingError::QdiscKindMissing);
    }
    return Qdisc::Other;
}

std::expected<Qdisc, PacingError>
parse_tc_qdisc_show(std::string_view text) noexcept {
    constexpr std::string_view prefix = "qdisc ";
    const std::size_t start = text.find(prefix);
    if (start == std::string_view::npos) {
        return std::unexpected(PacingError::QdiscKindMissing);
    }
    std::size_t pos = start + prefix.size();
    while (pos < text.size() && text[pos] == ' ') {
        ++pos;
    }
    const std::size_t kind_start = pos;
    while (pos < text.size() && text[pos] != ' ' && text[pos] != '\n') {
        ++pos;
    }
    return qdisc_from_kernel_name(text.substr(kind_start, pos - kind_start));
}

std::expected<Qdisc, PacingError>
query_active_qdisc(NicInterfaceName iface) noexcept {
    std::array<char, NicInterfaceName::max_bytes> ifname{};
    std::memcpy(ifname.data(), iface.view().data(), iface.view().size());
    const unsigned ifindex = ::if_nametoindex(ifname.data());
    if (ifindex == 0) {
        return std::unexpected(PacingError::InterfaceNotFound);
    }

    LocalFd nl{::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE)};
    if (!nl.valid()) {
        return std::unexpected(PacingError::NetlinkOpenFailed);
    }

    // fixy-A5-004 / fixy-A5-036: explicit bind with nl_pid=0 forces a
    // unique kernel-assigned PID per socket.  Without this, two threads
    // sharing getpid() collide on the implicit-bind path and can
    // observe each other's replies.  nl_pid=0 is the canonical
    // "kernel pick a free address for me" sentinel; the per-socket PID
    // is then stable for the lifetime of `nl`.
    sockaddr_nl bind_addr{};
    bind_addr.nl_family = AF_NETLINK;
    bind_addr.nl_pid    = 0;
    bind_addr.nl_groups = 0;
    if (::bind(nl.raw(),
               static_cast<sockaddr const*>(static_cast<void const*>(&bind_addr)),
               static_cast<socklen_t>(sizeof(bind_addr))) != 0) {
        static_cast<void>(errno);
        return std::unexpected(PacingError::NetlinkOpenFailed);
    }

    // fixy-A5-004: bounded receive — kernel dropping NLMSG_DONE (broken
    // iface, firewall) would otherwise hang this thread forever.  5 s
    // upper bound matches the worst-case observed in production NIC
    // fleets; failure returns NetlinkReceiveFailed.
    timeval rcv_timeout{};
    rcv_timeout.tv_sec  = 5;
    rcv_timeout.tv_usec = 0;
    if (::setsockopt(nl.raw(), SOL_SOCKET, SO_RCVTIMEO,
                     &rcv_timeout,
                     static_cast<socklen_t>(sizeof(rcv_timeout))) != 0) {
        static_cast<void>(errno);
        return std::unexpected(PacingError::NetlinkOpenFailed);
    }

    constexpr std::uint32_t kRequestSeq = 1;
    QdiscDumpRequest request{};
    request.header.nlmsg_len = sizeof(request);
    request.header.nlmsg_type = RTM_GETQDISC;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.header.nlmsg_seq = kRequestSeq;
    request.message.tcm_family = AF_UNSPEC;

    const auto sent = ::send(nl.raw(), &request, sizeof(request), 0);
    if (sent != static_cast<ssize_t>(sizeof(request))) {
        static_cast<void>(errno);
        return std::unexpected(PacingError::NetlinkSendFailed);
    }

    std::array<char, 16 * 1024> buffer{};
    for (;;) {
        const auto received = ::recv(nl.raw(), buffer.data(), buffer.size(), 0);
        if (received <= 0) {
            static_cast<void>(errno);
            return std::unexpected(PacingError::NetlinkReceiveFailed);
        }

        auto const* cursor = static_cast<std::byte const*>(
            static_cast<void const*>(buffer.data()));
        std::size_t remaining = static_cast<std::size_t>(received);
        while (remaining >= sizeof(nlmsghdr)) {
            auto const* header = std::start_lifetime_as<nlmsghdr>(cursor);
            if (header->nlmsg_len < sizeof(nlmsghdr) ||
                header->nlmsg_len > remaining) {
                return std::unexpected(PacingError::NetlinkReceiveFailed);
            }

            // fixy-A5-036: filter replies that don't carry our request
            // sequence number.  Multiple threads sharing this codepath
            // can observe each other's leaked replies under the same
            // PID even with explicit bind; the seq check makes the
            // dispatch deterministic.  Skip-rather-than-fail because
            // benign kernel control messages share the socket.
            if (header->nlmsg_seq != kRequestSeq) {
                const std::size_t step = align4(header->nlmsg_len);
                if (step > remaining) {
                    return std::unexpected(PacingError::NetlinkReceiveFailed);
                }
                cursor += step;
                remaining -= step;
                continue;
            }
            if (header->nlmsg_type == NLMSG_DONE) {
                return std::unexpected(PacingError::QdiscKindMissing);
            }
            if (header->nlmsg_type == NLMSG_ERROR) {
                return std::unexpected(PacingError::NetlinkReceiveFailed);
            }
            if (header->nlmsg_type == RTM_NEWQDISC) {
                if (header->nlmsg_len < NLMSG_LENGTH(sizeof(tcmsg))) {
                    return std::unexpected(PacingError::NetlinkReceiveFailed);
                }

                auto const* msg = std::start_lifetime_as<tcmsg>(
                    cursor + NLMSG_HDRLEN);
                if (msg->tcm_ifindex == static_cast<int>(ifindex) &&
                    msg->tcm_parent == TC_H_ROOT) {
                    const auto payload_len =
                        header->nlmsg_len - NLMSG_LENGTH(sizeof(tcmsg));
                    return qdisc_from_attrs(msg, payload_len);
                }
            }

            const std::size_t step = align4(header->nlmsg_len);
            if (step > remaining) {
                return std::unexpected(PacingError::NetlinkReceiveFailed);
            }
            cursor += step;
            remaining -= step;
        }
    }
}

std::expected<void, PacingError>
ensure_fq_active(DeclaredQdiscConfig config) noexcept {
    auto const& raw = config.value();
    auto active = query_active_qdisc(raw.interface);
    if (!active.has_value()) {
        return std::unexpected(active.error());
    }
    if (*active == Qdisc::Fq || *active == Qdisc::FqCodel) {
        return {};
    }
    if (raw.allow_auto_config) {
        return std::unexpected(PacingError::AutoConfigDeferred);
    }
    return std::unexpected(PacingError::FqRequired);
}

std::expected<void, PacingError>
set_socket_pacing_rate(SocketFd fd, PositivePacingRate bytes_per_second) noexcept {
    const std::uint64_t value = bytes_per_second.value();
    const int rc = ::setsockopt(
        fd.value(),
        SOL_SOCKET,
        SO_MAX_PACING_RATE,
        &value,
        static_cast<socklen_t>(sizeof(value)));
    if (rc != 0) {
        static_cast<void>(errno);
        return std::unexpected(PacingError::SetSockOptFailed);
    }
    return {};
}

}  // namespace crucible::cntp

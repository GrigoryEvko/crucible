#include <crucible/topology/Ptp.h>

#include <crucible/Platform.h>
#include <foundation/Lifetime.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <system_error>

#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

namespace crucible::topology {

namespace {

[[nodiscard]] constexpr bool timespec_nonzero(timespec const& ts) noexcept { return ts.tv_sec != 0 || ts.tv_nsec != 0; }

// The conversion refuses a count past 64 bits as value_too_large and every
// other malformed timespec as result_out_of_range.
[[nodiscard]] PtpError timestamp_error(std::error_code const& error) noexcept {
    return error == std::errc::value_too_large ? PtpError::TimestampOverflow : PtpError::ClockReadFailed;
}

[[nodiscard]] std::expected<PtpTimestampNs, PtpError> timestamp_from_timespec(timespec const& ts) noexcept {
    auto const nanos = ::fixy::time::nanos_from_timespec(ts);
    if (!nanos) {
        return std::unexpected(timestamp_error(nanos.error()));
    }
    return ::fixy::mint_tagged<::fixy::tags::source::Ptp>(*nanos);
}

}  // namespace

std::expected<TimestampedPacketView, PtpError>
timestamp_packet_view(std::span<const std::byte> payload, PtpTimestampNs timestamp, std::uint64_t sequence) noexcept {
    if (payload.empty()) {
        return std::unexpected(PtpError::Degraded);
    }
    return TimestampedPacketView{
        .payload = payload,
        .timestamp_ns = timestamp,
        .sequence = sequence,
    };
}

std::expected<PtpClockCaps, PtpError> query_ptp_clock_caps(PtpClockReader const& clock) noexcept {
    auto const caps = clock.caps();
    if (!caps) {
        return std::unexpected(PtpError::ClockCapsUnavailable);
    }
    return PtpClockCaps{
        .max_adjustment_ppb = caps->max_adj,
        .alarms = caps->n_alarm,
        .external_timestamp_channels = caps->n_ext_ts,
        .periodic_outputs = caps->n_per_out,
        .pps = caps->pps != 0,
        .pins = caps->n_pins,
        .cross_timestamping = caps->cross_timestamping != 0,
        .adjust_phase = caps->adjust_phase != 0,
        .max_phase_adjustment_ns = caps->max_phase_adj,
    };
}

// The reading is stamped as a PHC reading by the reader, which read it from
// the clock of its own descriptor.  The value keeps that provenance under
// the Ptp source tag.
std::expected<PtpTimestampNs, PtpError> ptp_now(PtpClockReader const& clock) noexcept {
    auto reading = clock.read();
    if (!reading) {
        return std::unexpected(timestamp_error(reading.error()));
    }
    return ::fixy::mint_tagged<::fixy::tags::source::Ptp>(std::move(*reading).consume());
}

std::expected<void, PtpError> enable_socket_timestamping(cntp::SocketFd socket) noexcept {
    const int flags = SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_RAW_HARDWARE
                    | SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_OPT_CMSG | SOF_TIMESTAMPING_OPT_TSONLY;
    const int rc =
        ::setsockopt(socket.value(), SOL_SOCKET, SO_TIMESTAMPING, &flags, static_cast<socklen_t>(sizeof(flags)));
    if (rc != 0) {
        static_cast<void>(errno);
        return std::unexpected(PtpError::SocketTimestampingFailed);
    }
    return {};
}

std::expected<void, PtpError> configure_hardware_timestamping(cntp::SocketFd control_socket,
                                                              cntp::NicInterfaceName iface) noexcept {
    hwtstamp_config config{
        .flags = 0,
        .tx_type = HWTSTAMP_TX_ON,
        .rx_filter = HWTSTAMP_FILTER_ALL,
    };
    // The kernel declares ifr_name as char[IFNAMSIZ].  If max_bytes ever grows
    // past IFNAMSIZ, the trailing-NUL write below lands one byte past the end
    // of ifr_name.
    static_assert(::crucible::cntp::NicInterfaceName::max_bytes <= IFNAMSIZ,
                  "NicInterfaceName max bytes exceeds kernel ifr_name");
    // The bound now holds by construction.  NicInterfaceName keeps its
    // length private and from() is the only writer, so view().size() is
    // below max_bytes for every value of the type, and the static_assert
    // above puts max_bytes at or below IFNAMSIZ.  Together those give
    // view().size() < IFNAMSIZ without reading anything at run time.
    //
    // The check stays because it costs one predictable branch on a path
    // that then makes an ioctl, and because it is what turns a future
    // regression in NicInterfaceName into an abort here rather than a
    // memcpy plus an indexed store past the end of ifr_name.  It is not
    // [[assume]]: an assume would hand the optimizer the very bound the
    // check exists to confirm.
    CRUCIBLE_FATAL_INVARIANT(iface.view().size() < IFNAMSIZ);
    ifreq request{};
    std::memcpy(request.ifr_name, iface.view().data(), iface.view().size());
    request.ifr_name[iface.view().size()] = '\0';
    request.ifr_data = static_cast<char*>(static_cast<void*>(&config));

    const int rc = ::ioctl(control_socket.value(), SIOCSHWTSTAMP, &request);
    if (rc != 0) {
        static_cast<void>(errno);
        return std::unexpected(PtpError::HardwareTimestampingFailed);
    }
    return {};
}

std::expected<TimestampedPacket, PtpError> recv_with_hw_timestamp(cntp::SocketFd socket,
                                                                  std::span<std::byte> buffer) noexcept {
    if (buffer.empty()) {
        return std::unexpected(PtpError::InvalidReceiveBuffer);
    }

    iovec iov{
        .iov_base = buffer.data(),
        .iov_len = buffer.size(),
    };
    alignas(cmsghdr) std::array<unsigned char, 256> control{};
    msghdr msg{
        .msg_name = nullptr,
        .msg_namelen = 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control.data(),
        .msg_controllen = control.size(),
        .msg_flags = 0,
    };

    const auto nread = ::recvmsg(socket.value(), &msg, MSG_DONTWAIT);
    if (nread <= 0) {
        static_cast<void>(errno);
        return std::unexpected(PtpError::RecvFailed);
    }

    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_TIMESTAMPING) {
            continue;
        }
        if (cmsg->cmsg_len < CMSG_LEN(sizeof(timespec) * 3u)) {
            return std::unexpected(PtpError::MalformedTimestampControl);
        }

        auto const ts = ::foundation::lifetime::start_as_array<timespec>(CMSG_DATA(cmsg), 3);
        const bool has_hardware = timespec_nonzero(ts[2]);
        const bool has_software = timespec_nonzero(ts[0]);
        if (!has_hardware && !has_software) {
            return std::unexpected(PtpError::NoTimestamp);
        }

        auto timestamp = timestamp_from_timespec(has_hardware ? ts[2] : ts[0]);
        if (!timestamp.has_value()) {
            return std::unexpected(timestamp.error());
        }
        return TimestampedPacket{
            .payload = buffer.first(static_cast<std::size_t>(nread)),
            .size = static_cast<std::size_t>(nread),
            .timestamp_ns = *timestamp,
            .hardware = has_hardware,
        };
    }

    return std::unexpected(PtpError::NoTimestamp);
}

}  // namespace crucible::topology

#include <crucible/topology/Ptp.h>

#include <foundation/Lifetime.h>
#include <foundation/Platform.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <ctime>

#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

namespace crucible::topology {

namespace {

[[nodiscard]] constexpr bool timespec_nonzero(timespec const& ts) noexcept { return ts.tv_sec != 0 || ts.tv_nsec != 0; }

// The conversion refuses a count past 64 bits with eoverflow and each other
// malformed timespec with erange.  A failed read gives the error number of
// the clock call.
[[nodiscard]] PtpError timestamp_error(::foundation::os::Errno error) noexcept {
    return error == ::foundation::os::Errno::eoverflow ? PtpError::TimestampOverflow : PtpError::ClockReadFailed;
}

[[nodiscard]] ::fixy::Result<PtpTimestampNs, PtpError> timestamp_from_timespec(timespec const& ts) noexcept {
    using Stamped = ::fixy::Result<PtpTimestampNs, PtpError>;
    return ::fixy::time::nanos_from_timespec(ts).match(
        [](std::uint64_t nanos) noexcept -> Stamped { return ::fixy::mint_tagged<::fixy::tags::source::Ptp>(nanos); },
        [](::foundation::os::Errno error) noexcept -> Stamped { return ::fixy::err(timestamp_error(error)); });
}

}  // namespace

::fixy::Result<TimestampedPacketView, PtpError> timestamp_packet_view(::fixy::View<std::byte const> payload,
                                                                      PtpTimestampNs timestamp,
                                                                      std::uint64_t sequence) noexcept {
    if (payload.size() == 0) {
        return ::fixy::err(PtpError::Degraded);
    }
    return TimestampedPacketView{
        .payload = payload,
        .timestamp_ns = timestamp,
        .sequence = sequence,
    };
}

::fixy::Result<PtpClockCaps, PtpError> query_ptp_clock_caps(PtpClockReader const& clock) noexcept {
    using Queried = ::fixy::Result<PtpClockCaps, PtpError>;
    return clock.caps().match(
        [](PtpClockCaps caps) noexcept -> Queried { return caps; },
        [](::foundation::os::Errno) noexcept -> Queried { return ::fixy::err(PtpError::ClockCapsUnavailable); });
}

// The reading is stamped as a PHC reading by the reader, which read it from
// the clock of its own descriptor.  The value keeps that provenance under
// the Ptp source tag.
::fixy::Result<PtpTimestampNs, PtpError> ptp_now(PtpClockReader const& clock) noexcept {
    using Stamped = ::fixy::Result<PtpTimestampNs, PtpError>;
    return clock.read().match(
        [](::fixy::PtpHwClockBytes<std::uint64_t> reading) noexcept -> Stamped {
            return ::fixy::mint_tagged<::fixy::tags::source::Ptp>(std::move(reading).consume());
        },
        [](::foundation::os::Errno error) noexcept -> Stamped { return ::fixy::err(timestamp_error(error)); });
}

::fixy::Result<::fixy::Unit, PtpError> enable_socket_timestamping(cntp::SocketFd socket) noexcept {
    const int flags = SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_RAW_HARDWARE
                    | SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_OPT_CMSG | SOF_TIMESTAMPING_OPT_TSONLY;
    const int rc =
        ::setsockopt(socket.value(), SOL_SOCKET, SO_TIMESTAMPING, &flags, static_cast<socklen_t>(sizeof(flags)));
    if (rc != 0) {
        static_cast<void>(errno);
        return ::fixy::err(PtpError::SocketTimestampingFailed);
    }
    return ::fixy::Unit{};
}

::fixy::Result<::fixy::Unit, PtpError> configure_hardware_timestamping(cntp::SocketFd control_socket,
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
        return ::fixy::err(PtpError::HardwareTimestampingFailed);
    }
    return ::fixy::Unit{};
}

::fixy::Result<TimestampedPacket, PtpError> recv_with_hw_timestamp(cntp::SocketFd socket,
                                                                   std::span<std::byte> buffer) noexcept {
    if (buffer.empty()) {
        return ::fixy::err(PtpError::InvalidReceiveBuffer);
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
        return ::fixy::err(PtpError::RecvFailed);
    }

    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_TIMESTAMPING) {
            continue;
        }
        if (cmsg->cmsg_len < CMSG_LEN(sizeof(timespec) * 3u)) {
            return ::fixy::err(PtpError::MalformedTimestampControl);
        }

        auto const ts = ::foundation::lifetime::start_as_array<timespec>(CMSG_DATA(cmsg), 3);
        const bool has_hardware = timespec_nonzero(ts[2]);
        const bool has_software = timespec_nonzero(ts[0]);
        if (!has_hardware && !has_software) {
            return ::fixy::err(PtpError::NoTimestamp);
        }

        CRUCIBLE_TRY(timestamp, timestamp_from_timespec(has_hardware ? ts[2] : ts[0]));
        return TimestampedPacket{
            .payload = buffer.first(static_cast<std::size_t>(nread)),
            .size = static_cast<std::size_t>(nread),
            .timestamp_ns = timestamp,
            .hardware = has_hardware,
        };
    }

    return ::fixy::err(PtpError::NoTimestamp);
}

}  // namespace crucible::topology

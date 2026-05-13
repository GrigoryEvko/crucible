#include <crucible/topology/Ptp.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <limits>

#include <fcntl.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace crucible::topology {

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
    [[nodiscard]] int release() noexcept {
        const int out = fd_;
        fd_ = -1;
        return out;
    }

private:
    int fd_ = -1;

    void close() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
};

[[nodiscard]] constexpr clockid_t
clockid_from_fd(PtpClockFd fd) noexcept {
    const auto raw = static_cast<unsigned int>(fd.value());
    const auto encoded =
        (static_cast<unsigned long>(~raw) << 3u) | 3ul;
    return static_cast<clockid_t>(encoded);
}

[[nodiscard]] constexpr bool
timespec_nonzero(timespec const& ts) noexcept {
    return ts.tv_sec != 0 || ts.tv_nsec != 0;
}

[[nodiscard]] std::expected<PtpTimestampNs, PtpError>
timestamp_from_timespec(timespec const& ts) noexcept {
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1'000'000'000L) {
        return std::unexpected(PtpError::ClockReadFailed);
    }

    const auto seconds = static_cast<std::uint64_t>(ts.tv_sec);
    constexpr auto limit = std::numeric_limits<std::uint64_t>::max();
    if (seconds > (limit - static_cast<std::uint64_t>(ts.tv_nsec)) /
                      1'000'000'000ull) {
        return std::unexpected(PtpError::TimestampOverflow);
    }
    return PtpTimestampNs{
        seconds * 1'000'000'000ull + static_cast<std::uint64_t>(ts.tv_nsec)};
}

}  // namespace

PtpClock::PtpClock(PtpClockFd fd) noexcept : fd_{fd.value()} {}

PtpClock::~PtpClock() noexcept { close(); }

PtpClock::PtpClock(PtpClock&& other) noexcept : fd_{other.fd_} {
    other.fd_ = -1;
}

PtpClock& PtpClock::operator=(PtpClock&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

PtpClockFd PtpClock::fd() const noexcept {
    CRUCIBLE_PRE(fd_ >= 0);
    return PtpClockFd{fd_, typename PtpClockFd::Trusted{}};
}

bool PtpClock::valid() const noexcept {
    return fd_ >= 0;
}

PtpClockFd PtpClock::release() noexcept {
    CRUCIBLE_PRE(fd_ >= 0);
    const int out = fd_;
    fd_ = -1;
    return PtpClockFd{out, typename PtpClockFd::Trusted{}};
}

void PtpClock::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

std::string_view ptp_error_name(PtpError error) noexcept {
    switch (error) {
        case PtpError::None:           return "None";
        case PtpError::ZeroNic:        return "ZeroNic";
        case PtpError::NonNicCog:      return "NonNicCog";
        case PtpError::InvalidClockFd: return "InvalidClockFd";
        case PtpError::NoTimestamp:    return "NoTimestamp";
        case PtpError::Degraded:       return "Degraded";
        case PtpError::InvalidDeviceIndex: return "InvalidDeviceIndex";
        case PtpError::OpenClockFailed: return "OpenClockFailed";
        case PtpError::ClockReadFailed: return "ClockReadFailed";
        case PtpError::ClockCapsUnavailable: return "ClockCapsUnavailable";
        case PtpError::SocketTimestampingFailed:
            return "SocketTimestampingFailed";
        case PtpError::HardwareTimestampingFailed:
            return "HardwareTimestampingFailed";
        case PtpError::RecvFailed: return "RecvFailed";
        case PtpError::InvalidReceiveBuffer: return "InvalidReceiveBuffer";
        case PtpError::TimestampOverflow: return "TimestampOverflow";
        case PtpError::MalformedTimestampControl:
            return "MalformedTimestampControl";
        default:                       return "<unknown PtpError>";
    }
}

std::string_view ptp_servo_state_name(PtpServoState state) noexcept {
    switch (state) {
        case PtpServoState::Unknown:      return "Unknown";
        case PtpServoState::Initializing: return "Initializing";
        case PtpServoState::Listening:    return "Listening";
        case PtpServoState::Slave:        return "Slave";
        case PtpServoState::Master:       return "Master";
        case PtpServoState::Faulty:       return "Faulty";
        case PtpServoState::Degraded:     return "Degraded";
        default:                          return "<unknown PtpServoState>";
    }
}

std::string_view
ptp_degradation_reason_name(PtpDegradationReason reason) noexcept {
    switch (reason) {
        case PtpDegradationReason::None:
            return "None";
        case PtpDegradationReason::Ptp4lUnavailable:
            return "Ptp4lUnavailable";
        case PtpDegradationReason::Phc2sysUnavailable:
            return "Phc2sysUnavailable";
        case PtpDegradationReason::GrandmasterMissing:
            return "GrandmasterMissing";
        case PtpDegradationReason::ServoUnlocked:
            return "ServoUnlocked";
        case PtpDegradationReason::ExcessiveOffset:
            return "ExcessiveOffset";
        case PtpDegradationReason::ExcessiveSkew:
            return "ExcessiveSkew";
        default:
            return "<unknown PtpDegradationReason>";
    }
}

std::expected<TimestampedPacketView, PtpError>
timestamp_packet_view(std::span<const std::byte> payload,
                      PtpTimestampNs timestamp,
                      std::uint64_t sequence) noexcept {
    if (payload.empty()) {
        return std::unexpected(PtpError::Degraded);
    }
    return TimestampedPacketView{
        .payload = payload,
        .timestamp_ns = timestamp,
        .sequence = sequence,
    };
}

std::expected<OwnedPtpClock, PtpError>
open_ptp_clock(PtpDeviceIndex index) noexcept {
    const auto path = ptp_device_path(index);
    LocalFd fd{::open(path.bytes.data(), O_RDONLY | O_CLOEXEC)};
    if (!fd.valid()) {
        static_cast<void>(errno);
        return std::unexpected(PtpError::OpenClockFailed);
    }
    const int raw = fd.release();
    return OwnedPtpClock{
        PtpClock{PtpClockFd{raw, typename PtpClockFd::Trusted{}}}};
}

std::expected<PtpClockCaps, PtpError>
query_ptp_clock_caps(PtpClockFd fd) noexcept {
    ptp_clock_caps caps{};
    const int rc = ::ioctl(fd.value(), PTP_CLOCK_GETCAPS2, &caps);
    if (rc != 0) {
        static_cast<void>(errno);
        return std::unexpected(PtpError::ClockCapsUnavailable);
    }
    return PtpClockCaps{
        .max_adjustment_ppb = caps.max_adj,
        .alarms = caps.n_alarm,
        .external_timestamp_channels = caps.n_ext_ts,
        .periodic_outputs = caps.n_per_out,
        .pps = caps.pps != 0,
        .pins = caps.n_pins,
        .cross_timestamping = caps.cross_timestamping != 0,
        .adjust_phase = caps.adjust_phase != 0,
        .max_phase_adjustment_ns = caps.max_phase_adj,
    };
}

std::expected<PtpTimestampNs, PtpError>
ptp_now(PtpClockFd fd) noexcept {
    timespec ts{};
    const int rc = ::clock_gettime(clockid_from_fd(fd), &ts);
    if (rc != 0) {
        static_cast<void>(errno);
        return std::unexpected(PtpError::ClockReadFailed);
    }
    return timestamp_from_timespec(ts);
}

std::expected<void, PtpError>
enable_socket_timestamping(cntp::SocketFd socket) noexcept {
    const int flags =
        SOF_TIMESTAMPING_RX_HARDWARE |
        SOF_TIMESTAMPING_RX_SOFTWARE |
        SOF_TIMESTAMPING_RAW_HARDWARE |
        SOF_TIMESTAMPING_SOFTWARE |
        SOF_TIMESTAMPING_OPT_CMSG |
        SOF_TIMESTAMPING_OPT_TSONLY;
    const int rc = ::setsockopt(
        socket.value(),
        SOL_SOCKET,
        SO_TIMESTAMPING,
        &flags,
        static_cast<socklen_t>(sizeof(flags)));
    if (rc != 0) {
        static_cast<void>(errno);
        return std::unexpected(PtpError::SocketTimestampingFailed);
    }
    return {};
}

std::expected<void, PtpError>
configure_hardware_timestamping(cntp::SocketFd control_socket,
                                cntp::NicInterfaceName iface) noexcept {
    hwtstamp_config config{
        .flags = 0,
        .tx_type = HWTSTAMP_TX_ON,
        .rx_filter = HWTSTAMP_FILTER_ALL,
    };
    ifreq request{};
    std::memcpy(request.ifr_name, iface.view().data(), iface.view().size());
    request.ifr_name[iface.view().size()] = '\0';
    request.ifr_data = reinterpret_cast<char*>(&config);

    const int rc = ::ioctl(control_socket.value(), SIOCSHWTSTAMP, &request);
    if (rc != 0) {
        static_cast<void>(errno);
        return std::unexpected(PtpError::HardwareTimestampingFailed);
    }
    return {};
}

std::expected<TimestampedPacket, PtpError>
recv_with_hw_timestamp(cntp::SocketFd socket,
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

    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
         cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET ||
            cmsg->cmsg_type != SCM_TIMESTAMPING) {
            continue;
        }
        if (cmsg->cmsg_len < CMSG_LEN(sizeof(timespec) * 3u)) {
            return std::unexpected(PtpError::MalformedTimestampControl);
        }

        auto* ts = reinterpret_cast<timespec*>(CMSG_DATA(cmsg));
        const bool has_hardware = timespec_nonzero(ts[2]);
        const bool has_software = timespec_nonzero(ts[0]);
        if (!has_hardware && !has_software) {
            return std::unexpected(PtpError::NoTimestamp);
        }

        auto timestamp =
            timestamp_from_timespec(has_hardware ? ts[2] : ts[0]);
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

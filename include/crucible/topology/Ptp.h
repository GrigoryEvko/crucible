#pragma once

// The PTP hardware clock of a NIC, its status as the ptp4l and phc2sys
// daemons report it, and the socket timestamping path.
//
// The clock itself is read through fixy::time::PtpClockReader.  The
// reader owns the /dev/ptpN descriptor and is the only code that stamps a
// value as a PHC reading, so this header reads the clock and queries its
// capabilities only through a reader.

#include <crucible/cntp/Pacing.h>
#include <crucible/cog/CogIdentity.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/os/Socket.h>
#include <fixy/os/Time.h>
#include <foundation/Pinned.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::topology {

// The descriptor that names the clock of a PTP handle.  The handle keeps
// it as an identity and never reads through it.
using PtpClockFd = ::fixy::NonNegative<int>;
using PtpTimestampNs = ::fixy::Tagged<std::uint64_t, ::fixy::tags::source::Ptp>;
using PositivePtpSkewBoundNs = ::fixy::Positive<std::uint64_t>;
using PositivePtpPathDelayNs = ::fixy::Positive<std::uint64_t>;
using PositivePtpOffsetBoundNs = ::fixy::Positive<std::uint64_t>;
using PtpClockReader = ::fixy::time::PtpClockReader;
using PtpDeviceIndex = ::fixy::time::PtpDeviceIndex;
using PtpClockCaps = ::fixy::time::PtpClockCaps;

// foundation::reflect::enum_name gives the log spelling of the three enums
// below.
enum class PtpError : std::uint8_t {
    None = 0,
    ZeroNic = 1,
    NonNicCog = 2,
    InvalidClockFd = 3,
    NoTimestamp = 4,
    Degraded = 5,
    InvalidDeviceIndex = 6,
    OpenClockFailed = 7,
    ClockReadFailed = 8,
    ClockCapsUnavailable = 9,
    SocketTimestampingFailed = 10,
    HardwareTimestampingFailed = 11,
    RecvFailed = 12,
    InvalidReceiveBuffer = 13,
    TimestampOverflow = 14,
    MalformedTimestampControl = 15,
};

enum class PtpServoState : std::uint8_t {
    Unknown = 0,
    Initializing = 1,
    Listening = 2,
    Slave = 3,
    Master = 4,
    Faulty = 5,
    Degraded = 6,
};

enum class PtpDegradationReason : std::uint8_t {
    None = 0,
    Ptp4lUnavailable = 1,
    Phc2sysUnavailable = 2,
    GrandmasterMissing = 3,
    ServoUnlocked = 4,
    ExcessiveOffset = 5,
    ExcessiveSkew = 6,
};

struct PtpStatus {
    PtpServoState servo = PtpServoState::Unknown;
    std::int64_t offset_from_master_ns = 0;
    PositivePtpPathDelayNs mean_path_delay_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1});
    std::int64_t frequency_adjustment_ppb = 0;
    PositivePtpSkewBoundNs skew_bound_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1000});
    std::uint64_t sequence = 0;

    [[nodiscard]] constexpr bool synchronized() const noexcept {
        return servo == PtpServoState::Slave || servo == PtpServoState::Master;
    }
};

using DeclaredPtpStatus = ::fixy::Tagged<PtpStatus, ::fixy::tags::source::Ptp>;

struct PtpDaemonReport {
    bool ptp4l_running = false;
    bool phc2sys_running = false;
    bool grandmaster_present = false;
    PtpServoState servo = PtpServoState::Unknown;
    std::int64_t offset_from_master_ns = 0;
    PositivePtpPathDelayNs mean_path_delay_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1});
    std::int64_t frequency_adjustment_ppb = 0;
    PositivePtpSkewBoundNs skew_bound_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1000});
    PositivePtpSkewBoundNs max_accepted_skew_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1000});
    PositivePtpOffsetBoundNs max_accepted_offset_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1000});
    std::uint64_t sequence = 0;
};

struct PtpDiagnostic {
    PtpDegradationReason reason = PtpDegradationReason::None;
    PtpStatus status{};
    std::uint64_t sequence = 0;

    [[nodiscard]] constexpr bool degraded() const noexcept { return reason != PtpDegradationReason::None; }
};

using DeclaredPtpDaemonReport = ::fixy::Tagged<PtpDaemonReport, ::fixy::tags::source::Ptp>;
using DeclaredPtpDiagnostic = ::fixy::Tagged<PtpDiagnostic, ::fixy::tags::source::Ptp>;

struct TimestampedPacketView {
    ::fixy::View<std::byte const> payload{};
    PtpTimestampNs timestamp_ns{};
    std::uint64_t sequence = 0;
};

// The payload borrows the receive buffer of recv_with_hw_timestamp, which
// gives the buffer to the kernel.
struct TimestampedPacket {
    ::fixy::View<std::byte> payload{};
    std::size_t size = 0;
    PtpTimestampNs timestamp_ns{};
    bool hardware = false;
};

template <class Ctx>
concept CtxFitsPtpMint =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsPtpRecord =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Bg>>;

[[nodiscard]] constexpr ::fixy::Result<PtpClockFd, PtpError> admit_ptp_clock_fd(int fd) noexcept {
    return ::fixy::admit_refined<::fixy::non_negative>(fd, PtpError::InvalidClockFd);
}

[[nodiscard]] constexpr ::fixy::Result<PtpDeviceIndex, PtpError> admit_ptp_device_index(std::uint16_t index) noexcept {
    return ::fixy::admit_refined<::fixy::bounded_above<std::uint16_t{255}>>(index, PtpError::InvalidDeviceIndex);
}

[[nodiscard]] constexpr bool ptp_capable_cog(cog::CogIdentity const& nic) noexcept {
    return !nic.uuid.is_zero() && (nic.kind == cog::CogKind::NicPort || nic.kind == cog::CogKind::NicCard);
}

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsPtpRecord<Ctx>
[[nodiscard]] constexpr DeclaredPtpDaemonReport admit_ptp_daemon_report(Ctx const&, PtpDaemonReport report) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::Ptp>(report);
}

[[nodiscard]] constexpr PtpDegradationReason ptp_degradation_reason(PtpDaemonReport const& report) noexcept {
    if (!report.ptp4l_running) {
        return PtpDegradationReason::Ptp4lUnavailable;
    }
    if (!report.phc2sys_running) {
        return PtpDegradationReason::Phc2sysUnavailable;
    }
    if (!report.grandmaster_present) {
        return PtpDegradationReason::GrandmasterMissing;
    }
    if (report.servo != PtpServoState::Slave && report.servo != PtpServoState::Master) {
        return PtpDegradationReason::ServoUnlocked;
    }
    const auto abs_offset = report.offset_from_master_ns < 0
                              ? std::uint64_t{0} - static_cast<std::uint64_t>(report.offset_from_master_ns)
                              : static_cast<std::uint64_t>(report.offset_from_master_ns);
    if (abs_offset > report.max_accepted_offset_ns.value()) {
        return PtpDegradationReason::ExcessiveOffset;
    }
    if (report.skew_bound_ns.value() > report.max_accepted_skew_ns.value()) {
        return PtpDegradationReason::ExcessiveSkew;
    }
    return PtpDegradationReason::None;
}

[[nodiscard]] constexpr DeclaredPtpStatus
ptp_status_from_daemon_report(DeclaredPtpDaemonReport const& report) noexcept {
    auto const& raw = report.value();
    const auto reason = ptp_degradation_reason(raw);
    return ::fixy::mint_tagged<::fixy::tags::source::Ptp>(PtpStatus{
        .servo = reason == PtpDegradationReason::None ? raw.servo : PtpServoState::Degraded,
        .offset_from_master_ns = raw.offset_from_master_ns,
        .mean_path_delay_ns = raw.mean_path_delay_ns,
        .frequency_adjustment_ppb = raw.frequency_adjustment_ppb,
        .skew_bound_ns = raw.skew_bound_ns,
        .sequence = raw.sequence,
    });
}

[[nodiscard]] constexpr DeclaredPtpDiagnostic
ptp_diagnostic_from_daemon_report(DeclaredPtpDaemonReport const& report) noexcept {
    auto const status = ptp_status_from_daemon_report(report);
    return ::fixy::mint_tagged<::fixy::tags::source::Ptp>(PtpDiagnostic{
        .reason = ptp_degradation_reason(report.value()),
        .status = status.value(),
        .sequence = report.value().sequence,
    });
}

class PtpHandle;

// The only door into a PTP handle.  An Init-row context mints it; a
// background worker then records status and timestamps into it.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsPtpMint<Ctx>
[[nodiscard]] PtpHandle mint_ptp_handle(Ctx const&, cog::CogIdentity nic, PtpClockFd clock_fd,
                                        PtpStatus initial_status = {}) noexcept;

class PtpHandle : public ::foundation::Pinned<PtpHandle> {
public:
    [[nodiscard]] cog::CogIdentity nic() const noexcept { return nic_; }
    [[nodiscard]] PtpClockFd clock_fd() const noexcept { return clock_fd_; }

    // Every value that store_status writes comes from a positive field, and
    // the cells start positive, so each cell holds a positive value at
    // every instant, torn read or not.  The checked mints below never fire.
    //
    // Each payload load is an acquire load.  A payload load that reads a
    // store of a writer then happens after the first epoch increment of that
    // writer, so the closing epoch load reads a different epoch, and the loop
    // tries again.  No payload load moves before the opening epoch load or
    // after the closing one.
    [[nodiscard]] PtpStatus status() const noexcept {
        for (;;) {
            std::uint64_t const before = status_epoch_.load_acquire();
            if ((before & 1u) != 0u) {
                continue;
            }

            PtpStatus out{
                .servo = servo_.load_acquire(),
                .offset_from_master_ns = offset_from_master_ns_.load_acquire(),
                .mean_path_delay_ns = ::fixy::mint_refined<::fixy::positive>(mean_path_delay_ns_.load_acquire()),
                .frequency_adjustment_ppb = frequency_adjustment_ppb_.load_acquire(),
                .skew_bound_ns = ::fixy::mint_refined<::fixy::positive>(skew_bound_ns_.load_acquire()),
                .sequence = sequence_.load_acquire(),
            };

            std::uint64_t const after = status_epoch_.load_acquire();
            if (before == after) {
                return out;
            }
        }
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsPtpRecord<Ctx>
    void record_status(Ctx const&, DeclaredPtpStatus const& status) noexcept {
        store_status(status.value());
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsPtpRecord<Ctx>
    void record_timestamp(Ctx const&, PtpTimestampNs timestamp, std::uint64_t sequence) noexcept {
        latest_timestamp_ns_.store_release(timestamp.value());
        timestamp_sequence_.store_release(sequence);
        has_timestamp_.store_release(true);
    }

    [[nodiscard]] ::fixy::Result<PtpTimestampNs, PtpError> latest_timestamp() const noexcept {
        if (!has_timestamp_.load_acquire()) {
            return ::fixy::err(PtpError::NoTimestamp);
        }
        return ::fixy::mint_tagged<::fixy::tags::source::Ptp>(latest_timestamp_ns_.load_acquire());
    }

    [[nodiscard]] std::uint64_t latest_timestamp_sequence() const noexcept {
        return timestamp_sequence_.load_acquire();
    }

private:
    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsPtpMint<Ctx>
    friend PtpHandle mint_ptp_handle(Ctx const&, cog::CogIdentity nic, PtpClockFd clock_fd,
                                     PtpStatus initial_status) noexcept;

    PtpHandle(cog::CogIdentity nic, PtpClockFd clock_fd, PtpStatus const& initial_status) noexcept
        : nic_{nic}, clock_fd_{clock_fd} {
        store_status(initial_status);
    }

    // The epoch is odd while a store is in progress.  Each payload store is a
    // release store, so a reader that sees it also sees the odd epoch before
    // it (status()).
    void store_status(PtpStatus const& status) noexcept {
        static_cast<void>(status_epoch_.fetch_add_acq_rel(1));
        servo_.store_release(status.servo);
        offset_from_master_ns_.store_release(status.offset_from_master_ns);
        mean_path_delay_ns_.store_release(status.mean_path_delay_ns.value());
        frequency_adjustment_ppb_.store_release(status.frequency_adjustment_ppb);
        skew_bound_ns_.store_release(status.skew_bound_ns.value());
        sequence_.store_release(status.sequence);
        static_cast<void>(status_epoch_.fetch_add_acq_rel(1));
    }

    cog::CogIdentity nic_{};
    PtpClockFd clock_fd_ = ::fixy::mint_refined<::fixy::non_negative>(0);
    ::fixy::Atomic<std::uint64_t> status_epoch_{};
    ::fixy::Atomic<PtpServoState> servo_{PtpServoState::Unknown};
    ::fixy::Atomic<std::int64_t> offset_from_master_ns_{};
    ::fixy::Atomic<std::uint64_t> mean_path_delay_ns_{std::uint64_t{1}};
    ::fixy::Atomic<std::int64_t> frequency_adjustment_ppb_{};
    ::fixy::Atomic<std::uint64_t> skew_bound_ns_{std::uint64_t{1000}};
    ::fixy::Atomic<std::uint64_t> sequence_{};
    ::fixy::Atomic<std::uint64_t> latest_timestamp_ns_{};
    ::fixy::Atomic<std::uint64_t> timestamp_sequence_{};
    ::fixy::Atomic<bool> has_timestamp_{false};
};

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsPtpMint<Ctx>
[[nodiscard]] PtpHandle mint_ptp_handle(Ctx const&, cog::CogIdentity nic, PtpClockFd clock_fd,
                                        PtpStatus initial_status) noexcept {
    CRUCIBLE_PRE(!nic.uuid.is_zero());
    CRUCIBLE_PRE(ptp_capable_cog(nic));
    return PtpHandle{nic, clock_fd, initial_status};
}

// Opens /dev/ptpN through the reader's mint.  The gate is the reader's: a
// context off the replay-bound foreground path that may block on the file
// system.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::time::CtxFitsPtpClockReaderMint<Ctx>
[[nodiscard]] ::fixy::Result<PtpClockReader, PtpError> open_ptp_clock(Ctx const& ctx, PtpDeviceIndex index) noexcept {
    using Opened = ::fixy::Result<PtpClockReader, PtpError>;
    return ::fixy::time::mint_ptp_clock_reader(ctx, index)
        .match([](PtpClockReader reader) noexcept -> Opened { return reader; },
               [](::foundation::os::Errno) noexcept -> Opened { return ::fixy::err(PtpError::OpenClockFailed); });
}

[[nodiscard]] ::fixy::Result<TimestampedPacketView, PtpError>
timestamp_packet_view(::fixy::View<std::byte const> payload, PtpTimestampNs timestamp, std::uint64_t sequence) noexcept;

[[nodiscard]] ::fixy::Result<PtpClockCaps, PtpError> query_ptp_clock_caps(PtpClockReader const& clock) noexcept;

[[nodiscard]] ::fixy::Result<PtpTimestampNs, PtpError> ptp_now(PtpClockReader const& clock) noexcept;

namespace detail {

// The packet of one receive of recv_with_hw_timestamp.  The raw hardware
// stamp wins over the software stamp, and a stamp of zero is no stamp.  A
// receive of no bytes gives RecvFailed.  The payload borrows buffer.
[[nodiscard]] ::fixy::Result<TimestampedPacket, PtpError>
packet_of_receive(::fixy::View<std::byte> buffer, ::fixy::net::TimestampedReceive const& received) noexcept;

}  // namespace detail

// Turns on the receive stamps of a socket: the software stamp of the
// kernel, and the raw stamp of the hardware clock of the NIC.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::net::CtxFitsSocketCall<Ctx>
[[nodiscard]] ::fixy::Result<::fixy::Unit, PtpError> enable_socket_timestamping(Ctx const& ctx,
                                                                                cntp::SocketFd socket) noexcept {
    using Enabled = ::fixy::Result<::fixy::Unit, PtpError>;
    return ::fixy::net::set_socket_timestamping(ctx, socket)
        .match([](::fixy::Unit) -> Enabled { return ::fixy::Unit{}; },
               [](::foundation::os::Errno) -> Enabled { return ::fixy::err(PtpError::SocketTimestampingFailed); });
}

// Turns on the hardware stamps of the NIC of iface, for each sent and each
// received packet.  The kernel needs CAP_NET_ADMIN for the call, and a NIC
// with no hardware clock refuses it.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::net::CtxFitsSocketCall<Ctx>
[[nodiscard]] ::fixy::Result<::fixy::Unit, PtpError>
configure_hardware_timestamping(Ctx const& ctx, cntp::SocketFd control_socket, cntp::NicInterfaceName iface) noexcept {
    using Configured = ::fixy::Result<::fixy::Unit, PtpError>;
    return ::fixy::net::set_hardware_timestamping(ctx, control_socket, iface.view())
        .match([](::fixy::Unit) -> Configured { return ::fixy::Unit{}; },
               [](::foundation::os::Errno) -> Configured { return ::fixy::err(PtpError::HardwareTimestampingFailed); });
}

// Receives one datagram into buffer, with its stamp.  It does not wait: a
// socket with no datagram gives RecvFailed.  The payload of the packet
// borrows buffer.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::net::CtxFitsSocketCall<Ctx>
[[nodiscard]] ::fixy::Result<TimestampedPacket, PtpError>
recv_with_hw_timestamp(Ctx const& ctx, cntp::SocketFd socket, ::fixy::View<std::byte> buffer) noexcept {
    using Received = ::fixy::Result<TimestampedPacket, PtpError>;
    if (buffer.size() == 0) {
        return ::fixy::err(PtpError::InvalidReceiveBuffer);
    }
    // The packet borrows buffer, so the match reads a named Result.
    auto const received = ::fixy::net::receive_with_timestamps(ctx, socket, buffer);
    return received.match(
        [buffer](::fixy::net::TimestampedReceive const& receive) -> Received {
            return detail::packet_of_receive(buffer, receive);
        },
        [](::foundation::os::Errno) -> Received { return ::fixy::err(PtpError::RecvFailed); });
}

}  // namespace crucible::topology

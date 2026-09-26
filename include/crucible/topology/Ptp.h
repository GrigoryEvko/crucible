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
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/os/Time.h>
#include <foundation/Pinned.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
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
    std::span<const std::byte> payload{};
    PtpTimestampNs timestamp_ns{};
    std::uint64_t sequence = 0;
};

struct TimestampedPacket {
    std::span<std::byte> payload{};
    std::size_t size = 0;
    PtpTimestampNs timestamp_ns{};
    bool hardware = false;
};

struct PtpClockCaps {
    std::int32_t max_adjustment_ppb = 0;
    std::int32_t alarms = 0;
    std::int32_t external_timestamp_channels = 0;
    std::int32_t periodic_outputs = 0;
    bool pps = false;
    std::int32_t pins = 0;
    bool cross_timestamping = false;
    bool adjust_phase = false;
    std::int32_t max_phase_adjustment_ns = 0;
};

template <class Ctx>
concept CtxFitsPtpMint =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsPtpRecord =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Bg>>;

// A negative descriptor is refused by the branch below, so the checked
// mint that follows it never fires.
[[nodiscard]] constexpr std::expected<PtpClockFd, PtpError> admit_ptp_clock_fd(int fd) noexcept {
    if (fd < 0) {
        return std::unexpected(PtpError::InvalidClockFd);
    }
    return ::fixy::mint_refined<::fixy::non_negative>(fd);
}

[[nodiscard]] constexpr std::expected<PtpDeviceIndex, PtpError> admit_ptp_device_index(std::uint16_t index) noexcept {
    if (index > 255u) {
        return std::unexpected(PtpError::InvalidDeviceIndex);
    }
    return ::fixy::mint_refined<::fixy::bounded_above<std::uint16_t{255}>>(index);
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

[[nodiscard]] constexpr DeclaredPtpStatus ptp_status_from_daemon_report(DeclaredPtpDaemonReport const& report) noexcept {
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
    // the atomics start positive, so each atomic holds a positive value at
    // every instant, torn read or not.  The checked mints below never fire.
    [[nodiscard]] PtpStatus status() const noexcept {
        for (;;) {
            auto const before = status_epoch_.load(std::memory_order_acquire);
            if ((before & 1u) != 0u) {
                continue;
            }

            PtpStatus out{
                .servo = static_cast<PtpServoState>(servo_.load(std::memory_order_relaxed)),
                .offset_from_master_ns = offset_from_master_ns_.load(std::memory_order_relaxed),
                .mean_path_delay_ns =
                    ::fixy::mint_refined<::fixy::positive>(mean_path_delay_ns_.load(std::memory_order_relaxed)),
                .frequency_adjustment_ppb = frequency_adjustment_ppb_.load(std::memory_order_relaxed),
                .skew_bound_ns = ::fixy::mint_refined<::fixy::positive>(skew_bound_ns_.load(std::memory_order_relaxed)),
                .sequence = sequence_.load(std::memory_order_relaxed),
            };

            // The acquire on the opening load at the top of the loop orders
            // the payload loads after itself, but an acquire orders only what
            // follows it.  Nothing in the closing load stops the six relaxed
            // loads above from sinking past it, so without this fence the
            // epoch can compare equal across a payload the writer tore.  The
            // fence makes the payload loads happen before the re-read on both
            // the compiler and the hardware side.  On x86 it emits no
            // instruction, because TSO already forbids load-load reordering;
            // on aarch64 it lowers to dmb ishld, where the reordering is real.
            std::atomic_thread_fence(std::memory_order_acquire);
            auto const after = status_epoch_.load(std::memory_order_acquire);
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
        latest_timestamp_ns_.store(timestamp.value(), std::memory_order_release);
        timestamp_sequence_.store(sequence, std::memory_order_release);
        has_timestamp_.store(true, std::memory_order_release);
    }

    [[nodiscard]] std::expected<PtpTimestampNs, PtpError> latest_timestamp() const noexcept {
        if (!has_timestamp_.load(std::memory_order_acquire)) {
            return std::unexpected(PtpError::NoTimestamp);
        }
        return ::fixy::mint_tagged<::fixy::tags::source::Ptp>(latest_timestamp_ns_.load(std::memory_order_acquire));
    }

    [[nodiscard]] std::uint64_t latest_timestamp_sequence() const noexcept {
        return timestamp_sequence_.load(std::memory_order_acquire);
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

    void store_status(PtpStatus const& status) noexcept {
        status_epoch_.fetch_add(1, std::memory_order_acq_rel);
        servo_.store(static_cast<std::uint8_t>(status.servo), std::memory_order_relaxed);
        offset_from_master_ns_.store(status.offset_from_master_ns, std::memory_order_relaxed);
        mean_path_delay_ns_.store(status.mean_path_delay_ns.value(), std::memory_order_relaxed);
        frequency_adjustment_ppb_.store(status.frequency_adjustment_ppb, std::memory_order_relaxed);
        skew_bound_ns_.store(status.skew_bound_ns.value(), std::memory_order_relaxed);
        sequence_.store(status.sequence, std::memory_order_relaxed);
        status_epoch_.fetch_add(1, std::memory_order_release);
    }

    cog::CogIdentity nic_{};
    PtpClockFd clock_fd_ = ::fixy::mint_refined<::fixy::non_negative>(0);
    std::atomic<std::uint64_t> status_epoch_{0};
    std::atomic<std::uint8_t> servo_{static_cast<std::uint8_t>(PtpServoState::Unknown)};
    std::atomic<std::int64_t> offset_from_master_ns_{0};
    std::atomic<std::uint64_t> mean_path_delay_ns_{1};
    std::atomic<std::int64_t> frequency_adjustment_ppb_{0};
    std::atomic<std::uint64_t> skew_bound_ns_{1000};
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<std::uint64_t> latest_timestamp_ns_{0};
    std::atomic<std::uint64_t> timestamp_sequence_{0};
    std::atomic<bool> has_timestamp_{false};
};

// On an ISA that lacks the required instruction the standard library
// substitutes a mutex-backed atomic without saying so.  A hidden mutex inside
// the status publication would serialize every reader against the servo update
// loop, so the build refuses such a target instead of regressing quietly.
static_assert(std::atomic<std::uint8_t>::is_always_lock_free, "std::atomic<uint8_t> must be lock-free on this target");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "std::atomic<uint64_t> must be lock-free on this target");
static_assert(std::atomic<std::int64_t>::is_always_lock_free, "std::atomic<int64_t> must be lock-free on this target");
static_assert(std::atomic<bool>::is_always_lock_free, "std::atomic<bool> must be lock-free on this target");

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
[[nodiscard]] std::expected<PtpClockReader, PtpError> open_ptp_clock(Ctx const& ctx, PtpDeviceIndex index) noexcept {
    auto reader = ::fixy::time::mint_ptp_clock_reader(ctx, index);
    if (!reader) {
        return std::unexpected(PtpError::OpenClockFailed);
    }
    return std::move(*reader);
}

[[nodiscard]] std::expected<TimestampedPacketView, PtpError>
timestamp_packet_view(std::span<const std::byte> payload, PtpTimestampNs timestamp, std::uint64_t sequence) noexcept;

[[nodiscard]] std::expected<PtpClockCaps, PtpError> query_ptp_clock_caps(PtpClockReader const& clock) noexcept;

[[nodiscard]] std::expected<PtpTimestampNs, PtpError> ptp_now(PtpClockReader const& clock) noexcept;

[[nodiscard]] std::expected<void, PtpError> enable_socket_timestamping(cntp::SocketFd socket) noexcept;

[[nodiscard]] std::expected<void, PtpError> configure_hardware_timestamping(cntp::SocketFd control_socket,
                                                                            cntp::NicInterfaceName iface) noexcept;

[[nodiscard]] std::expected<TimestampedPacket, PtpError> recv_with_hw_timestamp(cntp::SocketFd socket,
                                                                                std::span<std::byte> buffer) noexcept;

static_assert(sizeof(PtpClockFd) == sizeof(int));
static_assert(sizeof(PtpTimestampNs) == sizeof(std::uint64_t));
static_assert(sizeof(PtpDeviceIndex) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredPtpDaemonReport) == sizeof(PtpDaemonReport));
static_assert(sizeof(DeclaredPtpDiagnostic) == sizeof(PtpDiagnostic));
static_assert(std::is_trivially_copyable_v<PtpClockCaps>);
// A refined field keeps no byte route into it, so neither record is
// trivially copyable.  Their copies stay trivial.
static_assert(std::is_trivially_copy_constructible_v<PtpDaemonReport>
              && std::is_trivially_destructible_v<PtpDaemonReport>);
static_assert(std::is_trivially_copy_constructible_v<PtpDiagnostic> && std::is_trivially_destructible_v<PtpDiagnostic>);
static_assert(!CtxFitsPtpMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsPtpMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsPtpRecord<::fixy::HotFgCtx>);
static_assert(CtxFitsPtpRecord<::fixy::BgDrainCtx>);
static_assert(std::is_base_of_v<::foundation::Pinned<PtpHandle>, PtpHandle>);
static_assert(!std::is_constructible_v<PtpHandle, cog::CogIdentity, PtpClockFd, PtpStatus const&>,
              "a PTP handle is reached only through mint_ptp_handle");

}  // namespace crucible::topology

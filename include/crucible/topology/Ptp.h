#pragma once

// GAPS-129.  PTP clock substrate.
//
// This header owns typed PTP facts and admission gates.  It does not
// start ptp4l/phc2sys, open /dev/ptp*, issue SIOCSHWTSTAMP, or program
// socket timestamping.  Those privileged integration points feed this
// substrate later with admitted clock fds, status samples, and hardware
// timestamps carrying source::Ptp provenance.

#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Pre.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::topology {

using PtpClockFd = safety::NonNegative<int>;
using PtpTimestampNs = safety::Tagged<std::uint64_t, safety::source::Ptp>;
using PositivePtpSkewBoundNs = safety::Positive<std::uint64_t>;
using PositivePtpPathDelayNs = safety::Positive<std::uint64_t>;

enum class PtpError : std::uint8_t {
    None = 0,
    ZeroNic = 1,
    NonNicCog = 2,
    InvalidClockFd = 3,
    NoTimestamp = 4,
    Degraded = 5,
};

[[nodiscard]] std::string_view ptp_error_name(PtpError error) noexcept;

enum class PtpServoState : std::uint8_t {
    Unknown = 0,
    Initializing = 1,
    Listening = 2,
    Slave = 3,
    Master = 4,
    Faulty = 5,
    Degraded = 6,
};

[[nodiscard]] std::string_view ptp_servo_state_name(PtpServoState state) noexcept;

struct PtpStatus {
    PtpServoState servo = PtpServoState::Unknown;
    std::int64_t offset_from_master_ns = 0;
    PositivePtpPathDelayNs mean_path_delay_ns{std::uint64_t{1}};
    std::int64_t frequency_adjustment_ppb = 0;
    PositivePtpSkewBoundNs skew_bound_ns{std::uint64_t{1'000}};
    std::uint64_t sequence = 0;

    [[nodiscard]] constexpr bool synchronized() const noexcept {
        return servo == PtpServoState::Slave || servo == PtpServoState::Master;
    }
};

using DeclaredPtpStatus = safety::Tagged<PtpStatus, safety::source::Ptp>;

struct TimestampedPacketView {
    std::span<const std::byte> payload{};
    PtpTimestampNs timestamp_ns{0};
    std::uint64_t sequence = 0;
};

template <class Ctx>
concept CtxFitsPtpMint =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsPtpRecord =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Bg>>;

[[nodiscard]] constexpr std::expected<PtpClockFd, PtpError>
admit_ptp_clock_fd(int fd) noexcept {
    if (fd < 0) {
        return std::unexpected(PtpError::InvalidClockFd);
    }
    return PtpClockFd{fd, typename PtpClockFd::Trusted{}};
}

[[nodiscard]] constexpr bool
ptp_capable_cog(cog::CogIdentity const& nic) noexcept {
    return !nic.uuid.is_zero()
        && (nic.kind == cog::CogKind::NicPort || nic.kind == cog::CogKind::NicCard);
}

class PtpHandle : public safety::Pinned<PtpHandle> {
public:
    explicit PtpHandle(cog::CogIdentity nic,
                       PtpClockFd clock_fd,
                       PtpStatus initial_status = {}) noexcept
        : nic_{nic},
          clock_fd_{clock_fd} {
        store_status(initial_status);
    }

    [[nodiscard]] cog::CogIdentity nic() const noexcept { return nic_; }
    [[nodiscard]] PtpClockFd clock_fd() const noexcept { return clock_fd_; }

    [[nodiscard]] PtpStatus status() const noexcept {
        for (;;) {
            auto const before = status_epoch_.load(std::memory_order_acquire);
            if ((before & 1u) != 0u) {
                continue;
            }

            PtpStatus out{
                .servo = static_cast<PtpServoState>(
                    servo_.load(std::memory_order_relaxed)),
                .offset_from_master_ns =
                    offset_from_master_ns_.load(std::memory_order_relaxed),
                .mean_path_delay_ns = PositivePtpPathDelayNs{
                    mean_path_delay_ns_.load(std::memory_order_relaxed),
                    typename PositivePtpPathDelayNs::Trusted{}},
                .frequency_adjustment_ppb =
                    frequency_adjustment_ppb_.load(std::memory_order_relaxed),
                .skew_bound_ns = PositivePtpSkewBoundNs{
                    skew_bound_ns_.load(std::memory_order_relaxed),
                    typename PositivePtpSkewBoundNs::Trusted{}},
                .sequence = sequence_.load(std::memory_order_relaxed),
            };

            auto const after = status_epoch_.load(std::memory_order_acquire);
            if (before == after) {
                return out;
            }
        }
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsPtpRecord<Ctx>
    void record_status(Ctx const&, DeclaredPtpStatus status) noexcept {
        store_status(status.value());
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsPtpRecord<Ctx>
    void record_timestamp(Ctx const&,
                          PtpTimestampNs timestamp,
                          std::uint64_t sequence) noexcept {
        latest_timestamp_ns_.store(timestamp.value(), std::memory_order_release);
        timestamp_sequence_.store(sequence, std::memory_order_release);
        has_timestamp_.store(true, std::memory_order_release);
    }

    [[nodiscard]] std::expected<PtpTimestampNs, PtpError>
    latest_timestamp() const noexcept {
        if (!has_timestamp_.load(std::memory_order_acquire)) {
            return std::unexpected(PtpError::NoTimestamp);
        }
        return PtpTimestampNs{
            latest_timestamp_ns_.load(std::memory_order_acquire)};
    }

    [[nodiscard]] std::uint64_t latest_timestamp_sequence() const noexcept {
        return timestamp_sequence_.load(std::memory_order_acquire);
    }

private:
    void store_status(PtpStatus const& status) noexcept {
        status_epoch_.fetch_add(1, std::memory_order_acq_rel);
        servo_.store(static_cast<std::uint8_t>(status.servo),
                     std::memory_order_relaxed);
        offset_from_master_ns_.store(status.offset_from_master_ns,
                                     std::memory_order_relaxed);
        mean_path_delay_ns_.store(status.mean_path_delay_ns.value(),
                                  std::memory_order_relaxed);
        frequency_adjustment_ppb_.store(status.frequency_adjustment_ppb,
                                        std::memory_order_relaxed);
        skew_bound_ns_.store(status.skew_bound_ns.value(),
                             std::memory_order_relaxed);
        sequence_.store(status.sequence, std::memory_order_relaxed);
        status_epoch_.fetch_add(1, std::memory_order_release);
    }

    cog::CogIdentity nic_{};
    PtpClockFd clock_fd_{0, typename PtpClockFd::Trusted{}};
    std::atomic<std::uint64_t> status_epoch_{0};
    std::atomic<std::uint8_t> servo_{
        static_cast<std::uint8_t>(PtpServoState::Unknown)};
    std::atomic<std::int64_t> offset_from_master_ns_{0};
    std::atomic<std::uint64_t> mean_path_delay_ns_{1};
    std::atomic<std::int64_t> frequency_adjustment_ppb_{0};
    std::atomic<std::uint64_t> skew_bound_ns_{1'000};
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<std::uint64_t> latest_timestamp_ns_{0};
    std::atomic<std::uint64_t> timestamp_sequence_{0};
    std::atomic<bool> has_timestamp_{false};
};

template <effects::IsExecCtx Ctx>
    requires CtxFitsPtpMint<Ctx>
[[nodiscard]] PtpHandle
mint_ptp_handle(Ctx const&,
                cog::CogIdentity nic,
                PtpClockFd clock_fd,
                PtpStatus initial_status = {}) noexcept {
    CRUCIBLE_PRE(!nic.uuid.is_zero());
    CRUCIBLE_PRE(ptp_capable_cog(nic));
    return PtpHandle{nic, clock_fd, initial_status};
}

[[nodiscard]] std::expected<TimestampedPacketView, PtpError>
timestamp_packet_view(std::span<const std::byte> payload,
                      PtpTimestampNs timestamp,
                      std::uint64_t sequence) noexcept;

static_assert(sizeof(PtpClockFd) == sizeof(int));
static_assert(sizeof(PtpTimestampNs) == sizeof(std::uint64_t));
static_assert(!CtxFitsPtpMint<effects::BgDrainCtx>);
static_assert(CtxFitsPtpMint<effects::ColdInitCtx>);
static_assert(!CtxFitsPtpRecord<effects::HotFgCtx>);
static_assert(CtxFitsPtpRecord<effects::BgDrainCtx>);
static_assert(std::is_base_of_v<safety::Pinned<PtpHandle>, PtpHandle>);

}  // namespace crucible::topology

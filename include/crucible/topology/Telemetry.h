#pragma once

// GAPS-112 substrate: per-NIC effective-capacity telemetry.
//
// This header intentionally admits already-collected Linux-visible facts
// into bounded, typed storage. Live sysfs/procfs/netlink/tc harvesting is
// a separate boundary; this substrate owns the typed accounting, effective
// bandwidth calculation, and bounded history surface consumed by later
// health and routing owners.

#include <crucible/Platform.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/topology/CongestionTelemetry.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::topology {

using ExternalTelemetryText =
    safety::Tagged<std::string_view, safety::source::External>;
using PositiveEffectiveBandwidthBps = safety::Positive<double>;

enum class NicTelemetryError : std::uint8_t {
    None = 0,
    EmptyInput = 1,
    MalformedRecord = 2,
    MissingRequiredField = 3,
    InvalidNicCog = 4,
    EmptyHistory = 5,
    InvalidWindow = 6,
    NonPositiveCapacity = 7,
};

[[nodiscard]] std::string_view
nic_telemetry_error_name(NicTelemetryError error) noexcept;

struct NetdevCounters {
    std::uint64_t rx_bytes = 0;
    std::uint64_t tx_bytes = 0;
    std::uint64_t rx_packets = 0;
    std::uint64_t tx_packets = 0;
    std::uint64_t rx_dropped = 0;
    std::uint64_t tx_dropped = 0;
    std::uint64_t rx_errors = 0;
    std::uint64_t tx_errors = 0;
    std::uint64_t rx_fifo_errors = 0;
    std::uint64_t tx_fifo_errors = 0;
};

struct QdiscBacklog {
    std::uint64_t backlog_bytes = 0;
    std::uint32_t backlog_packets = 0;
    std::uint64_t drops = 0;
    std::uint64_t overlimits = 0;
};

struct SysctlSnapshot {
    std::uint64_t rmem_max_bytes = 0;
    std::uint64_t wmem_max_bytes = 0;
    std::uint32_t busy_poll_us = 0;
    std::uint32_t tcp_rmem_max_bytes = 0;
    std::uint32_t tcp_wmem_max_bytes = 0;
};

struct NicThermalSample {
    std::int32_t temperature_millicelsius = 0;
};

using DeclaredNetdevCounters =
    safety::Tagged<NetdevCounters, safety::source::KernelTelemetry>;
using DeclaredQdiscBacklog =
    safety::Tagged<QdiscBacklog, safety::source::KernelTelemetry>;
using DeclaredSysctlSnapshot =
    safety::Tagged<SysctlSnapshot, safety::source::KernelTelemetry>;
using DeclaredNicThermalSample =
    safety::Tagged<NicThermalSample, safety::source::KernelTelemetry>;

struct NicTelemetryPolicy {
    std::uint32_t fairness_penalty_ppm = 100'000;
    std::uint32_t drift_drop_ppm = 150'000;
    std::uint16_t min_drift_samples = 2;
};

struct NicTelemetrySnapshot {
    cog::Uuid nic_uuid{};
    safety::Tagged<std::uint64_t, safety::source::Calibrated>
        line_rate_bps{std::uint64_t{0}};
    safety::Stale<DeclaredNetdevCounters> netdev{
        DeclaredNetdevCounters{NetdevCounters{}},
        safety::Stale<DeclaredNetdevCounters>::semiring_type::bottom()};
    safety::Stale<DeclaredQdiscBacklog> qdisc{
        DeclaredQdiscBacklog{QdiscBacklog{}},
        safety::Stale<DeclaredQdiscBacklog>::semiring_type::bottom()};
    safety::Stale<DeclaredSysctlSnapshot> sysctl{
        DeclaredSysctlSnapshot{SysctlSnapshot{}},
        safety::Stale<DeclaredSysctlSnapshot>::semiring_type::bottom()};
    safety::Stale<TcpInfoSnapshot> tcp{
        TcpInfoSnapshot{CongestionState{}},
        safety::Stale<TcpInfoSnapshot>::semiring_type::bottom()};
    safety::Stale<DeclaredNicThermalSample> thermal{
        DeclaredNicThermalSample{NicThermalSample{}},
        safety::Stale<DeclaredNicThermalSample>::semiring_type::bottom()};
    safety::Tagged<double, safety::source::Calibrated>
        effective_bandwidth_bps{0.0};
    std::uint64_t sequence = 0;
};

struct NicTelemetryDrift {
    cog::Uuid nic_uuid{};
    std::uint32_t bandwidth_drop_ppm = 0;
    std::uint16_t observed_samples = 0;
    bool degraded = false;
};

template <class Ctx>
concept CtxFitsNicTelemetryMint =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Init>;

template <class Ctx>
concept CtxFitsNicTelemetryRecord =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Bg>;

template <class Ctx>
concept CtxFitsNicTelemetryRead =
       effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<>>;

[[nodiscard]] constexpr ExternalTelemetryText
tag_external_telemetry_text(std::string_view text) noexcept {
    return ExternalTelemetryText{text};
}

[[nodiscard]] constexpr DeclaredNetdevCounters
declare_netdev_counters(NetdevCounters counters) noexcept {
    return DeclaredNetdevCounters{counters};
}

[[nodiscard]] constexpr DeclaredQdiscBacklog
declare_qdisc_backlog(QdiscBacklog backlog) noexcept {
    return DeclaredQdiscBacklog{backlog};
}

[[nodiscard]] constexpr DeclaredSysctlSnapshot
declare_sysctl_snapshot(SysctlSnapshot snapshot) noexcept {
    return DeclaredSysctlSnapshot{snapshot};
}

[[nodiscard]] constexpr DeclaredNicThermalSample
declare_nic_thermal_sample(NicThermalSample sample) noexcept {
    return DeclaredNicThermalSample{sample};
}

[[nodiscard]] std::expected<DeclaredNetdevCounters, NicTelemetryError>
parse_netdev_counters(ExternalTelemetryText text) noexcept;

[[nodiscard]] std::expected<DeclaredQdiscBacklog, NicTelemetryError>
parse_qdisc_backlog(ExternalTelemetryText text) noexcept;

[[nodiscard]] std::expected<DeclaredSysctlSnapshot, NicTelemetryError>
parse_sysctl_snapshot(ExternalTelemetryText text) noexcept;

[[nodiscard]] constexpr std::uint64_t
netdev_drop_ppm(NetdevCounters counters) noexcept {
    const std::uint64_t packets = counters.rx_packets + counters.tx_packets;
    const std::uint64_t dropped = counters.rx_dropped + counters.tx_dropped
        + counters.rx_fifo_errors + counters.tx_fifo_errors;
    if (packets == 0 || dropped == 0) {
        return 0;
    }
    const long double ppm =
        static_cast<long double>(dropped) * 1'000'000.0L
        / static_cast<long double>(packets);
    return static_cast<std::uint64_t>(std::min<long double>(1'000'000.0L, ppm));
}

[[nodiscard]] constexpr std::uint64_t
sysctl_throughput_ceiling_bps(SysctlSnapshot sysctl,
                              std::uint64_t rtt_us) noexcept {
    if (rtt_us == 0) {
        rtt_us = 1;
    }
    std::uint64_t window = std::max(sysctl.rmem_max_bytes, sysctl.wmem_max_bytes);
    window = std::max<std::uint64_t>(window, sysctl.tcp_rmem_max_bytes);
    window = std::max<std::uint64_t>(window, sysctl.tcp_wmem_max_bytes);
    if (window == 0) {
        return UINT64_MAX;
    }
    long double bps = static_cast<long double>(window) * 8'000'000.0L
        / static_cast<long double>(rtt_us);
    if (bps >= static_cast<long double>(UINT64_MAX)) {
        return UINT64_MAX;
    }
    return static_cast<std::uint64_t>(bps);
}

[[nodiscard]] constexpr std::expected<PositiveEffectiveBandwidthBps, NicTelemetryError>
compute_effective_bandwidth(NicTelemetrySnapshot const& snapshot,
                            NicTelemetryPolicy policy = {}) noexcept {
    auto const& tcp = snapshot.tcp.peek().value();
    const std::uint64_t rtt_us = std::max<std::uint64_t>(1, tcp.rt_prop_us.value());
    const std::uint64_t line_rate = snapshot.line_rate_bps.value() == 0
        ? UINT64_MAX
        : snapshot.line_rate_bps.value();
    const std::uint64_t sysctl_ceiling =
        sysctl_throughput_ceiling_bps(snapshot.sysctl.peek().value(), rtt_us);
    const std::uint64_t tcp_btl = tcp.btl_bw_bps.value();

    long double base = static_cast<long double>(
        std::min({line_rate, sysctl_ceiling, tcp_btl}));
    const long double in_flight_bps =
        static_cast<long double>(tcp.in_flight_bytes) * 8'000'000.0L
        / static_cast<long double>(rtt_us);
    const long double penalty =
        in_flight_bps * static_cast<long double>(policy.fairness_penalty_ppm)
        / 1'000'000.0L;
    base = std::max<long double>(1.0L, base - penalty);
    return PositiveEffectiveBandwidthBps{
        static_cast<double>(base),
        typename PositiveEffectiveBandwidthBps::Trusted{}};
}

[[nodiscard]] constexpr std::expected<NicTelemetrySnapshot, NicTelemetryError>
mint_nic_telemetry_snapshot(cog::CogIdentity const& nic,
                            std::uint64_t line_rate_bps,
                            DeclaredNetdevCounters netdev,
                            DeclaredQdiscBacklog qdisc,
                            DeclaredSysctlSnapshot sysctl,
                            TcpInfoSnapshot tcp,
                            DeclaredNicThermalSample thermal,
                            std::uint64_t sequence,
                            NicTelemetryPolicy policy = {}) noexcept {
    if (!is_nic_cog(nic) || nic.uuid.is_zero()) {
        return std::unexpected(NicTelemetryError::InvalidNicCog);
    }
    NicTelemetrySnapshot snapshot{
        .nic_uuid = nic.uuid,
        .line_rate_bps = safety::Tagged<std::uint64_t,
            safety::source::Calibrated>{line_rate_bps},
        .netdev = safety::Stale<DeclaredNetdevCounters>::at(netdev, sequence),
        .qdisc = safety::Stale<DeclaredQdiscBacklog>::at(qdisc, sequence),
        .sysctl = safety::Stale<DeclaredSysctlSnapshot>::at(sysctl, sequence),
        .tcp = safety::Stale<TcpInfoSnapshot>::at(tcp, sequence),
        .thermal = safety::Stale<DeclaredNicThermalSample>::at(thermal, sequence),
        .sequence = sequence,
    };
    auto effective = compute_effective_bandwidth(snapshot, policy);
    if (!effective.has_value()) {
        return std::unexpected(effective.error());
    }
    snapshot.effective_bandwidth_bps = safety::Tagged<double,
        safety::source::Calibrated>{effective->value()};
    return snapshot;
}

template <std::size_t Window>
class NicTelemetryHistory {
    static_assert(Window > 0, "NicTelemetryHistory requires at least one slot");
    static_assert(Window <= UINT16_MAX,
                  "NicTelemetryHistory stores bounded history indices in uint16_t");

    std::array<NicTelemetrySnapshot, Window> snapshots_{};
    std::uint16_t count_ = 0;
    std::uint16_t next_ = 0;

    [[nodiscard]] constexpr std::uint16_t oldest_index() const noexcept {
        if (count_ < Window) {
            return 0;
        }
        return next_;
    }

public:
    [[nodiscard]] constexpr std::uint16_t count() const noexcept {
        return count_;
    }

    [[nodiscard]] constexpr std::span<const NicTelemetrySnapshot>
    storage_view() const noexcept {
        return {snapshots_.data(), count_};
    }

    template <class Ctx>
        requires CtxFitsNicTelemetryRecord<Ctx>
    [[nodiscard]] constexpr NicTelemetryError
    record(Ctx const&, NicTelemetrySnapshot snapshot) noexcept {
        snapshots_[next_] = snapshot;
        next_ = static_cast<std::uint16_t>((next_ + 1u) % Window);
        if (count_ < Window) {
            ++count_;
        }
        return NicTelemetryError::None;
    }

    template <class Ctx>
        requires CtxFitsNicTelemetryRead<Ctx>
    [[nodiscard]] constexpr std::expected<NicTelemetrySnapshot, NicTelemetryError>
    current_snapshot(Ctx const&) const noexcept {
        if (count_ == 0) {
            return std::unexpected(NicTelemetryError::EmptyHistory);
        }
        const std::uint16_t idx = next_ == 0
            ? static_cast<std::uint16_t>(Window - 1u)
            : static_cast<std::uint16_t>(next_ - 1u);
        return snapshots_[idx];
    }

    [[nodiscard]] constexpr std::expected<NicTelemetryDrift, NicTelemetryError>
    detect_drift(NicTelemetryPolicy policy = {}) const noexcept {
        if (count_ == 0) {
            return std::unexpected(NicTelemetryError::EmptyHistory);
        }
        NicTelemetryDrift drift{
            .nic_uuid = snapshots_[oldest_index()].nic_uuid,
            .observed_samples = count_,
        };
        if (count_ < policy.min_drift_samples) {
            return drift;
        }
        auto const& oldest = snapshots_[oldest_index()];
        auto const& newest = snapshots_[next_ == 0
            ? static_cast<std::uint16_t>(Window - 1u)
            : static_cast<std::uint16_t>(next_ - 1u)];
        const double baseline = oldest.effective_bandwidth_bps.value();
        const double observed = newest.effective_bandwidth_bps.value();
        if (baseline <= 0.0 || observed >= baseline) {
            return drift;
        }
        const double drop = (baseline - observed) * 1'000'000.0 / baseline;
        drift.bandwidth_drop_ppm = static_cast<std::uint32_t>(
            std::min<double>(1'000'000.0, drop));
        drift.degraded = drift.bandwidth_drop_ppm >= policy.drift_drop_ppm;
        return drift;
    }
};

template <std::size_t Window, class Ctx>
    requires CtxFitsNicTelemetryMint<Ctx>
[[nodiscard]] constexpr NicTelemetryHistory<Window>
mint_nic_telemetry_history(Ctx const&) noexcept {
    static_assert(Window > 0, "mint_nic_telemetry_history requires Window > 0");
    return {};
}

static_assert(sizeof(ExternalTelemetryText) == sizeof(std::string_view));
static_assert(sizeof(DeclaredNetdevCounters) == sizeof(NetdevCounters));
static_assert(sizeof(DeclaredQdiscBacklog) == sizeof(QdiscBacklog));
static_assert(sizeof(DeclaredSysctlSnapshot) == sizeof(SysctlSnapshot));
static_assert(std::is_trivially_copyable_v<NetdevCounters>);
static_assert(std::is_trivially_copyable_v<QdiscBacklog>);
static_assert(std::is_trivially_copyable_v<SysctlSnapshot>);
static_assert(std::is_trivially_destructible_v<NicTelemetrySnapshot>);

}  // namespace crucible::topology

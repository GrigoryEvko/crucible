#pragma once

// NIC telemetry: the parsers of the text that the host reports, the
// snapshot of one NIC at one sequence number, and a bounded history of
// snapshots.
//
// foundation::reflect::enum_name gives the name of each enumerator.

#include <crucible/cog/CogIdentity.h>
#include <crucible/topology/CongestionTelemetry.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::topology {

using ExternalTelemetryText = ::fixy::Tagged<std::string_view, ::fixy::tags::source::External>;
using PositiveEffectiveBandwidthBps = ::fixy::Positive<double>;

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

using DeclaredNetdevCounters = ::fixy::Tagged<NetdevCounters, ::fixy::tags::source::KernelTelemetry>;
using DeclaredQdiscBacklog = ::fixy::Tagged<QdiscBacklog, ::fixy::tags::source::KernelTelemetry>;
using DeclaredSysctlSnapshot = ::fixy::Tagged<SysctlSnapshot, ::fixy::tags::source::KernelTelemetry>;
using DeclaredNicThermalSample = ::fixy::Tagged<NicThermalSample, ::fixy::tags::source::KernelTelemetry>;

struct NicTelemetryPolicy {
    std::uint32_t fairness_penalty_ppm = 100000;
    std::uint32_t drift_drop_ppm = 150000;
    std::uint16_t min_drift_samples = 2;
};

struct NicTelemetryDrift {
    cog::Uuid nic_uuid{};
    std::uint32_t bandwidth_drop_ppm = 0;
    std::uint16_t observed_samples = 0;
    bool degraded = false;
};

template <class Ctx>
concept CtxFitsNicTelemetryMint = ::foundation::effects::IsExecCtx<Ctx>
                               && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

template <class Ctx>
concept CtxFitsNicTelemetryRecord = ::foundation::effects::IsExecCtx<Ctx>
                                 && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Bg>;

// A history has no lock and no atomic, so a read is sound only on the
// side that owns it: startup before the background starts, and the
// background thread that records. The foreground row owns neither.
template <class Ctx>
concept CtxFitsNicTelemetryRead =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxOwnsAnyOf<Ctx, ::foundation::effects::Effect::Init, ::foundation::effects::Effect::Bg>;

[[nodiscard]] constexpr ExternalTelemetryText tag_external_telemetry_text(std::string_view text) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::External>(text);
}

[[nodiscard]] constexpr DeclaredNetdevCounters declare_netdev_counters(NetdevCounters counters) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::KernelTelemetry>(counters);
}

[[nodiscard]] constexpr DeclaredQdiscBacklog declare_qdisc_backlog(QdiscBacklog backlog) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::KernelTelemetry>(backlog);
}

[[nodiscard]] constexpr DeclaredSysctlSnapshot declare_sysctl_snapshot(SysctlSnapshot snapshot) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::KernelTelemetry>(snapshot);
}

[[nodiscard]] constexpr DeclaredNicThermalSample declare_nic_thermal_sample(NicThermalSample sample) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::KernelTelemetry>(sample);
}

[[nodiscard]] std::expected<DeclaredNetdevCounters, NicTelemetryError>
parse_netdev_counters(ExternalTelemetryText text) noexcept;

[[nodiscard]] std::expected<DeclaredQdiscBacklog, NicTelemetryError>
parse_qdisc_backlog(ExternalTelemetryText text) noexcept;

[[nodiscard]] std::expected<DeclaredSysctlSnapshot, NicTelemetryError>
parse_sysctl_snapshot(ExternalTelemetryText text) noexcept;

[[nodiscard]] constexpr std::uint64_t netdev_drop_ppm(NetdevCounters counters) noexcept {
    const std::uint64_t packets = counters.rx_packets + counters.tx_packets;
    const std::uint64_t dropped =
        counters.rx_dropped + counters.tx_dropped + counters.rx_fifo_errors + counters.tx_fifo_errors;
    if (packets == 0 || dropped == 0) {
        return 0;
    }
    const long double ppm = static_cast<long double>(dropped) * 1000000.0L / static_cast<long double>(packets);
    return static_cast<std::uint64_t>(std::min<long double>(1000000.0L, ppm));
}

[[nodiscard]] constexpr std::uint64_t sysctl_throughput_ceiling_bps(SysctlSnapshot sysctl,
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
    long double bps = static_cast<long double>(window) * 8000000.0L / static_cast<long double>(rtt_us);
    if (bps >= static_cast<long double>(UINT64_MAX)) {
        return UINT64_MAX;
    }
    return static_cast<std::uint64_t>(bps);
}

class NicTelemetrySnapshot;

template <std::size_t Window>
class NicTelemetryHistory;

[[nodiscard]] constexpr std::expected<NicTelemetrySnapshot, NicTelemetryError>
mint_nic_telemetry_snapshot(cog::CogIdentity const& nic, std::uint64_t line_rate_bps, DeclaredNetdevCounters netdev,
                            DeclaredQdiscBacklog qdisc, DeclaredSysctlSnapshot sysctl, TcpInfoSnapshot tcp,
                            DeclaredNicThermalSample thermal, std::uint64_t sequence,
                            NicTelemetryPolicy policy = {}) noexcept;

// One NIC at one sequence number. The mint is the only door: it checks
// that the cog is a NIC and computes the effective bandwidth from the
// samples, so the two cannot disagree. Callers read the fields through
// the accessors, and nothing outside the mint writes them.
class NicTelemetrySnapshot {
public:
    using line_rate_type = ::fixy::Tagged<std::uint64_t, ::fixy::tags::source::Calibrated>;
    using bandwidth_type = ::fixy::Tagged<double, ::fixy::tags::source::Calibrated>;

    [[nodiscard]] constexpr cog::Uuid nic_uuid() const noexcept { return nic_uuid_; }
    [[nodiscard]] constexpr line_rate_type const& line_rate_bps() const noexcept { return line_rate_bps_; }
    [[nodiscard]] constexpr ::fixy::Stale<DeclaredNetdevCounters> const& netdev() const noexcept { return netdev_; }
    [[nodiscard]] constexpr ::fixy::Stale<DeclaredQdiscBacklog> const& qdisc() const noexcept { return qdisc_; }
    [[nodiscard]] constexpr ::fixy::Stale<DeclaredSysctlSnapshot> const& sysctl() const noexcept { return sysctl_; }
    [[nodiscard]] constexpr ::fixy::Stale<TcpInfoSnapshot> const& tcp() const noexcept { return tcp_; }
    [[nodiscard]] constexpr ::fixy::Stale<DeclaredNicThermalSample> const& thermal() const noexcept { return thermal_; }
    [[nodiscard]] constexpr bandwidth_type const& effective_bandwidth_bps() const noexcept {
        return effective_bandwidth_bps_;
    }
    [[nodiscard]] constexpr std::uint64_t sequence() const noexcept { return sequence_; }

private:
    // The empty slot of a history. Only the history builds one, and it
    // never reads a slot that no record filled.
    constexpr NicTelemetrySnapshot() noexcept = default;

    constexpr NicTelemetrySnapshot(cog::Uuid nic_uuid, std::uint64_t line_rate_bps, DeclaredNetdevCounters netdev,
                                   DeclaredQdiscBacklog qdisc, DeclaredSysctlSnapshot sysctl, TcpInfoSnapshot tcp,
                                   DeclaredNicThermalSample thermal, std::uint64_t sequence) noexcept
        : nic_uuid_{nic_uuid},
          line_rate_bps_{::fixy::mint_tagged<::fixy::tags::source::Calibrated>(line_rate_bps)},
          netdev_{::fixy::Stale<DeclaredNetdevCounters>::at(netdev, sequence)},
          qdisc_{::fixy::Stale<DeclaredQdiscBacklog>::at(qdisc, sequence)},
          sysctl_{::fixy::Stale<DeclaredSysctlSnapshot>::at(sysctl, sequence)},
          tcp_{::fixy::Stale<TcpInfoSnapshot>::at(tcp, sequence)},
          thermal_{::fixy::Stale<DeclaredNicThermalSample>::at(thermal, sequence)},
          sequence_{sequence} {}

    friend constexpr std::expected<NicTelemetrySnapshot, NicTelemetryError>
    mint_nic_telemetry_snapshot(cog::CogIdentity const& nic, std::uint64_t line_rate_bps, DeclaredNetdevCounters netdev,
                                DeclaredQdiscBacklog qdisc, DeclaredSysctlSnapshot sysctl, TcpInfoSnapshot tcp,
                                DeclaredNicThermalSample thermal, std::uint64_t sequence,
                                NicTelemetryPolicy policy) noexcept;

    template <std::size_t Window>
    friend class NicTelemetryHistory;

    cog::Uuid nic_uuid_{};
    line_rate_type line_rate_bps_{};
    ::fixy::Stale<DeclaredNetdevCounters> netdev_{};
    ::fixy::Stale<DeclaredQdiscBacklog> qdisc_{};
    ::fixy::Stale<DeclaredSysctlSnapshot> sysctl_{};
    ::fixy::Stale<TcpInfoSnapshot> tcp_{};
    ::fixy::Stale<DeclaredNicThermalSample> thermal_{};
    bandwidth_type effective_bandwidth_bps_{};
    std::uint64_t sequence_ = 0;
};

// The base is at least one bit per second, so the positive mint passes.
[[nodiscard]] constexpr std::expected<PositiveEffectiveBandwidthBps, NicTelemetryError>
compute_effective_bandwidth(NicTelemetrySnapshot const& snapshot, NicTelemetryPolicy policy = {}) noexcept {
    auto const& tcp = snapshot.tcp().peek().value();
    const std::uint64_t rtt_us = std::max<std::uint64_t>(1, tcp.rt_prop_us.value());
    const std::uint64_t line_rate =
        snapshot.line_rate_bps().value() == 0 ? UINT64_MAX : snapshot.line_rate_bps().value();
    const std::uint64_t sysctl_ceiling = sysctl_throughput_ceiling_bps(snapshot.sysctl().peek().value(), rtt_us);
    const std::uint64_t tcp_btl = tcp.btl_bw_bps.value();

    long double base = static_cast<long double>(std::min({line_rate, sysctl_ceiling, tcp_btl}));
    const long double in_flight_bps =
        static_cast<long double>(tcp.in_flight_bytes) * 8000000.0L / static_cast<long double>(rtt_us);
    const long double penalty = in_flight_bps * static_cast<long double>(policy.fairness_penalty_ppm) / 1000000.0L;
    base = std::max<long double>(1.0L, base - penalty);
    return ::fixy::mint_refined<::fixy::positive>(static_cast<double>(base));
}

[[nodiscard]] constexpr std::expected<NicTelemetrySnapshot, NicTelemetryError>
mint_nic_telemetry_snapshot(cog::CogIdentity const& nic, std::uint64_t line_rate_bps, DeclaredNetdevCounters netdev,
                            DeclaredQdiscBacklog qdisc, DeclaredSysctlSnapshot sysctl, TcpInfoSnapshot tcp,
                            DeclaredNicThermalSample thermal, std::uint64_t sequence,
                            NicTelemetryPolicy policy) noexcept {
    if (!is_nic_cog(nic) || nic.uuid.is_zero()) {
        return std::unexpected(NicTelemetryError::InvalidNicCog);
    }
    NicTelemetrySnapshot snapshot{nic.uuid, line_rate_bps, netdev, qdisc, sysctl, tcp, thermal, sequence};
    auto effective = compute_effective_bandwidth(snapshot, policy);
    if (!effective.has_value()) {
        return std::unexpected(effective.error());
    }
    snapshot.effective_bandwidth_bps_ = ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(effective->value());
    return snapshot;
}

template <std::size_t Window, class Ctx>
    requires CtxFitsNicTelemetryMint<Ctx>
[[nodiscard]] constexpr NicTelemetryHistory<Window> mint_nic_telemetry_history(Ctx const&) noexcept;

// A ring of the last Window snapshots. The mint is the only door. Every
// write takes a background context, and every read takes a context that
// owns the history.
template <std::size_t Window>
class NicTelemetryHistory {
    static_assert(Window > 0, "NicTelemetryHistory requires at least one slot");
    static_assert(Window <= UINT16_MAX, "NicTelemetryHistory stores bounded history indices in uint16_t");

    std::array<NicTelemetrySnapshot, Window> snapshots_{};
    std::uint16_t count_ = 0;
    std::uint16_t next_ = 0;

    constexpr NicTelemetryHistory() noexcept = default;

    template <std::size_t W, class Ctx>
        requires CtxFitsNicTelemetryMint<Ctx>
    friend constexpr NicTelemetryHistory<W> mint_nic_telemetry_history(Ctx const&) noexcept;

    [[nodiscard]] constexpr std::uint16_t oldest_index() const noexcept {
        if (count_ < Window) {
            return 0;
        }
        return next_;
    }

    [[nodiscard]] constexpr std::uint16_t newest_index() const noexcept {
        return next_ == 0 ? static_cast<std::uint16_t>(Window - 1u) : static_cast<std::uint16_t>(next_ - 1u);
    }

public:
    template <class Ctx>
        requires CtxFitsNicTelemetryRead<Ctx>
    [[nodiscard]] constexpr std::uint16_t count(Ctx const&) const noexcept {
        return count_;
    }

    template <class Ctx>
        requires CtxFitsNicTelemetryRecord<Ctx>
    [[nodiscard]] constexpr NicTelemetryError record(Ctx const&, NicTelemetrySnapshot const& snapshot) noexcept {
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
        return snapshots_[newest_index()];
    }

    template <class Ctx>
        requires CtxFitsNicTelemetryRead<Ctx>
    [[nodiscard]] constexpr std::expected<NicTelemetryDrift, NicTelemetryError>
    detect_drift(Ctx const&, NicTelemetryPolicy policy = {}) const noexcept {
        if (count_ == 0) {
            return std::unexpected(NicTelemetryError::EmptyHistory);
        }
        auto const& oldest = snapshots_[oldest_index()];
        NicTelemetryDrift drift{
            .nic_uuid = oldest.nic_uuid(),
            .observed_samples = count_,
        };
        if (count_ < policy.min_drift_samples) {
            return drift;
        }
        auto const& newest = snapshots_[newest_index()];
        const double baseline = oldest.effective_bandwidth_bps().value();
        const double observed = newest.effective_bandwidth_bps().value();
        if (baseline <= 0.0 || observed >= baseline) {
            return drift;
        }
        const double drop = (baseline - observed) * 1000000.0 / baseline;
        drift.bandwidth_drop_ppm = static_cast<std::uint32_t>(std::min<double>(1000000.0, drop));
        drift.degraded = drift.bandwidth_drop_ppm >= policy.drift_drop_ppm;
        return drift;
    }
};

template <std::size_t Window, class Ctx>
    requires CtxFitsNicTelemetryMint<Ctx>
[[nodiscard]] constexpr NicTelemetryHistory<Window> mint_nic_telemetry_history(Ctx const&) noexcept {
    return NicTelemetryHistory<Window>{};
}

}  // namespace crucible::topology

#pragma once

// GAPS-134.  Topology pingmesh substrate.
//
// This header owns the bounded per-pair latency accounting surface used
// by later networking policy.  It does not open sockets, timestamp
// packets, gossip snapshots, or export telemetry.  Transport workers and
// future PTP/Scuttlebutt/OTel layers feed this substrate with admitted
// measurements carrying source::Pingmesh provenance.

#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/observe/HdrHistogram.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::topology {

using PositivePingmeshPeerCount = safety::Positive<std::uint16_t>;
using PositivePingmeshPeriodNs = safety::Positive<std::uint64_t>;
using PositivePingmeshProbeBytes = safety::Positive<std::uint16_t>;
using PositivePingmeshLatencyNs = safety::Positive<std::uint64_t>;
using PositivePingmeshZScoreMilli = safety::Positive<std::uint32_t>;

enum class PingmeshProbeStatus : std::uint8_t {
    Delivered = 0,
    Lost = 1,
    Rejected = 2,
};

[[nodiscard]] constexpr std::string_view
pingmesh_probe_status_name(PingmeshProbeStatus status) noexcept {
    switch (status) {
        case PingmeshProbeStatus::Delivered: return "Delivered";
        case PingmeshProbeStatus::Lost:      return "Lost";
        case PingmeshProbeStatus::Rejected:  return "Rejected";
        default:                             return "<unknown PingmeshProbeStatus>";
    }
}

enum class PingmeshError : std::uint8_t {
    None = 0,
    ZeroPeer = 1,
    DuplicatePeer = 2,
    Full = 3,
    UnknownPeer = 4,
    SelfPair = 5,
    DisabledPair = 6,
    LatencyOutOfRange = 7,
    InsufficientPeers = 8,
};

[[nodiscard]] constexpr std::string_view
pingmesh_error_name(PingmeshError error) noexcept {
    switch (error) {
        case PingmeshError::None:              return "None";
        case PingmeshError::ZeroPeer:          return "ZeroPeer";
        case PingmeshError::DuplicatePeer:     return "DuplicatePeer";
        case PingmeshError::Full:              return "Full";
        case PingmeshError::UnknownPeer:       return "UnknownPeer";
        case PingmeshError::SelfPair:          return "SelfPair";
        case PingmeshError::DisabledPair:      return "DisabledPair";
        case PingmeshError::LatencyOutOfRange: return "LatencyOutOfRange";
        case PingmeshError::InsufficientPeers: return "InsufficientPeers";
        default:                               return "<unknown PingmeshError>";
    }
}

struct PingmeshConfig {
    PositivePingmeshPeerCount max_peer_count{std::uint16_t{256}};
    PositivePingmeshPeriodNs period_ns{std::uint64_t{5'000'000'000ull}};
    PositivePingmeshProbeBytes probe_size_bytes{std::uint16_t{64}};
    PositivePingmeshZScoreMilli anomaly_zscore_milli{std::uint32_t{3'000}};
    bool prefer_hardware_timestamp = true;
};

struct PingmeshMeasurement {
    cog::Uuid src{};
    cog::Uuid dst{};
    PositivePingmeshLatencyNs latency_ns{std::uint64_t{1}};
    std::uint64_t sequence = 0;
    PingmeshProbeStatus status = PingmeshProbeStatus::Delivered;

    [[nodiscard]] constexpr bool delivered() const noexcept {
        return status == PingmeshProbeStatus::Delivered;
    }
};

using DeclaredPingmeshMeasurement =
    safety::Tagged<PingmeshMeasurement, safety::source::Pingmesh>;

struct PingmeshPairStats {
    cog::Uuid src{};
    cog::Uuid dst{};
    std::uint64_t sent = 0;
    std::uint64_t delivered = 0;
    std::uint64_t lost = 0;
    std::uint64_t rejected = 0;
    std::uint64_t last_sequence = 0;
    std::uint64_t p50_latency_ns = 0;
    std::uint64_t p99_latency_ns = 0;
    std::uint64_t mean_latency_ns = 0;
    std::uint64_t stddev_latency_ns = 0;
};

struct PingmeshAnomaly {
    PingmeshPairStats stats{};
    std::uint32_t zscore_milli = 0;
    bool anomalous = false;
};

template <std::size_t MaxPairs>
struct PingmeshAnomalyReport {
    std::array<PingmeshAnomaly, MaxPairs> entries{};
    std::size_t count = 0;
};

template <class Ctx>
concept CtxFitsPingmeshMint =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsPingmeshRecord =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Bg>>;

namespace detail {

struct AtomicPingmeshPairCounters {
    std::atomic<std::uint64_t> sent{0};
    std::atomic<std::uint64_t> delivered{0};
    std::atomic<std::uint64_t> lost{0};
    std::atomic<std::uint64_t> rejected{0};
    std::atomic<std::uint64_t> last_sequence{0};
};

[[nodiscard]] constexpr std::uint32_t
zscore_milli(std::uint64_t value,
             std::uint64_t mean,
             std::uint64_t stddev) noexcept {
    if (stddev == 0 || value <= mean) {
        return 0;
    }
    auto const delta = value - mean;
    if (delta > UINT64_MAX / 1000u) {
        return UINT32_MAX;
    }
    auto const scaled = (delta * 1000u) / stddev;
    return scaled > UINT32_MAX ? UINT32_MAX : static_cast<std::uint32_t>(scaled);
}

}  // namespace detail

template <
    std::size_t MaxPeers,
    std::uint8_t Significant = 2,
    std::uint64_t MaxLatencyNs = 60'000'000'000ull>
class Pingmesh : public safety::Pinned<Pingmesh<MaxPeers, Significant, MaxLatencyNs>> {
    static_assert(MaxPeers > 1, "Pingmesh<MaxPeers> requires at least two peers.");
    static_assert(MaxPeers <= 256,
        "Pingmesh keeps a fixed all-pairs matrix; shard fleets above 256 peers.");

public:
    using histogram_type = observe::HdrHistogram<Significant, MaxLatencyNs>;
    static constexpr std::size_t max_peers = MaxPeers;
    static constexpr std::size_t max_pairs = MaxPeers * MaxPeers;
    static constexpr std::uint64_t max_latency_ns = MaxLatencyNs;

    struct PeerSlot {
        cog::CogIdentity peer{};
        bool active = false;
    };

private:
    PingmeshConfig config_{};
    std::array<PeerSlot, MaxPeers> peers_{};
    std::array<bool, max_pairs> enabled_pairs_{};
    std::array<histogram_type, max_pairs> latency_{};
    std::array<detail::AtomicPingmeshPairCounters, max_pairs> counters_{};

    [[nodiscard]] static constexpr std::size_t
    pair_index(std::size_t src, std::size_t dst) noexcept {
        return src * MaxPeers + dst;
    }

    [[nodiscard]] std::size_t find_peer(cog::Uuid uuid) const noexcept {
        for (std::size_t i = 0; i < MaxPeers; ++i) {
            if (peers_[i].active && peers_[i].peer.uuid == uuid) {
                return i;
            }
        }
        return MaxPeers;
    }

    [[nodiscard]] PingmeshError
    validate_pair(std::size_t src, std::size_t dst) const noexcept {
        if (src == MaxPeers || dst == MaxPeers) {
            return PingmeshError::UnknownPeer;
        }
        if (src == dst) {
            return PingmeshError::SelfPair;
        }
        if (!enabled_pairs_[pair_index(src, dst)]) {
            return PingmeshError::DisabledPair;
        }
        return PingmeshError::None;
    }

public:
    explicit Pingmesh(PingmeshConfig config = {}) noexcept
        : config_{config} {}

    [[nodiscard]] PingmeshConfig config() const noexcept { return config_; }

    [[nodiscard]] std::span<const PeerSlot, MaxPeers>
    peers() const noexcept {
        return std::span<const PeerSlot, MaxPeers>{peers_};
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsPingmeshMint<Ctx>
    [[nodiscard]] PingmeshError
    start_probing(Ctx const&, std::span<const cog::CogIdentity> peers) noexcept {
        if (peers.size() < 2) {
            return PingmeshError::InsufficientPeers;
        }
        if (peers.size() > MaxPeers || peers.size() > config_.max_peer_count.value()) {
            return PingmeshError::Full;
        }

        for (std::size_t i = 0; i < peers.size(); ++i) {
            if (peers[i].uuid.is_zero()) {
                return PingmeshError::ZeroPeer;
            }
            if (find_peer(peers[i].uuid) != MaxPeers) {
                return PingmeshError::DuplicatePeer;
            }
            for (std::size_t j = i + 1; j < peers.size(); ++j) {
                if (peers[i].uuid == peers[j].uuid) {
                    return PingmeshError::DuplicatePeer;
                }
            }
        }

        for (auto const& peer : peers) {
            auto const err = register_peer(peer);
            if (err != PingmeshError::None) {
                return err;
            }
        }
        enable_all_registered_pairs();
        return PingmeshError::None;
    }

    [[nodiscard]] PingmeshError
    register_peer(cog::CogIdentity peer) noexcept {
        if (peer.uuid.is_zero()) {
            return PingmeshError::ZeroPeer;
        }
        if (find_peer(peer.uuid) != MaxPeers) {
            return PingmeshError::DuplicatePeer;
        }
        for (auto& slot : peers_) {
            if (!slot.active) {
                slot.peer = peer;
                slot.active = true;
                return PingmeshError::None;
            }
        }
        return PingmeshError::Full;
    }

    void enable_all_registered_pairs() noexcept {
        for (std::size_t src = 0; src < MaxPeers; ++src) {
            for (std::size_t dst = 0; dst < MaxPeers; ++dst) {
                enabled_pairs_[pair_index(src, dst)] =
                    src != dst && peers_[src].active && peers_[dst].active;
            }
        }
    }

    [[nodiscard]] PingmeshError
    enable_pair(cog::Uuid src, cog::Uuid dst) noexcept {
        auto const src_i = find_peer(src);
        auto const dst_i = find_peer(dst);
        auto const err = validate_pair(src_i, dst_i);
        if (err == PingmeshError::DisabledPair) {
            enabled_pairs_[pair_index(src_i, dst_i)] = true;
            return PingmeshError::None;
        }
        return err;
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsPingmeshRecord<Ctx>
    [[nodiscard]] PingmeshError
    record_measurement(Ctx const&,
                       DeclaredPingmeshMeasurement measurement) noexcept {
        auto const& value = measurement.value();
        auto const src = find_peer(value.src);
        auto const dst = find_peer(value.dst);
        auto const pair_err = validate_pair(src, dst);
        if (pair_err != PingmeshError::None) {
            return pair_err;
        }

        auto const index = pair_index(src, dst);
        auto& counters = counters_[index];
        counters.sent.fetch_add(1, std::memory_order_relaxed);
        counters.last_sequence.store(value.sequence, std::memory_order_release);

        if (value.status == PingmeshProbeStatus::Lost) {
            counters.lost.fetch_add(1, std::memory_order_relaxed);
            return PingmeshError::None;
        }
        if (value.status == PingmeshProbeStatus::Rejected) {
            counters.rejected.fetch_add(1, std::memory_order_relaxed);
            return PingmeshError::None;
        }

        auto const latency_ns = value.latency_ns.value();
        if (latency_ns > MaxLatencyNs) {
            counters.rejected.fetch_add(1, std::memory_order_relaxed);
            return PingmeshError::LatencyOutOfRange;
        }
        latency_[index].record(histogram_type::checked_value(latency_ns));
        counters.delivered.fetch_add(1, std::memory_order_relaxed);
        return PingmeshError::None;
    }

    [[nodiscard]] histogram_type const*
    per_pair_latency(cog::Uuid src, cog::Uuid dst) const noexcept {
        auto const src_i = find_peer(src);
        auto const dst_i = find_peer(dst);
        if (validate_pair(src_i, dst_i) != PingmeshError::None) {
            return nullptr;
        }
        return &latency_[pair_index(src_i, dst_i)];
    }

    [[nodiscard]] PingmeshPairStats
    pair_stats(cog::Uuid src, cog::Uuid dst) const noexcept {
        auto const src_i = find_peer(src);
        auto const dst_i = find_peer(dst);
        if (validate_pair(src_i, dst_i) != PingmeshError::None) {
            return {};
        }

        auto const index = pair_index(src_i, dst_i);
        auto const& counters = counters_[index];
        auto const& hist = latency_[index];
        return PingmeshPairStats{
            .src = src,
            .dst = dst,
            .sent = counters.sent.load(std::memory_order_relaxed),
            .delivered = counters.delivered.load(std::memory_order_relaxed),
            .lost = counters.lost.load(std::memory_order_relaxed),
            .rejected = counters.rejected.load(std::memory_order_relaxed),
            .last_sequence = counters.last_sequence.load(std::memory_order_acquire),
            .p50_latency_ns = hist.percentile(50.0),
            .p99_latency_ns = hist.percentile(99.0),
            .mean_latency_ns = hist.mean(),
            .stddev_latency_ns = hist.std_dev(),
        };
    }

    // Cumulative substrate-level detector.  The `window` argument is a
    // reserved API hook for the future rolling-snapshot side channel; this
    // carrier intentionally stores no wall-clock buckets yet.
    [[nodiscard]] PingmeshAnomalyReport<max_pairs>
    detect_anomalies(std::chrono::nanoseconds /* window */ = {}) const noexcept {
        PingmeshAnomalyReport<max_pairs> report{};
        auto const threshold = config_.anomaly_zscore_milli.value();
        for (std::size_t src = 0; src < MaxPeers; ++src) {
            for (std::size_t dst = 0; dst < MaxPeers; ++dst) {
                auto const index = pair_index(src, dst);
                if (!enabled_pairs_[index]) {
                    continue;
                }
                auto stats = pair_stats(peers_[src].peer.uuid, peers_[dst].peer.uuid);
                auto const z = detail::zscore_milli(
                    stats.p99_latency_ns, stats.mean_latency_ns, stats.stddev_latency_ns);
                if (z >= threshold || stats.lost != 0 || stats.rejected != 0) {
                    report.entries[report.count++] = PingmeshAnomaly{
                        .stats = stats,
                        .zscore_milli = z,
                        .anomalous = true,
                    };
                }
            }
        }
        return report;
    }
};

template <effects::IsExecCtx Ctx,
          std::size_t MaxPeers,
          std::uint8_t Significant = 2,
          std::uint64_t MaxLatencyNs = 60'000'000'000ull>
    requires CtxFitsPingmeshMint<Ctx>
[[nodiscard]] Pingmesh<MaxPeers, Significant, MaxLatencyNs>
mint_pingmesh(Ctx const&, PingmeshConfig config = {}) noexcept {
    return Pingmesh<MaxPeers, Significant, MaxLatencyNs>{config};
}

static_assert(!CtxFitsPingmeshMint<effects::BgDrainCtx>);
static_assert(CtxFitsPingmeshMint<effects::ColdInitCtx>);
static_assert(!CtxFitsPingmeshRecord<effects::HotFgCtx>);
static_assert(CtxFitsPingmeshRecord<effects::BgDrainCtx>);
static_assert(std::is_base_of_v<safety::Pinned<Pingmesh<2>>, Pingmesh<2>>);

}  // namespace crucible::topology

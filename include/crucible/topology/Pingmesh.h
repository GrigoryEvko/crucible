#pragma once

#include <crucible/cog/CogIdentity.h>
#include <crucible/observe/HdrHistogram.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>
#include <foundation/core/Array.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::topology {

using PositivePingmeshPeerCount = ::fixy::Positive<std::uint16_t>;
using PositivePingmeshPeriodNs = ::fixy::Positive<std::uint64_t>;
using PositivePingmeshProbeBytes = ::fixy::Positive<std::uint16_t>;
using PositivePingmeshLatencyNs = ::fixy::Positive<std::uint64_t>;
using PositivePingmeshZScoreMilli = ::fixy::Positive<std::uint32_t>;

// foundation::reflect::enum_name gives the log spelling of both enums.
enum class PingmeshProbeStatus : std::uint8_t {
    Delivered = 0,
    Lost = 1,
    Rejected = 2,
};

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

struct PingmeshConfig {
    PositivePingmeshPeerCount max_peer_count = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{256});
    PositivePingmeshPeriodNs period_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{5000000000ull});
    PositivePingmeshProbeBytes probe_size_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{64});
    PositivePingmeshZScoreMilli anomaly_zscore_milli = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{3000});
    bool prefer_hardware_timestamp = true;
};

struct PingmeshMeasurement {
    cog::Uuid src{};
    cog::Uuid dst{};
    PositivePingmeshLatencyNs latency_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1});
    std::uint64_t sequence = 0;
    PingmeshProbeStatus status = PingmeshProbeStatus::Delivered;

    [[nodiscard]] constexpr bool delivered() const noexcept { return status == PingmeshProbeStatus::Delivered; }
};

// A measurement that a probe worker reported.  The worker names the
// source with mint_tagged, and record_measurement takes only this type.
using DeclaredPingmeshMeasurement = ::fixy::Tagged<PingmeshMeasurement, ::fixy::tags::source::Pingmesh>;

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

// Each byte of the anomaly is a member, so a report of many anomalies has
// no padding byte.
struct PingmeshAnomaly {
    PingmeshPairStats stats{};
    std::uint32_t zscore_milli = 0;
    bool anomalous = false;
    ::foundation::core::FixedArray<std::uint8_t, 3> pad_tail{};
};

// The anomalous pairs of one detection, in the order of the pair grid.
template <std::size_t MaxPairs>
struct PingmeshAnomalyReport {
    ::fixy::FixedList<PingmeshAnomaly, MaxPairs> entries{};
};

template <class Ctx>
concept CtxFitsPingmeshMint =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsPingmeshRecord =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Bg>>;

namespace detail {

// One of these structs sits in every cell of an all-pairs grid, and separate
// threads record into neighbouring cells at the same time.  Without the
// alignment two cells share one cache line and those threads then contend for
// it.  The five cells inside one struct are written by the single thread that
// recorded that pair, so keeping them together is wanted.  The alignment
// separates cells from each other and nothing else.
struct alignas(64) AtomicPingmeshPairCounters {
    ::fixy::Tally sent{};
    ::fixy::Tally delivered{};
    ::fixy::Tally lost{};
    ::fixy::Tally rejected{};
    ::fixy::Atomic<std::uint64_t> last_sequence{};
};

[[nodiscard]] constexpr std::uint32_t zscore_milli(std::uint64_t value, std::uint64_t mean,
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

template <std::size_t MaxPeers, std::uint8_t Significant = 2, std::uint64_t MaxLatencyNs = 60000000000ull>
class Pingmesh;

// The only door into a pingmesh.  An Init-row context mints it and starts
// probing; a background probe worker then records measurements into it.
template <::foundation::effects::IsExecCtx Ctx, std::size_t MaxPeers, std::uint8_t Significant = 2,
          std::uint64_t MaxLatencyNs = 60000000000ull>
    requires CtxFitsPingmeshMint<Ctx>
[[nodiscard]] constexpr Pingmesh<MaxPeers, Significant, MaxLatencyNs>
mint_pingmesh(Ctx const&, PingmeshConfig config = {}) noexcept;

template <std::size_t MaxPeers, std::uint8_t Significant, std::uint64_t MaxLatencyNs>
class Pingmesh : public ::foundation::Pinned<Pingmesh<MaxPeers, Significant, MaxLatencyNs>> {
    static_assert(MaxPeers > 1, "Pingmesh<MaxPeers> requires at least two peers.");
    static_assert(MaxPeers <= 256, "Pingmesh keeps a fixed all-pairs matrix; shard fleets above 256 peers.");

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
    // The slots of the tables: MaxPeers, and two slots for a MaxPeers below
    // two, so such a MaxPeers fails only at its static_assert above.
    static constexpr std::size_t kPeerSlots = MaxPeers > 1 ? MaxPeers : 2;
    static constexpr std::size_t kPairSlots = kPeerSlots * kPeerSlots;

    using PeerIndex = ::fixy::Index<kPeerSlots>;
    using PairIndex = ::fixy::Index<kPairSlots>;

    PingmeshConfig config_{};
    ::foundation::core::FixedArray<PeerSlot, kPeerSlots> peers_{};
    ::foundation::core::FixedArray<bool, kPairSlots> enabled_pairs_{};
    ::foundation::core::FixedArray<histogram_type, kPairSlots> latency_{};
    ::foundation::core::FixedArray<detail::AtomicPingmeshPairCounters, kPairSlots> counters_{};

    // The cell of the directed pair from src to dst.  The product of two
    // indexes below kPeerSlots is below kPairSlots, so the check cannot fail.
    [[nodiscard]] static constexpr PairIndex pair_index(PeerIndex src, PeerIndex dst) noexcept {
        return PairIndex::check(src.raw() * kPeerSlots + dst.raw())
            .expect("the cell of two peer indexes is inside the pair grid");
    }

    // The index of the active slot of the peer, or no value.  Complexity:
    // linear in the peers.
    [[nodiscard]] ::fixy::Option<PeerIndex> find_peer(cog::Uuid uuid) const noexcept {
        for (PeerIndex const index : peers_.indices()) {
            PeerSlot const& slot = peers_.at(index);
            if (slot.active && slot.peer.uuid == uuid) {
                return ::fixy::some(index);
            }
        }
        return ::fixy::none;
    }

    // The cell of the pair from src to dst, two different registered peers.
    [[nodiscard]] ::fixy::Result<PairIndex, PingmeshError> locate_pair(cog::Uuid src, cog::Uuid dst) const noexcept {
        CRUCIBLE_TRY(src_index, find_peer(src).ok_or(PingmeshError::UnknownPeer));
        CRUCIBLE_TRY(dst_index, find_peer(dst).ok_or(PingmeshError::UnknownPeer));
        if (src_index == dst_index) {
            return ::fixy::err(PingmeshError::SelfPair);
        }
        return pair_index(src_index, dst_index);
    }

    // The cell of the pair from src to dst, when the pair is enabled.
    [[nodiscard]] ::fixy::Result<PairIndex, PingmeshError> enabled_pair(cog::Uuid src, cog::Uuid dst) const noexcept {
        CRUCIBLE_TRY(index, locate_pair(src, dst));
        if (!enabled_pairs_.at(index)) {
            return ::fixy::err(PingmeshError::DisabledPair);
        }
        return index;
    }

    template <::foundation::effects::IsExecCtx Ctx, std::size_t Peers, std::uint8_t Sig, std::uint64_t MaxNs>
        requires CtxFitsPingmeshMint<Ctx>
    friend constexpr Pingmesh<Peers, Sig, MaxNs> mint_pingmesh(Ctx const&, PingmeshConfig config) noexcept;

    constexpr explicit Pingmesh(PingmeshConfig config) noexcept : config_{config} {}

    // The two steps of start_probing.  They write the peer table and the
    // pair mask that record_measurement reads, so only start_probing, under
    // an Init-row context, reaches them.
    [[nodiscard]] PingmeshError register_peer(cog::CogIdentity peer) noexcept {
        if (peer.uuid.is_zero()) {
            return PingmeshError::ZeroPeer;
        }
        if (find_peer(peer.uuid).is_some()) {
            return PingmeshError::DuplicatePeer;
        }
        for (PeerSlot& slot : peers_) {
            if (!slot.active) {
                slot.peer = peer;
                slot.active = true;
                return PingmeshError::None;
            }
        }
        return PingmeshError::Full;
    }

    void enable_all_registered_pairs() noexcept {
        for (PeerIndex const src : peers_.indices()) {
            for (PeerIndex const dst : peers_.indices()) {
                enabled_pairs_.at(pair_index(src, dst)) = src != dst && peers_.at(src).active && peers_.at(dst).active;
            }
        }
    }

    // The statistics of the cell of the pair from src to dst.
    [[nodiscard]] PingmeshPairStats stats_of(PairIndex index, cog::Uuid src, cog::Uuid dst) const noexcept {
        detail::AtomicPingmeshPairCounters const& counters = counters_.at(index);
        histogram_type const& hist = latency_.at(index);
        return PingmeshPairStats{
            .src = src,
            .dst = dst,
            .sent = counters.sent.read(),
            .delivered = counters.delivered.read(),
            .lost = counters.lost.read(),
            .rejected = counters.rejected.read(),
            .last_sequence = counters.last_sequence.load_acquire(),
            .p50_latency_ns = hist.percentile(50.0),
            .p99_latency_ns = hist.percentile(99.0),
            .mean_latency_ns = hist.mean(),
            .stddev_latency_ns = hist.std_dev(),
        };
    }

public:
    [[nodiscard]] PingmeshConfig config() const noexcept { return config_; }

    [[nodiscard]] ::fixy::View<PeerSlot const, kPeerSlots> peers() const noexcept { return peers_.view(); }

    // Complexity: quadratic in the peers, for the test of duplicates.
    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsPingmeshMint<Ctx>
    [[nodiscard]] PingmeshError start_probing(Ctx const&, ::fixy::View<cog::CogIdentity const> peers) noexcept {
        if (peers.size() < 2) {
            return PingmeshError::InsufficientPeers;
        }
        if (peers.size() > MaxPeers || peers.size() > config_.max_peer_count.value()) {
            return PingmeshError::Full;
        }

        std::size_t position = 0;
        for (cog::CogIdentity const& peer : peers) {
            if (peer.uuid.is_zero()) {
                return PingmeshError::ZeroPeer;
            }
            if (find_peer(peer.uuid).is_some()) {
                return PingmeshError::DuplicatePeer;
            }
            std::size_t other_position = 0;
            for (cog::CogIdentity const& other : peers) {
                if (other_position > position && other.uuid == peer.uuid) {
                    return PingmeshError::DuplicatePeer;
                }
                ++other_position;
            }
            ++position;
        }

        for (cog::CogIdentity const& peer : peers) {
            PingmeshError const error = register_peer(peer);
            if (error != PingmeshError::None) {
                return error;
            }
        }
        enable_all_registered_pairs();
        return PingmeshError::None;
    }

    // Turns on one directed pair between two registered peers.  It writes
    // the pair mask that record_measurement reads, so it takes the same
    // Init-row context as start_probing.
    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsPingmeshMint<Ctx>
    [[nodiscard]] PingmeshError enable_pair(Ctx const&, cog::Uuid src, cog::Uuid dst) noexcept {
        return locate_pair(src, dst).match(
            [&](PairIndex index) {
                enabled_pairs_.at(index) = true;
                return PingmeshError::None;
            },
            [](PingmeshError error) { return error; });
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsPingmeshRecord<Ctx>
    [[nodiscard]] PingmeshError record_measurement(Ctx const&,
                                                   DeclaredPingmeshMeasurement const& measurement) noexcept {
        PingmeshMeasurement const& value = measurement.value();
        return enabled_pair(value.src, value.dst)
            .match(
                [&](PairIndex index) {
                    detail::AtomicPingmeshPairCounters& counters = counters_.at(index);
                    counters.sent.add(1);
                    counters.last_sequence.store_release(value.sequence);

                    if (value.status == PingmeshProbeStatus::Lost) {
                        counters.lost.add(1);
                        return PingmeshError::None;
                    }
                    if (value.status == PingmeshProbeStatus::Rejected) {
                        counters.rejected.add(1);
                        return PingmeshError::None;
                    }

                    std::uint64_t const latency_ns = value.latency_ns.value();
                    if (latency_ns > MaxLatencyNs) {
                        counters.rejected.add(1);
                        return PingmeshError::LatencyOutOfRange;
                    }
                    latency_.at(index).record(histogram_type::checked_value(latency_ns));
                    counters.delivered.add(1);
                    return PingmeshError::None;
                },
                [](PingmeshError error) { return error; });
    }

    // The latency histogram of the enabled pair from src to dst, or no value.
    [[nodiscard]] ::fixy::Option<::fixy::Ref<histogram_type const>> per_pair_latency(cog::Uuid src,
                                                                                     cog::Uuid dst) const noexcept {
        using Latency = ::fixy::Option<::fixy::Ref<histogram_type const>>;
        ::fixy::Result<PairIndex, PingmeshError> const located = enabled_pair(src, dst);
        return located.match([&](PairIndex const& index) { return Latency::some(::fixy::to(latency_.at(index))); },
                             [](PingmeshError const&) { return Latency{::fixy::none}; });
    }

    [[nodiscard]] PingmeshPairStats pair_stats(cog::Uuid src, cog::Uuid dst) const noexcept {
        return enabled_pair(src, dst).match([&](PairIndex index) { return stats_of(index, src, dst); },
                                            [](PingmeshError) { return PingmeshPairStats{}; });
    }

    // The detector is cumulative, because this carrier keeps no
    // time-bucketed history.  Complexity: linear in the pairs.
    [[nodiscard]] PingmeshAnomalyReport<max_pairs> detect_anomalies() const noexcept {
        PingmeshAnomalyReport<max_pairs> report{};
        auto const threshold = config_.anomaly_zscore_milli.value();
        for (PeerIndex const src : peers_.indices()) {
            for (PeerIndex const dst : peers_.indices()) {
                PairIndex const index = pair_index(src, dst);
                if (!enabled_pairs_.at(index)) {
                    continue;
                }
                PingmeshPairStats const stats = stats_of(index, peers_.at(src).peer.uuid, peers_.at(dst).peer.uuid);
                auto const z =
                    detail::zscore_milli(stats.p99_latency_ns, stats.mean_latency_ns, stats.stddev_latency_ns);
                if (z >= threshold || stats.lost != 0 || stats.rejected != 0) {
                    // The report holds one entry for each pair, so the push
                    // cannot find the report full.
                    static_cast<void>(report.entries.push(PingmeshAnomaly{
                        .stats = stats,
                        .zscore_milli = z,
                        .anomalous = true,
                    }));
                }
            }
        }
        return report;
    }
};

template <::foundation::effects::IsExecCtx Ctx, std::size_t MaxPeers, std::uint8_t Significant,
          std::uint64_t MaxLatencyNs>
    requires CtxFitsPingmeshMint<Ctx>
[[nodiscard]] constexpr Pingmesh<MaxPeers, Significant, MaxLatencyNs> mint_pingmesh(Ctx const&,
                                                                                    PingmeshConfig config) noexcept {
    return Pingmesh<MaxPeers, Significant, MaxLatencyNs>{config};
}

}  // namespace crucible::topology

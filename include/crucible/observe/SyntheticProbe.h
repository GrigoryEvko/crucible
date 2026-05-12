#pragma once

// GAPS-141.  Synthetic per-peer/per-transport probe substrate.
//
// This header owns the fixed-size registration and statistics surface.
// It does not execute RDMA, AF_XDP, QUIC, TCP, collective, or federation
// traffic; those transport-specific executors land with their respective
// CNT/PERF tasks and feed this runner with measured outcomes.

#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/observe/Observation.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::observe {

using PositiveProbeCount = safety::Positive<std::uint16_t>;
using PositiveProbePeriodNs = safety::Positive<std::uint64_t>;

enum class TransportProbeKind : std::uint32_t {
    RdmaWrite                 = 1u <<  0,
    RdmaSend                  = 1u <<  1,
    RdmaRead                  = 1u <<  2,
    AfXdp                     = 1u <<  3,
    Quic                      = 1u <<  4,
    TcpBbr3                   = 1u <<  5,
    TcpCubic                  = 1u <<  6,
    TcpDctcp                  = 1u <<  7,
    CollectiveSmallAllReduce  = 1u <<  8,
    CollectiveSmallAllGather  = 1u <<  9,
    FederationMtls            = 1u << 10,
};

inline constexpr std::array all_transport_probe_kinds{
    TransportProbeKind::RdmaWrite,
    TransportProbeKind::RdmaSend,
    TransportProbeKind::RdmaRead,
    TransportProbeKind::AfXdp,
    TransportProbeKind::Quic,
    TransportProbeKind::TcpBbr3,
    TransportProbeKind::TcpCubic,
    TransportProbeKind::TcpDctcp,
    TransportProbeKind::CollectiveSmallAllReduce,
    TransportProbeKind::CollectiveSmallAllGather,
    TransportProbeKind::FederationMtls,
};

[[nodiscard]] constexpr std::string_view
transport_probe_kind_name(TransportProbeKind kind) noexcept {
    switch (kind) {
        case TransportProbeKind::RdmaWrite:                return "RdmaWrite";
        case TransportProbeKind::RdmaSend:                 return "RdmaSend";
        case TransportProbeKind::RdmaRead:                 return "RdmaRead";
        case TransportProbeKind::AfXdp:                    return "AfXdp";
        case TransportProbeKind::Quic:                     return "Quic";
        case TransportProbeKind::TcpBbr3:                  return "TcpBbr3";
        case TransportProbeKind::TcpCubic:                 return "TcpCubic";
        case TransportProbeKind::TcpDctcp:                 return "TcpDctcp";
        case TransportProbeKind::CollectiveSmallAllReduce: return "CollectiveSmallAllReduce";
        case TransportProbeKind::CollectiveSmallAllGather: return "CollectiveSmallAllGather";
        case TransportProbeKind::FederationMtls:           return "FederationMtls";
        default:                                           return "<unknown TransportProbeKind>";
    }
}

enum class SyntheticProbeFailureClass : std::uint8_t {
    None = 0,
    Timeout = 1,
    Refused = 2,
    Handshake = 3,
    Completion = 4,
    Permission = 5,
    DataMismatch = 6,
    TransportUnavailable = 7,
};

[[nodiscard]] constexpr std::string_view
synthetic_probe_failure_name(SyntheticProbeFailureClass failure) noexcept {
    switch (failure) {
        case SyntheticProbeFailureClass::None:                 return "None";
        case SyntheticProbeFailureClass::Timeout:              return "Timeout";
        case SyntheticProbeFailureClass::Refused:              return "Refused";
        case SyntheticProbeFailureClass::Handshake:            return "Handshake";
        case SyntheticProbeFailureClass::Completion:           return "Completion";
        case SyntheticProbeFailureClass::Permission:           return "Permission";
        case SyntheticProbeFailureClass::DataMismatch:         return "DataMismatch";
        case SyntheticProbeFailureClass::TransportUnavailable: return "TransportUnavailable";
        default:                                               return "<unknown SyntheticProbeFailureClass>";
    }
}

struct SyntheticProbeFailure : safety::diag::tag_base {
    static constexpr std::string_view name = "SyntheticProbeFailure";
    static constexpr std::string_view description =
        "A synthetic transport probe failed for a peer/protocol pair.";
    static constexpr std::string_view remediation =
        "Route the failure class into Health/Quarantine policy and "
        "inspect the concrete transport executor that produced it.";
};

struct ProbeConfig {
    PositiveProbePeriodNs period_per_kind_ns{std::uint64_t{60'000'000'000ull}};
    PositiveProbeCount max_peer_count{std::uint16_t{1}};
    std::uint32_t metric_id_base = 0;
};

struct ProbeOutcome {
    TransportProbeKind kind = TransportProbeKind::TcpCubic;
    SyntheticProbeFailureClass failure = SyntheticProbeFailureClass::None;
    std::uint64_t latency_ns = 0;
    std::uint64_t bytes_transferred = 0;
    std::uint64_t sequence = 0;

    [[nodiscard]] constexpr bool ok() const noexcept {
        return failure == SyntheticProbeFailureClass::None;
    }
};

struct ProbeStats {
    std::uint64_t scheduled = 0;
    std::uint64_t succeeded = 0;
    std::uint64_t failed = 0;
    std::uint64_t timed_out = 0;
    std::uint64_t bytes_transferred = 0;
    std::uint64_t last_latency_ns = 0;
    std::uint64_t last_sequence = 0;
    SyntheticProbeFailureClass last_failure = SyntheticProbeFailureClass::None;
};

template <class Ctx>
concept CtxFitsSyntheticProbeMint =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsSyntheticProbeRecord =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Bg>>;

namespace detail {

[[nodiscard]] constexpr std::size_t
transport_probe_index(TransportProbeKind kind) noexcept {
    auto raw = static_cast<std::uint32_t>(kind);
    std::size_t index = 0;
    while (raw > 1u) {
        raw >>= 1u;
        ++index;
    }
    return index;
}

[[nodiscard]] constexpr std::uint32_t
metric_id_for(std::uint32_t base,
              std::size_t peer_index,
              TransportProbeKind kind,
              std::uint32_t lane) noexcept {
    return base
        + static_cast<std::uint32_t>(peer_index * all_transport_probe_kinds.size() * 2u)
        + static_cast<std::uint32_t>(transport_probe_index(kind) * 2u)
        + lane;
}

struct AtomicProbeStats {
    std::atomic<std::uint64_t> scheduled{0};
    std::atomic<std::uint64_t> succeeded{0};
    std::atomic<std::uint64_t> failed{0};
    std::atomic<std::uint64_t> timed_out{0};
    std::atomic<std::uint64_t> bytes_transferred{0};
    std::atomic<std::uint64_t> last_latency_ns{0};
    std::atomic<std::uint64_t> last_sequence{0};
    std::atomic<std::uint8_t> last_failure{
        static_cast<std::uint8_t>(SyntheticProbeFailureClass::None)};
};

}  // namespace detail

template <std::size_t MaxPeers>
class SyntheticProbeRunner
    : public safety::Pinned<SyntheticProbeRunner<MaxPeers>> {
    static_assert(MaxPeers > 0, "SyntheticProbeRunner<MaxPeers> requires MaxPeers > 0.");

public:
    static constexpr std::size_t max_peers = MaxPeers;
    static constexpr std::size_t transport_kind_count =
        all_transport_probe_kinds.size();

    struct PeerSlot {
        cog::CogIdentity peer{};
        safety::Bits<TransportProbeKind> enabled_kinds{};
        bool active = false;
    };

private:
    ProbeConfig config_{};
    std::array<PeerSlot, MaxPeers> peers_{};
    std::array<std::array<detail::AtomicProbeStats, transport_kind_count>, MaxPeers>
        stats_{};

    [[nodiscard]] std::size_t find_peer(cog::CogIdentity const& peer) const noexcept {
        for (std::size_t i = 0; i < peers_.size(); ++i) {
            if (peers_[i].active && peers_[i].peer.uuid == peer.uuid) {
                return i;
            }
        }
        return peers_.size();
    }

public:
    explicit SyntheticProbeRunner(ProbeConfig config = {}) noexcept
        : config_{config} {}

    [[nodiscard]] ProbeConfig config() const noexcept { return config_; }

    [[nodiscard]] std::span<const PeerSlot, MaxPeers> peers() const noexcept {
        return std::span<const PeerSlot, MaxPeers>{peers_};
    }

    [[nodiscard]] bool
    register_peer(cog::CogIdentity peer,
                  safety::Bits<TransportProbeKind> kinds) noexcept {
        if (peer.uuid.is_zero() || kinds.none()) {
            return false;
        }
        if (find_peer(peer) != peers_.size()) {
            return false;
        }
        for (auto& slot : peers_) {
            if (!slot.active) {
                slot.peer = peer;
                slot.enabled_kinds = kinds;
                slot.active = true;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool
    enabled(cog::CogIdentity const& peer, TransportProbeKind kind) const noexcept {
        auto const peer_index = find_peer(peer);
        return peer_index != peers_.size()
            && peers_[peer_index].enabled_kinds.test(kind);
    }

    template <effects::IsExecCtx Ctx>
        requires CtxFitsSyntheticProbeRecord<Ctx>
    [[nodiscard]] bool
    record_outcome(Ctx const&,
                   cog::CogIdentity const& peer,
                   ProbeOutcome outcome,
                   observe::ObservationSnapshot* observations = nullptr) noexcept {
        auto const peer_index = find_peer(peer);
        if (peer_index == peers_.size()
            || !peers_[peer_index].enabled_kinds.test(outcome.kind)) {
            return false;
        }

        auto& counters =
            stats_[peer_index][detail::transport_probe_index(outcome.kind)];
        counters.scheduled.fetch_add(1, std::memory_order_relaxed);
        counters.last_latency_ns.store(outcome.latency_ns, std::memory_order_relaxed);
        counters.last_sequence.store(outcome.sequence, std::memory_order_release);
        counters.last_failure.store(
            static_cast<std::uint8_t>(outcome.failure), std::memory_order_release);
        counters.bytes_transferred.fetch_add(
            outcome.bytes_transferred, std::memory_order_relaxed);

        if (outcome.ok()) {
            counters.succeeded.fetch_add(1, std::memory_order_relaxed);
        } else {
            counters.failed.fetch_add(1, std::memory_order_relaxed);
            if (outcome.failure == SyntheticProbeFailureClass::Timeout) {
                counters.timed_out.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (observations != nullptr) {
            observe::record_observation(*observations, observe::latency_ns(
                detail::metric_id_for(config_.metric_id_base, peer_index,
                    outcome.kind, 0),
                outcome.latency_ns,
                outcome.sequence));
            observe::record_observation(*observations, observe::bits_transferred(
                detail::metric_id_for(config_.metric_id_base, peer_index,
                    outcome.kind, 1),
                outcome.bytes_transferred * 8ull,
                outcome.sequence));
        }
        return true;
    }

    [[nodiscard]] ProbeStats
    stats(cog::CogIdentity const& peer, TransportProbeKind kind) const noexcept {
        auto const peer_index = find_peer(peer);
        if (peer_index == peers_.size()
            || !peers_[peer_index].enabled_kinds.test(kind)) {
            return {};
        }

        auto const& counters =
            stats_[peer_index][detail::transport_probe_index(kind)];
        return ProbeStats{
            .scheduled = counters.scheduled.load(std::memory_order_relaxed),
            .succeeded = counters.succeeded.load(std::memory_order_relaxed),
            .failed = counters.failed.load(std::memory_order_relaxed),
            .timed_out = counters.timed_out.load(std::memory_order_relaxed),
            .bytes_transferred =
                counters.bytes_transferred.load(std::memory_order_relaxed),
            .last_latency_ns =
                counters.last_latency_ns.load(std::memory_order_relaxed),
            .last_sequence =
                counters.last_sequence.load(std::memory_order_acquire),
            .last_failure = static_cast<SyntheticProbeFailureClass>(
                counters.last_failure.load(std::memory_order_acquire)),
        };
    }
};

template <effects::IsExecCtx Ctx, std::size_t MaxPeers>
    requires CtxFitsSyntheticProbeMint<Ctx>
[[nodiscard]] SyntheticProbeRunner<MaxPeers>
mint_synthetic_probes(Ctx const&, ProbeConfig config = {}) noexcept {
    return SyntheticProbeRunner<MaxPeers>{config};
}

static_assert(std::is_base_of_v<
    safety::Pinned<SyntheticProbeRunner<1>>, SyntheticProbeRunner<1>>);
static_assert(!CtxFitsSyntheticProbeMint<effects::BgDrainCtx>);
static_assert(CtxFitsSyntheticProbeMint<effects::ColdInitCtx>);
static_assert(!CtxFitsSyntheticProbeRecord<effects::HotFgCtx>);
static_assert(CtxFitsSyntheticProbeRecord<effects::BgDrainCtx>);

}  // namespace crucible::observe

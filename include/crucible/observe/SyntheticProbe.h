#pragma once

#include <crucible/cog/CogIdentity.h>
#include <crucible/observe/Observation.h>
#include <fixy/Bits.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <foundation/Pinned.h>
#include <foundation/diag/Tag.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::observe {

using PositiveProbeCount = ::fixy::Positive<std::uint16_t>;
using PositiveProbePeriodNs = ::fixy::Positive<std::uint64_t>;

enum class TransportProbeKind : std::uint32_t {
    RdmaWrite = 1u << 0,
    RdmaSend = 1u << 1,
    RdmaRead = 1u << 2,
    TcpBbr3 = 1u << 3,
    TcpCubic = 1u << 4,
    TcpDctcp = 1u << 5,
    CollectiveSmallAllReduce = 1u << 6,
    CollectiveSmallAllGather = 1u << 7,
    FederationMtls = 1u << 8,
};

inline constexpr ::fixy::FixedArray<TransportProbeKind, 9> all_transport_probe_kinds{
    TransportProbeKind::RdmaWrite,
    TransportProbeKind::RdmaSend,
    TransportProbeKind::RdmaRead,
    TransportProbeKind::TcpBbr3,
    TransportProbeKind::TcpCubic,
    TransportProbeKind::TcpDctcp,
    TransportProbeKind::CollectiveSmallAllReduce,
    TransportProbeKind::CollectiveSmallAllGather,
    TransportProbeKind::FederationMtls,
};

[[nodiscard]] constexpr ::fixy::TextView transport_probe_kind_name(TransportProbeKind kind) noexcept {
    switch (kind) {
        case TransportProbeKind::RdmaWrite:
            return "RdmaWrite";
        case TransportProbeKind::RdmaSend:
            return "RdmaSend";
        case TransportProbeKind::RdmaRead:
            return "RdmaRead";
        case TransportProbeKind::TcpBbr3:
            return "TcpBbr3";
        case TransportProbeKind::TcpCubic:
            return "TcpCubic";
        case TransportProbeKind::TcpDctcp:
            return "TcpDctcp";
        case TransportProbeKind::CollectiveSmallAllReduce:
            return "CollectiveSmallAllReduce";
        case TransportProbeKind::CollectiveSmallAllGather:
            return "CollectiveSmallAllGather";
        case TransportProbeKind::FederationMtls:
            return "FederationMtls";
        default:
            return "<unknown TransportProbeKind>";
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

[[nodiscard]] constexpr ::fixy::TextView synthetic_probe_failure_name(SyntheticProbeFailureClass failure) noexcept {
    switch (failure) {
        case SyntheticProbeFailureClass::None:
            return "None";
        case SyntheticProbeFailureClass::Timeout:
            return "Timeout";
        case SyntheticProbeFailureClass::Refused:
            return "Refused";
        case SyntheticProbeFailureClass::Handshake:
            return "Handshake";
        case SyntheticProbeFailureClass::Completion:
            return "Completion";
        case SyntheticProbeFailureClass::Permission:
            return "Permission";
        case SyntheticProbeFailureClass::DataMismatch:
            return "DataMismatch";
        case SyntheticProbeFailureClass::TransportUnavailable:
            return "TransportUnavailable";
        default:
            return "<unknown SyntheticProbeFailureClass>";
    }
}

struct SyntheticProbeFailure : ::foundation::diag::tag_base {
    static constexpr ::foundation::diag::tag_text_literal name = "SyntheticProbeFailure";
    static constexpr ::foundation::diag::tag_text_literal description =
        "A synthetic transport probe failed for a peer/protocol pair.";
    static constexpr ::foundation::diag::tag_text_literal remediation =
        "Route the failure class into Health/Quarantine policy and "
        "inspect the concrete transport executor that produced it.";
};

struct ProbeConfig {
    PositiveProbePeriodNs period_per_kind_ns =
        ::fixy::mint_refined_constant<::fixy::positive, std::uint64_t{60000000000ull}>();
    PositiveProbeCount max_peer_count = ::fixy::mint_refined_constant<::fixy::positive, std::uint16_t{1}>();
    std::uint32_t metric_id_base = 0;
};

struct ProbeOutcome {
    TransportProbeKind kind = TransportProbeKind::TcpCubic;
    SyntheticProbeFailureClass failure = SyntheticProbeFailureClass::None;
    std::uint64_t latency_ns = 0;
    std::uint64_t bytes_transferred = 0;
    std::uint64_t sequence = 0;

    [[nodiscard]] constexpr bool ok() const noexcept { return failure == SyntheticProbeFailureClass::None; }
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
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsSyntheticProbeRecord =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Bg>>;

namespace detail {

[[nodiscard]] constexpr std::size_t transport_probe_index(TransportProbeKind kind) noexcept {
    auto raw = static_cast<std::uint32_t>(kind);
    std::size_t index = 0;
    while (raw > 1u) {
        raw >>= 1u;
        ++index;
    }
    return index;
}

// The lane of a probe metric: the latency of an outcome, or the bits that
// the outcome moved.
enum class ProbeMetricLane : std::uint8_t {
    LatencyNs = 0,
    BitsTransferred = 1,
};

inline constexpr std::size_t kProbeMetricLaneCount = 2;

// The count of peers whose ids fit the 32 bits of a metric id: each peer
// takes one id for each lane of each kind.
inline constexpr std::size_t kMaxProbeMetricPeers =
    (std::size_t{1} << 32u) / (all_transport_probe_kinds.size() * kProbeMetricLaneCount);

// The metric id of one lane of one kind of the peer at peer_index: base plus
// the offset (peer_index * kinds + kind) * lanes + lane, modulo 2^32.  The
// kind and the lane are digits below their counts.  A peer index below
// kMaxProbeMetricPeers keeps the offset below 2^32, so two ids of one base
// are equal only when the peer, the kind and the lane are equal.
[[nodiscard]] constexpr std::uint32_t metric_id_for(std::uint32_t base, std::size_t peer_index, TransportProbeKind kind,
                                                    ProbeMetricLane lane) noexcept {
    std::size_t const offset =
        (peer_index * all_transport_probe_kinds.size() + transport_probe_index(kind)) * kProbeMetricLaneCount
        + static_cast<std::size_t>(lane);
    return base + static_cast<std::uint32_t>(offset);
}

// alignas(64) is load-bearing. The struct is embedded in a per-peer,
// per-kind grid whose adjacent slots are recorded concurrently by different
// probe threads. Unaligned, the struct is smaller than a cache line and two
// slots share one, which is false sharing. The atomics inside one struct are
// mutated by the same producer, so packing those on one line is intentional.
// The alignment isolates across slots only.
struct alignas(64) AtomicProbeStats {
    ::fixy::Atomic<std::uint64_t> scheduled{};
    ::fixy::Atomic<std::uint64_t> succeeded{};
    ::fixy::Atomic<std::uint64_t> failed{};
    ::fixy::Atomic<std::uint64_t> timed_out{};
    ::fixy::Atomic<std::uint64_t> bytes_transferred{};
    ::fixy::Atomic<std::uint64_t> last_latency_ns{};
    ::fixy::Atomic<std::uint64_t> last_sequence{};
    ::fixy::Atomic<SyntheticProbeFailureClass> last_failure{SyntheticProbeFailureClass::None};
};

}  // namespace detail

template <std::size_t MaxPeers>
class SyntheticProbeRunner : public ::foundation::Pinned<SyntheticProbeRunner<MaxPeers>> {
    static_assert(MaxPeers > 0, "SyntheticProbeRunner<MaxPeers> requires MaxPeers > 0.");
    static_assert(MaxPeers <= detail::kMaxProbeMetricPeers,
                  "SyntheticProbeRunner<MaxPeers> gives each peer its own metric ids only when MaxPeers is at most "
                  "detail::kMaxProbeMetricPeers.");

public:
    static constexpr std::size_t max_peers = MaxPeers;
    static constexpr std::size_t transport_kind_count = all_transport_probe_kinds.size();

    struct PeerSlot {
        cog::CogIdentity peer{};
        ::fixy::Bits<TransportProbeKind> enabled_kinds{};
        bool active = false;
    };

private:
    using PeerIndex = ::fixy::Index<MaxPeers>;
    using KindRow = ::fixy::FixedArray<detail::AtomicProbeStats, transport_kind_count>;

    ProbeConfig config_{};
    ::fixy::FixedArray<PeerSlot, MaxPeers> peers_{};
    ::fixy::FixedArray<KindRow, MaxPeers> stats_{};

    // The index of the active slot of peer, or no value.  O(MaxPeers).
    [[nodiscard]] ::fixy::Option<PeerIndex> find_peer(cog::CogIdentity const& peer) const noexcept {
        for (auto const [index, slot] : peers_.enumerate()) {
            if (slot->active && slot->peer.uuid == peer.uuid) {
                return ::fixy::some(index);
            }
        }
        return ::fixy::none;
    }

    // The index of the active slot of peer when the slot enables kind, or no
    // value.  O(MaxPeers).
    [[nodiscard]] ::fixy::Option<PeerIndex> find_enabled_(cog::CogIdentity const& peer,
                                                          TransportProbeKind kind) const noexcept {
        return find_peer(peer).filter(
            [&](PeerIndex const& index) noexcept { return peers_.at(index).enabled_kinds.test(kind); });
    }

    // The counters of one kind of the peer at index.  Each kind is one bit,
    // so its position is below transport_kind_count.
    [[nodiscard]] detail::AtomicProbeStats const& counters_(PeerIndex index, TransportProbeKind kind) const noexcept {
        return *stats_.at(index).get(detail::transport_probe_index(kind)).expect("each kind has a slot of the grid");
    }

    // Writes the counters of one outcome, and gives the index of the peer, or
    // no value for a peer or a kind that the runner does not probe.
    [[nodiscard]] ::fixy::Option<PeerIndex> record_counters_(cog::CogIdentity const& peer,
                                                             ProbeOutcome outcome) noexcept {
        return find_enabled_(peer, outcome.kind)
            .match(
                [&](PeerIndex index) noexcept {
                    auto const kind_slot =
                        ::fixy::Index<transport_kind_count>::check(detail::transport_probe_index(outcome.kind))
                            .expect("each kind has a slot of the grid");
                    // Each counter is an Atomic, so a concurrent write of the same counters is safe.
                    stats_.with_element(index, [kind_slot, outcome](KindRow& row) noexcept {
                        row.with_element(kind_slot, [outcome](detail::AtomicProbeStats& counters) noexcept {
                            // The scheduled counter is deliberately not touched here.
                            // Dispatch is counted by schedule_probe.
                            counters.last_latency_ns.store_release(outcome.latency_ns);
                            counters.last_sequence.store_release(outcome.sequence);
                            counters.last_failure.store_release(outcome.failure);
                            static_cast<void>(counters.bytes_transferred.fetch_add_acq_rel(outcome.bytes_transferred));
                            if (outcome.ok()) {
                                static_cast<void>(counters.succeeded.fetch_add_acq_rel(1));
                            } else {
                                static_cast<void>(counters.failed.fetch_add_acq_rel(1));
                                if (outcome.failure == SyntheticProbeFailureClass::Timeout) {
                                    static_cast<void>(counters.timed_out.fetch_add_acq_rel(1));
                                }
                            }
                        });
                    });
                    return ::fixy::some(index);
                },
                []() noexcept { return ::fixy::Option<PeerIndex>{::fixy::none}; });
    }

public:
    constexpr explicit SyntheticProbeRunner(ProbeConfig config = {}) noexcept : config_{config} {}

    [[nodiscard]] ProbeConfig config() const noexcept { return config_; }

    [[nodiscard]] ::fixy::View<PeerSlot const, MaxPeers> peers() const noexcept { return peers_.view(); }

    [[nodiscard]] bool register_peer(cog::CogIdentity peer, ::fixy::Bits<TransportProbeKind> kinds) noexcept {
        if (peer.uuid.is_zero() || kinds.none()) {
            return false;
        }
        if (find_peer(peer).is_some()) {
            return false;
        }
        for (PeerIndex const index : peers_.indices()) {
            if (!peers_.at(index).active) {
                // The body reads the peer through a const reference, so it
                // writes no borrow of its frame into the peer.
                cog::CogIdentity const& peer_read = peer;
                peers_.with_element(index, [&peer_read, kinds](PeerSlot& slot) noexcept {
                    slot.peer = peer_read;
                    slot.enabled_kinds = kinds;
                    slot.active = true;
                });
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool enabled(cog::CogIdentity const& peer, TransportProbeKind kind) const noexcept {
        return find_enabled_(peer, kind).is_some();
    }

    // Call this once when a probe is committed to the wire, before
    // record_outcome reports its result. Counting dispatch separately from
    // outcome is what makes scheduled minus succeeded minus failed a real
    // loss rate. One shared counter would hide a probe that never reports
    // back.
    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsSyntheticProbeRecord<Ctx>
    [[nodiscard]] bool schedule_probe(Ctx const&, cog::CogIdentity const& peer, TransportProbeKind kind) noexcept {
        return find_enabled_(peer, kind)
            .match(
                [&](PeerIndex index) noexcept {
                    auto const kind_slot =
                        ::fixy::Index<transport_kind_count>::check(detail::transport_probe_index(kind))
                            .expect("each kind has a slot of the grid");
                    // The counter is an Atomic, so a concurrent write of the same counter is safe.
                    stats_.with_element(index, [kind_slot](KindRow& row) noexcept {
                        row.with_element(kind_slot, [](detail::AtomicProbeStats& counters) noexcept {
                            static_cast<void>(counters.scheduled.fetch_add_acq_rel(1));
                        });
                    });
                    return true;
                },
                []() noexcept { return false; });
    }

    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsSyntheticProbeRecord<Ctx>
    [[nodiscard]] bool record_outcome(Ctx const&, cog::CogIdentity const& peer, ProbeOutcome outcome) noexcept {
        return record_counters_(peer, outcome).is_some();
    }

    // The same, and the latency and the bits of the outcome go into
    // observations too.
    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsSyntheticProbeRecord<Ctx>
    [[nodiscard]] bool record_outcome(Ctx const&, cog::CogIdentity const& peer, ProbeOutcome outcome,
                                      observe::ObservationSink::Writer& observations) noexcept {
        return record_counters_(peer, outcome)
            .match(
                [&](PeerIndex index) noexcept {
                    observations.record(
                        ::fixy::index<0>(),
                        observe::latency_ns(detail::metric_id_for(config_.metric_id_base, index.raw(), outcome.kind,
                                                                  detail::ProbeMetricLane::LatencyNs),
                                            outcome.latency_ns, outcome.sequence));
                    observations.record(::fixy::index<0>(),
                                        observe::bits_transferred(
                                            detail::metric_id_for(config_.metric_id_base, index.raw(), outcome.kind,
                                                                  detail::ProbeMetricLane::BitsTransferred),
                                            outcome.bytes_transferred * 8ull, outcome.sequence));
                    return true;
                },
                []() noexcept { return false; });
    }

    [[nodiscard]] ProbeStats stats(cog::CogIdentity const& peer, TransportProbeKind kind) const noexcept {
        return find_enabled_(peer, kind)
            .map([&](PeerIndex index) noexcept {
                detail::AtomicProbeStats const& counters = counters_(index, kind);
                return ProbeStats{
                    .scheduled = counters.scheduled.load_acquire(),
                    .succeeded = counters.succeeded.load_acquire(),
                    .failed = counters.failed.load_acquire(),
                    .timed_out = counters.timed_out.load_acquire(),
                    .bytes_transferred = counters.bytes_transferred.load_acquire(),
                    .last_latency_ns = counters.last_latency_ns.load_acquire(),
                    .last_sequence = counters.last_sequence.load_acquire(),
                    .last_failure = counters.last_failure.load_acquire(),
                };
            })
            .value_or(ProbeStats{});
    }
};

template <::foundation::effects::IsExecCtx Ctx, std::size_t MaxPeers>
    requires CtxFitsSyntheticProbeMint<Ctx>
[[nodiscard]] constexpr SyntheticProbeRunner<MaxPeers> mint_synthetic_probes(Ctx const&,
                                                                             ProbeConfig config = {}) noexcept {
    return SyntheticProbeRunner<MaxPeers>{config};
}

}  // namespace crucible::observe

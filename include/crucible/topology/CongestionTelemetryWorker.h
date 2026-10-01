#pragma once

#include <crucible/observe/Observation.h>
#include <crucible/topology/CongestionTelemetry.h>
#include <fixy/Ctx.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/EnumName.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <type_traits>

namespace crucible::topology {

enum class CongestionMetricSlot : std::uint32_t {
    SampleCount = 0,
    P50RttUs = 1,
    P95RttUs = 2,
    P95BandwidthBps = 3,
    MeanBandwidthBps = 4,
    InFlightBytes = 5,
    RetransCount = 6,
    LostCount = 7,
    EcnMarkPpm = 8,
    WorstMode = 9,
};

inline constexpr std::uint32_t kCongestionMetricBase = 0x43430000u;
// One observation for each slot.
inline constexpr std::size_t kCongestionObservationCount = ::foundation::reflect::enum_count<CongestionMetricSlot>;

using CongestionObservationSet = std::array<::crucible::observe::ObservationSnapshot, kCongestionObservationCount>;
using CongestionObservationBatch = std::array<::crucible::observe::Observation, kCongestionObservationCount>;

template <class Ctx>
concept CtxFitsCongestionTelemetryStart =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsCongestionTelemetryHarvest =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Bg>>;

// A poll is background work that reads each socket through getsockopt, so
// its context admits the background row and the socket option row.  A
// drain context carries no IO and records only.
template <class Ctx>
concept CtxFitsCongestionTelemetryPoll = CtxFitsCongestionTelemetryHarvest<Ctx> && cntp::CtxFitsSocketOption<Ctx>;

[[nodiscard]] constexpr std::uint32_t congestion_metric_id(std::uint16_t link_slot,
                                                           CongestionMetricSlot slot) noexcept {
    return kCongestionMetricBase | (static_cast<std::uint32_t>(link_slot) << 8u) | static_cast<std::uint32_t>(slot);
}

[[nodiscard]] constexpr CongestionObservationBatch congestion_observations(
    std::uint16_t link_slot, topology::CongestionAggregate const& aggregate, std::uint64_t sequence,
    ::crucible::observe::ObservationSource source = ::crucible::observe::ObservationSource::Runtime) noexcept {
    return CongestionObservationBatch{{
        ::crucible::observe::make_observation(::crucible::observe::ObservationKind::Metric, source,
                                              congestion_metric_id(link_slot, CongestionMetricSlot::SampleCount),
                                              aggregate.sample_count, sequence),
        ::crucible::observe::make_observation(::crucible::observe::ObservationKind::LatencyNs, source,
                                              congestion_metric_id(link_slot, CongestionMetricSlot::P50RttUs),
                                              aggregate.p50_rtt_us, sequence),
        ::crucible::observe::make_observation(::crucible::observe::ObservationKind::LatencyNs, source,
                                              congestion_metric_id(link_slot, CongestionMetricSlot::P95RttUs),
                                              aggregate.p95_rtt_us, sequence),
        ::crucible::observe::make_observation(::crucible::observe::ObservationKind::BitsTransferred, source,
                                              congestion_metric_id(link_slot, CongestionMetricSlot::P95BandwidthBps),
                                              aggregate.p95_btl_bw_bps, sequence),
        ::crucible::observe::make_observation(::crucible::observe::ObservationKind::BitsTransferred, source,
                                              congestion_metric_id(link_slot, CongestionMetricSlot::MeanBandwidthBps),
                                              aggregate.mean_btl_bw_bps, sequence),
        ::crucible::observe::make_observation(::crucible::observe::ObservationKind::BitsTransferred, source,
                                              congestion_metric_id(link_slot, CongestionMetricSlot::InFlightBytes),
                                              aggregate.total_in_flight_bytes, sequence),
        ::crucible::observe::make_observation(::crucible::observe::ObservationKind::Metric, source,
                                              congestion_metric_id(link_slot, CongestionMetricSlot::RetransCount),
                                              aggregate.retrans_count, sequence),
        ::crucible::observe::make_observation(::crucible::observe::ObservationKind::Metric, source,
                                              congestion_metric_id(link_slot, CongestionMetricSlot::LostCount),
                                              aggregate.lost_count, sequence),
        ::crucible::observe::make_observation(::crucible::observe::ObservationKind::Metric, source,
                                              congestion_metric_id(link_slot, CongestionMetricSlot::EcnMarkPpm),
                                              aggregate.ecn_mark_ppm, sequence),
        ::crucible::observe::make_observation(::crucible::observe::ObservationKind::Metric, source,
                                              congestion_metric_id(link_slot, CongestionMetricSlot::WorstMode),
                                              static_cast<std::uint64_t>(aggregate.worst_mode), sequence),
    }};
}

inline void publish_congestion(
    CongestionObservationSet& sinks, std::uint16_t link_slot, topology::CongestionAggregate const& aggregate,
    std::uint64_t sequence,
    ::crucible::observe::ObservationSource source = ::crucible::observe::ObservationSource::Runtime) noexcept {
    CongestionObservationBatch const observations = congestion_observations(link_slot, aggregate, sequence, source);
    for (std::size_t i = 0; i < observations.size(); ++i) {
        ::crucible::observe::record_observation(sinks[i], observations[i]);
    }
}

template <std::size_t MaxLinks, std::size_t MaxFlows>
class CongestionTelemetryWorker;

// The only door into a worker.  An Init-row context mints it; a
// background worker then records into it.
template <std::size_t MaxLinks, std::size_t MaxFlows, class Ctx>
    requires CtxFitsCongestionTelemetryStart<Ctx>
[[nodiscard]] constexpr CongestionTelemetryWorker<MaxLinks, MaxFlows>
mint_congestion_telemetry_worker(Ctx const&) noexcept;

template <std::size_t MaxLinks, std::size_t MaxFlows>
class CongestionTelemetryWorker : public ::foundation::Pinned<CongestionTelemetryWorker<MaxLinks, MaxFlows>> {
    static_assert(MaxLinks > 0, "CongestionTelemetryWorker requires link slots");
    static_assert(MaxFlows > 0, "CongestionTelemetryWorker requires flow slots");

    struct Slot {
        bool occupied = false;
        cog::CogIdentity nic{};
        topology::CongestionAggregate last{};
        std::uint64_t sequence = 0;
    };

    std::array<Slot, MaxLinks> slots_{};
    topology::TelemetrySchedule schedule_{};

    template <std::size_t Links, std::size_t Flows, class Ctx>
        requires CtxFitsCongestionTelemetryStart<Ctx>
    friend constexpr CongestionTelemetryWorker<Links, Flows> mint_congestion_telemetry_worker(Ctx const&) noexcept;

    constexpr CongestionTelemetryWorker() noexcept = default;

    // The started slot of this NIC, or nullptr.  One lookup serves the
    // const and the mutable callers.
    template <class Self>
    [[nodiscard]] constexpr auto find(this Self& self, cog::CogIdentity const& nic) noexcept
        -> decltype(std::addressof(self.slots_[0])) {
        for (auto& slot : self.slots_) {
            if (slot.occupied && slot.nic.uuid == nic.uuid) {
                return std::addressof(slot);
            }
        }
        return nullptr;
    }

public:
    template <class Ctx>
        requires CtxFitsCongestionTelemetryStart<Ctx>
    [[nodiscard]] constexpr std::expected<void, topology::TelemetryError>
    start(Ctx const&, std::span<const cog::CogIdentity> nics, topology::TelemetrySchedule schedule = {}) noexcept {
        if (nics.size() > MaxLinks) {
            return std::unexpected(topology::TelemetryError::TooManyLinks);
        }
        for (auto const& nic : nics) {
            if (!topology::is_nic_cog(nic)) {
                return std::unexpected(topology::TelemetryError::InvalidNicCog);
            }
        }
        for (auto& slot : slots_) {
            slot = Slot{};
        }
        for (std::size_t i = 0; i < nics.size(); ++i) {
            slots_[i].occupied = true;
            slots_[i].nic = nics[i];
            slots_[i].last.nic_uuid = nics[i].uuid;
        }
        schedule_ = schedule;
        return {};
    }

    [[nodiscard]] constexpr topology::TelemetrySchedule schedule() const noexcept { return schedule_; }

    template <class Ctx>
        requires CtxFitsCongestionTelemetryHarvest<Ctx>
    [[nodiscard]] std::expected<topology::CongestionAggregate, topology::TelemetryError>
    record_link(Ctx const&, cog::CogIdentity const& nic, std::span<const topology::TcpInfoSnapshot> samples,
                std::uint64_t sequence) noexcept {
        Slot* slot = find(nic);
        if (slot == nullptr) {
            return std::unexpected(topology::TelemetryError::LinkNotStarted);
        }
        slot->last = topology::aggregate_congestion(nic, samples);
        slot->sequence = sequence;
        return slot->last;
    }

    template <class Ctx>
        requires CtxFitsCongestionTelemetryPoll<Ctx>
    [[nodiscard]] std::expected<topology::CongestionAggregate, topology::TelemetryError>
    poll_link(Ctx const& ctx, cog::CogIdentity const& nic, std::span<const cntp::SocketFd> active_fds,
              std::uint64_t sequence) noexcept {
        Slot* slot = find(nic);
        if (slot == nullptr) {
            return std::unexpected(topology::TelemetryError::LinkNotStarted);
        }
        auto aggregate = topology::harvest_per_link(ctx, nic, active_fds);
        if (!aggregate.has_value()) {
            return std::unexpected(aggregate.error());
        }
        slot->last = *aggregate;
        slot->sequence = sequence;
        return slot->last;
    }

    [[nodiscard]] constexpr std::expected<topology::CongestionAggregate, topology::TelemetryError>
    last(cog::CogIdentity const& nic) const noexcept {
        Slot const* slot = find(nic);
        if (slot == nullptr) {
            return std::unexpected(topology::TelemetryError::LinkNotStarted);
        }
        return slot->last;
    }
};

template <std::size_t MaxLinks, std::size_t MaxFlows, class Ctx>
    requires CtxFitsCongestionTelemetryStart<Ctx>
[[nodiscard]] constexpr CongestionTelemetryWorker<MaxLinks, MaxFlows>
mint_congestion_telemetry_worker(Ctx const&) noexcept {
    return CongestionTelemetryWorker<MaxLinks, MaxFlows>{};
}

}  // namespace crucible::topology

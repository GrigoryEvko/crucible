// The compile-time checks of crucible/topology/CongestionTelemetryWorker.h.

#include <crucible/topology/CongestionTelemetryWorker.h>

namespace crucible::topology {

static_assert(std::is_trivially_copyable_v<CongestionObservationBatch>);

// Observation i carries slot i. A slot with no observation in the batch
// gets an empty entry with metric id 0, and publish then writes that
// entry to the sink of the slot, so this check refuses it.
static_assert(
    [] {
        CongestionObservationBatch const batch = congestion_observations(1, topology::CongestionAggregate{}, 0);
        for (std::size_t index = 0; index < batch.size(); ++index) {
            auto const slot = static_cast<CongestionMetricSlot>(index);
            if (batch[index].metric_id != congestion_metric_id(1, slot)) {
                return false;
            }
        }
        return true;
    }(),
    "congestion_observations does not give observation i the metric id of slot i.");
static_assert(!std::is_default_constructible_v<CongestionTelemetryWorker<1, 1>>,
              "the worker is reached only through mint_congestion_telemetry_worker");
static_assert(CtxFitsCongestionTelemetryStart<::fixy::ColdInitCtx>);
static_assert(!CtxFitsCongestionTelemetryStart<::fixy::BgDrainCtx>);
static_assert(CtxFitsCongestionTelemetryHarvest<::fixy::BgDrainCtx>);
static_assert(!CtxFitsCongestionTelemetryHarvest<::fixy::HotFgCtx>);
static_assert(CtxFitsCongestionTelemetryPoll<::fixy::BgLoadCtx>);
static_assert(!CtxFitsCongestionTelemetryPoll<::fixy::BgDrainCtx>, "a drain context carries no IO for getsockopt.");
static_assert(!CtxFitsCongestionTelemetryPoll<::fixy::BgCompileCtx>, "getsockopt takes the socket lock, so Block too.");
static_assert(!CtxFitsCongestionTelemetryPoll<::fixy::InitLoadCtx>, "a poll is background work.");

}  // namespace crucible::topology

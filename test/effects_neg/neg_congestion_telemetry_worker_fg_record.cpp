#include <crucible/topology/CongestionTelemetryWorker.h>
#include <crucible/topology/CongestionTelemetry.h>

#include <array>
#include <span>

// GAPS-123 fixture #3: congestion telemetry recording is Bg-row work.
// Foreground hot-path contexts may not update per-link telemetry slots.

int main() {
    namespace topology = crucible::topology;
    namespace topology = crucible::topology;
    crucible::effects::ColdInitCtx init{};
    crucible::effects::HotFgCtx fg{};
    crucible::cog::CogIdentity nic{};
    nic.kind = crucible::cog::CogKind::NicPort;

    auto worker = topology::mint_congestion_telemetry_worker<1, 1>(init);
    std::array nics{nic};
    auto started = worker.start(init, std::span{nics});
    (void)started;
    std::array samples{topology::TcpInfoSnapshot{topology::CongestionState{}}};
    auto recorded = worker.record_link(fg, nic, std::span{samples}, 1);
    (void)recorded;
    return 0;
}

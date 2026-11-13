// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Recording congestion telemetry is background work.  The foreground
// hot-path context cannot update a per-link slot.  A foreground context is
// built only from the producer claim, so the refused call names it in an
// unevaluated operand.

#include <crucible/topology/CongestionTelemetryWorker.h>

#include <utility>

int main() {
    namespace topology = crucible::topology;
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    crucible::cog::CogIdentity nic{};
    nic.kind = crucible::cog::CogKind::NicPort;

    auto worker = topology::mint_congestion_telemetry_worker<1, 1>(init);
    ::foundation::core::FixedArray<crucible::cog::CogIdentity, 1> const nics{nic};
    auto started = worker.start(init, nics.view());
    (void)started;
    return sizeof(worker.record_link(std::declval<::fixy::HotFgCtx const&>(), nic,
                                     ::fixy::View<topology::TcpInfoSnapshot const>{}, 1))
                == 0
             ? 1
             : 0;
}

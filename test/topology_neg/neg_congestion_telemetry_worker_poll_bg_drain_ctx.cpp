// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A poll reads each socket through getsockopt.  The background drain
// context passes the background half of the poll gate and carries no IO,
// so the socket option half refuses it.

#include <crucible/topology/CongestionTelemetryWorker.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace topology = crucible::topology;
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    ::fixy::BgDrainCtx drain{::foundation::effects::testing::bg()};
    crucible::cog::CogIdentity nic{};
    nic.kind = crucible::cog::CogKind::NicPort;

    auto worker = topology::mint_congestion_telemetry_worker<1, 1>(init);
    ::foundation::core::FixedArray<crucible::cog::CogIdentity, 1> const nics{nic};
    auto started = worker.start(init, nics.view());
    (void)started;
    auto polled = worker.poll_link(drain, nic, ::fixy::View<crucible::cntp::SocketFd const>{}, 1);
    (void)polled;
    return 0;
}

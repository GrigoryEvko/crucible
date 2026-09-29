// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A poll is background work.  The startup load context carries the IO and
// Block of the socket option half of the poll gate, but not the background
// effect, so the background half refuses it.

#include <crucible/topology/CongestionTelemetryWorker.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

#include <array>
#include <span>

int main() {
    namespace topology = crucible::topology;
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    ::fixy::InitLoadCtx load{::foundation::effects::testing::init()};
    crucible::cog::CogIdentity nic{};
    nic.kind = crucible::cog::CogKind::NicPort;

    auto worker = topology::mint_congestion_telemetry_worker<1, 1>(init);
    std::array nics{nic};
    auto started = worker.start(init, std::span{nics});
    (void)started;
    std::array fds{crucible::cntp::admit_socket_fd(0).value()};
    auto polled = worker.poll_link(load, nic, std::span<const crucible::cntp::SocketFd>{fds}, 1);
    (void)polled;
    return 0;
}

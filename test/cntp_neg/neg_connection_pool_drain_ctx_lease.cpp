// A lease waits on the blocking gate of the pool, and that wait is a block.
// The background drain context owns Bg and no Block, so it cannot lease.

#include <crucible/cntp/ConnectionPoolRuntime.h>
#include <fixy/Ctx.h>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace fe = ::foundation::effects;

int main() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgDrainCtx drain{fe::testing::bg()};
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::Tcp, 1, 1>(init);

    cog::CogIdentity remote{};
    remote.uuid = cog::Uuid{1, 2};
    auto lease = pool.lease(drain, remote);
    (void)lease;
    return 0;
}

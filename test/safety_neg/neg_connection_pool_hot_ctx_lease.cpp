// GAPS-136 fixture #3: lease/return mutates runtime pool state and requires
// a Bg/Test execution row. Foreground hot replay cannot lease connections.

#include <crucible/rt/ConnectionPool.h>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace effects = crucible::effects;
namespace rt = crucible::rt;

int main() {
    effects::ColdInitCtx init{};
    effects::HotFgCtx hot{};
    auto pool = rt::mint_connection_pool<cntp::TransportClass::Tcp, 1, 1>(init);

    cog::CogIdentity remote{};
    remote.uuid = cog::Uuid{1, 2};
    auto lease = pool.lease(hot, remote);
    (void)lease;
    return 0;
}

// mint_connection_pool is the only door to a pool, so a pool exists only
// where a context that owns Init built it.  The constructor is private.

#include <crucible/cntp/ConnectionPoolRuntime.h>
#include <fixy/Ctx.h>

namespace cntp = crucible::cntp;
namespace fe = ::foundation::effects;

int main() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto minted = cntp::mint_connection_pool<cntp::TransportClass::Tcp, 1, 1>(init);
    (void)minted;
    cntp::ConnectionPool<cntp::TransportClass::Tcp, 1, 1> forged{cntp::PoolConfig{}};
    (void)forged;
    return 0;
}

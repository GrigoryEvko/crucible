// A pool is built at startup, in a context that owns Init.  The background
// load context owns Bg and no Init, so the mint refuses it.

#include <crucible/cntp/ConnectionPoolRuntime.h>
#include <fixy/Ctx.h>

namespace cntp = crucible::cntp;
namespace fe = ::foundation::effects;

int main() {
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::Tcp, 1, 1>(bg);
    (void)pool;
    return 0;
}

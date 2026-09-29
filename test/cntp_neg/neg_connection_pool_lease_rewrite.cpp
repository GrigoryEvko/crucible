// A lease reads its connection and cannot rewrite it.  The identity of a
// leased connection is pool bookkeeping: a holder that replaced it would move
// the slot to another remote behind the back of the remote count.

#include <crucible/cntp/ConnectionPoolRuntime.h>
#include <fixy/Ctx.h>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace fe = ::foundation::effects;

int main() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::Tcp, 2, 1>(init);

    cog::CogIdentity first{};
    first.uuid = cog::Uuid{1, 1};
    cog::CogIdentity second{};
    second.uuid = cog::Uuid{2, 2};
    auto socket = cntp::admit_socket_fd(3).value();
    auto id = cntp::admit_connection_id(4).value();
    auto held = cntp::mint_connection<cntp::TransportClass::Tcp>(socket, first, id).value();
    auto other = cntp::mint_connection<cntp::TransportClass::Tcp>(socket, second, id).value();
    auto added = pool.add_connection(bg, std::move(held));
    (void)added;

    auto lease = pool.lease(bg, first);
    **lease = other.peek();
    drop(std::move(other));
    return 0;
}

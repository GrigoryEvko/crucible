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
    auto socket = cntp::admit_socket_fd(3).expect("3 is not negative");
    auto id = cntp::admit_connection_id(4).expect("4 is positive");
    auto held = cntp::mint_connection<cntp::TransportClass::Tcp>(socket, first, id).expect("the remote is valid");
    auto other = cntp::mint_connection<cntp::TransportClass::Tcp>(socket, second, id).expect("the remote is valid");
    auto added = pool.add_connection(bg, std::move(held));
    (void)added;

    auto lease = pool.lease(bg, first).expect("the pool holds a connection of the remote");
    *lease = other.peek();
    drop(std::move(other));
    return 0;
}

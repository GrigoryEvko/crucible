// mint_connection is the only door to a connection, because it is the one
// place that refuses a remote whose UUID is zero.  The constructor is
// private, so the linear mint cannot build a connection around the check.

#include <crucible/cntp/ConnectionPool.h>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;

int main() {
    cog::CogIdentity remote{};
    remote.uuid = cog::Uuid{1, 2};
    auto socket = cntp::admit_socket_fd(3).value();
    auto id = cntp::admit_connection_id(4).value();
    auto minted = cntp::mint_connection<cntp::TransportClass::Tcp>(socket, remote, id);
    (void)minted;
    auto forged = ::fixy::mint_linear<cntp::Connection<cntp::TransportClass::Tcp>>(socket, cog::Uuid{}, id);
    (void)forged;
    return 0;
}

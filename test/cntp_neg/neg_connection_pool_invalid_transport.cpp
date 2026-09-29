// A connection exists only for an enumerated transport class.  A value cast
// in from outside the enumeration fails the PoolTransportClass gate of
// mint_connection.

#include <crucible/cntp/ConnectionPool.h>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;

int main() {
    cog::CogIdentity remote{};
    remote.uuid = cog::Uuid{1, 2};
    auto socket = cntp::admit_socket_fd(3).value();
    auto id = cntp::admit_connection_id(4).value();
    auto connection = cntp::mint_connection<static_cast<cntp::TransportClass>(255)>(socket, remote, id);
    (void)connection;
    return 0;
}

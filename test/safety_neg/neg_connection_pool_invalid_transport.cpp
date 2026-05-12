// GAPS-136 fixture #2: connection pools are only defined for the enumerated
// transport classes. An out-of-catalog enum value must fail the concept gate.

#include <crucible/cntp/ConnectionPool.h>

namespace cntp = crucible::cntp;

using BadConnection = cntp::Connection<static_cast<cntp::TransportClass>(255)>;

int main() {
    (void)sizeof(BadConnection);
    return 0;
}

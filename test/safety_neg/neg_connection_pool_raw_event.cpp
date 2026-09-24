// GAPS-136 fixture #1: pool lease/reuse audit events must be minted as
// source::ConnectionPool. Raw PoolEvent cannot cross the runtime boundary.

#include <crucible/cntp/ConnectionPool.h>

namespace cntp = crucible::cntp;

void requires_declared(cntp::DeclaredPoolEvent) {}

int main() {
    cntp::PoolEvent raw{};
    requires_declared(raw);
    return 0;
}

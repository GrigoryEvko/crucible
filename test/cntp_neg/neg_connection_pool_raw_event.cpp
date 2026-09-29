// A pool event crosses the runtime boundary under the ConnectionPool tag.  A
// bare PoolEvent has no conversion to the tagged event.

#include <crucible/cntp/ConnectionPool.h>

namespace cntp = crucible::cntp;

namespace {
void requires_declared(cntp::DeclaredPoolEvent) {}
}  // namespace

int main() {
    cntp::PoolEvent raw{};
    requires_declared(raw);
    return 0;
}

// A concurrent row holds resource tags only.  A plain int names no axis
// and no budget, so the row refuses it at the ResourceTag constraint,
// before any sum can read a demand from it.

#include <foundation/effects/Concurrent.h>

namespace fe = ::foundation::effects;

int main() {
    [[maybe_unused]] fe::ConcurrentRow<int> row{};
    return 0;
}

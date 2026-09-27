// A row that names the SM axis twice, with budgets whose sum passes the
// top of uint64_t, would read as a demand of one after the wrap.  The
// row refuses the pack at its constraint, so the wrapped demand never
// becomes a type and never reaches a hash or a fitting check.

#include <foundation/effects/Concurrent.h>

#include <cstdint>

namespace fe = ::foundation::effects;

int main() {
    [[maybe_unused]] fe::ConcurrentRow<fe::resource::SmBudget<UINT64_MAX>, fe::resource::SmBudget<2>> row{};
    return 0;
}

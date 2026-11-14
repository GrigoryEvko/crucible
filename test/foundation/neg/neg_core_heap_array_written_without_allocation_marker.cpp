// mint_heap_array_written takes the allocation capability first, so each heap
// allocation shows at its call.  A different capability tag fails the gate.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    auto written = ::foundation::core::mint_heap_array_written<int>(
        ::foundation::effects::testing::test().io, 1,
        [](::foundation::core::WriteLease<int>& lease) noexcept { lease.put(1); });
    return static_cast<int>(written.size());
}

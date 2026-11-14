// The writer of mint_heap_array_written gets a WriteLease, and the lease has
// no read.  The first element has no write yet, so a read of it would give
// bytes that hold no value.  The read does not compile.

#include <cstdint>

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    auto written = ::foundation::core::mint_heap_array_written<std::uint32_t>(
        ::foundation::effects::testing::test().alloc, 2,
        [](::foundation::core::WriteLease<std::uint32_t>& lease) noexcept {
            std::uint32_t const first = *lease.get(0).expect("the lease holds two elements");
            lease.put(first);
            lease.put(first);
        });
    return static_cast<int>(written.size());
}

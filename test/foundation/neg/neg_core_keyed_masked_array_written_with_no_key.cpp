// A KeyedMaskedArray lends an element that a body writes only with its key.
// A body of a keyed door can reach the holder through a Ref, and a door with no
// key would let it lend the same element a second time.

#include <foundation/core/Array.h>
#include <foundation/core/KeyedArray.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace core = ::foundation::core;

int main() {
    auto [table, key] =
        core::mint_keyed_owner(core::keyed_masked_array_owner<std::uint32_t>,
                               core::mint_masked_array<std::uint32_t>(::foundation::effects::testing::test().alloc, 8));
    table.with_masked(3, [](std::uint32_t& slot) noexcept { slot = 1; });
    return static_cast<int>(table.at_masked(3));
}

// A body of a keyed element door opens a second door of the same holder with a
// captured key.  The second door could replace the storage under the element
// of the first.  The gate refuses a body that reaches a key of the brand.

#include <foundation/core/Array.h>
#include <foundation/core/KeyedArray.h>
#include <foundation/core/KeyedMint.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace core = ::foundation::core;

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto [table, key] = core::mint_keyed_owner(core::keyed_masked_array_owner<std::uint32_t>,
                                               core::mint_masked_array<std::uint32_t>(alloc, 8));
    table.with_masked(key, 3, [&table, &key, alloc](std::uint32_t& slot) noexcept {
        table.replace(key, core::mint_masked_array<std::uint32_t>(alloc, 16));
        slot = 1;
    });
    return static_cast<int>(table.at_masked(3));
}

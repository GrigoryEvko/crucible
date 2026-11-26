// A KeyedMaskedArray takes no assignment.  An assignment would end the
// storage of the holder with no key, while a body of a keyed door holds a
// reference into it.  New storage comes only through replace and the key.

#include <foundation/core/Array.h>
#include <foundation/core/KeyedArray.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace core = ::foundation::core;

// The brand of the two holders.  No key of it is necessary for the
// assignment.
struct AssignedBrand final {};

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    core::KeyedMaskedArray<std::uint32_t, AssignedBrand> table{core::mint_masked_array<std::uint32_t>(alloc, 8)};
    core::KeyedMaskedArray<std::uint32_t, AssignedBrand> other{core::mint_masked_array<std::uint32_t>(alloc, 4)};
    table = std::move(other);
    return static_cast<int>(table.size());
}

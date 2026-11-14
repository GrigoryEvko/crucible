// mint_heap_array_with takes the allocation capability first, so each heap
// allocation shows at its call.  A different capability tag fails the gate.

#include <cstddef>

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    auto numbers = ::foundation::core::mint_heap_array_with<int>(
        ::foundation::effects::testing::test().io, 4,
        [](std::size_t position) noexcept { return static_cast<int>(position); });
    return static_cast<int>(numbers.size());
}

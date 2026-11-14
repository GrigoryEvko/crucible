// mint_heap_array_with builds each element in place from the result of init,
// so the result is exactly the element type.  An init that gives a long for
// an array of int would convert each value, and it fails the gate.

#include <cstddef>

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    auto numbers = ::foundation::core::mint_heap_array_with<int>(
        ::foundation::effects::testing::test().alloc, 4,
        [](std::size_t position) noexcept { return static_cast<long>(position); });
    return static_cast<int>(numbers.size());
}

// The alignment of the storage of a HeapArray is a power of two.  An
// alignment of 12 fails the gate of the mint.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    auto numbers = ::foundation::core::mint_heap_array<int, 12>(::foundation::effects::Alloc{}, 4, 1);
    return static_cast<int>(numbers.size());
}

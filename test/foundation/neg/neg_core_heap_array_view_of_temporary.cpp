// A View of a temporary HeapArray would point into storage that the end of
// the full expression frees, so view() of an rvalue is refused.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    auto elements = ::foundation::core::mint_heap_array(::foundation::effects::Alloc{}, 4, 1).view();
    return static_cast<int>(elements.size());
}

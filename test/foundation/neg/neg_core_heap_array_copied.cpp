// A HeapArray owns its storage alone, so a copy, which would free the
// storage two times, is refused.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::HeapArray<int> const first =
        ::foundation::core::mint_heap_array(::foundation::effects::Alloc{}, 4, 1);
    ::foundation::core::HeapArray<int> const second = first;
    return static_cast<int>(second.size());
}

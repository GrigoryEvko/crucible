// A body of the free frame door of a HeapArray that can throw.  The compiler
// orders the refused twin of the door below the door, so the twin binds to this
// body and gives the reason.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

#include <utility>

int main() {
    ::foundation::core::HeapArray<int> array =
        ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 0);
    array = ::foundation::core::with_mut(std::move(array), [](auto run) { static_cast<void>(run.size()); });
    return static_cast<int>(array.view().size());
}

// The body of a scope runs with no throw.  An exception out of the body would
// leave the frame of the mint past the blocks that the body holds, so the
// mint refuses a body that is not noexcept.

#include <foundation/effects/Effect.h>
#include <foundation/heap/HeapScope.h>

int main() {
    auto const context = ::foundation::effects::testing::test();
    return ::foundation::heap::mint_heap_scope(context.alloc, [](auto& scope) {
        scope.flush_bins();
        return 0;
    });
}

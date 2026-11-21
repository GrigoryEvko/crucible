// The body of a scope runs with no throw.  An exception out of the body would
// leave the frame of the mint past the blocks that the body holds, so the
// mint refuses a body that is not noexcept.

#include <foundation/effects/Effect.h>
#include <foundation/heap/HeapScope.h>
#include <foundation/permissions/Permission.h>

#include <utility>

int main() {
    auto const context = ::foundation::effects::testing::test();
    auto owner = ::foundation::permissions::mint_permission_root<::foundation::heap::heap_tag::Owner>();
    return ::foundation::heap::mint_heap_scope(context.alloc, std::move(owner), [](auto& scope) {
        scope.flush_bins();
        return 0;
    });
}

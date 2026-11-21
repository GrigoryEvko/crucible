// The doors of a scope take only a class with a slab, 1 to 52.  The class 53
// needs the large runs, so its allocation does not compile.

#include <foundation/alloc/Layout.h>
#include <foundation/effects/Effect.h>
#include <foundation/heap/HeapScope.h>

#include <utility>

int main() {
    auto const context = ::foundation::effects::testing::test();
    return ::foundation::heap::mint_heap_scope(context.alloc, [](auto& scope) noexcept {
        auto block = scope.template allocate<::foundation::alloc::SizeClass{53}>();
        scope.release(std::move(block));
        return 0;
    });
}

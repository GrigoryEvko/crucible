// Each scope has the scope brand of its call site, and each block carries the
// brand of its scope.  So the release of a scope of another brand does not
// take a block, also on the same thread.

#include <foundation/alloc/Layout.h>
#include <foundation/effects/Effect.h>
#include <foundation/heap/HeapScope.h>

#include <utility>

int main() {
    auto const context = ::foundation::effects::testing::test();
    return ::foundation::heap::mint_heap_scope(context.alloc, [&](auto& outer) noexcept {
        return ::foundation::heap::mint_heap_scope(context.alloc, [&](auto& inner) noexcept {
            auto block = outer.template allocate<::foundation::alloc::SizeClass{1}>();
            inner.release(std::move(block));
            return 0;
        });
    });
}

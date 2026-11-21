// The result of the body of a scope must not name the brand of the scope.  A
// block that leaves the body would outlive the scope and could reach a
// release of another thread, so the mint refuses the body.

#include <foundation/alloc/Layout.h>
#include <foundation/effects/Effect.h>
#include <foundation/heap/HeapScope.h>

int main() {
    auto const context = ::foundation::effects::testing::test();
    auto block = ::foundation::heap::mint_heap_scope(context.alloc, [](auto& scope) noexcept {
        return scope.template allocate<::foundation::alloc::SizeClass{1}>();
    });
    static_cast<void>(block.address());
    return 0;
}

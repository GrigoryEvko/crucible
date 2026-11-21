// The result of the body of a scope must not name the brand of the scope.  A
// block that leaves the body would outlive the scope and could reach a
// release of another thread, so the mint refuses the body.

#include <foundation/alloc/Layout.h>
#include <foundation/effects/Effect.h>
#include <foundation/heap/HeapScope.h>
#include <foundation/permissions/Permission.h>

#include <utility>

int main() {
    auto const context = ::foundation::effects::testing::test();
    auto owner = ::foundation::permissions::mint_permission_root<::foundation::heap::heap_tag::Owner>();
    auto block = ::foundation::heap::mint_heap_scope(context.alloc, std::move(owner), [](auto& scope) noexcept {
        return scope.template allocate<::foundation::alloc::SizeClass{1}>();
    });
    static_cast<void>(block.address());
    return 0;
}

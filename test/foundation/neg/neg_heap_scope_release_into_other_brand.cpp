// A block carries the brand of the permission of its scope.  So the release
// of a scope of another brand does not take it, also on the same thread.

#include <foundation/alloc/Layout.h>
#include <foundation/effects/Effect.h>
#include <foundation/heap/HeapScope.h>
#include <foundation/permissions/Permission.h>

#include <utility>

int main() {
    auto const context = ::foundation::effects::testing::test();
    auto outer_owner = ::foundation::permissions::mint_permission_root<::foundation::heap::heap_tag::Owner>();
    auto inner_owner = ::foundation::permissions::mint_permission_root<::foundation::heap::heap_tag::Owner>();
    return ::foundation::heap::mint_heap_scope(context.alloc, std::move(outer_owner), [&](auto& outer) noexcept {
        return ::foundation::heap::mint_heap_scope(context.alloc, std::move(inner_owner), [&](auto& inner) noexcept {
            auto block = outer.template allocate<::foundation::alloc::SizeClass{1}>();
            inner.release(std::move(block));
            return 0;
        });
    });
}

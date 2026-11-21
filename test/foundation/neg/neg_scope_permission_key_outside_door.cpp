// Only the scope door that a tag names builds the key of its scope mint.  A
// body of a heap scope can name the brand of its scope, so a key in its hands
// would mint a second permission of the owner of that brand.  The key of the
// tag of the owner of a heap thus does not build outside HeapScopeDoor.

#include <foundation/heap/HeapScope.h>
#include <foundation/permissions/Permission.h>

namespace {

struct HeldBrand final {};

}  // namespace

int main() {
    ::foundation::permissions::scope_mint_key<::foundation::heap::heap_tag::Owner> const key{};
    auto token = ::foundation::permissions::mint_scope_permission<::foundation::heap::heap_tag::Owner, HeldBrand>(key);
    static_cast<void>(token);
    return 0;
}

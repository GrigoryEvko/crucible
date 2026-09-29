// std::make_unique puts an exclusive token on the heap, and
// unique_ptr::release then gives up the only owner of the storage.
// Permission deletes its class-scope operator new, so std::make_unique of
// a token is refused inside the library.

#include <foundation/permissions/Permission.h>

#include <memory>
#include <utility>

namespace fp = ::foundation::permissions;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    auto root = fp::mint_permission_root<Region>();
    auto owner = std::make_unique<decltype(root)>(std::move(root));
    (void)owner.release();
    return 0;
}

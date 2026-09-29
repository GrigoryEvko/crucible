// A new-expression gives an exclusive token storage that no scope ends.
// Each copy of the pointer reaches the one token, so two holders can move
// it out and hold two tokens for one region.  Permission deletes its
// class-scope operator new, so the expression is refused.

#include <foundation/permissions/Permission.h>

#include <utility>

namespace fp = ::foundation::permissions;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    auto root = fp::mint_permission_root<Region>();
    auto* leaked = new decltype(root)(std::move(root));
    (void)leaked;
    return 0;
}

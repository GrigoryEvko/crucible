// One split authorizes one rebuild.  A copied receipt would authorize a
// second, so the copy constructor is deleted with its reason.

#include <fixy/OwnedRegion.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <utility>

namespace {
struct Buffer {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    int storage[4] = {};
    auto region =
        ::fixy::mint_owned_region(storage, std::size_t{4}, ::foundation::permissions::mint_permission_root<Buffer>());
    auto parts = ::fixy::mint_split<2>(std::move(region));
    auto second_receipt = parts.witness;
    (void)second_receipt;
    return 0;
}

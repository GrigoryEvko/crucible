// A share carries the brand of the exclusive it came from, so shares of
// two regions of one tag are two types and a callee that wants two
// shares of one region refuses the pair by deduction.

#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

template <class Brand>
constexpr void same_region(::foundation::permissions::SharedPermission<Region, Brand>,
                           ::foundation::permissions::SharedPermission<Region, Brand>) noexcept {}
}  // namespace

int main() {
    auto first = ::foundation::permissions::mint_permission_share(
        ::foundation::permissions::mint_permission_root<Region>());
    auto second = ::foundation::permissions::mint_permission_share(
        ::foundation::permissions::mint_permission_root<Region>());
    same_region(first, second);
    return 0;
}

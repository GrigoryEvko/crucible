// The children of a split carry their parent's brand, and a combine
// demands that its two children agree.  The left child of one region
// and the right child of another region of the same tag satisfy the
// manifest and fail the brand check, which is what keeps two regions
// from being folded into one token.

#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct Left {
    using permission_row = ::foundation::effects::Row<>;
};
struct Right {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

namespace foundation::permissions {
template <>
struct splits_into<Whole, Left, Right> : std::true_type {};
template <>
struct splits_into_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    auto first = ::foundation::permissions::mint_permission_split<Left, Right>(
        ::foundation::permissions::mint_permission_root<Whole>());
    auto second = ::foundation::permissions::mint_permission_split<Left, Right>(
        ::foundation::permissions::mint_permission_root<Whole>());
    auto folded = ::foundation::permissions::mint_permission_combine<Whole>(std::move(first.first),
                                                                           std::move(second.second));
    (void)folded;
    return 0;
}

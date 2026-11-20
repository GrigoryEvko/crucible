// Two singleton roots of two child tags share the erased identity, so the
// brand check of a combine does not tell them apart.  Their parent would be
// an erased token of the parent tag, and no count stands behind it: a
// singleton root of the parent tag would then be a second erased token of
// that tag.  So the combine refuses children on the erased identity.

#include <foundation/permissions/Permission.h>

#include <type_traits>

// The children are singleton roots, so each tag has a stable identity and
// external linkage.
namespace neg_permission_combine_of_singleton_roots_types {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct Left {
    using permission_row = ::foundation::effects::Row<>;
};
struct Right {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace neg_permission_combine_of_singleton_roots_types

using namespace neg_permission_combine_of_singleton_roots_types;

namespace foundation::permissions {
template <>
struct can_split_into<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    [[maybe_unused]] auto whole = ::foundation::permissions::mint_permission_combine<Whole>(
        ::foundation::permissions::mint_singleton_root<Left>(),
        ::foundation::permissions::mint_singleton_root<Right>());
    return 0;
}

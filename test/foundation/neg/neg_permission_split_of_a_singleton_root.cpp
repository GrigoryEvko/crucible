// The singleton root of a tag is on the erased identity, and the live count
// of its tag stands behind it.  Its children would be erased tokens of the
// child tags, and no count stands behind them: a singleton root of a child
// tag would then be a second erased token of that tag.  So the split
// refuses a parent on the erased identity.

#include <foundation/permissions/Permission.h>

#include <type_traits>

// The parent is a singleton root, so its tag has a stable identity and
// external linkage.
namespace neg_permission_split_of_a_singleton_root_types {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct Left {
    using permission_row = ::foundation::effects::Row<>;
};
struct Right {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace neg_permission_split_of_a_singleton_root_types

using namespace neg_permission_split_of_a_singleton_root_types;

namespace foundation::permissions {
template <>
struct can_split_into<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    [[maybe_unused]] auto halves = ::foundation::permissions::mint_permission_split<Left, Right>(
        ::foundation::permissions::mint_singleton_root<Whole>());
    return 0;
}

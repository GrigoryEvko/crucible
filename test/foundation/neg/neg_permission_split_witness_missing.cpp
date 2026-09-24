// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A split needs two traits declared together: the manifest that says the
// parent splits into the two children, and the authoring witness beside
// it.  A forged manifest written alone, far from the tags, lacks the
// witness, and the witness check in the body of the split refuses it.
//
// Expected diagnostic: the static_assert in mint_permission_split that
// asks for has_split_authoring_witness.

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
struct can_split_into<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto whole = perm::mint_permission_root<Whole>();
    [[maybe_unused]] auto halves = perm::mint_permission_split<Left, Right>(std::move(whole));
    return 0;
}

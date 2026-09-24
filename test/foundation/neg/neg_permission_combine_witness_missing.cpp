// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A combine needs the manifest and its authoring witness, as a split
// does.  The manifest for Other here is written alone, far from the
// tags, with no witness beside it, and the witness check in the body of
// the combine refuses it.
//
// Expected diagnostic: the static_assert in mint_permission_combine that
// asks for has_split_authoring_witness.

#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct Other {
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
template <>
struct has_split_authoring_witness<Whole, Left, Right> : std::true_type {};
template <>
struct can_split_into<Other, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto whole = perm::mint_permission_root<Whole>();
    auto halves = perm::mint_permission_split<Left, Right>(std::move(whole));
    [[maybe_unused]] auto rebuilt = perm::mint_permission_combine<Other>(std::move(halves.first), std::move(halves.second));
    return 0;
}

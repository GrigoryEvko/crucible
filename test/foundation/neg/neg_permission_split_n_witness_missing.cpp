// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A pack manifest needs its authoring witness beside it.  The manifest
// here is written alone, and the witness check in the body of split_n
// refuses it.
//
// Expected diagnostic: the static_assert in mint_permission_split_n that
// asks for has_split_pack_authoring_witness.

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
struct can_split_into_pack<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto whole = perm::mint_permission_root<Whole>();
    [[maybe_unused]] auto parts = perm::mint_permission_split_n<Left, Right>(std::move(whole));
    return 0;
}

// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A combine rebuilds a parent from its two children, so the manifest
// must say that the parent splits into those children.  The halves here
// come from a split of Whole, and the combine asks for Other.  Only the
// authoring witness is written for Other, as a forger who knew one of
// the two traits would write it.  The fit concept of the token form
// reads no manifest, so the manifest check in the body is what refuses
// the call.
//
// Expected diagnostic: the static_assert in mint_permission_combine that
// asks for can_split_into<In, L, R>.

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
struct has_split_authoring_witness<Other, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto whole = perm::mint_permission_root<Whole>();
    auto halves = perm::mint_permission_split<Left, Right>(std::move(whole));
    [[maybe_unused]] auto rebuilt =
        perm::mint_permission_combine<Other>(std::move(halves.first), std::move(halves.second));
    return 0;
}

// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An n-ary split hands out one token per child, so a manifest must say
// that the parent splits into exactly those children.  Only the
// authoring witness is written for this pack, as a forger who knew one
// of the two traits would write it.  The fit concept of the token form
// reads no manifest, so the manifest check in the body of split_n is
// what refuses the call.
//
// Expected diagnostic: the static_assert in mint_permission_split_n that
// asks for can_split_into_pack.

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
struct has_split_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto whole = perm::mint_permission_root<Whole>();
    [[maybe_unused]] auto parts = perm::mint_permission_split_n<Left, Right>(std::move(whole));
    return 0;
}

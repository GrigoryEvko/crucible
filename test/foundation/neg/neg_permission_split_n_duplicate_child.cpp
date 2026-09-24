// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An n-ary split hands out one token per child, so two children of one
// tag would be two owners of one region.  The manifest here names the
// same tag twice, with its witness, and the distinctness check in the
// body of split_n refuses it.
//
// Expected diagnostic: the static_assert in mint_permission_split_n that
// asks for pairwise distinct child tags.

#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct Half {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Half, Half> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Half, Half> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto whole = perm::mint_permission_root<Whole>();
    [[maybe_unused]] auto parts = perm::mint_permission_split_n<Half, Half>(std::move(whole));
    return 0;
}

// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A split without a context names no scope that may touch the halves.
// That is sound only when every row is empty.  The parent here was
// minted under a context that does IO, and the token form of the split
// is then refused.  Its fit concept admits the call, because with no
// context it has no row to compare, and the static_assert in the body is
// the only check.
//
// Expected diagnostic: the static_assert in mint_permission_split that
// asks for empty rows when no context is passed.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace {
struct Whole {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
struct Left {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
struct Right {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace eff = ::foundation::effects;
    namespace perm = ::foundation::permissions;
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>
        test_ctx{eff::testing::test()};
    auto whole = perm::mint_permission_root<Whole>(test_ctx);
    [[maybe_unused]] auto halves = perm::mint_permission_split<Left, Right>(std::move(whole));
    return 0;
}

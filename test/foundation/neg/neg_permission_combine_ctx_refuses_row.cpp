// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A ctx-bound combine rebuilds the parent under its context, so the
// context must admit the rows of the parent and of both children.  The
// halves here were split under a context that does IO, and the
// foreground context admits no IO, so the combine's fit concept refuses
// the call.  The shape half of the concept alone admits it, because the
// arguments are one context and two moved tokens.
//
// Expected diagnostic: the combine's fit concept is not satisfied.

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
    auto halves = perm::mint_permission_split<Left, Right>(test_ctx, std::move(whole));
    [[maybe_unused]] auto rebuilt = perm::mint_permission_combine<Whole>(
        ::foundation::effects::testing::foreground(), std::move(halves.first), std::move(halves.second));
    return 0;
}

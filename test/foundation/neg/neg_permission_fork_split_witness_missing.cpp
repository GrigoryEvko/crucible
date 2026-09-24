// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A split manifest is two specializations, the pack trait and its
// authoring witness, so a forged pack trait alone does not open the
// fork.  Here the pack trait is declared and the witness is not.  The fit
// concept of the inline arm refuses the call at its door and names the
// witness.
//
// Without the witness conjunct the call passes the door and fails later,
// inside the body, at the split's static assertion.  That failure names
// no fork, so the regexes below tell the two apart.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

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

using FgCtx = ::foundation::effects::detail::ctx_witnesses::FgWitness;
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    auto whole = ::foundation::permissions::mint_permission_root<Whole>();
    [[maybe_unused]] auto rebuilt = ::foundation::permissions::mint_permission_fork_inline<Left, Right>(
        ::foundation::effects::testing::foreground(), std::move(whole),
        [](::foundation::permissions::Permission<Left>, FgCtx const&) noexcept {},
        [](::foundation::permissions::Permission<Right>, FgCtx const&) noexcept {});
    return 0;
}

// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A caller with no friendship names the fork body without calling it:
// it takes the address of one instantiation, which a later call through
// the pointer would use with no access check.  Naming a private member
// is itself the access, so the address is refused where it is taken.
//
// Expected diagnostic: the body is private within this context.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <type_traits>

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

struct LeftBody {
    void operator()(::foundation::permissions::Permission<Left>, FgCtx const&) const noexcept {}
};
struct RightBody {
    void operator()(::foundation::permissions::Permission<Right>, FgCtx const&) const noexcept {}
};
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    // The target type deduces everything after the child pack.
    using Door = ::foundation::permissions::Permission<Whole> (*)(FgCtx const&,
                                                                   ::foundation::permissions::Permission<Whole>&&,
                                                                   LeftBody&&, RightBody&&) noexcept;
    [[maybe_unused]] Door door = &::foundation::permissions::PermissionForkRunner::run_<false, Left, Right>;
    return 0;
}

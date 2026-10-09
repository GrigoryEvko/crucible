// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A caller with no friendship names the body of the fork without calling
// it: it takes the address of one instantiation, which a later call
// through the pointer would use with no access check.  Naming a private
// member is itself the access, so the address is refused where it is
// taken.
//
// Expected diagnostic: the body is private within this context.

#include <fixy/concurrent/PermissionFork.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

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

using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;

// The brand that the target type names for the region of the fork.
struct WholeBrand {};

struct LeftBody {
    void operator()(auto const& /*left_view*/, BgCtx const&) const noexcept {}
};
struct RightBody {
    void operator()(auto const& /*right_view*/, BgCtx const&) const noexcept {}
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
    using Door = ::foundation::permissions::Permission<Whole, WholeBrand> (*)(
        BgCtx const&, ::foundation::permissions::Permission<Whole, WholeBrand>&&, LeftBody&&, RightBody&&) noexcept;
    [[maybe_unused]] Door door = &::fixy::concurrent::PermissionForkThreads::run_<Left, Right>;
    return 0;
}

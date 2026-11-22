// Negative-compile test. This file must fail to compile.
//
// A body of a fork gives its value back to the caller of the fork (the
// value of a body of foundation/permissions/PermissionFork.h).  The value
// leaves the frame of the body, so it holds no borrow.  A View is a borrow,
// so the fork refuses a body that gives one.

#include <foundation/core/Region.h>
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
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace fp = ::foundation::permissions;
    auto whole = fp::mint_permission_root<Whole>();
    [[maybe_unused]] auto rebuilt = fp::mint_permission_fork_inline<Left, Right>(
        ::foundation::effects::testing::foreground(), std::move(whole),
        [](auto const& /*left_view*/, auto const& /*ctx*/) noexcept { return ::foundation::core::View<int const>{}; },
        [](auto const& /*right_view*/, auto const& /*ctx*/) noexcept {});
    return 0;
}

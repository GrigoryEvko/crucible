// Negative-compile test. This file must fail to compile.
//
// The view that the fork lends a body is the proof of its child, and it
// ends with the body.  The body below keeps a Ref of its view in a variable
// of the caller that it captures by reference, so the caller would hold a
// proof past the join.  The fork owns a copy of each body, and the store
// check of the fork (StoresNoBorrowOutside of foundation/core/CaptureStore.h)
// refuses a body that stores a borrow in an object of the caller.

#include <foundation/Brand.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
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
    namespace core = ::foundation::core;
    auto whole = fp::mint_permission_root<Whole>();
    using WholeBrand = ::foundation::brand::brand_of_t<decltype(whole)>;
    core::Option<core::Ref<fp::WriteView<Left, WholeBrand> const>> kept{core::none};
    [[maybe_unused]] auto rebuilt = fp::mint_permission_fork_inline<Left, Right>(
        ::foundation::effects::testing::foreground(), std::move(whole),
        [&kept](fp::WriteView<Left, WholeBrand> const& left, auto const& /*ctx*/) noexcept {
            kept = core::some(core::erase_brand(core::to(left)));
        },
        [](auto const& /*right_view*/, auto const& /*ctx*/) noexcept {});
    return 0;
}

// The arm of the fork that starts one thread per child runs each body on a
// thread of its own.  The body below keeps a View in a variable of the
// caller that it captures by reference, so the caller would read after the
// join a borrow that the thread of the child made, such as a view of a
// local of that thread.  The fork owns a copy of each body, and its store
// check (StoresNoBorrowOutside of foundation/core/CaptureStore.h) refuses
// such a body.

#include <fixy/concurrent/PermissionFork.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Ctx.h>
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
struct can_split_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    auto whole = ::foundation::permissions::mint_permission_root<Whole>();
    ::foundation::core::View<int const> kept{};
    [[maybe_unused]] auto rebuilt = ::fixy::concurrent::mint_permission_fork<Left, Right>(
        ::foundation::effects::testing::bg(), std::move(whole),
        [&kept](auto const& /*left_view*/, auto const& /*ctx*/) noexcept {
            kept = ::foundation::core::View<int const>{};
        },
        [](auto const& /*right_view*/, auto const& /*ctx*/) noexcept {});
    return 0;
}

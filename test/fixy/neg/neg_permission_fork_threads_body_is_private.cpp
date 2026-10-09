// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The body of the fork splits a parent, starts one thread per child and
// rebuilds the parent.  That is the work of mint_permission_fork, so only
// the mint may reach it: the body is a private static member of
// PermissionForkThreads, and the mint is its only friend.
//
// This caller builds everything a fork needs, honestly: a declared
// partition, a background context and noexcept bodies.  So the only
// rejection left is the access check on the body itself.  A body that any
// translation unit could call would start threads under a context that the
// mint refuses.
//
// Expected diagnostic: the body is private within this context.

#include <fixy/concurrent/PermissionFork.h>
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

using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    auto whole = ::foundation::permissions::mint_permission_root<Whole>();
    [[maybe_unused]] auto rebuilt = ::fixy::concurrent::PermissionForkThreads::run_<Left, Right>(
        BgCtx{::foundation::effects::testing::bg()}, std::move(whole),
        [](auto const& /*left_view*/, BgCtx const&) noexcept {},
        [](auto const& /*right_view*/, BgCtx const&) noexcept {});
    return 0;
}

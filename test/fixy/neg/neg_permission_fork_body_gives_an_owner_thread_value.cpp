// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The fork on threads gives the value of each body back to its caller (THE
// VALUE OF A BODY of foundation/permissions/PermissionFork.h).  The value
// crosses from the thread of the body to the caller, so it is Sendable.  A
// value of a class of one owner thread is not Sendable, so the fork refuses
// a body that gives one.

#include <fixy/concurrent/PermissionFork.h>
#include <foundation/Transfer.h>
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

struct[[= ::foundation::transfer::thread_bound{}]] OwnerThreadValue {
    int value = 0;
};

using BgCtx = ::foundation::effects::ExecCtx<::foundation::effects::Bg,
                                             ::foundation::effects::Row<::foundation::effects::Effect::Bg>>;
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
    [[maybe_unused]] auto rebuilt = ::fixy::concurrent::mint_permission_fork<Left, Right>(
        BgCtx{::foundation::effects::testing::bg()}, std::move(whole),
        [](auto const& /*left_view*/, BgCtx const&) noexcept { return OwnerThreadValue{}; },
        [](auto const& /*right_view*/, BgCtx const&) noexcept {});
    return 0;
}

// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// IsSharedPermission holds for a shared read token and not for an
// exclusive one.  A template that asks for a shared token refuses the
// exclusive token, so a reader cannot be handed the write authority of
// the region it reads.
//
// Expected diagnostic: the IsSharedPermission constraint is not
// satisfied.

#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

template <typename T>
    requires ::foundation::permissions::IsSharedPermission<T>
constexpr int takes_a_shared_token(T&&) noexcept {
    return 0;
}
}  // namespace

int main() {
    auto token = ::foundation::permissions::mint_permission_root<Region>();
    return takes_a_shared_token(std::move(token));
}

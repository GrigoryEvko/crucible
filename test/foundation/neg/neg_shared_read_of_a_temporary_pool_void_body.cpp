// Negative-compile test. This file must fail to compile.
//
// The void form of with_shared_read makes the same shape check as the
// value form.  A pool passed as an rvalue is one the caller is giving
// up, so the shape check refuses it here too.  The body here returns
// nothing.
//
// Expected diagnostic: with_shared_read has no viable candidate, and the
// note names WithSharedReadArgs.

#include <foundation/permissions/SharedPermission.h>

#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    namespace perm = ::foundation::permissions;
    perm::SharedPermissionPool pool{perm::mint_permission_root<Region>()};
    const ::foundation::effects::ExecCtx<::foundation::effects::Test,
                                         ::foundation::effects::Row<::foundation::effects::Effect::Nondet>>
        pool_ctx{::foundation::effects::testing::test()};
    [[maybe_unused]] bool ran = perm::with_shared_read(pool_ctx, std::move(pool), [](auto) noexcept {});
    return 0;
}

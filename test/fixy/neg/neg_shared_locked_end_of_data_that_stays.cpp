// A lock has an end only for data that gives up what it holds (LeavesLock).  An
// int gives nothing up, so the lock of an int has no end.

#include <fixy/os/SharedLocked.h>

#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace eff = ::foundation::effects;

namespace {

struct CountTag final {
    using permission_row = eff::Row<>;
};

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const ctx{eff::testing::test()};
    auto root = ::foundation::permissions::mint_permission_root<CountTag>();
    using Brand = typename decltype(root)::brand_type;
    ::fixy::SharedLocked<int, CountTag, Brand> shared{std::move(root), 5};
    static_cast<void>(shared.end_lock(ctx));
    return 0;
}

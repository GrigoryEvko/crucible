// A lock has an end only for data whose leave_lock gives a value that holds no
// borrow of what the data keeps (LeavesLock).  This data gives a View of its
// run, which would outlive the end of the lock, so the lock of it has no end.

#include <fixy/os/SharedLocked.h>

#include <fixy/Ctx.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace eff = ::foundation::effects;

namespace {

struct RunTag final {
    using permission_row = eff::Row<>;
};

class LeavesAView final {
public:
    [[nodiscard]] ::foundation::core::View<int const>
    leave_lock(::fixy::SharedLockEndKey<LeavesAView> const& /*key*/) noexcept {
        return kept_;
    }

private:
    ::foundation::core::View<int const> kept_{};
};

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const ctx{eff::testing::test()};
    auto root = ::foundation::permissions::mint_permission_root<RunTag>();
    using Brand = typename decltype(root)::brand_type;
    ::fixy::SharedLocked<LeavesAView, RunTag, Brand> shared{std::move(root)};
    static_cast<void>(shared.end_lock(ctx));
    return 0;
}

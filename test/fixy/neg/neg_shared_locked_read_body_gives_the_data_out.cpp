// A read body of a SharedLocked gives back the reference to the data that
// the share lent it.  The caller would read the data after the share ends,
// while a writer holds the lease.  The door refuses a body whose result holds
// a borrow.

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
    auto escaped = shared.with_read(ctx, [](int const& value) noexcept -> int const& { return value; });
    return 0;
}

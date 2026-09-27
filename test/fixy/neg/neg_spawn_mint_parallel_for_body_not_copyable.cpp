// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// With more than one shard, each worker thread gets its own copy of the
// body, so the body must be copyable.  The body here is move-only.
// Without the copy check on the door, the call passes the door and fails
// later, where the fan-out copies the body.  The regexes below name the
// door and its failed conjunct, so they tell the two apart.
//
// Expected diagnostic: mint_parallel_for has no viable candidate, and the
// note names the copy conjunct of CtxFitsParallelFor.

#include <fixy/os/Spawn.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct RegionWhole {
    using permission_row = ::foundation::effects::Row<>;
};
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
std::array<int, 16> storage{};

struct MoveOnlyBody {
    MoveOnlyBody() = default;
    MoveOnlyBody(MoveOnlyBody const&) = delete("a body that owns a resource is not copied");
    MoveOnlyBody(MoveOnlyBody&&) noexcept = default;
    template <class Shard>
    void operator()(Shard& shard) noexcept {
        (void)shard;
    }
};
}  // namespace

int main() {
    BgCtx ctx{eff::testing::bg()};
    auto region = fixy::mint_owned_region(storage.data(), storage.size(), perm::mint_permission_root<RegionWhole>());
    [[maybe_unused]] auto whole =
        fixy::spawn::mint_parallel_for<2>(ctx, fixy::concurrent::WorkBudget{}, std::move(region), MoveOnlyBody{});
    return 0;
}

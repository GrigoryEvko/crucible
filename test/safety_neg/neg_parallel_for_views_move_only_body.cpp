// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F01-AUDIT-3 — locks in the API-boundary
// `static_assert(std::is_copy_constructible_v<Body>, ...)`
// in safety/Workload.h's `parallel_for_views<N>` else-branch (N >= 2).
//
// Without the gate, a move-only body (one that captures a
// std::unique_ptr, a Linear<T>, etc.) would fall through to
// `spawn_workers_`, where the per-worker jthread lambda's BY-VALUE
// capture (`[..., body]`) would emit a deep "use of deleted function"
// diagnostic from inside std::__invoke_impl / std::jthread machinery
// — actionable but unpredictable.  The static_assert lifts the
// contract to the API boundary.
//
// The gate fires only for N >= 2 (the parallel branch); the N == 1
// fast path consumes body by direct inline call and does NOT require
// copy-constructibility.  This fixture uses N == 2 to ensure the
// parallel branch is taken.
//
// [GCC-WRAPPER-TEXT] — static_assert with "must be CopyConstructible"
// diagnostic text.

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/Workload.h>

#include <cstdint>
#include <memory>
#include <utility>

struct DataNeg {};

int main() {
    using namespace crucible;

    Arena arena;
    auto perm = safety::mint_permission_root<DataNeg>();
    constexpr std::size_t N = 8;
    auto region = safety::OwnedRegion<std::uint64_t, DataNeg>::adopt(
        effects::Test{}.alloc, arena, N, std::move(perm));

    // A move-only state captured by the body.  std::unique_ptr is the
    // canonical example; capturing it by value into the lambda makes
    // the lambda itself move-only (no copy ctor).
    auto move_only_state = std::make_unique<int>(42);

    // Should FAIL: body captures `move_only_state` by-move into the
    // lambda, so the lambda is move-only.  spawn_workers_'s
    // by-value capture (`[..., body]`) requires Body to be
    // CopyConstructible.  The API-boundary static_assert names the
    // contract directly.
    auto recombined = safety::parallel_for_views<2>(
        std::move(region),
        [state = std::move(move_only_state)](auto sub) noexcept {
            for (auto& x : sub.span()) x = static_cast<std::uint64_t>(*state);
        }
    );
    (void)recombined;

    return 0;
}

// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F02 — locks in the API-boundary
// `static_assert(std::is_copy_constructible_v<Body>, ...)` in
// safety/Workload.h's `parallel_apply_pair<N>` else-branch (N >= 2).
//
// Without the gate, a move-only body would fall through to
// `spawn_workers_pair_`, where the per-worker jthread lambda's
// BY-VALUE capture (`[..., body]`) would emit a deep "use of deleted
// function" diagnostic from inside std::__invoke_impl / std::jthread
// machinery.  The static_assert lifts the contract to the API
// boundary.  Mirrors neg_parallel_for_views_move_only_body /
// neg_parallel_reduce_views_move_only_mapper for the pair primitive.
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

struct DataNegA {};
struct DataNegB {};

int main() {
    using namespace crucible;

    Arena arena;
    constexpr std::size_t N = 8;
    auto region_a = safety::OwnedRegion<std::uint64_t, DataNegA>::adopt(
        effects::Test{}.alloc, arena, N,
        safety::mint_permission_root<DataNegA>());
    auto region_b = safety::OwnedRegion<std::uint64_t, DataNegB>::adopt(
        effects::Test{}.alloc, arena, N,
        safety::mint_permission_root<DataNegB>());

    // Move-only state captured by the body (canonical move-only:
    // std::unique_ptr).
    auto move_only_state = std::make_unique<int>(42);

    // Should FAIL: body captures `move_only_state` by-move; the
    // body-by-value capture in spawn_workers_pair_ requires
    // CopyConstructible.  The API-boundary static_assert names the
    // contract directly.
    auto recombined = safety::parallel_apply_pair<2>(
        std::move(region_a), std::move(region_b),
        [state = std::move(move_only_state)](auto sub_a, auto sub_b) noexcept {
            (void)sub_a; (void)sub_b; (void)*state;
        }
    );
    (void)recombined;

    return 0;
}

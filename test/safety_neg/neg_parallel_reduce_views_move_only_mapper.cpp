// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F04-AUDIT-2 — locks in the API-boundary
// `static_assert(std::is_copy_constructible_v<Mapper>, ...)`
// in safety/Workload.h's `parallel_reduce_views<N, R>` else-branch
// (N >= 2).
//
// Without the gate, a move-only mapper would fall through to
// `spawn_workers_with_partials_`, where the per-worker jthread
// lambda's BY-VALUE capture (`[..., mapper, &slot]`) would emit a
// deep "use of deleted function" diagnostic from inside
// std::__invoke_impl / std::jthread machinery.  The static_assert
// lifts the contract to the API boundary.
//
// The gate fires only for N >= 2 (the parallel branch).  Reducer is
// used by reference in the post-join fold loop and does NOT require
// copy-constructibility — ONLY the mapper does.  This fixture uses
// N == 2 with a move-only mapper to ensure the parallel branch is
// taken.
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
    for (std::size_t i = 0; i < N; ++i) region.span()[i] = 1;

    // Move-only state captured by the mapper.  std::unique_ptr is the
    // canonical example; capturing it by-move into the lambda makes
    // the lambda itself move-only (no copy ctor).
    auto move_only_state = std::make_unique<std::uint64_t>(1);

    // Should FAIL: mapper captures `move_only_state` by-move; the
    // mapper-by-value capture in spawn_workers_with_partials_ requires
    // CopyConstructible.  The API-boundary static_assert names the
    // contract directly.
    auto [sum, recombined] = safety::parallel_reduce_views<2, std::uint64_t>(
        std::move(region),
        /*init=*/std::uint64_t{0},
        [state = std::move(move_only_state)](auto sub) noexcept -> std::uint64_t {
            std::uint64_t local = 0;
            for (auto x : sub.span()) local += x * (*state);
            return local;
        },
        [](std::uint64_t a, std::uint64_t b) noexcept -> std::uint64_t {
            return a + b;
        }
    );
    (void)sum;
    (void)recombined;

    return 0;
}

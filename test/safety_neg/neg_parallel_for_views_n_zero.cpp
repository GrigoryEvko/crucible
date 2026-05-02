// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F01-AUDIT — locks in the `static_assert(N > 0)` body
// assertion in safety/Workload.h's `parallel_for_views<N>` definition.
//
// Mirrors neg_parallel_reduce_views_n_zero.cpp; the same defense
// applies to the for_views variant.  N=0 would degenerate the
// for-each into "no shards" — body never invoked, region passed
// through unchanged.  The static_assert refuses N=0 at the call
// site, naming the precondition.
//
// [GCC-WRAPPER-TEXT] — static_assert with the `requires N > 0` text.

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/Workload.h>

#include <cstdint>

struct DataNeg {};

int main() {
    using namespace crucible;

    Arena arena;
    auto perm = safety::mint_permission_root<DataNeg>();
    constexpr std::size_t N = 8;
    auto region = safety::OwnedRegion<std::uint64_t, DataNeg>::adopt(
        effects::Test{}.alloc, arena, N, std::move(perm));
    for (std::size_t i = 0; i < N; ++i) region.span()[i] = 0;

    // Should FAIL: N=0 violates the `static_assert(N > 0)` precondition.
    auto recombined = safety::parallel_for_views<0>(
        std::move(region),
        [](auto sub) noexcept {
            for (auto& x : sub.span()) x = 1;
        }
    );
    (void)recombined;

    return 0;
}

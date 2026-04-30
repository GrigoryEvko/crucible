// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F02 — locks in the `static_assert(N > 0)` body assertion in
// safety/Workload.h's `parallel_apply_pair<N>` definition.
//
// Mirrors neg_parallel_for_views_n_zero / neg_parallel_reduce_views_n_zero
// for the pair-iterated sibling primitive.  N=0 would degenerate the
// dispatcher into "no shards" — body never invoked, both regions
// passed through unchanged.  The static_assert refuses N=0 at the
// call site, naming the precondition.
//
// [GCC-WRAPPER-TEXT] — static_assert with the `requires N > 0` text.

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/Workload.h>

#include <cstdint>

struct DataNegA {};
struct DataNegB {};

int main() {
    using namespace crucible;

    Arena arena;
    constexpr std::size_t N = 8;
    auto region_a = safety::OwnedRegion<std::uint64_t, DataNegA>::adopt(
        effects::Test{}.alloc, arena, N,
        safety::permission_root_mint<DataNegA>());
    auto region_b = safety::OwnedRegion<std::uint64_t, DataNegB>::adopt(
        effects::Test{}.alloc, arena, N,
        safety::permission_root_mint<DataNegB>());

    // Should FAIL: N=0 violates the `static_assert(N > 0)` precondition.
    auto recombined = safety::parallel_apply_pair<0>(
        std::move(region_a), std::move(region_b),
        [](auto sub_a, auto sub_b) noexcept {
            (void)sub_a; (void)sub_b;
        }
    );
    (void)recombined;

    return 0;
}

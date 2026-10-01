// The merge grid of test_session_global_attack (session_global_attack.h
// gives the grid).  This file runs the grid, and it checks part 0 of the
// cells.  test_session_global_attack_grid_<k>.cpp checks part k.

#include "session_global_attack.h"

#include <cstdio>

namespace test_session_global_attack {

void check_grid_part_0(FamilyCounts& counts) { check_grid_part<0>(counts); }

void run_merge_grid() {
    std::printf("merge grid (%zu pairs)\n", shape_count * shape_count);
    FamilyCounts counts;
    check_grid_part_0(counts);
    check_grid_part_1(counts);
    check_grid_part_2(counts);
    check_grid_part_3(counts);
    check_grid_part_4(counts);
    check_grid_part_5(counts);
    check_grid_part_6(counts);
    std::printf("  %-34s generated=%zu live=%zu variants_live=%zu/%zu projectable_but_unbalanced=%zu\n", "merge grid",
                counts.generated, counts.live_by_construction, counts.variants_live, counts.variants,
                counts.projectable_but_unbalanced);
    expect(counts.live_by_construction > shape_count, "the merge grid accepted too few pairs to mean anything");
}

// The parts of the grid cover its cells, in order.
static_assert(grid_cuts.front() == 0 && grid_cuts.back() == shape_count * shape_count);

}  // namespace test_session_global_attack

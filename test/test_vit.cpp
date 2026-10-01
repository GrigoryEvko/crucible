// A mini vision transformer driven through the dispatch pipeline: a patch
// embedding, one transformer layer and a classification head, forward and
// backward.  test_vit_ops.cpp gives the op table.
//
// The test is several source files of one executable, so that no
// translation unit holds the whole run:
//
//   vit.h                         the shared part
//   this file                     the run up to the memory plan, and main
//   ..._ops.cpp                   the op table and the op packets
//   ..._replay.cpp                the compiled replay

#include "vit.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <cstdint>
#include <cstdio>

using namespace crucible;
using namespace test_vit;

using test::flush_and_wait_region_published;

int main() {
    std::printf("═══ ViT Training Simulation ═══\n\n");
    std::printf("Mini-ViT: patch_embed → 1 transformer layer → cls_head\n");
    std::printf("  batch=%lld  seq=%lld  hidden=%lld  mlp=%lld  classes=%lld\n", static_cast<long long>(B),
                static_cast<long long>(SEQ), static_cast<long long>(D), static_cast<long long>(MLP),
                static_cast<long long>(CL));
    std::printf("Ops/iteration: %u (19 fwd + 11 bwd)\n\n", NUM_OPS);

    Vigil vigil;

    feed_iteration(vigil, 0);
    feed_iteration(vigil, 1);
    feed_trigger(vigil, 2);
    flush_and_wait_region_published(vigil);

    const auto* region = vigil.active_region();
    assert(region && region->plan);
    const auto* plan = region->plan;

    std::printf("── Region ──\n");
    std::printf("   %u ops | pool %llu bytes\n", region->num_ops, static_cast<unsigned long long>(plan->pool_bytes));
    std::printf("   %u slots: %u external + %u internal\n", plan->num_slots, plan->num_external,
                plan->num_slots - plan->num_external);

    std::printf("\n   Key lifetimes (saved for backward):\n");
    for (uint32_t s = 0; s < plan->num_slots; s++) {
        const auto& sl = plan->slots[s];
        // A span of more than ten ops separates an activation held for
        // the backward pass from an ordinary short-lived intermediate.
        if (!sl.is_external && sl.death_op.raw() - sl.birth_op.raw() > 10)
            std::printf("     slot %u: birth=op%u death=op%u (%llu bytes)\n", s, sl.birth_op.raw(), sl.death_op.raw(),
                        static_cast<unsigned long long>(sl.nbytes));
    }

    align_and_complete_iteration(vigil);
    const double ns_op = run_compiled_iterations(vigil);
    verify_attention_data_flow(vigil);

    std::printf("\n═══ Summary ═══\n");
    std::printf("Pool: %llu bytes | Compiled: %u iters | %.1f ns/op\n",
                static_cast<unsigned long long>(plan->pool_bytes), vigil.compiled_iterations(), ns_op);
    std::printf("\ntest_vit: PASSED\n");
    return 0;
}

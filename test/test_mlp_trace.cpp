// A two-layer perceptron driven through the dispatch pipeline the way a
// frontend adapter would drive it, forward and backward:
//
//   input[4,8] → mm(weight1[8,4]) → relu → mm(weight2[4,2]) → output[4,2]
//
// No arithmetic happens here.  The tensors are metadata over fixed fake
// addresses, which is enough for the recorder to see the data flow, and
// the point is the transition from recording every op to replaying them
// out of the pool with nothing allocated per op.
//
// The test is several source files of one executable, so that no
// translation unit holds the whole run:
//
//   mlp_trace.h                   the shared part and the op table
//   this file                     the run up to the memory plan, and main
//   ..._ops.cpp                   the op packets
//   ..._replay.cpp                the compiled replay

#include "mlp_trace.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <cstdint>
#include <cstdio>

using namespace crucible;
using namespace test_mlp_trace;

using test::flush_and_wait_region_published;

int main() {
    std::printf("═══ Crucible MLP Training Simulation ═══\n\n");
    std::printf("Model: input[%lld,%lld] -> mm -> relu -> mm -> output[%lld,%lld]\n", static_cast<long long>(BATCH),
                static_cast<long long>(IN_DIM), static_cast<long long>(BATCH), static_cast<long long>(OUT_DIM));
    std::printf("Ops per iteration: %u (5 forward + 5 backward)\n\n", NUM_OPS);

    Vigil vigil;

    std::printf("── Iteration 0: RECORDING (building signature) ──\n");
    feed_iteration(vigil, 0);
    std::printf("   Recorded %u ops. Mode: RECORDING\n", NUM_OPS);

    std::printf("── Iteration 1: RECORDING (candidate match) ──\n");
    feed_iteration(vigil, 1);
    std::printf("   Recorded %u ops. Mode: RECORDING\n", NUM_OPS);

    // Three iterations are the minimum: the first builds a signature,
    // the second proposes it as a boundary, and the third confirms it.
    std::printf("── Trigger: feeding first %u ops of iter 2 ──\n", IterationDetector::K);
    feed_trigger(vigil, 2);

    flush_and_wait_region_published(vigil);

    std::printf("\n   >>> Iteration boundary detected! <<<\n");
    std::printf("   BackgroundThread built RegionNode with %u ops\n", vigil.active_region()->num_ops);

    const auto* region = vigil.active_region();
    assert(region && region->plan);
    const auto* plan = region->plan;

    std::printf("\n── Memory Plan ──\n");
    std::printf("   Pool size: %llu bytes\n", static_cast<unsigned long long>(plan->pool_bytes));
    std::printf("   Total slots: %u\n", plan->num_slots);
    std::printf("   External slots: %u (parameters)\n", plan->num_external);
    std::printf("   Internal slots: %u (activations — pre-allocated)\n", plan->num_slots - plan->num_external);

    std::printf("\n   Slot details:\n");
    for (uint32_t s = 0; s < plan->num_slots; s++) {
        const auto& slot = plan->slots[s];
        std::printf("     [%2u] %s  %6llu bytes  birth=op%u  death=op%u", s, slot.is_external ? "EXTERN" : "INTERN",
                    static_cast<unsigned long long>(slot.nbytes), slot.birth_op.raw(), slot.death_op.raw());
        if (!slot.is_external) std::printf("  offset=%llu", static_cast<unsigned long long>(slot.offset_bytes));
        std::printf("\n");
    }

    activate_compiled_mode(vigil);
    run_compiled_iterations(vigil);
    verify_data_flow(vigil);

    std::printf("\n═══ Summary ═══\n");
    std::printf("Total compiled iterations: %u\n", vigil.compiled_iterations());
    std::printf("Divergences: %u\n", vigil.diverged_count());
    std::printf("Pool bytes: %llu (vs eager: ~%llu per iteration)\n", static_cast<unsigned long long>(plan->pool_bytes),
                static_cast<unsigned long long>(plan->num_slots - plan->num_external) * BATCH * HIDDEN * 4);
    std::printf("Mode: %s\n", vigil.context().is_compiled() ? "COMPILED" : "RECORDING");
    std::printf("\ntest_mlp_trace: PASSED\n");
    return 0;
}

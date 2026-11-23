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
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;
using namespace test_mlp_trace;

using test::flush_and_wait_region_published;

int main() {
    ::fixy::report(::fixy::Sink::Out, "═══ Crucible MLP Training Simulation ═══\n\n");
    ::fixy::report(::fixy::Sink::Out, "Model: input[{},{}] -> mm -> relu -> mm -> output[{},{}]\n",
                   static_cast<long long>(BATCH), static_cast<long long>(IN_DIM), static_cast<long long>(BATCH),
                   static_cast<long long>(OUT_DIM));
    ::fixy::report(::fixy::Sink::Out, "Ops per iteration: {} (5 forward + 5 backward)\n\n", NUM_OPS);

    Vigil vigil;

    ::fixy::report(::fixy::Sink::Out, "── Iteration 0: RECORDING (building signature) ──\n");
    feed_iteration(vigil, 0);
    ::fixy::report(::fixy::Sink::Out, "   Recorded {} ops. Mode: RECORDING\n", NUM_OPS);

    ::fixy::report(::fixy::Sink::Out, "── Iteration 1: RECORDING (candidate match) ──\n");
    feed_iteration(vigil, 1);
    ::fixy::report(::fixy::Sink::Out, "   Recorded {} ops. Mode: RECORDING\n", NUM_OPS);

    // Three iterations are the minimum: the first builds a signature,
    // the second proposes it as a boundary, and the third confirms it.
    ::fixy::report(::fixy::Sink::Out, "── Trigger: feeding first {} ops of iter 2 ──\n", IterationDetector::K);
    feed_trigger(vigil, 2);

    flush_and_wait_region_published(kTest, vigil);

    ::fixy::report(::fixy::Sink::Out, "\n   >>> Iteration boundary detected! <<<\n");
    const DagRef<const RegionNode> region = vigil.active_region().expect("the flush published a region");
    ::fixy::report(::fixy::Sink::Out, "   BackgroundThread built RegionNode with {} ops\n", region->num_ops());

    CRUCIBLE_TEST_ASSERT(kTest, region->plan.is_some());
    const DagRef<MemoryPlan const> plan = PlanLink{region->plan}.expect("the published region has a memory plan");

    ::fixy::report(::fixy::Sink::Out, "\n── Memory Plan ──\n");
    ::fixy::report(::fixy::Sink::Out, "   Pool size: {} bytes\n", static_cast<unsigned long long>(plan->pool_bytes));
    ::fixy::report(::fixy::Sink::Out, "   Total slots: {}\n", plan->num_slots());
    ::fixy::report(::fixy::Sink::Out, "   External slots: {} (parameters)\n", plan->num_external);
    ::fixy::report(::fixy::Sink::Out, "   Internal slots: {} (activations — pre-allocated)\n",
                   plan->num_slots() - plan->num_external);

    ::fixy::report(::fixy::Sink::Out, "\n   Slot details:\n");
    uint32_t s = 0;
    for (const TensorSlot& slot : plan->slots.view()) {
        ::fixy::report(::fixy::Sink::Out, "     [{}] {}  {} bytes  birth=op{}  death=op{}", s,
                       slot.is_external ? ::fixy::TextView{"EXTERN"} : ::fixy::TextView{"INTERN"},
                       static_cast<unsigned long long>(slot.nbytes), slot.birth_op.raw(), slot.death_op.raw());
        if (!slot.is_external)
            ::fixy::report(::fixy::Sink::Out, "  offset={}", static_cast<unsigned long long>(slot.offset_bytes));
        ::fixy::report(::fixy::Sink::Out, "\n");
        s++;
    }

    activate_compiled_mode(vigil);
    run_compiled_iterations(vigil);
    verify_data_flow(vigil);

    ::fixy::report(::fixy::Sink::Out, "\n═══ Summary ═══\n");
    ::fixy::report(::fixy::Sink::Out, "Total compiled iterations: {}\n", vigil.compiled_iterations());
    ::fixy::report(::fixy::Sink::Out, "Divergences: {}\n", vigil.diverged_count());
    ::fixy::report(::fixy::Sink::Out, "Pool bytes: {} (vs eager: ~{} per iteration)\n",
                   static_cast<unsigned long long>(plan->pool_bytes),
                   static_cast<unsigned long long>(plan->num_slots() - plan->num_external) * BATCH * HIDDEN * 4);
    ::fixy::report(::fixy::Sink::Out, "Mode: {}\n",
                   vigil.context().is_compiled() ? ::fixy::TextView{"COMPILED"} : ::fixy::TextView{"RECORDING"});
    ::fixy::check::pass(kTest, "\ntest_mlp_trace: PASSED\n");
    return 0;
}

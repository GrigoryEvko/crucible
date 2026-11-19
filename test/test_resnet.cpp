// A residual network of fifty layers, driven through the dispatch
// pipeline at production scale.  The shape is a stem, then four
// stages of three, four, six and three bottleneck blocks, then a
// classification head.  That comes to 175 operations and 25,557,032
// parameters, both of which are asserted below, and both of which
// match the reference implementation of this network.
//
// The pipeline under test is the whole of it: two recorded
// iterations, a detected boundary, then a thousand compiled ones.
//
// The test is several source files of one executable, so that no
// translation unit holds the whole run:
//
//   resnet.h                      the shared part
//   this file                     the run up to the memory plan, and main
//   ..._model.cpp                 the network builder and the op packets
//   ..._replay.cpp                the compiled replay

#include "resnet.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <cstdint>
#include <cstdio>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;
using namespace test_resnet;

using test::flush_and_wait_region_published;

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_resnet: ResNet-50 (He et al. 2015)\n");

    ResNet50 net;
    net.build(2);
    ::fixy::report(::fixy::Sink::Out, "  {} ops, {} params, {} param tensors, {} activations\n", net.ops.size(),
                   static_cast<unsigned long long>(net.params), static_cast<uint32_t>(net.np - 1),
                   static_cast<uint32_t>(net.na));

    CRUCIBLE_TEST_ASSERT(kTest, net.ops.size() == 175 && "the forward pass is 175 operations");
    CRUCIBLE_TEST_ASSERT(kTest, net.params == 25557032
                                    && "the parameter count must match the "
                                       "reference implementation");

    Vigil vigil;

    feed_iter(vigil, net.ops, 0);
    feed_iter(vigil, net.ops, 1);
    feed_trigger(vigil, net.ops, 2);
    flush_and_wait_region_published(kTest, vigil);

    const ::fixy::Ref<const crucible::RegionNode> region = vigil.active_region().expect("the flush published a region");
    CRUCIBLE_TEST_ASSERT(kTest, region->plan.is_some());
    const ::fixy::Ref<MemoryPlan const> plan = PlanLink{region->plan}.expect("the published region has a memory plan");
    ::fixy::report(::fixy::Sink::Out, "  region: {} ops, pool {} B, {} slots ({} ext)\n", region->num_ops(),
                   static_cast<unsigned long long>(plan->pool_bytes), plan->num_slots(), plan->num_external);

    align_and_complete_iteration(vigil, net.ops);
    run_compiled_iterations(vigil, net.ops);
    verify_data_flow(vigil, net.ops);

    ::fixy::check::pass(kTest, "test_resnet: PASSED\n");
    return 0;
}

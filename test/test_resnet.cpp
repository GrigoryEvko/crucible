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
#include "test_assert.h"
#include <cstdint>
#include <cstdio>

using namespace crucible;
using namespace test_resnet;

using test::flush_and_wait_region_published;

int main() {
    std::printf("test_resnet: ResNet-50 (He et al. 2015)\n");

    ResNet50 net;
    net.build(2);
    std::printf("  %zu ops, %llu params, %u param tensors, %u activations\n", net.ops.size(),
                static_cast<unsigned long long>(net.params), static_cast<uint32_t>(net.np - 1),
                static_cast<uint32_t>(net.na));

    assert(net.ops.size() == 175 && "the forward pass is 175 operations");
    assert(net.params == 25557032
           && "the parameter count must match the "
              "reference implementation");

    Vigil vigil;

    feed_iter(vigil, net.ops, 0);
    feed_iter(vigil, net.ops, 1);
    feed_trigger(vigil, net.ops, 2);
    flush_and_wait_region_published(vigil);

    const auto* region = vigil.active_region();
    assert(region && region->plan);
    std::printf("  region: %u ops, pool %llu B, %u slots (%u ext)\n", region->num_ops,
                static_cast<unsigned long long>(region->plan->pool_bytes), region->plan->num_slots,
                region->plan->num_external);

    align_and_complete_iteration(vigil, net.ops);
    run_compiled_iterations(vigil, net.ops);
    verify_data_flow(vigil, net.ops);

    std::printf("test_resnet: PASSED\n");
    return 0;
}

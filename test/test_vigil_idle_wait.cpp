// The stages of an idle background pipeline park, and a parked pipeline
// still takes each op.
//
// A stage with no work spins for a short time, and then it parks.  So an
// idle Vigil uses little CPU time.  The drain stage polls the trace ring
// after each sleep, each later stage wakes at a push into its channel, and
// the publish stage serves a job of its owner after each timed park.
//
// The first test reads the CPU time of the process across an idle interval
// of the wall clock.  Its bound is loose: half of one core.  Four stages
// that spin use four cores.

#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <fixy/FixedArray.h>
#include <fixy/os/Time.h>

#include "test_assert.h"
#include "test_harness.h"
#include <cstdint>

using crucible::Vigil;

namespace {

constexpr uint64_t kMillisecond = 1000000;

// The tensor of each op.  The detector and the region read the schemas and
// the shapes, so the tensor needs no data address.
crucible::TensorMeta make_meta() noexcept {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = crucible::tensor_dim(1024);
    meta.strides[0] = crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    meta.device_type = crucible::DeviceType::CPU;
    meta.device_idx = 0;
    meta.data_ptr = crucible::external_data_ptr(nullptr);
    return meta;
}

// Records the eight ops of one iteration of a loop.  Each iteration has the
// same schemas and shapes.
void record_iteration(Vigil& vigil) {
    for (uint64_t op = 0; op < 8; ++op) {
        crucible::TraceRing::Entry entry{};
        entry.schema_hash = crucible::SchemaHash{0x100 + op};
        entry.shape_hash = crucible::ShapeHash{0x200 + op};
        entry.num_inputs = 1;
        entry.num_outputs = 1;
        ::fixy::FixedArray<crucible::TensorMeta, 2> metas{};
        metas[0] = make_meta();
        metas[1] = make_meta();
        (void)vigil.record_op(crucible::test::certify_synthetic_entry(entry), metas.data(), 2);
    }
}

// Sleeps for the count of milliseconds, so that each stage goes idle.
void idle_for(uint64_t milliseconds) {
    const fixy::TestRunnerCtx runner{foundation::effects::testing::test()};
    const auto pause = fixy::time::mint_bounded_sleep<kMillisecond>(runner);
    for (uint64_t index = 0; index < milliseconds; ++index)
        (void)pause.sleep_for(kMillisecond);
}

void test_idle_vigil_uses_little_cpu_time() {
    const fixy::TestRunnerCtx runner{foundation::effects::testing::test()};
    const auto cpu = fixy::time::mint_clock_reader<fixy::ClockSource_v::ProcessCpu>(runner);
    const auto wall = fixy::time::mint_clock_reader<fixy::ClockSource_v::Monotonic>(runner);
    Vigil vigil;
    for (uint64_t iteration = 0; iteration < 4; ++iteration)
        record_iteration(vigil);
    vigil.flush();
    const uint64_t cpu_before = cpu.read().expect("the CPU clock of the process gives a reading").peek();
    const uint64_t wall_before = wall.read().expect("the monotonic clock gives a reading").peek();
    idle_for(300);
    const uint64_t cpu_nanos = cpu.read().expect("the CPU clock of the process gives a reading").peek() - cpu_before;
    const uint64_t wall_nanos = wall.read().expect("the monotonic clock gives a reading").peek() - wall_before;
    assert(cpu_nanos < wall_nanos / 2 && "the stages of an idle pipeline used more than half of one core");
}

void test_parked_pipeline_publishes_a_region() {
    // Each iteration comes after an idle interval, so each stage parks
    // before it takes the ops of the iteration.
    Vigil vigil;
    uint64_t iteration = 0;
    for (; iteration < 16 && vigil.current_step() == 0; ++iteration) {
        idle_for(20);
        record_iteration(vigil);
        vigil.flush();
    }
    assert(vigil.current_step() != 0 && "a parked pipeline published no region");
    assert(iteration <= 8);

    // A job of the owner runs on the publish stage, which parks while the
    // pipeline idles.  The timed park lets the stage serve the job.
    idle_for(20);
    assert(vigil.region_store_count() > 0);
}

}  // namespace

int main() {
    test_idle_vigil_uses_little_cpu_time();
    test_parked_pipeline_publishes_a_region();
    return 0;
}

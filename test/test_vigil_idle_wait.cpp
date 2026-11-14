// The stages of an idle background pipeline park, and a parked pipeline
// still takes each op.
//
// A stage with no work spins for a short time, and then it parks.  So an
// idle pipeline uses little CPU.  The drain stage polls the trace ring
// after each sleep, each later stage wakes at a push into its channel, and
// the publish stage serves a job of its owner after each timed park.
//
// The first test counts the user instructions of the whole process while the
// test thread does a fixed number of sleeps.  It reads no clock.  A stage
// that spins runs many more instructions in that interval than a stage that
// parks.  The test thread opens the counter with its children before it
// starts the pipeline.  So the counter also follows the pipeline thread and
// the four stages, and a read sums each thread.  If the host refuses the
// counter, the test prints the reason and skips.

#include <crucible/BackgroundThread.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <fixy/FixedArray.h>
#include <fixy/os/PerfEvent.h>
#include <fixy/os/Process.h>
#include <fixy/os/Time.h>

#include "test_assert.h"
#include "test_harness.h"
#include <cstdint>

using crucible::BackgroundThread;
using crucible::MetaIndex;
using crucible::MetaLog;
using crucible::TraceRing;
using crucible::Vigil;

namespace {

constexpr uint64_t kMillisecond = 1000000;
// The ops of one iteration of the loop of the first test, and its count of
// iterations.
constexpr uint64_t kPeriod = 8;
constexpr uint64_t kIterations = 8;
// The sleeps before the count.  The publish stage then parks for its
// longest bound.
constexpr uint64_t kSettleSleeps = 10;
// The sleeps of 1 ms in the count.
constexpr uint64_t kCountedSleeps = 300;
// The most user instructions of the process for each counted sleep.  The
// drain stage wakes about five times in each sleep, the publish stage about
// one time, and the detect and build stages park until work comes.  An idle
// process ran about 1200 for each sleep in release, 14000 in the default
// build and 30000 under tsan.  With a detect stage that spins, it ran 664000
// in release, 1.78 million in the default build and 8.9 million under tsan.
// The bound is about five times from each of the two nearest counts.  On a
// loaded host each sleep takes longer, so the drain stage wakes more times
// in each sleep.
constexpr uint64_t kInstructionsPerSleep = 150000;

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

TraceRing& test_ring() {
    static TraceRing ring{};
    return ring;
}

MetaLog& test_log() {
    static MetaLog log{};
    return log;
}

// Records the op at position index of the loop, with no tensor.
[[nodiscard]] bool record(TraceRing& ring, uint64_t index) {
    TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{0x100 + index % kPeriod};
    entry.shape_hash = crucible::ShapeHash{0x200 + index % kPeriod};
    return ring.try_append(entry, MetaIndex::none());
}

// Waits until the pipeline processed each op of the ring.
void wait_for_pipeline(BackgroundThread& bt, TraceRing& ring) {
    const fixy::TestRunnerCtx runner{foundation::effects::testing::test()};
    const auto pause = fixy::time::mint_bounded_sleep<kMillisecond>(runner);
    while (bt.total_processed.load_acquire() < ring.total_produced())
        (void)pause.sleep_for(kMillisecond);
}

// True for an error that a host gives when its settings, its kernel or its
// PMU refuse a counter of an unprivileged process.
[[nodiscard]] bool is_refusal(foundation::os::Errno error) noexcept {
    using foundation::os::Errno;
    return error == Errno::eacces || error == Errno::eperm || error == Errno::enoent || error == Errno::enosys
        || error == Errno::eopnotsupp;
}

// Starts the pipeline after the open of counter, gives it the ops of some
// iterations, and counts the instructions of the process in a fixed number
// of sleeps of the test thread.
void check_idle_count(fixy::TestRunnerCtx const& runner, fixy::fs::OwnedFd const& counter) {
    TraceRing& ring = test_ring();
    BackgroundThread bt;
    bt.start(&ring, &test_log());
    for (uint64_t index = 0; index < kPeriod * kIterations; ++index)
        assert(record(ring, index));
    wait_for_pipeline(bt, ring);
    idle_for(kSettleSleeps);
    const uint64_t before = fixy::perf::read_count(runner, counter).expect("the counter of the process gives a count");
    idle_for(kCountedSleeps);
    const uint64_t after = fixy::perf::read_count(runner, counter).expect("the counter of the process gives a count");
    bt.stop();
    const uint64_t count = after - before;
    if (count == 0) {
        ::fixy::report(::fixy::Sink::Out, "[skipped] the counter gave no instruction of the process\n");
        return;
    }
    ::fixy::report(::fixy::Sink::Out,
                   "  the idle pipeline and the test thread ran {} user instructions in {} sleeps of 1 ms\n", count,
                   kCountedSleeps);
    assert(count < kCountedSleeps * kInstructionsPerSleep && "the idle pipeline ran too many instructions");
}

void test_idle_pipeline_runs_few_instructions() {
    const fixy::TestRunnerCtx runner{foundation::effects::testing::test()};
    const fixy::perf::Event instructions = fixy::perf::hardware_event(fixy::perf::HardwareEvent::instructions);
    const fixy::perf::Task process{fixy::process::current_thread(runner), fixy::perf::Children::included};
    fixy::perf::mint_perf_event(runner, instructions, 0, process)
        .match(
            [&runner](fixy::Linear<fixy::fs::OwnedFd> opened) noexcept {
                const fixy::fs::OwnedFd counter = std::move(opened).consume();
                check_idle_count(runner, counter);
            },
            [](foundation::os::Errno error) noexcept {
                assert(is_refusal(error) && "the kernel refused the counter for a cause that is not a host setting");
                ::fixy::report(::fixy::Sink::Out, "[skipped] the host refused the instruction counter (errno {})\n",
                               foundation::os::error_number(error));
            });
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
    test_idle_pipeline_runs_few_instructions();
    test_parked_pipeline_publishes_a_region();
    return 0;
}

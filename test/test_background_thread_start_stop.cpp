// The pipeline thread of a BackgroundThread starts with start() and joins
// with stop(), and a stop with no thread joins nothing.  A start while a
// pipeline thread runs breaks the contract of start(), and the end of a
// BackgroundThread joins a pipeline that runs.  A start after stop() with
// the same ring and log continues the stream of ops, and a start after
// stop() with a different ring or log breaks the contract.  Each broken
// contract runs in a child process through expect_dies of the Check family,
// while the test process has one thread.
// The tsan preset runs the same binary, so each start and each join has a
// check of ThreadSanitizer.

#include <crucible/BackgroundThread.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Time.h>
#include <foundation/effects/Effect.h>

#include "test_harness.h"
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::BackgroundThread;
using crucible::MetaIndex;
using crucible::MetaLog;
using crucible::TraceRing;

namespace {

constexpr uint64_t kPeriod = 5;
constexpr uint64_t kSchemaBase = 0x300;
constexpr uint64_t kPollNanos = 100000;
constexpr uint64_t kIterations = 8;

TraceRing& test_ring() {
    static TraceRing ring{};
    return ring;
}

MetaLog& test_log() {
    static MetaLog log{::foundation::effects::testing::test().alloc};
    return log;
}

[[nodiscard]] ::fixy::InitLoadCtx startup() noexcept {
    return ::fixy::InitLoadCtx{::foundation::effects::testing::init()};
}

// Records kIterations iterations of a loop of kPeriod ops, from the op at
// index first.  Returns the index after the last op.
uint64_t record_loop(TraceRing& ring, uint64_t first) {
    uint64_t index = first;
    for (const uint64_t end = first + kIterations * kPeriod; index < end; ++index) {
        TraceRing::Entry entry{};
        entry.schema_hash = crucible::SchemaHash{kSchemaBase + index % kPeriod};
        entry.shape_hash = crucible::ShapeHash{0x400 + index % kPeriod};
        CRUCIBLE_TEST_ASSERT(kTest, ring.try_append(entry, MetaIndex::none()));
    }
    return index;
}

// Waits until the pipeline processed each op of the ring.
void wait_for_pipeline(BackgroundThread& bt, TraceRing& ring) {
    const fixy::TestRunnerCtx runner{foundation::effects::testing::test()};
    const auto pause = fixy::time::mint_bounded_sleep<kPollNanos>(runner);
    while (bt.total_processed.load_acquire() < ring.total_produced())
        (void)pause.sleep_for(kPollNanos);
}

void test_start_and_stop() {
    TraceRing& ring = test_ring();
    BackgroundThread bt{::foundation::effects::testing::test().alloc};

    bt.start(startup(), ring, test_log());
    static_cast<void>(record_loop(ring, 0));
    wait_for_pipeline(bt, ring);
    bt.stop();
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() == ring.total_produced());

    // A stop with no pipeline thread joins nothing.
    bt.stop();
}

// The child starts the pipeline thread and then starts again, so the test
// process has no second thread at the fork.
void test_start_while_running_breaks_the_contract(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            TraceRing& ring = test_ring();
            BackgroundThread bt{::foundation::effects::testing::test().alloc};
            bt.start(startup(), ring, test_log());
            bt.start(startup(), ring, test_log());
        },
        ::fixy::TextView{"foundation: contract violation: !thread_.holds_thread(), \"a start while the pipeline "
                         "thread runs: stop the pipeline first\""});
}

void test_end_joins_a_running_pipeline() {
    TraceRing& ring = test_ring();
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    bt.start(startup(), ring, test_log());
    // The end of bt stops the pipeline and joins its thread.
}

// The stream of the restart test.  The first pipeline stops in the middle of
// an iteration.  The ring then takes more ops while no pipeline runs, and the
// second pipeline reads them and the ops after them.
constexpr uint64_t kOpsBeforeStop = 8 * kPeriod + 2;
constexpr uint64_t kOpsWhileStopped = 3 * kPeriod;
constexpr uint64_t kOpsAfterRestart = 8 * kPeriod;
constexpr uint64_t kStreamOps = kOpsBeforeStop + kOpsWhileStopped + kOpsAfterRestart;

// The ring, the log and the second ring and log of the restart tests.  Each
// test reads the counts of its own ring and log.
TraceRing& restart_ring() {
    static TraceRing ring{};
    return ring;
}

MetaLog& restart_log() {
    static MetaLog log{::foundation::effects::testing::test().alloc};
    return log;
}

TraceRing& control_ring() {
    static TraceRing ring{};
    return ring;
}

MetaLog& control_log() {
    static MetaLog log{::foundation::effects::testing::test().alloc};
    return log;
}

// The metadata of the output tensor of the op at index.  The op has no data
// pointer, so the build gives it no slot of the memory plan.
[[nodiscard]] crucible::TensorMeta output_meta(uint64_t index) noexcept {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = crucible::tensor_dim(static_cast<int64_t>(64 + index % kPeriod));
    meta.strides[0] = crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    meta.device_type = crucible::DeviceType::CPU;
    meta.device_idx = 0;
    meta.layout = crucible::Layout::Strided;
    return meta;
}

// Records the ops of the stream from the op at index first to the op before
// index end.  Each op before kOpsBeforeStop has one output tensor and a
// record in the log.  Each later op has no tensor and no record, so the
// detect stage takes its position in the log from its cursor.
void record_stream(TraceRing& ring, MetaLog& log, uint64_t first, uint64_t end) {
    for (uint64_t index = first; index < end; ++index) {
        TraceRing::Entry entry{};
        entry.schema_hash = crucible::SchemaHash{kSchemaBase + index % kPeriod};
        entry.shape_hash = crucible::ShapeHash{0x400 + index % kPeriod};
        MetaIndex meta_start = MetaIndex::none();
        if (index < kOpsBeforeStop) {
            entry.num_outputs = 1;
            const ::fixy::FixedArray<crucible::TensorMeta, 1> record{output_meta(index)};
            meta_start = log.try_append(record.view());
            CRUCIBLE_TEST_ASSERT(kTest, meta_start.is_valid());
        }
        CRUCIBLE_TEST_ASSERT(kTest, ring.try_append(entry, meta_start));
    }
}

// A start after stop() with the same ring and log continues the stream.  The
// detect stage keeps its held ops, its detector and its cursor of the log.
// So the second pipeline reads each op one time, and it gives the regions and
// the release of the log of a pipeline that never stops.  The ops after the
// stop have no record, so each of them holds the cursor of the first run.
// The last floor is then the head of the log.  A second start that set the
// cursor to the tail of the log again would hold the floor at that tail.
void test_start_again_continues_the_stream() {
    const auto alloc = ::foundation::effects::testing::test().alloc;
    TraceRing& ring = restart_ring();
    MetaLog& log = restart_log();
    BackgroundThread bt{alloc};
    bt.start(startup(), ring, log);
    record_stream(ring, log, 0, kOpsBeforeStop);
    wait_for_pipeline(bt, ring);
    bt.stop();
    CRUCIBLE_TEST_ASSERT(kTest,
                         log.tail.get() < log.head.get() && "the first pipeline holds no op with a record of the log");

    record_stream(ring, log, kOpsBeforeStop, kOpsBeforeStop + kOpsWhileStopped);
    bt.start(startup(), ring, log);
    record_stream(ring, log, kOpsBeforeStop + kOpsWhileStopped, kStreamOps);
    wait_for_pipeline(bt, ring);
    bt.stop();

    TraceRing& one_ring = control_ring();
    MetaLog& one_log = control_log();
    BackgroundThread control{alloc};
    control.start(startup(), one_ring, one_log);
    record_stream(one_ring, one_log, 0, kStreamOps);
    wait_for_pipeline(control, one_ring);
    control.stop();

    CRUCIBLE_TEST_ASSERT(kTest, ring.total_produced() == kStreamOps && one_ring.total_produced() == kStreamOps);
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() == kStreamOps
                                    && "the second pipeline read an op twice or lost one");
    CRUCIBLE_TEST_ASSERT(kTest, control.total_processed.load_acquire() == kStreamOps);
    CRUCIBLE_TEST_ASSERT(kTest, bt.iterations_completed.get() == control.iterations_completed.get());
    CRUCIBLE_TEST_ASSERT(kTest, bt.works_built() == control.works_built());
    CRUCIBLE_TEST_ASSERT(kTest, control.uncompiled_regions.total() > 0 && "the stream gave no region");
    CRUCIBLE_TEST_ASSERT(kTest, bt.uncompiled_regions.total() == control.uncompiled_regions.total());
    // Each pipeline is stopped, so the queues can be read.
    for (uint32_t age = 0; age < control.uncompiled_regions.size(); ++age) {
        CRUCIBLE_TEST_ASSERT(kTest,
                             bt.uncompiled_regions.at(age).get().content_hash
                                     == control.uncompiled_regions.at(age).get().content_hash
                                 && "the second pipeline gave a region that a pipeline with no stop does not give");
    }
    CRUCIBLE_TEST_ASSERT(kTest, one_log.tail.get() == one_log.head.get());
    CRUCIBLE_TEST_ASSERT(kTest, log.tail.get() == one_log.tail.get() && "the second start moved the cursor of the log");
}

// A start after stop() that names a different ring or a different log breaks
// the contract of start(), and the report holds the message of the contract.
// Each refused start ends its child process.  A start with the first ring and
// log then runs a pipeline in the test process.
void test_start_again_with_another_ring_breaks_the_contract(::fixy::TestRunnerCtx const& test) {
    TraceRing& ring = test_ring();
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    bt.start(startup(), ring, test_log());
    bt.stop();
    ::fixy::check::expect_dies(
        test, [&bt] noexcept { bt.start(startup(), control_ring(), test_log()); },
        ::fixy::TextView{"a start after stop names the trace ring of the first start"});
    ::fixy::check::expect_dies(
        test, [&bt, &ring] noexcept { bt.start(startup(), ring, control_log()); },
        ::fixy::TextView{"a start after stop names the metadata log of the first start"});
    bt.start(startup(), ring, test_log());
    bt.stop();
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_start_and_stop();
    test_start_while_running_breaks_the_contract(test);
    test_end_joins_a_running_pipeline();
    test_start_again_continues_the_stream();
    test_start_again_with_another_ring_breaks_the_contract(test);
    ::fixy::check::pass(test, "test_background_thread_start_stop: PASSED\n");
    return 0;
}

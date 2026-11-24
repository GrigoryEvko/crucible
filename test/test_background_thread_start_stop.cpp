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
#include "trace_rig.h"
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::BackgroundThread;
using crucible::MetaIndex;
using crucible::TraceRing;
using crucible::test::TraceRig;

namespace {

constexpr uint64_t kPeriod = 5;
constexpr uint64_t kSchemaBase = 0x300;
constexpr uint64_t kPollNanos = 100000;
constexpr uint64_t kIterations = 8;

[[nodiscard]] ::fixy::InitLoadCtx startup() noexcept {
    return ::fixy::InitLoadCtx{::foundation::effects::testing::init()};
}

// Records kIterations iterations of a loop of kPeriod ops, from the op at
// index first.  Returns the index after the last op.
uint64_t record_loop(TraceRig& rig, uint64_t first) {
    uint64_t index = first;
    for (const uint64_t end = first + kIterations * kPeriod; index < end; ++index) {
        TraceRing::Entry entry{};
        entry.schema_hash = crucible::SchemaHash{kSchemaBase + index % kPeriod};
        entry.shape_hash = crucible::ShapeHash{0x400 + index % kPeriod};
        CRUCIBLE_TEST_ASSERT(kTest, rig.append_entry(entry, MetaIndex::none()));
    }
    return index;
}

// Waits until the pipeline processed each op of the ring.
void wait_for_pipeline(BackgroundThread& bt, TraceRig const& rig) {
    const fixy::TestRunnerCtx runner{foundation::effects::testing::test()};
    const auto pause = fixy::time::mint_bounded_sleep<kPollNanos>(runner);
    while (bt.total_processed.load_acquire() < rig.ring_total_produced())
        (void)pause.sleep_for(kPollNanos);
}

// The content hashes of the regions of the uncompiled queue, the newest
// first.  A slot of the queue is state of the publish stage, so the read
// runs in a job.
using QueuedHashes = ::fixy::FixedArray<crucible::ContentHash, crucible::PipelineLimits::DEFAULT_UNCOMPILED_QUEUE>;

QueuedHashes queued_hashes(BackgroundThread& bt) {
    QueuedHashes hashes{};
    bt.run_on_publish_stage([&hashes](BackgroundThread::PublishStageView& view,
                                      BackgroundThread::PublishStage const& stage) noexcept {
        BackgroundThread::UncompiledRegionQueue const& queue = view.uncompiled_regions();
        for (uint32_t age = 0; age < queue.size(); ++age) {
            crucible::ContentHash const hash =
                queue.at(stage, age).expect("a queued slot holds a region")->content_hash;
            ::fixy::Index<QueuedHashes::size()> const position =
                ::fixy::Index<QueuedHashes::size()>::check(age).expect("the queue holds at most its default capacity");
            hashes.with_element(position, [hash](crucible::ContentHash& held) noexcept { held = hash; });
        }
    });
    return hashes;
}

void test_start_and_stop() {
    // Each test holds its own ring and log, declared before its
    // BackgroundThread, so the thread ends first.
    TraceRig rig{};
    BackgroundThread bt{::foundation::effects::testing::test().alloc};

    bt.start(startup(), rig.ring_side(), rig.log_side());
    static_cast<void>(record_loop(rig, 0));
    wait_for_pipeline(bt, rig);
    bt.stop();
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() == rig.ring_total_produced());

    // A stop with no pipeline thread joins nothing.
    bt.stop();
}

// The child starts the pipeline thread and then starts again, so the test
// process has no second thread at the fork.
void test_start_while_running_breaks_the_contract(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            TraceRig rig{};
            BackgroundThread bt{::foundation::effects::testing::test().alloc};
            bt.start(startup(), rig.ring_side(), rig.log_side());
            bt.start(startup(), rig.ring_side(), rig.log_side());
        },
        ::fixy::TextView{"foundation: contract violation: !thread_.holds_thread(), \"a start while the pipeline "
                         "thread runs: stop the pipeline first\""});
}

void test_end_joins_a_running_pipeline() {
    TraceRig rig{};
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    bt.start(startup(), rig.ring_side(), rig.log_side());
    // The end of bt stops the pipeline and joins its thread.
}

// The stream of the restart test.  The first pipeline stops in the middle of
// an iteration.  The ring then takes more ops while no pipeline runs, and the
// second pipeline reads them and the ops after them.
constexpr uint64_t kOpsBeforeStop = 8 * kPeriod + 2;
constexpr uint64_t kOpsWhileStopped = 3 * kPeriod;
constexpr uint64_t kOpsAfterRestart = 8 * kPeriod;
constexpr uint64_t kStreamOps = kOpsBeforeStop + kOpsWhileStopped + kOpsAfterRestart;

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
void record_stream(TraceRig& rig, uint64_t first, uint64_t end) {
    for (uint64_t index = first; index < end; ++index) {
        TraceRing::Entry entry{};
        entry.schema_hash = crucible::SchemaHash{kSchemaBase + index % kPeriod};
        entry.shape_hash = crucible::ShapeHash{0x400 + index % kPeriod};
        MetaIndex meta_start = MetaIndex::none();
        if (index < kOpsBeforeStop) {
            entry.num_outputs = 1;
            const ::fixy::FixedArray<crucible::TensorMeta, 1> record{output_meta(index)};
            meta_start = rig.append_metas(record.view());
            CRUCIBLE_TEST_ASSERT(kTest, meta_start.is_valid());
        }
        CRUCIBLE_TEST_ASSERT(kTest, rig.append_entry(entry, meta_start));
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
    TraceRig rig{};
    TraceRig one_rig{};
    BackgroundThread bt{alloc};
    bt.start(startup(), rig.ring_side(), rig.log_side());
    record_stream(rig, 0, kOpsBeforeStop);
    wait_for_pipeline(bt, rig);
    bt.stop();
    CRUCIBLE_TEST_ASSERT(kTest, rig.log_total_released() < rig.log_total_produced()
                                    && "the first pipeline holds no op with a record of the log");

    record_stream(rig, kOpsBeforeStop, kOpsBeforeStop + kOpsWhileStopped);
    bt.start(startup(), rig.ring_side(), rig.log_side());
    record_stream(rig, kOpsBeforeStop + kOpsWhileStopped, kStreamOps);
    wait_for_pipeline(bt, rig);
    bt.stop();

    BackgroundThread control{alloc};
    control.start(startup(), one_rig.ring_side(), one_rig.log_side());
    record_stream(one_rig, 0, kStreamOps);
    wait_for_pipeline(control, one_rig);
    control.stop();

    CRUCIBLE_TEST_ASSERT(kTest, rig.ring_total_produced() == kStreamOps && one_rig.ring_total_produced() == kStreamOps);
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() == kStreamOps
                                    && "the second pipeline read an op twice or lost one");
    CRUCIBLE_TEST_ASSERT(kTest, control.total_processed.load_acquire() == kStreamOps);
    CRUCIBLE_TEST_ASSERT(kTest, bt.iterations_completed.get() == control.iterations_completed.get());
    CRUCIBLE_TEST_ASSERT(kTest, bt.works_built() == control.works_built());
    CRUCIBLE_TEST_ASSERT(kTest, control.uncompiled_regions().total() > 0 && "the stream gave no region");
    CRUCIBLE_TEST_ASSERT(kTest, bt.uncompiled_regions().total() == control.uncompiled_regions().total());
    const QueuedHashes hashes = queued_hashes(bt);
    const QueuedHashes control_hashes = queued_hashes(control);
    for (uint32_t age = 0; age < control.uncompiled_regions().size(); ++age) {
        CRUCIBLE_TEST_ASSERT(kTest,
                             *hashes.get(age).expect("the queue holds at most its default capacity")
                                     == *control_hashes.get(age).expect("the queue holds at most its default capacity")
                                 && "the second pipeline gave a region that a pipeline with no stop does not give");
    }
    CRUCIBLE_TEST_ASSERT(kTest, one_rig.log_total_released() == one_rig.log_total_produced());
    CRUCIBLE_TEST_ASSERT(kTest, rig.log_total_released() == one_rig.log_total_released()
                                    && "the second start moved the cursor of the log");
}

// A start after stop() that names a different ring or a different log breaks
// the contract of start(), and the report holds the message of the contract.
// Each refused start ends its child process.  A start with the first ring and
// log then runs a pipeline in the test process.
void test_start_again_with_another_ring_breaks_the_contract(::fixy::TestRunnerCtx const& test) {
    TraceRig rig{};
    TraceRig other{};
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    bt.start(startup(), rig.ring_side(), rig.log_side());
    bt.stop();
    ::fixy::check::expect_dies(
        test, [&bt, &rig, &other] noexcept { bt.start(startup(), other.ring_side(), rig.log_side()); },
        ::fixy::TextView{"a start after stop names the trace ring of the first start"});
    ::fixy::check::expect_dies(
        test, [&bt, &rig, &other] noexcept { bt.start(startup(), rig.ring_side(), other.log_side()); },
        ::fixy::TextView{"a start after stop names the metadata log of the first start"});
    bt.start(startup(), rig.ring_side(), rig.log_side());
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

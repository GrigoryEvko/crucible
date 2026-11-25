// The pipeline thread of a BackgroundThread starts with start() of the Stopped
// handle and joins with stop() of the Running handle, and the end of the frame
// joins a pipeline that runs.  A start after stop() reads the ring and the log
// of the frame door, so it continues the stream of ops.  The fixtures
// neg_background_thread_start_takes_no_sides and
// neg_background_thread_start_of_a_running_pipeline show that no start names a
// side, and that no start runs while a pipeline runs.
// The plan target of a thread that never started is PlanTarget{}.
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
#include <utility>

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
void wait_for_pipeline(BackgroundThread const& bt, TraceRig const& rig) {
    const fixy::TestRunnerCtx runner{foundation::effects::testing::test()};
    const auto pause = fixy::time::mint_bounded_sleep<kPollNanos>(runner);
    while (bt.total_processed.load_acquire() < rig.ring_total_produced())
        (void)pause.sleep_for(kPollNanos);
}

// The content hashes of the regions of the uncompiled queue, the newest
// first.  A slot of the queue is state of the publish stage, so the read
// runs in a job.
using QueuedHashes = ::fixy::FixedArray<crucible::ContentHash, crucible::PipelineLimits::DEFAULT_UNCOMPILED_QUEUE>;

template <class Stopped>
QueuedHashes queued_hashes(Stopped& stopped) {
    QueuedHashes hashes{};
    stopped.run_on_publish_stage([&hashes](BackgroundThread::PublishStageView& view,
                                           BackgroundThread::PublishStage const& stage) noexcept {
        BackgroundThread::UncompiledRegions const& queue = view.uncompiled_regions();
        for (uint32_t age = 0; age < queue.size(); ++age) {
            crucible::ContentHash const hash =
                queue.at(stage, age).expect("a queued slot holds a region")->content_hash;
            ::fixy::Index<QueuedHashes::size()> const position =
                ::fixy::Index<QueuedHashes::size()>::check(age).expect("the queue holds at most its default capacity");
            hashes.set(position, hash);
        }
    });
    return hashes;
}

constexpr auto kAlloc = ::foundation::effects::testing::test().alloc;

void test_start_and_stop() {
    // Each test holds its own ring and log, and the thread ends with its
    // frame, so the thread ends first.
    TraceRig rig{};
    crucible::with_background_thread(
        kAlloc, rig.ring_side(), rig.log_side(), {}, [&rig](BackgroundThread const& bt, auto stopped) noexcept {
            auto running = std::move(stopped).start(startup());
            static_cast<void>(record_loop(rig, 0));
            wait_for_pipeline(bt, rig);
            auto stopped_again = std::move(running).stop();
            CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() == rig.ring_total_produced());

            // A start after a stop starts a pipeline again.
            auto running_again = std::move(stopped_again).start(startup());
            static_cast<void>(std::move(running_again).stop());
        });
}

void test_end_joins_a_running_pipeline() {
    TraceRig rig{};
    crucible::with_background_thread(kAlloc, rig.ring_side(), rig.log_side(), {},
                                     [](BackgroundThread const& /*bt*/, auto stopped) noexcept {
                                         // The end of the frame stops the pipeline and joins its thread.
                                         static_cast<void>(std::move(stopped).start(startup()));
                                     });
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

// What a pipeline did with a stream: its counts and the hashes of its regions.
struct StreamCounts final {
    uint64_t total_processed = 0;
    uint32_t iterations = 0;
    uint64_t works_built = 0;
    uint64_t regions = 0;
    uint32_t queued = 0;
    QueuedHashes hashes{};
};

template <class Stopped>
StreamCounts counts_of(BackgroundThread const& bt, Stopped& stopped) {
    return StreamCounts{.total_processed = bt.total_processed.load_acquire(),
                        .iterations = bt.iterations_completed.get(),
                        .works_built = bt.works_built(),
                        .regions = bt.uncompiled_regions().total(),
                        .queued = bt.uncompiled_regions().size(),
                        .hashes = queued_hashes(stopped)};
}

// A start after stop() with the same ring and log continues the stream.  The
// detect stage keeps its held ops, its detector and its cursor of the log.
// So the second pipeline reads each op one time, and it gives the regions and
// the release of the log of a pipeline that never stops.  The ops after the
// stop have no record, so each of them holds the cursor of the first run.
// The last floor is then the head of the log.  A second start that set the
// cursor to the tail of the log again would hold the floor at that tail.
void test_start_again_continues_the_stream() {
    TraceRig rig{};
    TraceRig one_rig{};
    const StreamCounts with_stop = crucible::with_background_thread(
        kAlloc, rig.ring_side(), rig.log_side(), {}, [&rig](BackgroundThread const& bt, auto stopped) noexcept {
            auto running = std::move(stopped).start(startup());
            record_stream(rig, 0, kOpsBeforeStop);
            wait_for_pipeline(bt, rig);
            auto stopped_again = std::move(running).stop();
            CRUCIBLE_TEST_ASSERT(kTest, rig.log_total_released() < rig.log_total_produced()
                                            && "the first pipeline holds no op with a record of the log");

            record_stream(rig, kOpsBeforeStop, kOpsBeforeStop + kOpsWhileStopped);
            auto running_again = std::move(stopped_again).start(startup());
            record_stream(rig, kOpsBeforeStop + kOpsWhileStopped, kStreamOps);
            wait_for_pipeline(bt, rig);
            auto stopped_at_the_end = std::move(running_again).stop();
            return counts_of(bt, stopped_at_the_end);
        });
    const StreamCounts control =
        crucible::with_background_thread(kAlloc, one_rig.ring_side(), one_rig.log_side(), {},
                                         [&one_rig](BackgroundThread const& bt, auto stopped) noexcept {
                                             auto running = std::move(stopped).start(startup());
                                             record_stream(one_rig, 0, kStreamOps);
                                             wait_for_pipeline(bt, one_rig);
                                             auto stopped_at_the_end = std::move(running).stop();
                                             return counts_of(bt, stopped_at_the_end);
                                         });

    CRUCIBLE_TEST_ASSERT(kTest, rig.ring_total_produced() == kStreamOps && one_rig.ring_total_produced() == kStreamOps);
    CRUCIBLE_TEST_ASSERT(kTest,
                         with_stop.total_processed == kStreamOps && "the second pipeline read an op twice or lost one");
    CRUCIBLE_TEST_ASSERT(kTest, control.total_processed == kStreamOps);
    CRUCIBLE_TEST_ASSERT(kTest, with_stop.iterations == control.iterations);
    CRUCIBLE_TEST_ASSERT(kTest, with_stop.works_built == control.works_built);
    CRUCIBLE_TEST_ASSERT(kTest, control.regions > 0 && "the stream gave no region");
    CRUCIBLE_TEST_ASSERT(kTest, with_stop.regions == control.regions);
    for (uint32_t age = 0; age < control.queued; ++age) {
        CRUCIBLE_TEST_ASSERT(kTest,
                             *with_stop.hashes.get(age).expect("the queue holds at most its default capacity")
                                     == *control.hashes.get(age).expect("the queue holds at most its default capacity")
                                 && "the second pipeline gave a region that a pipeline with no stop does not give");
    }
    CRUCIBLE_TEST_ASSERT(kTest, one_rig.log_total_released() == one_rig.log_total_produced());
    CRUCIBLE_TEST_ASSERT(kTest, rig.log_total_released() == one_rig.log_total_released()
                                    && "the second start moved the cursor of the log");
}

// The plan target of a thread that never started is PlanTarget{}, so a test
// of a memory plan uses PlanTarget{} with no thread (PlanTarget of
// BackgroundThread.h).
void test_plan_target_before_start_is_the_default() {
    TraceRig rig{};
    BackgroundThread::PlanTarget const unstarted = crucible::with_background_thread(
        kAlloc, rig.ring_side(), rig.log_side(), {},
        [](BackgroundThread const& /*bt*/, auto stopped) noexcept { return stopped.plan_target(); });
    BackgroundThread::PlanTarget const preset{};
    CRUCIBLE_TEST_ASSERT(kTest, unstarted.rank == preset.rank && unstarted.world_size == preset.world_size
                                    && unstarted.device_capability.value() == preset.device_capability.value()
                                    && "the plan target of a thread that never started is not PlanTarget{}");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_start_and_stop();
    test_end_joins_a_running_pipeline();
    test_start_again_continues_the_stream();
    test_plan_target_before_start_is_the_default();
    ::fixy::check::pass(test, "test_background_thread_start_stop: PASSED\n");
    return 0;
}

// A flush of a BackgroundThread waits until the pipeline processed each op
// that the ring held at the call, or until no pipeline thread runs.  A flush
// after stop() with an op left in the ring gives pipeline_stopped at once,
// and does not hang.  A start after the stop reads the op, and a flush then
// gives Unit.  The wait spins and then parks, so only a context that owns
// Block and Nondet can flush.
//
// The tsan preset runs the same binary, so each notify and each park has a
// check of ThreadSanitizer.

#include <crucible/BackgroundThread.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include "trace_rig.h"

#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes, and
// which owns Block and Nondet for the flush.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::BackgroundThread;
using crucible::FlushRefusal;
using crucible::MetaIndex;
using crucible::TraceRing;
using crucible::test::RigLog;
using crucible::test::RigRing;

namespace {

// A flush parks, so its context owns Block and Nondet.  The foreground
// context of the hot path owns neither.
template <class Ctx>
concept Flushes = requires(BackgroundThread& bt, Ctx const& ctx) { bt.flush(ctx); };
static_assert(Flushes<::fixy::TestRunnerCtx> && Flushes<::fixy::InitLoadCtx> && Flushes<::fixy::BgLoadCtx>);
static_assert(!Flushes<::fixy::HotFgCtx>, "the hot path does not wait in the kernel");
static_assert(!Flushes<::fixy::BgDrainCtx> && !Flushes<::fixy::ColdInitCtx>,
              "a context with no Block and no Nondet does not park");

constexpr uint64_t kPeriod = 5;
constexpr uint64_t kSchemaBase = 0x700;
constexpr uint64_t kIterations = 8;

[[nodiscard]] ::fixy::InitLoadCtx startup() noexcept {
    return ::fixy::InitLoadCtx{::foundation::effects::testing::init()};
}

// Each case has a ring of its own, so a case reads only its own ops.  The
// ops take no record, so the cases share one metadata log, and the test holds
// one log of the size of a Vigil, not one for each case.  Each case declares
// its ring before its BackgroundThread, and main holds the log, so each
// thread ends first.

// Records count ops of a loop of kPeriod ops, from the op at index first.
// Returns the index after the last op.
uint64_t record_ops(RigRing& ring, uint64_t first, uint64_t count) {
    uint64_t index = first;
    for (const uint64_t end = first + count; index < end; ++index) {
        TraceRing::Entry entry{};
        entry.schema_hash = crucible::SchemaHash{kSchemaBase + index % kPeriod};
        entry.shape_hash = crucible::ShapeHash{0x800 + index % kPeriod};
        CRUCIBLE_TEST_ASSERT(kTest, ring.append_entry(entry, MetaIndex::none()));
    }
    return index;
}

// A flush of a pipeline that runs waits for each op, and gives Unit.
void test_flush_of_a_running_pipeline(RigLog& shared_log) {
    RigRing ring{};
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    bt.start(startup(), ring.side(), shared_log.side());
    static_cast<void>(record_ops(ring, 0, kIterations * kPeriod));
    CRUCIBLE_TEST_ASSERT(kTest, bt.flush(kTest).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() == ring.total_produced());
    bt.stop();
}

// The ring takes an op after stop(), and no pipeline thread reads it.  The
// flush gives pipeline_stopped at once.  A start reads the op, and the flush
// then gives Unit.
void test_flush_of_a_stopped_pipeline(RigLog& shared_log) {
    RigRing ring{};
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    bt.start(startup(), ring.side(), shared_log.side());
    const uint64_t before_stop = record_ops(ring, 0, kIterations * kPeriod);
    CRUCIBLE_TEST_ASSERT(kTest, bt.flush(kTest).is_ok());
    bt.stop();

    static_cast<void>(record_ops(ring, before_stop, 1));
    const crucible::FlushResult refused = bt.flush(kTest);
    CRUCIBLE_TEST_ASSERT(kTest, refused.is_err_and([](FlushRefusal why) noexcept {
        return why == FlushRefusal::pipeline_stopped;
    }) && "a flush of a stopped pipeline with an op in the ring gives pipeline_stopped");
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() < ring.total_produced());

    bt.start(startup(), ring.side(), shared_log.side());
    CRUCIBLE_TEST_ASSERT(kTest, bt.flush(kTest).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() == ring.total_produced());
    bt.stop();
}

// After stop(), a flush with no op left in the ring gives Unit.
void test_flush_with_no_op_left_after_stop(RigLog& shared_log) {
    RigRing ring{};
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    bt.start(startup(), ring.side(), shared_log.side());
    static_cast<void>(record_ops(ring, 0, kPeriod));
    CRUCIBLE_TEST_ASSERT(kTest, bt.flush(kTest).is_ok());
    bt.stop();
    CRUCIBLE_TEST_ASSERT(kTest, bt.flush(kTest).is_ok());
}

// A BackgroundThread that never started has no ring and holds no op.
void test_flush_before_start() {
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, bt.flush(kTest).is_ok());
}

// A second BackgroundThread on a ring that a first one used counts the ops
// of the ring from the tail at its bind.  Its flush of one new op ends when
// its pipeline processed that op, and does not wait for the ops of the first
// one, which its own count never reaches.  The test has a time limit in the
// harness, so a flush that waits with no end fails it.
void test_flush_of_a_second_thread_on_a_used_ring(RigLog& shared_log) {
    RigRing ring{};
    uint64_t first_end = 0;
    {
        BackgroundThread first{::foundation::effects::testing::test().alloc};
        first.start(startup(), ring.side(), shared_log.side());
        first_end = record_ops(ring, 0, kIterations * kPeriod);
        CRUCIBLE_TEST_ASSERT(kTest, first.flush(kTest).is_ok());
        first.stop();
    }
    BackgroundThread second{::foundation::effects::testing::test().alloc};
    second.start(startup(), ring.side(), shared_log.side());
    static_cast<void>(record_ops(ring, first_end, 1));
    CRUCIBLE_TEST_ASSERT(kTest, second.flush(kTest).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, second.total_processed.load_acquire() == 1);
    CRUCIBLE_TEST_ASSERT(kTest, second.count_processed_ring_ops() == ring.total_produced());
    second.stop();
}

}  // namespace

int main() {
    RigLog shared_log{};
    test_flush_of_a_running_pipeline(shared_log);
    test_flush_of_a_stopped_pipeline(shared_log);
    test_flush_with_no_op_left_after_stop(shared_log);
    test_flush_before_start();
    test_flush_of_a_second_thread_on_a_used_ring(shared_log);
    ::fixy::check::pass(kTest, "test_background_thread_flush: PASSED\n");
    return 0;
}

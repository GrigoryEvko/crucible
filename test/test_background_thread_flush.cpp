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

#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes, and
// which owns Block and Nondet for the flush.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::BackgroundThread;
using crucible::FlushRefusal;
using crucible::MetaIndex;
using crucible::MetaLog;
using crucible::TraceRing;

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

// Each case has a ring of its own: the count of the processed ops belongs to
// one BackgroundThread, and the head of a ring counts each op that the ring
// ever took.  The ops take no record, so the cases share one metadata log,
// and the test holds one log of the size of a Vigil, not three.
TraceRing& running_ring() {
    static TraceRing ring{};
    return ring;
}

TraceRing& stopped_ring() {
    static TraceRing ring{};
    return ring;
}

TraceRing& idle_ring() {
    static TraceRing ring{};
    return ring;
}

MetaLog& shared_log() {
    static MetaLog log{::foundation::effects::testing::test().alloc};
    return log;
}

// Records count ops of a loop of kPeriod ops, from the op at index first.
// Returns the index after the last op.
uint64_t record_ops(TraceRing& ring, uint64_t first, uint64_t count) {
    uint64_t index = first;
    for (const uint64_t end = first + count; index < end; ++index) {
        TraceRing::Entry entry{};
        entry.schema_hash = crucible::SchemaHash{kSchemaBase + index % kPeriod};
        entry.shape_hash = crucible::ShapeHash{0x800 + index % kPeriod};
        CRUCIBLE_TEST_ASSERT(kTest, ring.try_append(entry, MetaIndex::none()));
    }
    return index;
}

// A flush of a pipeline that runs waits for each op, and gives Unit.
void test_flush_of_a_running_pipeline() {
    TraceRing& ring = running_ring();
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    bt.start(startup(), ring, shared_log());
    static_cast<void>(record_ops(ring, 0, kIterations * kPeriod));
    CRUCIBLE_TEST_ASSERT(kTest, bt.flush(kTest).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() == ring.total_produced());
    bt.stop();
}

// The ring takes an op after stop(), and no pipeline thread reads it.  The
// flush gives pipeline_stopped at once.  A start reads the op, and the flush
// then gives Unit.
void test_flush_of_a_stopped_pipeline() {
    TraceRing& ring = stopped_ring();
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    bt.start(startup(), ring, shared_log());
    const uint64_t before_stop = record_ops(ring, 0, kIterations * kPeriod);
    CRUCIBLE_TEST_ASSERT(kTest, bt.flush(kTest).is_ok());
    bt.stop();

    static_cast<void>(record_ops(ring, before_stop, 1));
    const crucible::FlushResult refused = bt.flush(kTest);
    CRUCIBLE_TEST_ASSERT(kTest, refused.is_err_and([](FlushRefusal why) noexcept {
        return why == FlushRefusal::pipeline_stopped;
    }) && "a flush of a stopped pipeline with an op in the ring gives pipeline_stopped");
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() < ring.total_produced());

    bt.start(startup(), ring, shared_log());
    CRUCIBLE_TEST_ASSERT(kTest, bt.flush(kTest).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, bt.total_processed.load_acquire() == ring.total_produced());
    bt.stop();
}

// After stop(), a flush with no op left in the ring gives Unit.
void test_flush_with_no_op_left_after_stop() {
    TraceRing& ring = idle_ring();
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    bt.start(startup(), ring, shared_log());
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

}  // namespace

int main() {
    test_flush_of_a_running_pipeline();
    test_flush_of_a_stopped_pipeline();
    test_flush_with_no_op_left_after_stop();
    test_flush_before_start();
    ::fixy::check::pass(kTest, "test_background_thread_flush: PASSED\n");
    return 0;
}

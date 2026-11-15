// The pipeline thread of a BackgroundThread starts with start() and joins
// with stop(), and a stop with no thread joins nothing.  A start while a
// pipeline thread runs breaks the contract of start(), and the end of a
// BackgroundThread joins a pipeline that runs.
// The tsan preset runs the same binary, so each start and each join has a
// check of ThreadSanitizer.

#include <crucible/BackgroundThread.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <fixy/Ctx.h>
#include <fixy/os/Time.h>

#include "test_abort_probe.h"
#include "test_assert.h"
#include "test_harness.h"
#include <cstdint>

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
        assert(ring.try_append(entry, MetaIndex::none()));
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
    assert(bt.total_processed.load_acquire() == ring.total_produced());

    // A stop with no pipeline thread joins nothing.
    bt.stop();
}

void test_start_while_running_breaks_the_contract() {
    TraceRing& ring = test_ring();
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    bt.start(startup(), ring, test_log());
    assert(::crucible::test::aborts([&] { bt.start(startup(), ring, test_log()); }));
    bt.stop();
}

void test_end_joins_a_running_pipeline() {
    TraceRing& ring = test_ring();
    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    bt.start(startup(), ring, test_log());
    // The end of bt stops the pipeline and joins its thread.
}

}  // namespace

int main() {
    test_start_and_stop();
    test_start_while_running_breaks_the_contract();
    test_end_joins_a_running_pipeline();
    return 0;
}

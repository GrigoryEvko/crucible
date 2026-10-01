// The pool behind the replay of test_end_to_end: the data flow between the
// slots, and the bounds of each slot.

#include "end_to_end.h"

#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include "test_assert.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace crucible;

namespace test_end_to_end {

// Each op fills its output with a pattern of its own, so reading the
// expected pattern back through the next op's input is what proves the
// two entries resolve to the same pool bytes.
void test_pipeline_data_flow() {
    auto* ring = new TraceRing();
    auto* meta_log = new MetaLog();

    BackgroundThread bt;

    feed_iteration(ring, meta_log, 0);
    feed_iteration(ring, meta_log, 1);
    feed_trigger(ring, meta_log, 2);

    bt.start(ring, meta_log);

    wait_processed(bt, *ring);
    auto* region = bt.active_region.load(std::memory_order_acquire);
    assert(region != nullptr);

    CrucibleContext ctx;
    assert(ctx.activate(region));
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    assert(ctx.advance(SCHEMA[0], SHAPE[0], cv) == ReplayStatus::MATCH);
    std::memset(ctx.output_ptr(0, cv), 0x11, 4096);

    for (uint32_t i = 1; i < NUM_OPS - 1; i++) {
        assert(ctx.advance(SCHEMA[i], SHAPE[i], cv) == ReplayStatus::MATCH);

        auto* in_data = static_cast<uint8_t*>(ctx.input_ptr(0, cv));
        uint8_t expected = static_cast<uint8_t>(0x11 + i - 1);
        for (uint32_t b = 0; b < 4096; b++) {
            assert(in_data[b] == expected);
        }

        std::memset(ctx.output_ptr(0, cv), static_cast<int>(0x11 + i), 4096);
    }

    assert(ctx.advance(SCHEMA[7], SHAPE[7], cv) == ReplayStatus::COMPLETE);
    auto* in_last = static_cast<uint8_t*>(ctx.input_ptr(0, cv));
    uint8_t expected_last = static_cast<uint8_t>(0x11 + NUM_OPS - 2);
    for (uint32_t b = 0; b < 4096; b++) {
        assert(in_last[b] == expected_last);
    }

    assert(ctx.compiled_iterations() == 1);

    ctx.deactivate();
    bt.stop();
    delete meta_log;
    delete ring;

    std::printf("  test_pipeline_data_flow: PASSED\n");
}

void test_pipeline_pool_bounds() {
    auto* ring = new TraceRing();
    auto* meta_log = new MetaLog();

    BackgroundThread bt;

    feed_iteration(ring, meta_log, 0);
    feed_iteration(ring, meta_log, 1);
    feed_trigger(ring, meta_log, 2);

    bt.start(ring, meta_log);

    wait_processed(bt, *ring);
    auto* region = bt.active_region.load(std::memory_order_acquire);
    assert(region != nullptr);

    CrucibleContext ctx;
    assert(ctx.activate(region));

    auto* pool_base = static_cast<uint8_t*>(ctx.pool().pool_base());
    uint64_t pool_bytes = ctx.pool().pool_bytes();
    assert(pool_base != nullptr);
    assert(pool_bytes > 0);

    auto cv2 = ctx.mint_compiled_view(kVigilForeground);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto s = ctx.advance(SCHEMA[i], SHAPE[i], cv2);
        (void)s;
        auto* p = static_cast<uint8_t*>(ctx.output_ptr(0, cv2));
        assert(p >= pool_base);
        assert(p + 4096 <= pool_base + pool_bytes);
    }

    // The loop above consumed the iteration, so this one replays it.  No
    // reset is needed: the first advance after COMPLETE rewinds.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto s = ctx.advance(SCHEMA[i], SHAPE[i], cv2);
        (void)s;
        if (i > 0) {
            auto* p = static_cast<uint8_t*>(ctx.input_ptr(0, cv2));
            assert(p >= pool_base);
            assert(p + 4096 <= pool_base + pool_bytes);
        }
    }

    ctx.deactivate();
    bt.stop();
    delete meta_log;
    delete ring;

    std::printf("  test_pipeline_pool_bounds: PASSED\n");
}

}  // namespace test_end_to_end

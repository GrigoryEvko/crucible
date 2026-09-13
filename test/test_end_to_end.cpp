#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include "test_assert.h"
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace crucible;

// The synthetic workload is a linear chain: op 0 has no input, and
// every later op consumes exactly the tensor its predecessor produced.
// Each tensor is a contiguous 1D float of 1024 elements, which is where
// the literal 4096 in the fill and bounds checks below comes from.

static constexpr uint32_t NUM_OPS = 8;

static constexpr SchemaHash SCHEMA[NUM_OPS] = {SchemaHash{0x100}, SchemaHash{0x101}, SchemaHash{0x102},
                                               SchemaHash{0x103}, SchemaHash{0x104}, SchemaHash{0x105},
                                               SchemaHash{0x106}, SchemaHash{0x107}};
static constexpr ShapeHash SHAPE[NUM_OPS] = {ShapeHash{0x200}, ShapeHash{0x201}, ShapeHash{0x202}, ShapeHash{0x203},
                                             ShapeHash{0x204}, ShapeHash{0x205}, ShapeHash{0x206}, ShapeHash{0x207}};

static TensorMeta make_meta(void* data_ptr) {
    TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = ::crucible::tensor_dim(1024);
    m.strides[0] = ::crucible::tensor_dim(1);
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.device_idx = 0;
    m.layout = Layout::Strided;
    m.data_ptr = external_data_ptr(data_ptr);
    return m;
}

// These addresses are never dereferenced.  They serve only as hash keys
// for the producer-consumer match, so any distinct non-null value does.
static void* fake_ptr(uint32_t iter, uint32_t op) {
    return std::bit_cast<void*>(static_cast<std::uintptr_t>((iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

static void feed_iteration(TraceRing* ring, MetaLog* meta_log, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        TraceRing::Entry e{};
        e.schema_hash = SCHEMA[i];
        e.shape_hash = SHAPE[i];
        e.num_inputs = (i == 0) ? 0 : 1;
        e.num_outputs = 1;

        const auto n_metas = static_cast<uint16_t>(e.num_inputs + e.num_outputs);
        TensorMeta metas[2]{};
        uint16_t idx = 0;
        if (i > 0) metas[idx++] = make_meta(fake_ptr(iter, i - 1));
        metas[idx++] = make_meta(fake_ptr(iter, i));

        auto ms = meta_log->try_append(metas, n_metas);
        assert(ms.is_valid() && "MetaLog overflow");
        assert(ring->try_append(e, ms) && "TraceRing full");
    }
}

// K is the length of the detector's signature, so feeding exactly the
// first K ops of an iteration is what presents it with a repeat.
static void feed_trigger(TraceRing* ring, MetaLog* meta_log, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        TraceRing::Entry e{};
        e.schema_hash = SCHEMA[i];
        e.shape_hash = SHAPE[i];
        e.num_inputs = (i == 0) ? 0 : 1;
        e.num_outputs = 1;

        const auto n_metas = static_cast<uint16_t>(e.num_inputs + e.num_outputs);
        TensorMeta metas[2]{};
        uint16_t idx = 0;
        if (i > 0) metas[idx++] = make_meta(fake_ptr(iter, i - 1));
        metas[idx++] = make_meta(fake_ptr(iter, i));

        auto ms = meta_log->try_append(metas, n_metas);
        assert(ms.is_valid());
        assert(ring->try_append(e, ms));
    }
}

// The feeding thread has no other handshake with the background thread,
// so it spins on the processed counter until the drain has caught up
// with everything produced so far.  The spin cap turns a stalled
// background thread into a failed assertion instead of a hang.
static void wait_processed(BackgroundThread& bt, TraceRing& ring) {
    const uint64_t target = ring.total_produced();
    uint64_t spins = 0;
    while (bt.total_processed.get() < target) {
        assert(++spins < 100'000'000 && "bg thread did not finish processing");
        CRUCIBLE_SPIN_PAUSE;
    }
}

// A boundary needs two matches to confirm, which is why three feeds are
// required rather than two.  The first iteration builds the signature,
// the second produces a candidate match, and the third's K-th matching
// op confirms the boundary.
static void test_pipeline_basic() {
    auto* ring = new TraceRing();
    auto* meta_log = new MetaLog();

    BackgroundThread bt;

    // Everything is queued before the background thread starts, so the
    // drain sees one complete stream and the outcome does not depend on
    // how the two threads interleave.
    feed_iteration(ring, meta_log, 0);
    feed_iteration(ring, meta_log, 1);
    feed_trigger(ring, meta_log, 2);

    bt.start(ring, meta_log);

    wait_processed(bt, *ring);
    auto* region = bt.active_region.load(std::memory_order_acquire);
    assert(region != nullptr && "BackgroundThread did not produce a region");

    assert(region->kind == TraceNodeKind::REGION);
    assert(region->num_ops == NUM_OPS);
    assert(region->plan != nullptr);
    assert(region->plan->pool_bytes > 0);
    assert(region->plan->num_slots > 0);

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        assert(region->ops[i].schema_hash == SCHEMA[i]);
        assert(region->ops[i].shape_hash == SHAPE[i]);
        assert(region->ops[i].output_slot_ids != nullptr);
        assert(region->ops[i].output_slot_ids[0].is_valid());
    }

    // The feed never states an edge.  Matching slots here mean the
    // planner recovered the producer-consumer edge from the shared data
    // pointer alone.
    for (uint32_t i = 1; i < NUM_OPS; i++) {
        assert(region->ops[i].input_slot_ids != nullptr);
        assert(region->ops[i].input_slot_ids[0] == region->ops[i - 1].output_slot_ids[0]);
    }

    CrucibleContext ctx;
    assert(ctx.is_recording());
    assert(ctx.activate(region));
    assert(ctx.is_compiled());
    assert(ctx.active_region() == region);
    assert(ctx.pool().is_initialized());

    auto cv = ctx.mint_compiled_view();

    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto s = ctx.advance(SCHEMA[i], SHAPE[i], cv);
        if (i < NUM_OPS - 1) {
            assert(s == ReplayStatus::MATCH);
        } else {
            assert(s == ReplayStatus::COMPLETE);
        }
        assert(ctx.output_ptr(0, cv) != nullptr);
    }
    assert(ctx.compiled_iterations() == 1);

    // No explicit reset: the first advance after COMPLETE rewinds.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto s = ctx.advance(SCHEMA[i], SHAPE[i], cv);
        if (i < NUM_OPS - 1) {
            assert(s == ReplayStatus::MATCH);
        } else {
            assert(s == ReplayStatus::COMPLETE);
        }
    }
    assert(ctx.compiled_iterations() == 2);
    assert(ctx.diverged_count() == 0);

    ctx.deactivate();
    assert(ctx.is_recording());

    bt.stop();
    delete meta_log;
    delete ring;

    std::printf("  test_pipeline_basic: PASSED\n");
}

static void test_pipeline_divergence() {
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
    auto cv = ctx.mint_compiled_view();

    for (uint32_t i = 0; i < 3; i++) {
        assert(ctx.advance(SCHEMA[i], SHAPE[i], cv) == ReplayStatus::MATCH);
    }

    assert(ctx.advance(SchemaHash{0xBAD}, SHAPE[3], cv) == ReplayStatus::DIVERGED);
    assert(ctx.diverged_count() == 1);
    // A divergence does not switch modes.  Choosing a fallback is the
    // caller's decision, not the context's.
    assert(ctx.is_compiled());

    // The position did not move, so op 3 is still the one under test.
    // This time the schema matches and the shape does not.
    assert(ctx.advance(SCHEMA[3], ShapeHash{0xBAD}, cv) == ReplayStatus::DIVERGED);
    assert(ctx.diverged_count() == 2);

    ctx.deactivate();
    assert(ctx.is_recording());

    bt.stop();
    delete meta_log;
    delete ring;

    std::printf("  test_pipeline_divergence: PASSED\n");
}

// Each op fills its output with a pattern of its own, so reading the
// expected pattern back through the next op's input is what proves the
// two entries resolve to the same pool bytes.
static void test_pipeline_data_flow() {
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
    auto cv = ctx.mint_compiled_view();

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

static void test_pipeline_pool_bounds() {
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

    auto cv2 = ctx.mint_compiled_view();

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

// The second boundary costs less than the first.  Once the signature is
// confirmed the next match fires immediately, and the background thread
// keeps the K trigger ops as the opening of the next trace rather than
// discarding them.  So the feed here completes that partial iteration
// before starting a fresh one.
static void test_pipeline_multi_iteration() {
    auto* ring = new TraceRing();
    auto* meta_log = new MetaLog();

    BackgroundThread bt;

    feed_iteration(ring, meta_log, 0);
    feed_iteration(ring, meta_log, 1);
    feed_trigger(ring, meta_log, 2);

    bt.start(ring, meta_log);

    wait_processed(bt, *ring);
    auto* region1 = bt.active_region.load(std::memory_order_acquire);
    assert(region1 != nullptr);
    assert(region1->num_ops == NUM_OPS);

    // The loop starts at K because the background thread already holds
    // the first K ops of this iteration from the trigger feed above.
    for (uint32_t i = IterationDetector::K; i < NUM_OPS; i++) {
        TraceRing::Entry e{};
        e.schema_hash = SCHEMA[i];
        e.shape_hash = SHAPE[i];
        e.num_inputs = 1;
        e.num_outputs = 1;

        TensorMeta metas[2]{};
        metas[0] = make_meta(fake_ptr(2, i - 1));
        metas[1] = make_meta(fake_ptr(2, i));

        auto ms = meta_log->try_append(metas, 2);
        assert(ms.is_valid());
        assert(ring->try_append(e, ms));
    }

    feed_iteration(ring, meta_log, 3);
    feed_trigger(ring, meta_log, 4);

    wait_processed(bt, *ring);
    auto* region2 = bt.active_region.load(std::memory_order_acquire);
    assert(region2 != nullptr && region2 != region1 && "Second boundary did not fire");
    assert(region2->num_ops == NUM_OPS);
    assert(region2->plan != nullptr);

    CrucibleContext ctx;
    assert(ctx.activate(region2));
    auto cv = ctx.mint_compiled_view();
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto s = ctx.advance(SCHEMA[i], SHAPE[i], cv);
        if (i < NUM_OPS - 1) {
            assert(s == ReplayStatus::MATCH);
        } else {
            assert(s == ReplayStatus::COMPLETE);
        }
    }
    assert(ctx.compiled_iterations() == 1);

    ctx.deactivate();
    bt.stop();
    delete meta_log;
    delete ring;

    std::printf("  test_pipeline_multi_iteration: PASSED\n");
}

int main() {
    std::printf("test_end_to_end:\n");
    test_pipeline_basic();
    test_pipeline_divergence();
    test_pipeline_data_flow();
    test_pipeline_pool_bounds();
    test_pipeline_multi_iteration();
    std::printf("test_end_to_end: all tests passed\n");
    return 0;
}

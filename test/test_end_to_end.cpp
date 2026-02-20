// End-to-end integration test: TraceRing → BackgroundThread → CrucibleContext
//
// Validates the full Phase 5 pipeline:
//   1. Foreground feeds ops into TraceRing + MetaLog
//   2. BackgroundThread drains, detects iteration boundary, builds
//      TraceGraph with DFG edges, computes MemoryPlan via sweep-line
//   3. CrucibleContext activates with the produced RegionNode
//   4. Replay verifies guards, returns pre-allocated output pointers
//   5. Data written to op i's output is readable from op i+1's input

#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace crucible;

// ── Iteration layout: 8-op linear chain ────────────────────────────
//
// Op 0: 0 inputs, 1 output (source tensor)
// Op 1: 1 input (from op 0), 1 output
// ...
// Op 7: 1 input (from op 6), 1 output
//
// Each tensor: 1D float, size=1024, stride=1 → 4096 bytes.
// Sweep-line peak: 2 concurrent slots → 8192 bytes pool.

static constexpr uint32_t NUM_OPS = 8;

static constexpr SchemaHash SCHEMA[NUM_OPS] = {
    SchemaHash{0x100}, SchemaHash{0x101}, SchemaHash{0x102}, SchemaHash{0x103},
    SchemaHash{0x104}, SchemaHash{0x105}, SchemaHash{0x106}, SchemaHash{0x107}
};
static constexpr ShapeHash SHAPE[NUM_OPS] = {
    ShapeHash{0x200}, ShapeHash{0x201}, ShapeHash{0x202}, ShapeHash{0x203},
    ShapeHash{0x204}, ShapeHash{0x205}, ShapeHash{0x206}, ShapeHash{0x207}
};

// ── Helpers ──────────────────────────────────────────────────────

static TensorMeta make_meta(void* data_ptr) {
  TensorMeta m{};
  m.ndim = 1;
  m.sizes[0] = 1024;
  m.strides[0] = 1;
  m.dtype = ScalarType::Float;
  m.device_type = DeviceType::CPU;
  m.device_idx = 0;
  m.layout = Layout::Strided;
  m.data_ptr = data_ptr;
  return m;
}

// Unique fake pointer for (iteration, op_index). Never null, never zero.
// PtrMap uses these as hash keys — never dereferences them.
static void* fake_ptr(uint32_t iter, uint32_t op) {
  return reinterpret_cast<void*>(
      static_cast<uintptr_t>((iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

// Feed one full iteration (NUM_OPS ops) into ring + meta_log.
static void feed_iteration(TraceRing* ring, MetaLog* meta_log, uint32_t iter) {
  for (uint32_t i = 0; i < NUM_OPS; i++) {
    TraceRing::Entry e{};
    e.schema_hash = SCHEMA[i];
    e.shape_hash = SHAPE[i];
    e.num_inputs = (i == 0) ? 0 : 1;
    e.num_outputs = 1;

    uint16_t n_metas = e.num_inputs + e.num_outputs;
    TensorMeta metas[2]{};
    uint16_t idx = 0;
    if (i > 0)
      metas[idx++] = make_meta(fake_ptr(iter, i - 1));
    metas[idx++] = make_meta(fake_ptr(iter, i));

    auto ms = meta_log->try_append(metas, n_metas);
    assert(ms.is_valid() && "MetaLog overflow");
    assert(ring->try_append(e, ms) && "TraceRing full");
  }
}

// Feed the first K ops of an iteration — triggers the detector.
static void feed_trigger(TraceRing* ring, MetaLog* meta_log, uint32_t iter) {
  for (uint32_t i = 0; i < IterationDetector::K; i++) {
    TraceRing::Entry e{};
    e.schema_hash = SCHEMA[i];
    e.shape_hash = SHAPE[i];
    e.num_inputs = (i == 0) ? 0 : 1;
    e.num_outputs = 1;

    uint16_t n_metas = e.num_inputs + e.num_outputs;
    TensorMeta metas[2]{};
    uint16_t idx = 0;
    if (i > 0)
      metas[idx++] = make_meta(fake_ptr(iter, i - 1));
    metas[idx++] = make_meta(fake_ptr(iter, i));

    auto ms = meta_log->try_append(metas, n_metas);
    assert(ms.is_valid());
    assert(ring->try_append(e, ms));
  }
}

// Poll active_region until set (or timeout).
static RegionNode* wait_for_region(BackgroundThread& bt,
                                   uint32_t timeout_ms = 5000) {
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    auto* r = bt.active_region.load(std::memory_order_acquire);
    if (r) return r;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return nullptr;
}

// ── Test 1: Full pipeline — record → detect → plan → replay ──
//
// Feeds two iterations + K trigger ops (21 ops total). The IterationDetector
// needs two matches to confirm a boundary: iter 0 builds the signature,
// iter 1's K-th matching op is the candidate, iter 2's K-th matching op
// fires the confirmed boundary. Verifies the BackgroundThread produces a
// valid RegionNode with MemoryPlan. Activates CrucibleContext and replays
// two iterations, verifying MATCH/COMPLETE transitions.
static void test_pipeline_basic() {
  auto* ring = new TraceRing();
  auto* meta_log = new MetaLog();

  BackgroundThread bt;

  // Feed 2 full iterations + K trigger ops BEFORE starting bg thread.
  // Iter 0: builds detector signature (K=5 ops).
  // Iter 1: first match at op 12 (candidate only).
  // Trigger (iter 2's first K ops): second match at op 20 → confirmed boundary.
  feed_iteration(ring, meta_log, 0);
  feed_iteration(ring, meta_log, 1);
  feed_trigger(ring, meta_log, 2);

  bt.start(ring, meta_log);

  auto* region = wait_for_region(bt);
  assert(region != nullptr && "BackgroundThread did not produce a region");

  // ── Verify region structure ──
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

  // Verify DFG edges: op i's input_slot_ids[0] == op i-1's output_slot_ids[0].
  for (uint32_t i = 1; i < NUM_OPS; i++) {
    assert(region->ops[i].input_slot_ids != nullptr);
    assert(region->ops[i].input_slot_ids[0] ==
           region->ops[i - 1].output_slot_ids[0]);
  }

  // ── Activate and replay ──
  CrucibleContext ctx;
  assert(ctx.is_recording());
  assert(ctx.activate(region));
  assert(ctx.is_compiled());
  assert(ctx.active_region() == region);
  assert(ctx.pool().is_initialized());

  // Iteration 1.
  for (uint32_t i = 0; i < NUM_OPS; i++) {
    auto s = ctx.advance(SCHEMA[i], SHAPE[i]);
    if (i < NUM_OPS - 1) {
      assert(s == ReplayStatus::MATCH);
    } else {
      assert(s == ReplayStatus::COMPLETE);
    }
    assert(ctx.output_ptr(0) != nullptr);
  }
  assert(ctx.compiled_iterations() == 1);

  // Iteration 2 (lazy reset).
  for (uint32_t i = 0; i < NUM_OPS; i++) {
    auto s = ctx.advance(SCHEMA[i], SHAPE[i]);
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

// ── Test 2: Divergence detection through the full pipeline ──
static void test_pipeline_divergence() {
  auto* ring = new TraceRing();
  auto* meta_log = new MetaLog();

  BackgroundThread bt;

  feed_iteration(ring, meta_log, 0);
  feed_iteration(ring, meta_log, 1);
  feed_trigger(ring, meta_log, 2);

  bt.start(ring, meta_log);

  auto* region = wait_for_region(bt);
  assert(region != nullptr);

  CrucibleContext ctx;
  assert(ctx.activate(region));

  // First 3 ops match.
  for (uint32_t i = 0; i < 3; i++) {
    assert(ctx.advance(SCHEMA[i], SHAPE[i]) == ReplayStatus::MATCH);
  }

  // Op 3: wrong schema → DIVERGED.
  assert(ctx.advance(SchemaHash{0xBAD}, SHAPE[3]) == ReplayStatus::DIVERGED);
  assert(ctx.diverged_count() == 1);
  assert(ctx.is_compiled()); // mode unchanged — caller decides

  // Op 3 again: wrong shape → still DIVERGED (position unchanged).
  assert(ctx.advance(SCHEMA[3], ShapeHash{0xBAD}) == ReplayStatus::DIVERGED);
  assert(ctx.diverged_count() == 2);

  ctx.deactivate();
  assert(ctx.is_recording());

  bt.stop();
  delete meta_log;
  delete ring;

  std::printf("  test_pipeline_divergence: PASSED\n");
}

// ── Test 3: Data integrity — writes flow through the pipeline ──
//
// Writes distinct patterns to each op's output during replay and
// verifies the data is readable from the next op's input. This proves
// that PoolAllocator + ReplayEngine + CrucibleContext correctly wire
// producer→consumer dataflow through pre-allocated pool memory.
static void test_pipeline_data_flow() {
  auto* ring = new TraceRing();
  auto* meta_log = new MetaLog();

  BackgroundThread bt;

  feed_iteration(ring, meta_log, 0);
  feed_iteration(ring, meta_log, 1);
  feed_trigger(ring, meta_log, 2);

  bt.start(ring, meta_log);

  auto* region = wait_for_region(bt);
  assert(region != nullptr);

  CrucibleContext ctx;
  assert(ctx.activate(region));

  // Advance op 0: write pattern to output.
  assert(ctx.advance(SCHEMA[0], SHAPE[0]) == ReplayStatus::MATCH);
  std::memset(ctx.output_ptr(0), 0x11, 4096);

  // Ops 1-6: verify input carries previous output's data, write new pattern.
  for (uint32_t i = 1; i < NUM_OPS - 1; i++) {
    assert(ctx.advance(SCHEMA[i], SHAPE[i]) == ReplayStatus::MATCH);

    // Input should carry previous op's output pattern.
    auto* in_data = static_cast<uint8_t*>(ctx.input_ptr(0));
    uint8_t expected = static_cast<uint8_t>(0x11 + i - 1);
    for (uint32_t b = 0; b < 4096; b++) {
      assert(in_data[b] == expected);
    }

    // Write this op's output with a new pattern.
    std::memset(ctx.output_ptr(0), static_cast<int>(0x11 + i), 4096);
  }

  // Op 7 (final): verify input, advance to COMPLETE.
  assert(ctx.advance(SCHEMA[7], SHAPE[7]) == ReplayStatus::COMPLETE);
  auto* in_last = static_cast<uint8_t*>(ctx.input_ptr(0));
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

// ── Test 4: Pool bounds — all pointers within allocated pool ──
static void test_pipeline_pool_bounds() {
  auto* ring = new TraceRing();
  auto* meta_log = new MetaLog();

  BackgroundThread bt;

  feed_iteration(ring, meta_log, 0);
  feed_iteration(ring, meta_log, 1);
  feed_trigger(ring, meta_log, 2);

  bt.start(ring, meta_log);

  auto* region = wait_for_region(bt);
  assert(region != nullptr);

  CrucibleContext ctx;
  assert(ctx.activate(region));

  auto* pool_base = static_cast<uint8_t*>(ctx.pool().pool_base());
  uint64_t pool_bytes = ctx.pool().pool_bytes();
  assert(pool_base != nullptr);
  assert(pool_bytes > 0);

  // Replay and verify every output pointer is within [pool_base, pool_base + pool_bytes).
  for (uint32_t i = 0; i < NUM_OPS; i++) {
    auto s = ctx.advance(SCHEMA[i], SHAPE[i]);
    (void)s;
    auto* p = static_cast<uint8_t*>(ctx.output_ptr(0));
    assert(p >= pool_base);
    assert(p + 4096 <= pool_base + pool_bytes);
  }

  // Verify input pointers are within pool bounds too (ops 1-7).
  // Need to re-replay since we consumed the iteration.
  // After COMPLETE, lazy reset kicks in on next advance.
  for (uint32_t i = 0; i < NUM_OPS; i++) {
    auto s = ctx.advance(SCHEMA[i], SHAPE[i]);
    (void)s;
    if (i > 0) {
      auto* p = static_cast<uint8_t*>(ctx.input_ptr(0));
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

// ── Test 5: Multiple boundary detections ──
//
// After the first boundary, the bg thread retains the last K trigger ops
// as the start of the next iteration's current_trace. From there, we need
// one more full iteration + K trigger to fire the second boundary (since
// confirmed is already true, the next match is immediately a boundary).
//
// Timeline:
//   Boundary 1: feed_iteration(0) + feed_iteration(1) + feed_trigger(2)
//     → detector: signature built from iter 0, candidate at iter 1, confirmed at iter 2
//     → bg thread retains K trigger ops, resets current_trace
//   Boundary 2: feed remaining of iter 2 (3 ops) + feed_iteration(3) + feed_trigger(4)
//     → detector: confirmed=true already, next K-match fires immediately
static void test_pipeline_multi_iteration() {
  auto* ring = new TraceRing();
  auto* meta_log = new MetaLog();

  BackgroundThread bt;

  // Pre-load all ops for the first boundary.
  feed_iteration(ring, meta_log, 0);
  feed_iteration(ring, meta_log, 1);
  feed_trigger(ring, meta_log, 2);

  bt.start(ring, meta_log);

  // Wait for first region.
  auto* region1 = wait_for_region(bt);
  assert(region1 != nullptr);
  assert(region1->num_ops == NUM_OPS);

  // Feed remaining ops of iter 2 (ops K..NUM_OPS-1) to complete it.
  // The bg thread's current_trace already has K trigger ops from iter 2.
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

  // Full iteration 3 + trigger for iteration 4.
  feed_iteration(ring, meta_log, 3);
  feed_trigger(ring, meta_log, 4);

  // Wait for second region (active_region gets overwritten).
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(5000);
  RegionNode* region2 = nullptr;
  while (std::chrono::steady_clock::now() < deadline) {
    region2 = bt.active_region.load(std::memory_order_acquire);
    if (region2 && region2 != region1) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(region2 != nullptr && region2 != region1 &&
         "Second boundary did not fire");
  assert(region2->num_ops == NUM_OPS);
  assert(region2->plan != nullptr);

  // Replay with the second region.
  CrucibleContext ctx;
  assert(ctx.activate(region2));
  for (uint32_t i = 0; i < NUM_OPS; i++) {
    auto s = ctx.advance(SCHEMA[i], SHAPE[i]);
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

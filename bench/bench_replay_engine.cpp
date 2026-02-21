// Benchmark for ReplayEngine + CrucibleContext hot paths.
//
// Measures:
//   1. advance() alone (steady-state matching)
//   2. advance() + output_ptr(0) (typical per-op cost)
//   3. advance() + output_ptr(0) + input_ptr(0) (full per-op cost)
//   4. CrucibleContext::advance() overhead vs raw ReplayEngine
//   5. Scaling across region sizes: 8, 100, 1000 ops

#include "bench_harness.h"

#include <crucible/CrucibleContext.h>
#include <crucible/ReplayEngine.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace crucible;

// Build a synthetic region with N ops, each having 1 output and 1 input.
// All hashes are deterministic: schema_hash = 1000+i, shape_hash = 2000+i.
// Slot layout: ops[0] outputs to slot 0, ops[1] outputs to slot 1, etc.
// ops[i] reads from slot (i-1) (circular for i=0 -> slot N-1).
struct BenchRegion {
  TraceEntry* ops = nullptr;
  SlotId* out_slots = nullptr;     // N arrays of 1 SlotId each
  SlotId* in_slots = nullptr;      // N arrays of 1 SlotId each
  TensorSlot* slots = nullptr;
  MemoryPlan plan{};
  RegionNode region{};
  uint32_t num_ops = 0;

  explicit BenchRegion(uint32_t n) : num_ops(n) {
    ops = static_cast<TraceEntry*>(std::calloc(n, sizeof(TraceEntry)));
    out_slots = static_cast<SlotId*>(std::calloc(n, sizeof(SlotId)));
    in_slots = static_cast<SlotId*>(std::calloc(n, sizeof(SlotId)));
    slots = static_cast<TensorSlot*>(std::calloc(n, sizeof(TensorSlot)));
    if (!ops || !out_slots || !in_slots || !slots) std::abort();

    for (uint32_t i = 0; i < n; i++) {
      out_slots[i] = SlotId{i};
      in_slots[i] = SlotId{(i > 0) ? (i - 1) : (n - 1)};

      ops[i].schema_hash = SchemaHash{1000 + i};
      ops[i].shape_hash = ShapeHash{2000 + i};
      ops[i].num_outputs = 1;
      ops[i].output_slot_ids = &out_slots[i];
      ops[i].num_inputs = 1;
      ops[i].input_slot_ids = &in_slots[i];
    }

    for (uint32_t i = 0; i < n; i++) {
      slots[i] = TensorSlot{
        .offset_bytes = i * 256ULL, .nbytes = 256,
        .birth_op = OpIndex{0}, .death_op = OpIndex{n},
        .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
        .device_idx = 0, .layout = Layout::Strided, .is_external = false,
        .pad = {}, .slot_id = SlotId{i}, .pad2 = {}
      };
    }

    plan.slots = slots;
    plan.num_slots = n;
    plan.num_external = 0;
    plan.pool_bytes = static_cast<uint64_t>(n) * 256;
    plan.device_type = DeviceType::CPU;
    plan.device_idx = 0;

    std::memset(&region, 0, sizeof(region));
    region.kind = TraceNodeKind::REGION;
    region.ops = ops;
    region.num_ops = n;
    region.plan = &plan;
  }

  ~BenchRegion() {
    std::free(ops);
    std::free(out_slots);
    std::free(in_slots);
    std::free(slots);
  }

  BenchRegion(const BenchRegion&) = delete;
  BenchRegion& operator=(const BenchRegion&) = delete;
};

// Walk the entire region once with advance() only (no output_ptr).
// Returns the final status to prevent dead-code elimination.
static ReplayStatus walk_advance_only(ReplayEngine& engine,
                                      const TraceEntry* ops,
                                      uint32_t n) {
  engine.reset();
  ReplayStatus last = ReplayStatus::MATCH;
  for (uint32_t i = 0; i < n; i++) {
    last = engine.advance(ops[i].schema_hash, ops[i].shape_hash);
  }
  return last;
}

// Walk with advance() + output_ptr(0).
static void* walk_advance_output(ReplayEngine& engine,
                                 const TraceEntry* ops,
                                 uint32_t n) {
  engine.reset();
  void* last_ptr = nullptr;
  for (uint32_t i = 0; i < n; i++) {
    (void)engine.advance(ops[i].schema_hash, ops[i].shape_hash);
    last_ptr = engine.output_ptr(0);
  }
  return last_ptr;
}

// Walk with advance() + output_ptr(0) + input_ptr(0).
static void* walk_advance_both(ReplayEngine& engine,
                               const TraceEntry* ops,
                               uint32_t n) {
  engine.reset();
  void* last_ptr = nullptr;
  for (uint32_t i = 0; i < n; i++) {
    (void)engine.advance(ops[i].schema_hash, ops[i].shape_hash);
    last_ptr = engine.output_ptr(0);
    bench::DoNotOptimize(engine.input_ptr(0));
  }
  return last_ptr;
}

// CrucibleContext full iteration: advance + output_ptr for each op.
static void* walk_context(CrucibleContext& ctx,
                          const TraceEntry* ops,
                          uint32_t n) {
  void* last_ptr = nullptr;
  for (uint32_t i = 0; i < n; i++) {
    auto s = ctx.advance(ops[i].schema_hash, ops[i].shape_hash);
    last_ptr = ctx.output_ptr(0);
    bench::DoNotOptimize(s);
  }
  return last_ptr;
}

struct RegionThresholds {
  double advance_only_ns = 0.0;
  double advance_output_ns = 0.0;
  double advance_both_ns = 0.0;
  double context_ns = 0.0;
};

static void bench_region_size(uint32_t n, const RegionThresholds& thresh) {
  BenchRegion br(n);
  PoolAllocator pool;
  pool.init(&br.plan);

  ReplayEngine engine;
  engine.init(&br.region, &pool);

  char label[128];
  const uint64_t iters = (n <= 100) ? 1'000'000 : 100'000;

  // 1. advance() only
  std::snprintf(label, sizeof(label), "advance only [%u ops]", n);
  BENCH_ROUNDS_CHECK(label, iters, 21, thresh.advance_only_ns, {
    auto s = walk_advance_only(engine, br.ops, n);
    bench::DoNotOptimize(s);
  });

  // 2. advance() + output_ptr(0)
  std::snprintf(label, sizeof(label), "advance + output_ptr [%u ops]", n);
  BENCH_ROUNDS_CHECK(label, iters, 21, thresh.advance_output_ns, {
    auto p = walk_advance_output(engine, br.ops, n);
    bench::DoNotOptimize(p);
  });

  // 3. advance() + output_ptr(0) + input_ptr(0)
  std::snprintf(label, sizeof(label), "advance + out + in [%u ops]", n);
  BENCH_ROUNDS_CHECK(label, iters, 21, thresh.advance_both_ns, {
    auto p = walk_advance_both(engine, br.ops, n);
    bench::DoNotOptimize(p);
  });

  // 4. CrucibleContext overhead
  CrucibleContext ctx;
  (void)ctx.activate(&br.region);

  std::snprintf(label, sizeof(label), "CrucibleContext [%u ops]", n);
  BENCH_ROUNDS_CHECK(label, iters, 21, thresh.context_ns, {
    auto p = walk_context(ctx, br.ops, n);
    bench::DoNotOptimize(p);
  });

  pool.destroy();
}

int main() {
  std::printf("=== ReplayEngine Hot-Path Benchmark ===\n\n");
  std::printf("sizeof(ReplayEngine)   = %zu\n", sizeof(ReplayEngine));
  std::printf("sizeof(CrucibleContext) = %zu\n", sizeof(CrucibleContext));
  std::printf("sizeof(TraceEntry)     = %zu\n\n", sizeof(TraceEntry));

  std::printf("--- 8 ops/region ---\n");
  bench_region_size(8, {.advance_only_ns = 7.2, .advance_output_ns = 8.3,
                        .advance_both_ns = 14.0, .context_ns = 15.8});

  std::printf("\n--- 100 ops/region ---\n");
  bench_region_size(100, {.advance_only_ns = 70.4, .advance_output_ns = 98.1,
                          .advance_both_ns = 173.1, .context_ns = 202.1});

  std::printf("\n--- 1000 ops/region ---\n");
  bench_region_size(1000, {.advance_only_ns = 1019.6, .advance_output_ns = 1634.4,
                           .advance_both_ns = 2205.9, .context_ns = 2385.0});

  std::printf("\nDone.\n");
  return 0;
}

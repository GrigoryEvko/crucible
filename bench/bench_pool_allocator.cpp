// Benchmark: PoolAllocator hot and cold paths.
//
// Hot path: slot_ptr(SlotId) — must be a single 8-byte load (~1ns).
// Cold path: init(), destroy(), detach() — amortized over many iterations.

#include "bench_harness.h"

#include <crucible/PoolAllocator.h>
#include <crucible/MerkleDag.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>

using crucible::PoolAllocator;
using crucible::MemoryPlan;
using crucible::TensorSlot;
using crucible::SlotId;
using crucible::OpIndex;
using crucible::ScalarType;
using crucible::DeviceType;
using crucible::Layout;

// ═══════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════

// Build a MemoryPlan with N internal slots, each 256-byte aligned.
// Slot i has offset = i * slot_size, size = slot_size.
// No externals. Returns plan by value (cold path helper).
static MemoryPlan make_uniform_plan(TensorSlot* slots, uint32_t n,
                                     uint64_t slot_size) {
  for (uint32_t i = 0; i < n; i++) {
    slots[i].offset_bytes = static_cast<uint64_t>(i) * slot_size;
    slots[i].nbytes       = slot_size;
    slots[i].slot_id      = SlotId{i};
    slots[i].birth_op     = OpIndex{0};
    slots[i].death_op     = OpIndex{n};
    slots[i].dtype        = ScalarType::Float;
    slots[i].device_type  = DeviceType::CPU;
    slots[i].device_idx   = 0;
    slots[i].layout       = Layout::Strided;
    slots[i].is_external  = false;
    std::memset(slots[i].pad, 0, sizeof(slots[i].pad));
  }
  MemoryPlan plan{};
  plan.slots        = slots;
  plan.num_slots    = n;
  plan.num_external = 0;
  plan.pool_bytes   = static_cast<uint64_t>(n) * slot_size;
  plan.device_type  = DeviceType::CPU;
  plan.device_idx   = 0;
  return plan;
}

// Build a plan with some external slots.
static MemoryPlan make_mixed_plan(TensorSlot* slots, uint32_t n_internal,
                                   uint32_t n_external, uint64_t slot_size) {
  uint32_t total = n_internal + n_external;
  for (uint32_t i = 0; i < total; i++) {
    bool ext = (i >= n_internal);
    slots[i].offset_bytes = ext ? 0 : (static_cast<uint64_t>(i) * slot_size);
    slots[i].nbytes       = slot_size;
    slots[i].slot_id      = SlotId{i};
    slots[i].birth_op     = OpIndex{0};
    slots[i].death_op     = OpIndex{total};
    slots[i].dtype        = ScalarType::Float;
    slots[i].device_type  = DeviceType::CPU;
    slots[i].device_idx   = 0;
    slots[i].layout       = Layout::Strided;
    slots[i].is_external  = ext;
    std::memset(slots[i].pad, 0, sizeof(slots[i].pad));
  }
  MemoryPlan plan{};
  plan.slots        = slots;
  plan.num_slots    = total;
  plan.num_external = n_external;
  plan.pool_bytes   = static_cast<uint64_t>(n_internal) * slot_size;
  plan.device_type  = DeviceType::CPU;
  plan.device_idx   = 0;
  return plan;
}

// Pre-generate a random access pattern (Fisher-Yates shuffle repeated).
static void fill_random_indices(uint32_t* indices, uint32_t count,
                                 uint32_t num_slots) {
  std::mt19937 rng(42);
  for (uint32_t i = 0; i < count; i++) {
    indices[i] = rng() % num_slots;
  }
}

// ═══════════════════════════════════════════════════════════════════
// Benchmarks
// ═══════════════════════════════════════════════════════════════════

static void bench_slot_ptr_sequential() {
  std::printf("\n── slot_ptr() sequential access ──\n");

  // 1 slot
  {
    TensorSlot slots[1]{};
    auto plan = make_uniform_plan(slots, 1, 256);
    PoolAllocator pool;
    pool.init(&plan);
    SlotId s0{0};

    BENCH_CHECK("slot_ptr(1 slot, same)", 10'000'000, 0.3, {
      bench::DoNotOptimize(pool.slot_ptr(s0));
    });
    pool.destroy();
  }

  // 10 slots — sequential sweep
  {
    constexpr uint32_t N = 10;
    TensorSlot slots[N]{};
    auto plan = make_uniform_plan(slots, N, 256);
    PoolAllocator pool;
    pool.init(&plan);

    BENCH_CHECK("slot_ptr(10 slots, sweep)", 1'000'000, 2.1, {
      for (uint32_t i = 0; i < N; i++) {
        bench::DoNotOptimize(pool.slot_ptr(SlotId{i}));
      }
    });
    pool.destroy();
  }

  // 100 slots — sequential sweep
  {
    constexpr uint32_t N = 100;
    TensorSlot slots[N]{};
    auto plan = make_uniform_plan(slots, N, 256);
    PoolAllocator pool;
    pool.init(&plan);

    BENCH_CHECK("slot_ptr(100 slots, sweep)", 1'000'000, 38.7, {
      for (uint32_t i = 0; i < N; i++) {
        bench::DoNotOptimize(pool.slot_ptr(SlotId{i}));
      }
    });
    pool.destroy();
  }

  // 1000 slots — sequential sweep
  {
    constexpr uint32_t N = 1000;
    TensorSlot slots[N]{};
    auto plan = make_uniform_plan(slots, N, 256);
    PoolAllocator pool;
    pool.init(&plan);

    BENCH_CHECK("slot_ptr(1000 slots, sweep)", 100'000, 246.0, {
      for (uint32_t i = 0; i < N; i++) {
        bench::DoNotOptimize(pool.slot_ptr(SlotId{i}));
      }
    });
    pool.destroy();
  }
}

static void bench_slot_ptr_random() {
  std::printf("\n── slot_ptr() random access ──\n");

  // Pre-generate random indices for deterministic access patterns.
  constexpr uint32_t ACCESS_COUNT = 10'000;

  // 10 slots — random
  {
    constexpr uint32_t N = 10;
    TensorSlot slots[N]{};
    auto plan = make_uniform_plan(slots, N, 256);
    PoolAllocator pool;
    pool.init(&plan);

    uint32_t indices[ACCESS_COUNT];
    fill_random_indices(indices, ACCESS_COUNT, N);
    uint32_t idx = 0;

    BENCH_CHECK("slot_ptr(10 slots, random)", 1'000'000, 0.6, {
      bench::DoNotOptimize(pool.slot_ptr(SlotId{indices[idx % ACCESS_COUNT]}));
      idx++;
    });
    pool.destroy();
  }

  // 100 slots — random
  {
    constexpr uint32_t N = 100;
    TensorSlot slots[N]{};
    auto plan = make_uniform_plan(slots, N, 256);
    PoolAllocator pool;
    pool.init(&plan);

    uint32_t indices[ACCESS_COUNT];
    fill_random_indices(indices, ACCESS_COUNT, N);
    uint32_t idx = 0;

    BENCH_CHECK("slot_ptr(100 slots, random)", 1'000'000, 0.8, {
      bench::DoNotOptimize(pool.slot_ptr(SlotId{indices[idx % ACCESS_COUNT]}));
      idx++;
    });
    pool.destroy();
  }

  // 1000 slots — random
  {
    constexpr uint32_t N = 1000;
    TensorSlot slots[N]{};
    auto plan = make_uniform_plan(slots, N, 256);
    PoolAllocator pool;
    pool.init(&plan);

    uint32_t indices[ACCESS_COUNT];
    fill_random_indices(indices, ACCESS_COUNT, N);
    uint32_t idx = 0;

    BENCH_CHECK("slot_ptr(1000 slots, random)", 1'000'000, 0.8, {
      bench::DoNotOptimize(pool.slot_ptr(SlotId{indices[idx % ACCESS_COUNT]}));
      idx++;
    });
    pool.destroy();
  }
}

static void bench_slot_ptr_via_pointer() {
  std::printf("\n── slot_ptr() via pointer-to-pool (ReplayEngine pattern) ──\n");

  // ReplayEngine holds a const PoolAllocator* and calls pool_->slot_ptr(sid).
  // This tests whether the extra indirection costs anything compared to
  // calling slot_ptr() on a direct reference or a captured raw table.
  constexpr uint32_t N = 100;
  TensorSlot slots[N]{};
  auto plan = make_uniform_plan(slots, N, 256);
  PoolAllocator pool;
  pool.init(&plan);

  // 1. Direct reference (pool on stack, compiler knows the address)
  BENCH_CHECK("slot_ptr(100, direct ref)", 1'000'000, 44.6, {
    for (uint32_t i = 0; i < N; i++) {
      bench::DoNotOptimize(pool.slot_ptr(SlotId{i}));
    }
  });

  // 2. Via pointer (simulates ReplayEngine holding const PoolAllocator*)
  const PoolAllocator* pool_ptr = &pool;
  bench::DoNotOptimize(pool_ptr);

  BENCH_CHECK("slot_ptr(100, via pointer)", 1'000'000, 43.5, {
    for (uint32_t i = 0; i < N; i++) {
      bench::DoNotOptimize(pool_ptr->slot_ptr(SlotId{i}));
    }
  });

  // 3. Captured raw table (the optimization: one level of indirection removed)
  void* const* tbl = pool.table();
  bench::DoNotOptimize(tbl);

  BENCH_CHECK("table[sid] (100, captured raw)", 1'000'000, 40.4, {
    for (uint32_t i = 0; i < N; i++) {
      bench::DoNotOptimize(tbl[i]);
    }
  });

  pool.destroy();
}

static void bench_init() {
  std::printf("\n── init() cold path ──\n");

  // 10 slots
  {
    constexpr uint32_t N = 10;
    TensorSlot slots[N]{};
    auto plan = make_uniform_plan(slots, N, 256);

    BENCH_CHECK("init(10 slots)", 100'000, 61.1, {
      PoolAllocator pool;
      pool.init(&plan);
      bench::DoNotOptimize(pool.pool_base());
      pool.destroy();
    });
  }

  // 100 slots
  {
    constexpr uint32_t N = 100;
    TensorSlot slots[N]{};
    auto plan = make_uniform_plan(slots, N, 256);

    BENCH_CHECK("init(100 slots)", 100'000, 44.1, {
      PoolAllocator pool;
      pool.init(&plan);
      bench::DoNotOptimize(pool.pool_base());
      pool.destroy();
    });
  }

  // 1000 slots
  {
    constexpr uint32_t N = 1000;
    TensorSlot slots[N]{};
    auto plan = make_uniform_plan(slots, N, 256);

    BENCH_CHECK("init(1000 slots)", 10'000, 41.9, {
      PoolAllocator pool;
      pool.init(&plan);
      bench::DoNotOptimize(pool.pool_base());
      pool.destroy();
    });
  }
}

static void bench_destroy() {
  std::printf("\n── destroy() teardown ──\n");

  // Measure init+destroy pair, then subtract init-only cost.
  // This isolates destroy() timing without the batch-across-rounds problem.
  constexpr uint32_t N = 100;
  TensorSlot slots[N]{};
  auto plan = make_uniform_plan(slots, N, 256);

  // The init() bench already measures init+destroy. We can measure
  // destroy of an already-destroyed pool (no-op) as a baseline,
  // and init+destroy as the combined cost.
  BENCH_CHECK("destroy(already destroyed, no-op)", 1'000'000, 4.8, {
    PoolAllocator pool;
    // Default-constructed, never initialized. destroy() is a no-op
    // (free(nullptr) + zero assignments).
    pool.destroy();
    bench::DoNotOptimize(&pool);
  });

  // Combined init+destroy to see the pair cost.
  BENCH_CHECK("init+destroy(100 slots, pair)", 100'000, 46.2, {
    PoolAllocator pool;
    pool.init(&plan);
    bench::DoNotOptimize(pool.pool_base());
    pool.destroy();
  });
}

static void bench_detach_reinit() {
  std::printf("\n── detach() + init() region switch ──\n");

  // Simulate the CrucibleContext::switch_region pattern:
  // detach old pool → init new pool → let DetachedPool destruct.
  constexpr uint32_t N = 100;
  TensorSlot slots_a[N]{};
  TensorSlot slots_b[N]{};
  auto plan_a = make_uniform_plan(slots_a, N, 256);
  auto plan_b = make_uniform_plan(slots_b, N, 512);

  BENCH_CHECK("detach+init(100 slots)", 10'000, 91.1, {
    PoolAllocator pool;
    pool.init(&plan_a);
    {
      auto old = pool.detach();
      bench::DoNotOptimize(old.base);
      pool.init(&plan_b);
    }
    // DetachedPool destructs here, freeing old allocation.
    bench::DoNotOptimize(pool.pool_base());
    pool.destroy();
  });
}

static void bench_register_external() {
  std::printf("\n── register_external() ──\n");

  constexpr uint32_t N_INT = 80;
  constexpr uint32_t N_EXT = 20;
  constexpr uint32_t TOTAL = N_INT + N_EXT;
  TensorSlot slots[TOTAL]{};
  auto plan = make_mixed_plan(slots, N_INT, N_EXT, 256);

  PoolAllocator pool;
  pool.init(&plan);

  // Prepare fake external buffers.
  alignas(256) char ext_bufs[N_EXT][256];

  BENCH_CHECK("register_external(20 slots)", 1'000'000, 0.1, {
    for (uint32_t i = 0; i < N_EXT; i++) {
      pool.register_external(SlotId{N_INT + i}, ext_bufs[i]);
    }
  });

  pool.destroy();
}

static void bench_replay_pattern() {
  std::printf("\n── replay-like pattern (advance + slot_ptr) ──\n");

  // Simulate a realistic replay loop: for each op, look up 2 input
  // slots and 1 output slot via slot_ptr().
  constexpr uint32_t NUM_OPS = 200;
  constexpr uint32_t NUM_SLOTS = 100;
  TensorSlot slots[NUM_SLOTS]{};
  auto plan = make_uniform_plan(slots, NUM_SLOTS, 256);

  PoolAllocator pool;
  pool.init(&plan);

  // Pre-generate slot access pattern: (input0, input1, output) per op.
  struct OpSlots {
    SlotId in0;
    SlotId in1;
    SlotId out;
  };
  OpSlots op_slots[NUM_OPS];
  std::mt19937 rng(123);
  for (uint32_t i = 0; i < NUM_OPS; i++) {
    op_slots[i].in0 = SlotId{static_cast<uint32_t>(rng() % NUM_SLOTS)};
    op_slots[i].in1 = SlotId{static_cast<uint32_t>(rng() % NUM_SLOTS)};
    op_slots[i].out = SlotId{static_cast<uint32_t>(rng() % NUM_SLOTS)};
  }

  BENCH_CHECK("replay(200 ops, 3 slots each)", 100'000, 173.0, {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
      bench::DoNotOptimize(pool.slot_ptr(op_slots[i].in0));
      bench::DoNotOptimize(pool.slot_ptr(op_slots[i].in1));
      bench::DoNotOptimize(pool.slot_ptr(op_slots[i].out));
    }
  });

  pool.destroy();
}

static void bench_sizeof() {
  std::printf("\n── sizeof checks ──\n");
  std::printf("  sizeof(PoolAllocator)           = %zu bytes\n", sizeof(PoolAllocator));
  std::printf("  sizeof(PoolAllocator::DetachedPool) = %zu bytes\n",
              sizeof(PoolAllocator::DetachedPool));
  std::printf("  sizeof(TensorSlot)              = %zu bytes\n", sizeof(TensorSlot));
  std::printf("  sizeof(MemoryPlan)              = %zu bytes\n", sizeof(MemoryPlan));
  std::printf("  sizeof(SlotId)                  = %zu bytes\n", sizeof(SlotId));
}

int main() {
  std::printf("=== PoolAllocator Benchmark ===\n");

  bench_sizeof();
  bench_slot_ptr_sequential();
  bench_slot_ptr_random();
  bench_slot_ptr_via_pointer();
  bench_init();
  bench_destroy();
  bench_detach_reinit();
  bench_register_external();
  bench_replay_pattern();

  std::printf("\nDone.\n");
  return 0;
}

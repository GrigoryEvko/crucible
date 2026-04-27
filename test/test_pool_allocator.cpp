#include <crucible/PoolAllocator.h>
#include <crucible/BackgroundThread.h>
#include <crucible/effects/Capabilities.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

using crucible::PoolAllocator;
using crucible::MemoryPlan;
using crucible::TensorSlot;
using crucible::SlotId;
using crucible::OpIndex;
using crucible::ScalarType;
using crucible::DeviceType;
using crucible::Layout;

// Helper: build a MemoryPlan with manually-assigned offsets.
// Does NOT call compute_memory_plan — tests PoolAllocator in isolation.
static MemoryPlan make_manual_plan(TensorSlot* slots, uint32_t n,
                                   uint64_t pool_bytes, uint32_t num_ext) {
  MemoryPlan plan{};
  plan.slots = slots;
  plan.num_slots = n;
  plan.num_external = num_ext;
  plan.pool_bytes = pool_bytes;
  plan.device_type = DeviceType::CPU;
  plan.device_idx = 0;
  return plan;
}

// ── Test 1: Basic init, alignment, and slot pointer ranges ──
static void test_basic_init() {
  // Two internal slots at known offsets, one external.
  //   Slot 0: internal, offset=0, 1024 bytes
  //   Slot 1: internal, offset=1024 (256-aligned), 2048 bytes
  //   Slot 2: external
  TensorSlot slots[3]{};
  slots[0] = {.offset_bytes = 0, .nbytes = 1024,
              .birth_op = OpIndex{0}, .death_op = OpIndex{5},
              .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
              .device_idx = 0, .layout = Layout::Strided,
              .is_external = false, .pad = {}, .slot_id = SlotId{0}, .pad2 = {}};
  slots[1] = {.offset_bytes = 1024, .nbytes = 2048,
              .birth_op = OpIndex{1}, .death_op = OpIndex{4},
              .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
              .device_idx = 0, .layout = Layout::Strided,
              .is_external = false, .pad = {}, .slot_id = SlotId{1}, .pad2 = {}};
  slots[2] = {.offset_bytes = 0, .nbytes = 512,
              .birth_op = OpIndex{0}, .death_op = OpIndex{7},
              .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
              .device_idx = 0, .layout = Layout::Strided,
              .is_external = true, .pad = {}, .slot_id = SlotId{2}, .pad2 = {}};

  MemoryPlan plan = make_manual_plan(slots, 3, 3072, 1);

  PoolAllocator pool;
  pool.init(&plan);
  // ScopedView: one runtime check at construction, zero on every
  // slot_ptr / register_external call that takes `pv` as proof.
  auto pv = pool.mint_initialized_view();

  // Pool is allocated and 256-byte aligned.
  assert(pool.is_initialized());
  assert(pool.pool_base() != nullptr);
  assert(reinterpret_cast<uintptr_t>(pool.pool_base()) % PoolAllocator::ALIGNMENT == 0);
  assert(pool.pool_bytes() == 3072);
  assert(pool.num_slots() == 3);
  assert(pool.num_external() == 1);

  // Internal slot pointers are within pool bounds.
  auto* base = static_cast<char*>(pool.pool_base());
  assert(pool.slot_ptr(SlotId{0}, pv) == base + 0);
  assert(pool.slot_ptr(SlotId{1}, pv) == base + 1024);

  // Internal slot pointers are 256-aligned (offsets are 256-aligned multiples
  // or 0, and base is 256-aligned, so base+0 and base+1024 are both aligned).
  assert(reinterpret_cast<uintptr_t>(pool.slot_ptr(SlotId{0}, pv)) % 256 == 0);
  assert(reinterpret_cast<uintptr_t>(pool.slot_ptr(SlotId{1}, pv)) % 256 == 0);

  // External slot is nullptr before registration.
  assert(pool.slot_ptr(SlotId{2}, pv) == nullptr);

  std::printf("  test_basic_init: PASSED\n");
}

// ── Test 2: External slot registration ──
static void test_external_registration() {
  TensorSlot slots[2]{};
  slots[0] = {.offset_bytes = 0, .nbytes = 1024,
              .birth_op = OpIndex{0}, .death_op = OpIndex{3},
              .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
              .device_idx = 0, .layout = Layout::Strided,
              .is_external = false, .pad = {}, .slot_id = SlotId{0}, .pad2 = {}};
  slots[1] = {.offset_bytes = 0, .nbytes = 512,
              .birth_op = OpIndex{0}, .death_op = OpIndex{3},
              .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
              .device_idx = 0, .layout = Layout::Strided,
              .is_external = true, .pad = {}, .slot_id = SlotId{1}, .pad2 = {}};

  MemoryPlan plan = make_manual_plan(slots, 2, 1024, 1);

  PoolAllocator pool;
  pool.init(&plan);
  auto pv = pool.mint_initialized_view();

  // Before registration: external is null.
  assert(pool.slot_ptr(SlotId{1}, pv) == nullptr);

  // Register an external pointer (simulates param tensor).
  alignas(256) char fake_param[512];
  pool.register_external(SlotId{1}, crucible::safety::NonNull<void*>{fake_param}, pv);

  // After registration: returns the registered pointer.
  assert(pool.slot_ptr(SlotId{1}, pv) == fake_param);

  std::printf("  test_external_registration: PASSED\n");
}

// ── Test 3: Write-then-read — no overlapping corruption ──
static void test_write_read_isolation() {
  // Three internal slots with non-overlapping offsets.
  //   Slot 0: offset=0, 256 bytes
  //   Slot 1: offset=256, 512 bytes
  //   Slot 2: offset=768, 256 bytes
  TensorSlot slots[3]{};
  slots[0] = {.offset_bytes = 0, .nbytes = 256,
              .birth_op = OpIndex{0}, .death_op = OpIndex{3},
              .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
              .device_idx = 0, .layout = Layout::Strided,
              .is_external = false, .pad = {}, .slot_id = SlotId{0}, .pad2 = {}};
  slots[1] = {.offset_bytes = 256, .nbytes = 512,
              .birth_op = OpIndex{1}, .death_op = OpIndex{4},
              .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
              .device_idx = 0, .layout = Layout::Strided,
              .is_external = false, .pad = {}, .slot_id = SlotId{1}, .pad2 = {}};
  slots[2] = {.offset_bytes = 768, .nbytes = 256,
              .birth_op = OpIndex{2}, .death_op = OpIndex{5},
              .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
              .device_idx = 0, .layout = Layout::Strided,
              .is_external = false, .pad = {}, .slot_id = SlotId{2}, .pad2 = {}};

  MemoryPlan plan = make_manual_plan(slots, 3, 1024, 0);

  PoolAllocator pool;
  pool.init(&plan);
  auto pv = pool.mint_initialized_view();

  // Write distinct byte patterns to each slot.
  std::memset(pool.slot_ptr(SlotId{0}, pv), 0xAA, 256);
  std::memset(pool.slot_ptr(SlotId{1}, pv), 0xBB, 512);
  std::memset(pool.slot_ptr(SlotId{2}, pv), 0xCC, 256);

  // Read back and verify — no cross-contamination.
  auto* p0 = static_cast<uint8_t*>(pool.slot_ptr(SlotId{0}, pv));
  auto* p1 = static_cast<uint8_t*>(pool.slot_ptr(SlotId{1}, pv));
  auto* p2 = static_cast<uint8_t*>(pool.slot_ptr(SlotId{2}, pv));

  for (uint32_t i = 0; i < 256; i++) assert(p0[i] == 0xAA);
  for (uint32_t i = 0; i < 512; i++) assert(p1[i] == 0xBB);
  for (uint32_t i = 0; i < 256; i++) assert(p2[i] == 0xCC);

  std::printf("  test_write_read_isolation: PASSED\n");
}

// ── Test 4: All-external plan (zero pool bytes) ──
static void test_all_external() {
  TensorSlot slots[2]{};
  slots[0] = {.offset_bytes = 0, .nbytes = 1024,
              .birth_op = OpIndex{0}, .death_op = OpIndex{3},
              .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
              .device_idx = 0, .layout = Layout::Strided,
              .is_external = true, .pad = {}, .slot_id = SlotId{0}, .pad2 = {}};
  slots[1] = {.offset_bytes = 0, .nbytes = 2048,
              .birth_op = OpIndex{0}, .death_op = OpIndex{5},
              .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
              .device_idx = 0, .layout = Layout::Strided,
              .is_external = true, .pad = {}, .slot_id = SlotId{1}, .pad2 = {}};

  MemoryPlan plan = make_manual_plan(slots, 2, 0, 2);

  PoolAllocator pool;
  pool.init(&plan);
  auto pv = pool.mint_initialized_view();

  assert(pool.is_initialized());
  assert(pool.pool_base() == nullptr);
  assert(pool.pool_bytes() == 0);
  assert(pool.num_slots() == 2);

  // Both slots are null before registration.
  assert(pool.slot_ptr(SlotId{0}, pv) == nullptr);
  assert(pool.slot_ptr(SlotId{1}, pv) == nullptr);

  // Register externals.
  alignas(256) char buf_a[1024];
  alignas(256) char buf_b[2048];
  pool.register_external(SlotId{0}, crucible::safety::NonNull<void*>{buf_a}, pv);
  pool.register_external(SlotId{1}, crucible::safety::NonNull<void*>{buf_b}, pv);
  assert(pool.slot_ptr(SlotId{0}, pv) == buf_a);
  assert(pool.slot_ptr(SlotId{1}, pv) == buf_b);

  std::printf("  test_all_external: PASSED\n");
}

// ── Test 5: Destroy and re-init with a different plan ──
static void test_reinit() {
  TensorSlot slots_a[1]{};
  slots_a[0] = {.offset_bytes = 0, .nbytes = 512,
                .birth_op = OpIndex{0}, .death_op = OpIndex{3},
                .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                .device_idx = 0, .layout = Layout::Strided,
                .is_external = false, .pad = {}, .slot_id = SlotId{0}, .pad2 = {}};

  MemoryPlan plan_a = make_manual_plan(slots_a, 1, 512, 0);

  PoolAllocator pool;
  pool.init(&plan_a);
  assert(pool.pool_bytes() == 512);
  void* old_base = pool.pool_base();

  // Destroy and re-init with a larger plan.
  pool.destroy();
  assert(!pool.is_initialized());

  TensorSlot slots_b[2]{};
  slots_b[0] = {.offset_bytes = 0, .nbytes = 1024,
                .birth_op = OpIndex{0}, .death_op = OpIndex{5},
                .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                .device_idx = 0, .layout = Layout::Strided,
                .is_external = false, .pad = {}, .slot_id = SlotId{0}, .pad2 = {}};
  slots_b[1] = {.offset_bytes = 1024, .nbytes = 2048,
                .birth_op = OpIndex{1}, .death_op = OpIndex{4},
                .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                .device_idx = 0, .layout = Layout::Strided,
                .is_external = false, .pad = {}, .slot_id = SlotId{1}, .pad2 = {}};

  MemoryPlan plan_b = make_manual_plan(slots_b, 2, 3072, 0);
  pool.init(&plan_b);
  auto pv_b = pool.mint_initialized_view();

  assert(pool.is_initialized());
  assert(pool.pool_bytes() == 3072);
  assert(pool.num_slots() == 2);
  // New pool should be different memory (old was freed).
  // Note: can't guarantee this — allocator may reuse. Just check it works.
  (void)old_base;

  // Verify slots work.
  auto* base = static_cast<char*>(pool.pool_base());
  assert(pool.slot_ptr(SlotId{0}, pv_b) == base);
  assert(pool.slot_ptr(SlotId{1}, pv_b) == base + 1024);

  std::printf("  test_reinit: PASSED\n");
}

// ── Test 6: Integration with compute_memory_plan ──
//
// End-to-end: create realistic slots, run the sweep-line allocator,
// then materialize with PoolAllocator and verify write isolation.
static void test_integration_with_sweep_line() {
  crucible::effects::Test test;
  crucible::BackgroundThread bt;

  // Slot 0: birth=0, death=5, 1024B (internal)
  // Slot 1: birth=1, death=3, 2048B (internal)
  // Slot 2: birth=4, death=7, 1024B (internal, should reuse slot 1's space)
  // Slot 3: birth=0, death=7, 512B  (external)
  constexpr uint32_t N = 4;
  TensorSlot slots[N]{};
  slots[0] = {.offset_bytes = 0, .nbytes = 1024,
              .birth_op = OpIndex{0}, .death_op = OpIndex{5},
              .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
              .device_idx = 0, .layout = Layout::Strided,
              .is_external = false, .pad = {}, .slot_id = SlotId{0}, .pad2 = {}};
  slots[1] = {.offset_bytes = 0, .nbytes = 2048,
              .birth_op = OpIndex{1}, .death_op = OpIndex{3},
              .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
              .device_idx = 0, .layout = Layout::Strided,
              .is_external = false, .pad = {}, .slot_id = SlotId{1}, .pad2 = {}};
  slots[2] = {.offset_bytes = 0, .nbytes = 1024,
              .birth_op = OpIndex{4}, .death_op = OpIndex{7},
              .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
              .device_idx = 0, .layout = Layout::Strided,
              .is_external = false, .pad = {}, .slot_id = SlotId{2}, .pad2 = {}};
  slots[3] = {.offset_bytes = 0, .nbytes = 512,
              .birth_op = OpIndex{0}, .death_op = OpIndex{7},
              .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
              .device_idx = 0, .layout = Layout::Strided,
              .is_external = true, .pad = {}, .slot_id = SlotId{3}, .pad2 = {}};

  auto* plan = bt.compute_memory_plan(test.alloc, slots, N);
  assert(plan != nullptr);
  assert(plan->pool_bytes > 0);

  // Materialize the plan.
  PoolAllocator pool;
  pool.init(plan);
  auto pv = pool.mint_initialized_view();

  assert(pool.is_initialized());
  assert(pool.pool_bytes() == plan->pool_bytes);

  // External slot starts null.
  assert(pool.slot_ptr(SlotId{3}, pv) == nullptr);

  // Register external.
  alignas(256) char fake_param[512];
  pool.register_external(SlotId{3}, crucible::safety::NonNull<void*>{fake_param}, pv);
  assert(pool.slot_ptr(SlotId{3}, pv) == fake_param);

  // Verify simultaneously-alive slots don't corrupt each other.
  //
  // Slot 0 (birth=0..5) and Slot 1 (birth=1..3) overlap at ops 1-3.
  // Slot 0 (birth=0..5) and Slot 2 (birth=4..7) overlap at ops 4-5.
  // Slot 1 (birth=1..3) and Slot 2 (birth=4..7) do NOT overlap —
  //   the sweep-line may reuse Slot 1's memory for Slot 2.
  //
  // Test: write to co-alive slots, verify no cross-contamination.

  // Phase 1: ops 1-3 — Slot 0 and Slot 1 both alive.
  auto* p0 = static_cast<uint8_t*>(pool.slot_ptr(SlotId{0}, pv));
  auto* p1 = static_cast<uint8_t*>(pool.slot_ptr(SlotId{1}, pv));
  std::memset(p0, 0x11, 1024);
  std::memset(p1, 0x22, 2048);
  for (uint32_t i = 0; i < 1024; i++) assert(p0[i] == 0x11);
  for (uint32_t i = 0; i < 2048; i++) assert(p1[i] == 0x22);

  // Phase 2: ops 4-5 — Slot 0 and Slot 2 both alive.
  // Slot 1 is dead — writing to Slot 2 may overwrite it (reuse).
  auto* p2 = static_cast<uint8_t*>(pool.slot_ptr(SlotId{2}, pv));
  std::memset(p2, 0x33, 1024);
  // Slot 0 must survive — it's alive alongside Slot 2.
  for (uint32_t i = 0; i < 1024; i++) assert(p0[i] == 0x11);
  for (uint32_t i = 0; i < 1024; i++) assert(p2[i] == 0x33);

  std::printf("  test_integration_with_sweep_line: PASSED\n");
}

int main() {
  std::printf("test_pool_allocator:\n");
  test_basic_init();
  test_external_registration();
  test_write_read_isolation();
  test_all_external();
  test_reinit();
  test_integration_with_sweep_line();
  std::printf("test_pool_allocator: all tests passed\n");
  return 0;
}

#include <crucible/ReplayEngine.h>
#include <crucible/BackgroundThread.h>
#include <crucible/Effects.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

using crucible::ReplayEngine;
using crucible::ReplayStatus;
using crucible::PoolAllocator;
using crucible::MemoryPlan;
using crucible::TensorSlot;
using crucible::TraceEntry;
using crucible::RegionNode;
using crucible::TraceNode;
using crucible::TraceNodeKind;
using crucible::SlotId;
using crucible::OpIndex;
using crucible::ScalarType;
using crucible::DeviceType;
using crucible::Layout;
using crucible::SchemaHash;
using crucible::ShapeHash;

// ── Helpers ──────────────────────────────────────────────────────

// Fill a pre-allocated RegionNode with ops[] and num_ops.
// Only schema_hash, shape_hash, num_outputs, output_slot_ids,
// num_inputs, input_slot_ids are meaningful for ReplayEngine.
static void init_region(RegionNode* r, TraceEntry* ops, uint32_t n) {
  ::new (r) RegionNode{};
  r->kind = TraceNodeKind::REGION;
  r->ops = ops;
  r->num_ops = n;
}

// Build a minimal MemoryPlan + PoolAllocator for slot count.
// All slots are internal with 256B each at sequential offsets.
static MemoryPlan make_simple_plan(TensorSlot* slots, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    slots[i] = {.offset_bytes=i * 256, .nbytes=256,
                 .birth_op=OpIndex{0}, .death_op=OpIndex{n},
                 .dtype=ScalarType::Float, .device_type=DeviceType::CPU,
                 .device_idx=0, .layout=Layout::Strided,
                 .is_external=false, .pad={}, .slot_id=SlotId{i}, .pad2={}};
  }
  MemoryPlan plan{};
  plan.slots = slots;
  plan.num_slots = n;
  plan.num_external = 0;
  plan.pool_bytes = n * 256;
  plan.device_type = DeviceType::CPU;
  plan.device_idx = 0;
  return plan;
}

// ── Test 1: Linear match — all ops match in sequence ──
static void test_linear_match() {
  // 3 ops, each with 1 output.
  SlotId out_slots[3][1] = {{SlotId{0}}, {SlotId{1}}, {SlotId{2}}};

  TraceEntry ops[3]{};
  for (uint32_t i = 0; i < 3; i++) {
    ops[i].schema_hash = SchemaHash{100 + i};
    ops[i].shape_hash = ShapeHash{200 + i};
    ops[i].num_outputs = 1;
    ops[i].output_slot_ids = out_slots[i];
    ops[i].num_inputs = 0;
    ops[i].input_slot_ids = nullptr;
  }

  RegionNode region{};
  init_region(&region, ops, 3);

  // Build pool with 3 slots.
  TensorSlot slots[3];
  auto plan = make_simple_plan(slots, 3);
  PoolAllocator pool;
  pool.init(&plan);

  ReplayEngine engine;
  engine.init(&region, &pool);

  assert(engine.is_initialized());
  assert(engine.num_ops() == 3);
  assert(engine.ops_matched() == 0);
  assert(!engine.is_complete());

  // Advance through first 2 ops (non-last → MATCH).
  for (uint32_t i = 0; i < 2; i++) {
    auto status = engine.advance(SchemaHash{100 + i}, ShapeHash{200 + i});
    assert(status == ReplayStatus::MATCH);
    assert(engine.ops_matched() == i + 1);
    assert(engine.matched_op_index() == OpIndex{i});

    void* ptr = engine.output_ptr(0);
    assert(ptr == pool.slot_ptr(SlotId{i}));
  }

  // Last op (i=2) returns COMPLETE directly.
  {
    auto status = engine.advance(SchemaHash{102}, ShapeHash{202});
    assert(status == ReplayStatus::COMPLETE);
    assert(engine.ops_matched() == 3);
    assert(engine.matched_op_index() == OpIndex{2});
    void* ptr = engine.output_ptr(0);
    assert(ptr == pool.slot_ptr(SlotId{2}));
  }
  assert(engine.is_complete());

  std::printf("  test_linear_match: PASSED\n");
}

// ── Test 2: Schema divergence ──
static void test_schema_divergence() {
  SlotId out_slot[1] = {SlotId{0}};

  TraceEntry ops[3]{};
  for (uint32_t i = 0; i < 3; i++) {
    ops[i].schema_hash = SchemaHash{100 + i};
    ops[i].shape_hash = ShapeHash{200};
    ops[i].num_outputs = 1;
    ops[i].output_slot_ids = out_slot;
  }

  RegionNode region{};
  init_region(&region, ops, 3);

  TensorSlot slots[1];
  auto plan = make_simple_plan(slots, 1);
  PoolAllocator pool;
  pool.init(&plan);

  ReplayEngine engine;
  engine.init(&region, &pool);

  // Op 0: matches.
  assert(engine.advance(SchemaHash{100}, ShapeHash{200}) == ReplayStatus::MATCH);

  // Op 1: wrong schema_hash (expected 101, got 999).
  assert(engine.advance(SchemaHash{999}, ShapeHash{200}) == ReplayStatus::DIVERGED);
  assert(engine.diverged_op_index() == OpIndex{1});
  assert(engine.ops_matched() == 1);

  // Position stays at the diverged op — subsequent advance still diverges.
  assert(engine.advance(SchemaHash{999}, ShapeHash{200}) == ReplayStatus::DIVERGED);
  assert(engine.diverged_op_index() == OpIndex{1});

  std::printf("  test_schema_divergence: PASSED\n");
}

// ── Test 3: Shape divergence ──
static void test_shape_divergence() {
  SlotId out_slot[1] = {SlotId{0}};

  TraceEntry ops[2]{};
  ops[0].schema_hash = SchemaHash{100};
  ops[0].shape_hash = ShapeHash{200};
  ops[0].num_outputs = 1;
  ops[0].output_slot_ids = out_slot;
  ops[1].schema_hash = SchemaHash{101};
  ops[1].shape_hash = ShapeHash{201};
  ops[1].num_outputs = 1;
  ops[1].output_slot_ids = out_slot;

  RegionNode region{};
  init_region(&region, ops, 2);

  TensorSlot slots[1];
  auto plan = make_simple_plan(slots, 1);
  PoolAllocator pool;
  pool.init(&plan);

  ReplayEngine engine;
  engine.init(&region, &pool);

  // Op 0: matches.
  assert(engine.advance(SchemaHash{100}, ShapeHash{200}) == ReplayStatus::MATCH);

  // Op 1: correct schema, wrong shape.
  assert(engine.advance(SchemaHash{101}, ShapeHash{999}) == ReplayStatus::DIVERGED);
  assert(engine.diverged_op_index() == OpIndex{1});

  std::printf("  test_shape_divergence: PASSED\n");
}

// ── Test 4: Reset and re-walk ──
static void test_reset() {
  SlotId out_slot[1] = {SlotId{0}};

  TraceEntry ops[2]{};
  ops[0].schema_hash = SchemaHash{10};
  ops[0].shape_hash = ShapeHash{20};
  ops[0].num_outputs = 1;
  ops[0].output_slot_ids = out_slot;
  ops[1].schema_hash = SchemaHash{11};
  ops[1].shape_hash = ShapeHash{21};
  ops[1].num_outputs = 1;
  ops[1].output_slot_ids = out_slot;

  RegionNode region{};
  init_region(&region, ops, 2);

  TensorSlot slots[1];
  auto plan = make_simple_plan(slots, 1);
  PoolAllocator pool;
  pool.init(&plan);

  ReplayEngine engine;
  engine.init(&region, &pool);

  // Full walk: first op MATCH, last op COMPLETE.
  assert(engine.advance(SchemaHash{10}, ShapeHash{20}) == ReplayStatus::MATCH);
  assert(engine.advance(SchemaHash{11}, ShapeHash{21}) == ReplayStatus::COMPLETE);
  assert(engine.is_complete());

  // Reset and walk again.
  engine.reset();
  assert(!engine.is_complete());
  assert(engine.ops_matched() == 0);

  assert(engine.advance(SchemaHash{10}, ShapeHash{20}) == ReplayStatus::MATCH);
  assert(engine.advance(SchemaHash{11}, ShapeHash{21}) == ReplayStatus::COMPLETE);
  assert(engine.is_complete());

  std::printf("  test_reset: PASSED\n");
}

// ── Test 5: Input pointer resolution ──
static void test_input_ptr() {
  // Op 0: produces output in slot 0.
  // Op 1: reads input from slot 0, produces output in slot 1.
  SlotId out0[1] = {SlotId{0}};
  SlotId out1[1] = {SlotId{1}};
  SlotId in1[1]  = {SlotId{0}};

  TraceEntry ops[2]{};
  ops[0].schema_hash = SchemaHash{50};
  ops[0].shape_hash = ShapeHash{60};
  ops[0].num_outputs = 1;
  ops[0].output_slot_ids = out0;
  ops[0].num_inputs = 0;
  ops[0].input_slot_ids = nullptr;

  ops[1].schema_hash = SchemaHash{51};
  ops[1].shape_hash = ShapeHash{61};
  ops[1].num_outputs = 1;
  ops[1].output_slot_ids = out1;
  ops[1].num_inputs = 1;
  ops[1].input_slot_ids = in1;

  RegionNode region{};
  init_region(&region, ops, 2);

  TensorSlot slots[2];
  auto plan = make_simple_plan(slots, 2);
  PoolAllocator pool;
  pool.init(&plan);

  ReplayEngine engine;
  engine.init(&region, &pool);

  // Advance op 0 (MATCH — not last).
  assert(engine.advance(SchemaHash{50}, ShapeHash{60}) == ReplayStatus::MATCH);
  assert(engine.output_ptr(0) == pool.slot_ptr(SlotId{0}));

  // Advance op 1 (COMPLETE — last op).
  assert(engine.advance(SchemaHash{51}, ShapeHash{61}) == ReplayStatus::COMPLETE);
  assert(engine.output_ptr(0) == pool.slot_ptr(SlotId{1}));
  assert(engine.input_ptr(0) == pool.slot_ptr(SlotId{0}));

  // The input of op 1 points to the same pool memory as the output of op 0.
  assert(engine.input_ptr(0) == pool.slot_ptr(SlotId{0}));

  std::printf("  test_input_ptr: PASSED\n");
}

// ── Test 6: Invalid slot (SlotId::none()) returns nullptr ──
static void test_invalid_slot() {
  SlotId out[2] = {SlotId{0}, SlotId::none()};

  TraceEntry ops[1]{};
  ops[0].schema_hash = SchemaHash{77};
  ops[0].shape_hash = ShapeHash{88};
  ops[0].num_outputs = 2;
  ops[0].output_slot_ids = out;

  RegionNode region{};
  init_region(&region, ops, 1);

  TensorSlot slots[1];
  auto plan = make_simple_plan(slots, 1);
  PoolAllocator pool;
  pool.init(&plan);

  ReplayEngine engine;
  engine.init(&region, &pool);

  // Single-op region: last (only) op returns COMPLETE.
  assert(engine.advance(SchemaHash{77}, ShapeHash{88}) == ReplayStatus::COMPLETE);

  // Valid slot returns pool pointer.
  assert(engine.output_ptr(0) != nullptr);
  // Invalid slot (none) returns nullptr.
  assert(engine.output_ptr(1) == nullptr);

  std::printf("  test_invalid_slot: PASSED\n");
}

// ── Test 7: current_entry() returns correct TraceEntry ──
static void test_current_entry() {
  SlotId out[1] = {SlotId{0}};

  TraceEntry ops[3]{};
  for (uint32_t i = 0; i < 3; i++) {
    ops[i].schema_hash = SchemaHash{300 + i};
    ops[i].shape_hash = ShapeHash{400 + i};
    ops[i].num_outputs = 1;
    ops[i].output_slot_ids = out;
  }

  RegionNode region{};
  init_region(&region, ops, 3);

  TensorSlot slots[1];
  auto plan = make_simple_plan(slots, 1);
  PoolAllocator pool;
  pool.init(&plan);

  ReplayEngine engine;
  engine.init(&region, &pool);

  for (uint32_t i = 0; i < 3; i++) {
    auto s = engine.advance(SchemaHash{300 + i}, ShapeHash{400 + i});
    // Last op (i==2) returns COMPLETE, others MATCH.
    assert(s == (i < 2 ? ReplayStatus::MATCH : ReplayStatus::COMPLETE));
    const auto& entry = engine.current_entry();
    assert(entry.schema_hash == SchemaHash{300 + i});
    assert(entry.shape_hash == ShapeHash{400 + i});
  }

  std::printf("  test_current_entry: PASSED\n");
}

// ── Test 8: Integration — sweep-line plan + replay ──
//
// End-to-end: create realistic slots, run sweep-line, materialize
// pool, build ops with slot assignments, replay and verify pointers.
static void test_integration_with_pool() {
  crucible::fx::Test test;
  crucible::BackgroundThread bt;

  // 3 internal slots:
  //   Slot 0: birth=0, death=2, 512B
  //   Slot 1: birth=1, death=2, 256B
  //   Slot 2: birth=0, death=2, 128B (external — param)
  constexpr uint32_t NSLOTS = 3;
  TensorSlot slots[NSLOTS]{};
  slots[0] = {.offset_bytes=0, .nbytes=512,
               .birth_op=OpIndex{0}, .death_op=OpIndex{2},
               .dtype=ScalarType::Float, .device_type=DeviceType::CPU,
               .device_idx=0, .layout=Layout::Strided,
               .is_external=false, .pad={}, .slot_id=SlotId{0}, .pad2={}};
  slots[1] = {.offset_bytes=0, .nbytes=256,
               .birth_op=OpIndex{1}, .death_op=OpIndex{2},
               .dtype=ScalarType::Float, .device_type=DeviceType::CPU,
               .device_idx=0, .layout=Layout::Strided,
               .is_external=false, .pad={}, .slot_id=SlotId{1}, .pad2={}};
  slots[2] = {.offset_bytes=0, .nbytes=128,
               .birth_op=OpIndex{0}, .death_op=OpIndex{2},
               .dtype=ScalarType::Float, .device_type=DeviceType::CPU,
               .device_idx=0, .layout=Layout::Strided,
               .is_external=true, .pad={}, .slot_id=SlotId{2}, .pad2={}};

  auto* plan = bt.compute_memory_plan(test.alloc, slots, NSLOTS);
  assert(plan != nullptr);

  PoolAllocator pool;
  pool.init(plan);

  // Register external.
  alignas(256) char fake_param[128];
  pool.register_external(SlotId{2}, crucible::safety::NonNull<void*>{fake_param});

  // Build 2 ops:
  //   Op 0: produces slot 0, reads external slot 2
  //   Op 1: produces slot 1, reads slot 0
  SlotId op0_out[1] = {SlotId{0}};
  SlotId op0_in[1]  = {SlotId{2}};
  SlotId op1_out[1] = {SlotId{1}};
  SlotId op1_in[1]  = {SlotId{0}};

  TraceEntry ops[2]{};
  ops[0].schema_hash = SchemaHash{0xAAAA};
  ops[0].shape_hash  = ShapeHash{0xBBBB};
  ops[0].num_outputs = 1;
  ops[0].output_slot_ids = op0_out;
  ops[0].num_inputs = 1;
  ops[0].input_slot_ids = op0_in;

  ops[1].schema_hash = SchemaHash{0xCCCC};
  ops[1].shape_hash  = ShapeHash{0xDDDD};
  ops[1].num_outputs = 1;
  ops[1].output_slot_ids = op1_out;
  ops[1].num_inputs = 1;
  ops[1].input_slot_ids = op1_in;

  RegionNode region{};
  init_region(&region, ops, 2);

  ReplayEngine engine;
  engine.init(&region, &pool);

  // Replay op 0 (MATCH — not last).
  assert(engine.advance(SchemaHash{0xAAAA}, ShapeHash{0xBBBB}) == ReplayStatus::MATCH);
  assert(engine.output_ptr(0) == pool.slot_ptr(SlotId{0}));
  assert(engine.input_ptr(0) == fake_param);  // external

  // Write to op 0's output.
  std::memset(engine.output_ptr(0), 0x11, 512);

  // Replay op 1 (COMPLETE — last op).
  assert(engine.advance(SchemaHash{0xCCCC}, ShapeHash{0xDDDD}) == ReplayStatus::COMPLETE);
  assert(engine.output_ptr(0) == pool.slot_ptr(SlotId{1}));
  assert(engine.input_ptr(0) == pool.slot_ptr(SlotId{0}));

  // Op 1's input is op 0's output — verify data survived.
  auto* p = static_cast<uint8_t*>(engine.input_ptr(0));
  for (uint32_t i = 0; i < 512; i++) assert(p[i] == 0x11);

  assert(engine.is_complete());

  // Reset and replay again.
  engine.reset();
  assert(!engine.is_complete());
  assert(engine.advance(SchemaHash{0xAAAA}, ShapeHash{0xBBBB}) == ReplayStatus::MATCH);
  assert(engine.advance(SchemaHash{0xCCCC}, ShapeHash{0xDDDD}) == ReplayStatus::COMPLETE);
  assert(engine.is_complete());

  std::printf("  test_integration_with_pool: PASSED\n");
}

int main() {
  std::printf("test_replay_engine:\n");
  test_linear_match();
  test_schema_divergence();
  test_shape_divergence();
  test_reset();
  test_input_ptr();
  test_invalid_slot();
  test_current_entry();
  test_integration_with_pool();
  std::printf("test_replay_engine: all tests passed\n");
  return 0;
}

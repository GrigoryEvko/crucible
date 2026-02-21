#include <crucible/CrucibleContext.h>
#include <crucible/BackgroundThread.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

using crucible::CrucibleContext;
using crucible::ContextMode;
using crucible::ReplayStatus;
using crucible::RegionNode;
using crucible::TraceNodeKind;
using crucible::TraceEntry;
using crucible::TensorSlot;
using crucible::MemoryPlan;
using crucible::SlotId;
using crucible::OpIndex;
using crucible::ScalarType;
using crucible::DeviceType;
using crucible::Layout;
using crucible::SchemaHash;
using crucible::ShapeHash;

// ── Helpers ──────────────────────────────────────────────────────

static void init_region(RegionNode* r, TraceEntry* ops, uint32_t n,
                        MemoryPlan* plan) {
  std::memset(r, 0, sizeof(*r));
  r->kind = TraceNodeKind::REGION;
  r->ops = ops;
  r->num_ops = n;
  r->plan = plan;
}

static MemoryPlan make_simple_plan(TensorSlot* slots, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    slots[i] = {i * 256, 256, OpIndex{0}, OpIndex{n},
                 ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, false, {}, SlotId{i}, {}};
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

// ── Test 1: Starts in RECORD mode ──
static void test_initial_state() {
  CrucibleContext ctx;

  assert(ctx.mode() == ContextMode::RECORD);
  assert(ctx.is_recording());
  assert(!ctx.is_compiled());
  assert(ctx.compiled_iterations() == 0);
  assert(ctx.diverged_count() == 0);
  assert(ctx.active_region() == nullptr);

  std::printf("  test_initial_state: PASSED\n");
}

// ── Test 2: Activate with valid region → COMPILED ──
static void test_activate() {
  SlotId out[1] = {SlotId{0}};

  TraceEntry ops[2]{};
  ops[0].schema_hash = SchemaHash{100};
  ops[0].shape_hash = ShapeHash{200};
  ops[0].num_outputs = 1;
  ops[0].output_slot_ids = out;
  ops[1].schema_hash = SchemaHash{101};
  ops[1].shape_hash = ShapeHash{201};
  ops[1].num_outputs = 1;
  ops[1].output_slot_ids = out;

  TensorSlot slots[1];
  auto plan = make_simple_plan(slots, 1);

  RegionNode region{};
  init_region(&region, ops, 2, &plan);

  CrucibleContext ctx;
  assert(ctx.activate(&region));

  assert(ctx.mode() == ContextMode::COMPILED);
  assert(ctx.is_compiled());
  assert(ctx.active_region() == &region);
  assert(ctx.pool().is_initialized());
  assert(ctx.engine().is_initialized());

  std::printf("  test_activate: PASSED\n");
}

// ── Test 3: Activate fails if region has no plan ──
static void test_activate_no_plan() {
  TraceEntry ops[1]{};

  RegionNode region{};
  init_region(&region, ops, 1, nullptr);

  CrucibleContext ctx;
  assert(!ctx.activate(&region));
  assert(ctx.is_recording());

  std::printf("  test_activate_no_plan: PASSED\n");
}

// ── Test 4: Full replay cycle — MATCH × N → COMPLETE ──
static void test_full_replay() {
  SlotId out0[1] = {SlotId{0}};
  SlotId out1[1] = {SlotId{1}};

  TraceEntry ops[2]{};
  ops[0].schema_hash = SchemaHash{100};
  ops[0].shape_hash = ShapeHash{200};
  ops[0].num_outputs = 1;
  ops[0].output_slot_ids = out0;
  ops[0].num_inputs = 0;

  ops[1].schema_hash = SchemaHash{101};
  ops[1].shape_hash = ShapeHash{201};
  ops[1].num_outputs = 1;
  ops[1].output_slot_ids = out1;
  ops[1].num_inputs = 0;

  TensorSlot slots[2];
  auto plan = make_simple_plan(slots, 2);

  RegionNode region{};
  init_region(&region, ops, 2, &plan);

  CrucibleContext ctx;
  assert(ctx.activate(&region));

  // First iteration.
  assert(ctx.advance(SchemaHash{100}, ShapeHash{200}) == ReplayStatus::MATCH);
  void* p0 = ctx.output_ptr(0);
  assert(p0 == ctx.pool().slot_ptr(SlotId{0}));

  assert(ctx.advance(SchemaHash{101}, ShapeHash{201}) == ReplayStatus::COMPLETE);
  assert(ctx.compiled_iterations() == 1);

  // output_ptr() is valid after COMPLETE (lazy reset — engine not
  // yet reset, so the final op's pointer is still accessible).
  void* p1 = ctx.output_ptr(0);
  assert(p1 == ctx.pool().slot_ptr(SlotId{1}));

  // COMPLETE — next advance() auto-resets for second iteration.
  assert(ctx.is_compiled());  // still compiled

  assert(ctx.advance(SchemaHash{100}, ShapeHash{200}) == ReplayStatus::MATCH);
  assert(ctx.advance(SchemaHash{101}, ShapeHash{201}) == ReplayStatus::COMPLETE);
  assert(ctx.compiled_iterations() == 2);

  std::printf("  test_full_replay: PASSED\n");
}

// ── Test 5: Divergence detection ──
static void test_divergence() {
  SlotId out[1] = {SlotId{0}};

  TraceEntry ops[3]{};
  for (uint32_t i = 0; i < 3; i++) {
    ops[i].schema_hash = SchemaHash{100 + i};
    ops[i].shape_hash = ShapeHash{200 + i};
    ops[i].num_outputs = 1;
    ops[i].output_slot_ids = out;
  }

  TensorSlot slots[1];
  auto plan = make_simple_plan(slots, 1);

  RegionNode region{};
  init_region(&region, ops, 3, &plan);

  CrucibleContext ctx;
  assert(ctx.activate(&region));

  // Op 0: matches.
  assert(ctx.advance(SchemaHash{100}, ShapeHash{200}) == ReplayStatus::MATCH);

  // Op 1: wrong schema.
  assert(ctx.advance(SchemaHash{999}, ShapeHash{201}) == ReplayStatus::DIVERGED);
  assert(ctx.diverged_count() == 1);
  assert(ctx.is_compiled());  // mode unchanged — caller decides

  // Caller deactivates.
  ctx.deactivate();
  assert(ctx.is_recording());
  assert(ctx.active_region() == nullptr);

  std::printf("  test_divergence: PASSED\n");
}

// ── Test 6: Deactivate and re-activate with different region ──
static void test_reactivate() {
  SlotId out[1] = {SlotId{0}};

  // Region A: 1 op.
  TraceEntry ops_a[1]{};
  ops_a[0].schema_hash = SchemaHash{10};
  ops_a[0].shape_hash = ShapeHash{20};
  ops_a[0].num_outputs = 1;
  ops_a[0].output_slot_ids = out;

  TensorSlot slots_a[1];
  auto plan_a = make_simple_plan(slots_a, 1);

  RegionNode region_a{};
  init_region(&region_a, ops_a, 1, &plan_a);

  // Region B: 2 ops with different hashes.
  SlotId out_b[2] = {SlotId{0}, SlotId{1}};

  TraceEntry ops_b[2]{};
  ops_b[0].schema_hash = SchemaHash{30};
  ops_b[0].shape_hash = ShapeHash{40};
  ops_b[0].num_outputs = 1;
  ops_b[0].output_slot_ids = &out_b[0];
  ops_b[1].schema_hash = SchemaHash{31};
  ops_b[1].shape_hash = ShapeHash{41};
  ops_b[1].num_outputs = 1;
  ops_b[1].output_slot_ids = &out_b[1];

  TensorSlot slots_b[2];
  auto plan_b = make_simple_plan(slots_b, 2);

  RegionNode region_b{};
  init_region(&region_b, ops_b, 2, &plan_b);

  CrucibleContext ctx;

  // Activate region A, run one iteration.
  assert(ctx.activate(&region_a));
  assert(ctx.advance(SchemaHash{10}, ShapeHash{20}) == ReplayStatus::COMPLETE);
  assert(ctx.compiled_iterations() == 1);

  // Re-activate with region B (implicitly deactivates A).
  assert(ctx.activate(&region_b));
  assert(ctx.active_region() == &region_b);
  assert(ctx.advance(SchemaHash{30}, ShapeHash{40}) == ReplayStatus::MATCH);
  assert(ctx.advance(SchemaHash{31}, ShapeHash{41}) == ReplayStatus::COMPLETE);
  assert(ctx.compiled_iterations() == 2);

  std::printf("  test_reactivate: PASSED\n");
}

// ── Test 7: External slot registration ──
static void test_external_slots() {
  SlotId out[1] = {SlotId{0}};
  SlotId in[1] = {SlotId{1}};

  TraceEntry ops[1]{};
  ops[0].schema_hash = SchemaHash{50};
  ops[0].shape_hash = ShapeHash{60};
  ops[0].num_outputs = 1;
  ops[0].output_slot_ids = out;
  ops[0].num_inputs = 1;
  ops[0].input_slot_ids = in;

  // Slot 0: internal, slot 1: external.
  TensorSlot slots[2];
  std::memset(slots, 0, sizeof(slots));
  slots[0] = {0, 256, OpIndex{0}, OpIndex{1},
               ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, false, {}, SlotId{0}, {}};
  slots[1] = {0, 128, OpIndex{0}, OpIndex{1},
               ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, true, {}, SlotId{1}, {}};

  MemoryPlan plan{};
  plan.slots = slots;
  plan.num_slots = 2;
  plan.num_external = 1;
  plan.pool_bytes = 256;
  plan.device_type = DeviceType::CPU;
  plan.device_idx = 0;

  RegionNode region{};
  init_region(&region, ops, 1, &plan);

  CrucibleContext ctx;
  assert(ctx.activate(&region));

  // Register external param.
  alignas(256) char fake_param[128];
  ctx.register_external(SlotId{1}, fake_param);

  // Advance — input points to registered external.
  assert(ctx.advance(SchemaHash{50}, ShapeHash{60}) == ReplayStatus::COMPLETE);
  // Can't call input_ptr after COMPLETE (engine auto-reset), so test
  // via pool directly.
  assert(ctx.pool().slot_ptr(SlotId{1}) == fake_param);

  std::printf("  test_external_slots: PASSED\n");
}

// ── Test 8: Integration with sweep-line ──
static void test_integration_sweep_line() {
  crucible::BackgroundThread bt;

  // 2 internal slots, 1 external.
  constexpr uint32_t NSLOTS = 3;
  TensorSlot slots[NSLOTS];
  std::memset(slots, 0, sizeof(slots));
  slots[0] = {0, 512, OpIndex{0}, OpIndex{2},
               ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, false, {}, SlotId{0}, {}};
  slots[1] = {0, 256, OpIndex{1}, OpIndex{2},
               ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, false, {}, SlotId{1}, {}};
  slots[2] = {0, 128, OpIndex{0}, OpIndex{2},
               ScalarType::Float, DeviceType::CPU, 0, Layout::Strided, true,  {}, SlotId{2}, {}};

  auto* plan = bt.compute_memory_plan(slots, NSLOTS);
  assert(plan != nullptr);

  // Build ops with slot assignments.
  SlotId op0_out[1] = {SlotId{0}};
  SlotId op0_in[1]  = {SlotId{2}};
  SlotId op1_out[1] = {SlotId{1}};
  SlotId op1_in[1]  = {SlotId{0}};

  TraceEntry ops[2]{};
  ops[0].schema_hash = SchemaHash{0xAA};
  ops[0].shape_hash  = ShapeHash{0xBB};
  ops[0].num_outputs = 1;
  ops[0].output_slot_ids = op0_out;
  ops[0].num_inputs = 1;
  ops[0].input_slot_ids = op0_in;

  ops[1].schema_hash = SchemaHash{0xCC};
  ops[1].shape_hash  = ShapeHash{0xDD};
  ops[1].num_outputs = 1;
  ops[1].output_slot_ids = op1_out;
  ops[1].num_inputs = 1;
  ops[1].input_slot_ids = op1_in;

  RegionNode region{};
  init_region(&region, ops, 2, plan);

  CrucibleContext ctx;
  assert(ctx.activate(&region));

  // Register external.
  alignas(256) char fake_param[128];
  std::memset(fake_param, 0xEE, 128);
  ctx.register_external(SlotId{2}, fake_param);

  // Replay iteration 1.
  assert(ctx.advance(SchemaHash{0xAA}, ShapeHash{0xBB}) == ReplayStatus::MATCH);
  // Op 0: output → slot 0, input → slot 2 (external).
  assert(ctx.output_ptr(0) == ctx.pool().slot_ptr(SlotId{0}));
  assert(ctx.input_ptr(0) == fake_param);

  // Write to output.
  std::memset(ctx.output_ptr(0), 0x11, 512);

  assert(ctx.advance(SchemaHash{0xCC}, ShapeHash{0xDD}) == ReplayStatus::COMPLETE);
  assert(ctx.compiled_iterations() == 1);

  // Replay iteration 2 (auto-reset happened).
  assert(ctx.advance(SchemaHash{0xAA}, ShapeHash{0xBB}) == ReplayStatus::MATCH);
  // Op 1's input (slot 0) still has data from iteration 1.
  // (In Tier 1, the op executes eagerly and overwrites, but the
  // pointer is still valid from the previous write.)
  auto* p = static_cast<uint8_t*>(ctx.output_ptr(0));
  assert(p == ctx.pool().slot_ptr(SlotId{0}));

  assert(ctx.advance(SchemaHash{0xCC}, ShapeHash{0xDD}) == ReplayStatus::COMPLETE);
  assert(ctx.compiled_iterations() == 2);

  // Diverge on iteration 3.
  assert(ctx.advance(SchemaHash{0xAA}, ShapeHash{0xBB}) == ReplayStatus::MATCH);
  assert(ctx.advance(SchemaHash{0xFF}, ShapeHash{0xFF}) == ReplayStatus::DIVERGED);
  assert(ctx.diverged_count() == 1);

  // Deactivate.
  ctx.deactivate();
  assert(ctx.is_recording());

  std::printf("  test_integration_sweep_line: PASSED\n");
}

// ── Test 9: Multiple divergences accumulate ──
static void test_divergence_counter() {
  SlotId out[1] = {SlotId{0}};

  TraceEntry ops[2]{};
  ops[0].schema_hash = SchemaHash{10};
  ops[0].shape_hash = ShapeHash{20};
  ops[0].num_outputs = 1;
  ops[0].output_slot_ids = out;
  ops[1].schema_hash = SchemaHash{11};
  ops[1].shape_hash = ShapeHash{21};
  ops[1].num_outputs = 1;
  ops[1].output_slot_ids = out;

  TensorSlot slots[1];
  auto plan = make_simple_plan(slots, 1);

  RegionNode region{};
  init_region(&region, ops, 2, &plan);

  CrucibleContext ctx;

  // Cycle 1: activate → diverge → deactivate.
  assert(ctx.activate(&region));
  assert(ctx.advance(SchemaHash{10}, ShapeHash{20}) == ReplayStatus::MATCH);
  assert(ctx.advance(SchemaHash{99}, ShapeHash{99}) == ReplayStatus::DIVERGED);
  assert(ctx.diverged_count() == 1);
  ctx.deactivate();

  // Cycle 2: activate → diverge → deactivate.
  assert(ctx.activate(&region));
  assert(ctx.advance(SchemaHash{99}, ShapeHash{20}) == ReplayStatus::DIVERGED);
  assert(ctx.diverged_count() == 2);
  ctx.deactivate();

  // Cycle 3: activate → full iteration → no diverge.
  assert(ctx.activate(&region));
  assert(ctx.advance(SchemaHash{10}, ShapeHash{20}) == ReplayStatus::MATCH);
  assert(ctx.advance(SchemaHash{11}, ShapeHash{21}) == ReplayStatus::COMPLETE);
  assert(ctx.diverged_count() == 2);  // unchanged
  assert(ctx.compiled_iterations() == 1);

  std::printf("  test_divergence_counter: PASSED\n");
}

int main() {
  std::printf("test_crucible_context:\n");
  test_initial_state();
  test_activate();
  test_activate_no_plan();
  test_full_replay();
  test_divergence();
  test_reactivate();
  test_external_slots();
  test_integration_sweep_line();
  test_divergence_counter();
  std::printf("test_crucible_context: all tests passed\n");
  return 0;
}

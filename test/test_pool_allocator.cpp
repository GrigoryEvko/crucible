#include <crucible/PoolAllocator.h>
#include <crucible/BackgroundThread.h>
#include <crucible/effects/Capabilities.h>
#include "test_assert.h"
#include <bit>
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

// The offsets here are written by hand rather than produced by the
// planner, so these tests exercise the allocator on its own.
static MemoryPlan make_manual_plan(TensorSlot* slots, uint32_t n, uint64_t pool_bytes, uint32_t num_ext) {
    MemoryPlan plan{};
    plan.slots = slots;
    plan.num_slots = n;
    plan.num_external = num_ext;
    plan.pool_bytes = pool_bytes;
    plan.device_type = DeviceType::CPU;
    plan.device_idx = 0;
    return plan;
}

static void test_basic_init() {
    TensorSlot slots[3]{};
    slots[0] = {.offset_bytes = 0,
                .nbytes = 1024,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{5},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{0},
                .pad2 = {}};
    slots[1] = {.offset_bytes = 1024,
                .nbytes = 2048,
                .birth_op = OpIndex{1},
                .death_op = OpIndex{4},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{1},
                .pad2 = {}};
    slots[2] = {.offset_bytes = 0,
                .nbytes = 512,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{7},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = true,
                .pad = {},
                .slot_id = SlotId{2},
                .pad2 = {}};

    MemoryPlan plan = make_manual_plan(slots, 3, 3072, 1);

    PoolAllocator pool;
    pool.init(&plan);
    // The view is checked once, here.  Every later call that takes it
    // treats it as proof and re-checks nothing.
    auto pv = pool.mint_initialized_view();

    assert(pool.is_initialized());
    assert(pool.pool_base() != nullptr);
    assert(std::bit_cast<uintptr_t>(pool.pool_base()) % PoolAllocator::ALIGNMENT == 0);
    assert(pool.pool_bytes() == 3072);
    assert(pool.num_slots() == 3);
    assert(pool.num_external() == 1);

    auto* base = static_cast<char*>(pool.pool_base());
    assert(pool.slot_ptr(SlotId{0}, pv) == base + 0);
    assert(pool.slot_ptr(SlotId{1}, pv) == base + 1024);

    // The base is aligned and both offsets are multiples of the
    // alignment, so every internal slot inherits the alignment.
    assert(std::bit_cast<uintptr_t>(pool.slot_ptr(SlotId{0}, pv)) % 256 == 0);
    assert(std::bit_cast<uintptr_t>(pool.slot_ptr(SlotId{1}, pv)) % 256 == 0);

    // An external slot has no place in the pool, so it reads null until
    // someone registers storage for it.
    assert(pool.slot_ptr(SlotId{2}, pv) == nullptr);

    std::printf("  test_basic_init: PASSED\n");
}

static void test_external_registration() {
    TensorSlot slots[2]{};
    slots[0] = {.offset_bytes = 0,
                .nbytes = 1024,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{3},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{0},
                .pad2 = {}};
    slots[1] = {.offset_bytes = 0,
                .nbytes = 512,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{3},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = true,
                .pad = {},
                .slot_id = SlotId{1},
                .pad2 = {}};

    MemoryPlan plan = make_manual_plan(slots, 2, 1024, 1);

    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view();

    assert(pool.slot_ptr(SlotId{1}, pv) == nullptr);

    // This buffer stands in for a parameter tensor the pool does not own.
    alignas(256) char fake_param[512];
    pool.register_external(SlotId{1}, crucible::safety::NonNull<void*>{fake_param}, pv);

    assert(pool.slot_ptr(SlotId{1}, pv) == fake_param);

    std::printf("  test_external_registration: PASSED\n");
}

// The three offsets are laid out end to end with no overlap, which is
// the precondition the isolation check below rests on.
static void test_write_read_isolation() {
    TensorSlot slots[3]{};
    slots[0] = {.offset_bytes = 0,
                .nbytes = 256,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{3},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{0},
                .pad2 = {}};
    slots[1] = {.offset_bytes = 256,
                .nbytes = 512,
                .birth_op = OpIndex{1},
                .death_op = OpIndex{4},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{1},
                .pad2 = {}};
    slots[2] = {.offset_bytes = 768,
                .nbytes = 256,
                .birth_op = OpIndex{2},
                .death_op = OpIndex{5},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{2},
                .pad2 = {}};

    MemoryPlan plan = make_manual_plan(slots, 3, 1024, 0);

    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view();

    std::memset(pool.slot_ptr(SlotId{0}, pv), 0xAA, 256);
    std::memset(pool.slot_ptr(SlotId{1}, pv), 0xBB, 512);
    std::memset(pool.slot_ptr(SlotId{2}, pv), 0xCC, 256);

    auto* p0 = static_cast<uint8_t*>(pool.slot_ptr(SlotId{0}, pv));
    auto* p1 = static_cast<uint8_t*>(pool.slot_ptr(SlotId{1}, pv));
    auto* p2 = static_cast<uint8_t*>(pool.slot_ptr(SlotId{2}, pv));

    for (uint32_t i = 0; i < 256; i++)
        assert(p0[i] == 0xAA);
    for (uint32_t i = 0; i < 512; i++)
        assert(p1[i] == 0xBB);
    for (uint32_t i = 0; i < 256; i++)
        assert(p2[i] == 0xCC);

    std::printf("  test_write_read_isolation: PASSED\n");
}

// A plan whose slots are all external asks for no pool at all, so the
// allocator must report itself initialized while holding no memory.
static void test_all_external() {
    TensorSlot slots[2]{};
    slots[0] = {.offset_bytes = 0,
                .nbytes = 1024,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{3},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = true,
                .pad = {},
                .slot_id = SlotId{0},
                .pad2 = {}};
    slots[1] = {.offset_bytes = 0,
                .nbytes = 2048,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{5},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = true,
                .pad = {},
                .slot_id = SlotId{1},
                .pad2 = {}};

    MemoryPlan plan = make_manual_plan(slots, 2, 0, 2);

    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view();

    assert(pool.is_initialized());
    assert(pool.pool_base() == nullptr);
    assert(pool.pool_bytes() == 0);
    assert(pool.num_slots() == 2);

    assert(pool.slot_ptr(SlotId{0}, pv) == nullptr);
    assert(pool.slot_ptr(SlotId{1}, pv) == nullptr);

    alignas(256) char buf_a[1024];
    alignas(256) char buf_b[2048];
    pool.register_external(SlotId{0}, crucible::safety::NonNull<void*>{buf_a}, pv);
    pool.register_external(SlotId{1}, crucible::safety::NonNull<void*>{buf_b}, pv);
    assert(pool.slot_ptr(SlotId{0}, pv) == buf_a);
    assert(pool.slot_ptr(SlotId{1}, pv) == buf_b);

    std::printf("  test_all_external: PASSED\n");
}

static void test_reinit() {
    TensorSlot slots_a[1]{};
    slots_a[0] = {.offset_bytes = 0,
                  .nbytes = 512,
                  .birth_op = OpIndex{0},
                  .death_op = OpIndex{3},
                  .dtype = ScalarType::Float,
                  .device_type = DeviceType::CPU,
                  .device_idx = 0,
                  .layout = Layout::Strided,
                  .is_external = false,
                  .pad = {},
                  .slot_id = SlotId{0},
                  .pad2 = {}};

    MemoryPlan plan_a = make_manual_plan(slots_a, 1, 512, 0);

    PoolAllocator pool;
    pool.init(&plan_a);
    assert(pool.pool_bytes() == 512);
    void* old_base = pool.pool_base();

    pool.destroy();
    assert(!pool.is_initialized());

    TensorSlot slots_b[2]{};
    slots_b[0] = {.offset_bytes = 0,
                  .nbytes = 1024,
                  .birth_op = OpIndex{0},
                  .death_op = OpIndex{5},
                  .dtype = ScalarType::Float,
                  .device_type = DeviceType::CPU,
                  .device_idx = 0,
                  .layout = Layout::Strided,
                  .is_external = false,
                  .pad = {},
                  .slot_id = SlotId{0},
                  .pad2 = {}};
    slots_b[1] = {.offset_bytes = 1024,
                  .nbytes = 2048,
                  .birth_op = OpIndex{1},
                  .death_op = OpIndex{4},
                  .dtype = ScalarType::Float,
                  .device_type = DeviceType::CPU,
                  .device_idx = 0,
                  .layout = Layout::Strided,
                  .is_external = false,
                  .pad = {},
                  .slot_id = SlotId{1},
                  .pad2 = {}};

    MemoryPlan plan_b = make_manual_plan(slots_b, 2, 3072, 0);
    pool.init(&plan_b);
    auto pv_b = pool.mint_initialized_view();

    assert(pool.is_initialized());
    assert(pool.pool_bytes() == 3072);
    assert(pool.num_slots() == 2);
    // The old base is deliberately not compared against the new one: an
    // allocator is free to hand the same address back after the release.
    (void)old_base;

    auto* base = static_cast<char*>(pool.pool_base());
    assert(pool.slot_ptr(SlotId{0}, pv_b) == base);
    assert(pool.slot_ptr(SlotId{1}, pv_b) == base + 1024);

    std::printf("  test_reinit: PASSED\n");
}

// The plan here comes from the real planner, so the offsets are whatever
// it chooses and the assertions never name one.  The lifetimes are
// picked so that slot 2 outlives slot 1's death and can legitimately be
// placed in the space slot 1 vacated.
static void test_integration_with_sweep_line() {
    auto test = crucible::effects::testing::test();
    crucible::BackgroundThread bt;

    constexpr uint32_t N = 4;
    TensorSlot slots[N]{};
    slots[0] = {.offset_bytes = 0,
                .nbytes = 1024,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{5},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{0},
                .pad2 = {}};
    slots[1] = {.offset_bytes = 0,
                .nbytes = 2048,
                .birth_op = OpIndex{1},
                .death_op = OpIndex{3},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{1},
                .pad2 = {}};
    slots[2] = {.offset_bytes = 0,
                .nbytes = 1024,
                .birth_op = OpIndex{4},
                .death_op = OpIndex{7},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{2},
                .pad2 = {}};
    slots[3] = {.offset_bytes = 0,
                .nbytes = 512,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{7},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = true,
                .pad = {},
                .slot_id = SlotId{3},
                .pad2 = {}};

    auto* plan = bt.compute_memory_plan(test.alloc, slots, N);
    assert(plan != nullptr);
    assert(plan->pool_bytes > 0);

    PoolAllocator pool;
    pool.init(plan);
    auto pv = pool.mint_initialized_view();

    assert(pool.is_initialized());
    assert(pool.pool_bytes() == plan->pool_bytes);

    assert(pool.slot_ptr(SlotId{3}, pv) == nullptr);

    alignas(256) char fake_param[512];
    pool.register_external(SlotId{3}, crucible::safety::NonNull<void*>{fake_param}, pv);
    assert(pool.slot_ptr(SlotId{3}, pv) == fake_param);

    // Slot 0 is alive alongside slot 1 and then alongside slot 2, while
    // slots 1 and 2 are never alive at the same time.  So only the pairs
    // that overlap are required to stay isolated.

    // Both slots are alive here.
    auto* p0 = static_cast<uint8_t*>(pool.slot_ptr(SlotId{0}, pv));
    auto* p1 = static_cast<uint8_t*>(pool.slot_ptr(SlotId{1}, pv));
    std::memset(p0, 0x11, 1024);
    std::memset(p1, 0x22, 2048);
    for (uint32_t i = 0; i < 1024; i++)
        assert(p0[i] == 0x11);
    for (uint32_t i = 0; i < 2048; i++)
        assert(p1[i] == 0x22);

    // Slot 1 is dead by now, so writing slot 2 is allowed to overwrite
    // its bytes and nothing below reads them again.
    auto* p2 = static_cast<uint8_t*>(pool.slot_ptr(SlotId{2}, pv));
    std::memset(p2, 0x33, 1024);
    // Slot 0 is still alive, so its bytes must survive that write.
    for (uint32_t i = 0; i < 1024; i++)
        assert(p0[i] == 0x11);
    for (uint32_t i = 0; i < 1024; i++)
        assert(p2[i] == 0x33);

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

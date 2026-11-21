#include <crucible/PoolAllocator.h>
#include <crucible/BackgroundThread.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "tensor_data.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::PoolAllocator;
using crucible::MemoryPlan;
using crucible::TensorSlot;
using crucible::SlotId;
using crucible::OpIndex;
using crucible::ScalarType;
using crucible::DeviceType;
using crucible::Layout;
using crucible::test::host_data;
using crucible::test::host_ptr;

// The offsets here are written by hand rather than produced by the
// planner, so these tests exercise the allocator on its own.
// The pool is minted live on the thread that holds a Vigil's producer
// claim.  These tests run the pool without a Vigil, so they take that
// context from the test door.
static constexpr crucible::VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<crucible::Vigil>();

static MemoryPlan make_manual_plan(::fixy::View<TensorSlot> slots, uint64_t pool_bytes, uint32_t num_ext) {
    MemoryPlan plan{};
    plan.slots = slots;
    plan.num_external = num_ext;
    plan.pool_bytes = pool_bytes;
    plan.device_type = DeviceType::CPU;
    plan.device_idx = 0;
    return plan;
}

static void test_basic_init() {
    ::fixy::BumpArena arena{4096};
    ::fixy::View<TensorSlot> const slots = arena.array(::foundation::effects::testing::test().alloc, 3, TensorSlot{});
    *slots.get(0).expect("the run holds three slots") = {.offset_bytes = 0,
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
    *slots.get(1).expect("the run holds three slots") = {.offset_bytes = 1024,
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
    *slots.get(2).expect("the run holds three slots") = {.offset_bytes = 0,
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

    MemoryPlan plan = make_manual_plan(slots, 3072, 1);

    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan);
    // The view is checked once, here.  Every later call that takes it
    // treats it as proof and re-checks nothing.
    auto pv = pool.mint_initialized_view(kVigilForeground);

    CRUCIBLE_TEST_ASSERT(kTest, pool.is_initialized());
    CRUCIBLE_TEST_ASSERT(kTest, pool.pool_base().is_some());
    CRUCIBLE_TEST_ASSERT(kTest, pool.pool_base().expect("the pool holds bytes").is_aligned<PoolAllocator::ALIGNMENT>());
    CRUCIBLE_TEST_ASSERT(kTest, pool.pool_bytes() == 3072);
    CRUCIBLE_TEST_ASSERT(kTest, pool.num_slots() == 3);
    CRUCIBLE_TEST_ASSERT(kTest, pool.num_external() == 1);

    // Each byte of the pool reads zero before the first write of a slot, in
    // each build, so a read of an unwritten slot gives the same bytes on each
    // run.
    std::uint64_t pool_sum = 0;
    for (std::uint8_t const byte : pool.bytes()) {
        pool_sum += byte;
    }
    CRUCIBLE_TEST_ASSERT(kTest, pool_sum == 0);

    auto* base = host_ptr<char>(pool.pool_base());
    CRUCIBLE_TEST_ASSERT(kTest, host_ptr(pool.slot_ptr(SlotId{0}, pv)) == base + 0);
    CRUCIBLE_TEST_ASSERT(kTest, host_ptr(pool.slot_ptr(SlotId{1}, pv)) == base + 1024);

    // The base is aligned and both offsets are multiples of the
    // alignment, so every internal slot inherits the alignment.
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_ptr(SlotId{0}, pv).expect("slot 0 is pooled").is_aligned<256>());
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_ptr(SlotId{1}, pv).expect("slot 1 is pooled").is_aligned<256>());

    // An external slot has no place in the pool, so it holds no address
    // until someone registers storage for it.
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_ptr(SlotId{2}, pv).is_none());

    ::fixy::check::pass(kTest, "  test_basic_init: PASSED\n");
}

static void test_external_registration() {
    ::fixy::BumpArena arena{4096};
    ::fixy::View<TensorSlot> const slots = arena.array(::foundation::effects::testing::test().alloc, 2, TensorSlot{});
    *slots.get(0).expect("the run holds two slots") = {.offset_bytes = 0,
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
    *slots.get(1).expect("the run holds two slots") = {.offset_bytes = 0,
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

    MemoryPlan plan = make_manual_plan(slots, 1024, 1);

    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);

    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_ptr(SlotId{1}, pv).is_none());

    // This buffer stands in for a parameter tensor the pool does not own.
    alignas(256) char fake_param[512];
    pool.register_external(SlotId{1}, host_data(fake_param), pv);

    CRUCIBLE_TEST_ASSERT(kTest, host_ptr(pool.slot_ptr(SlotId{1}, pv)) == fake_param);

    ::fixy::check::pass(kTest, "  test_external_registration: PASSED\n");
}

// The three offsets are laid out end to end with no overlap, which is
// the precondition the isolation check below rests on.
static void test_write_read_isolation() {
    ::fixy::BumpArena arena{4096};
    ::fixy::View<TensorSlot> const slots = arena.array(::foundation::effects::testing::test().alloc, 3, TensorSlot{});
    *slots.get(0).expect("the run holds three slots") = {.offset_bytes = 0,
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
    *slots.get(1).expect("the run holds three slots") = {.offset_bytes = 256,
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
    *slots.get(2).expect("the run holds three slots") = {.offset_bytes = 768,
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

    MemoryPlan plan = make_manual_plan(slots, 1024, 0);

    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);

    std::memset(host_ptr(pool.slot_ptr(SlotId{0}, pv)), 0xAA, 256);
    std::memset(host_ptr(pool.slot_ptr(SlotId{1}, pv)), 0xBB, 512);
    std::memset(host_ptr(pool.slot_ptr(SlotId{2}, pv)), 0xCC, 256);

    auto* p0 = host_ptr<uint8_t>(pool.slot_ptr(SlotId{0}, pv));
    auto* p1 = host_ptr<uint8_t>(pool.slot_ptr(SlotId{1}, pv));
    auto* p2 = host_ptr<uint8_t>(pool.slot_ptr(SlotId{2}, pv));

    for (uint32_t i = 0; i < 256; i++)
        CRUCIBLE_TEST_ASSERT(kTest, p0[i] == 0xAA);
    for (uint32_t i = 0; i < 512; i++)
        CRUCIBLE_TEST_ASSERT(kTest, p1[i] == 0xBB);
    for (uint32_t i = 0; i < 256; i++)
        CRUCIBLE_TEST_ASSERT(kTest, p2[i] == 0xCC);

    ::fixy::check::pass(kTest, "  test_write_read_isolation: PASSED\n");
}

// A plan whose slots are all external asks for no pool at all, so the
// allocator must report itself initialized while holding no memory.
static void test_all_external() {
    ::fixy::BumpArena arena{4096};
    ::fixy::View<TensorSlot> const slots = arena.array(::foundation::effects::testing::test().alloc, 2, TensorSlot{});
    *slots.get(0).expect("the run holds two slots") = {.offset_bytes = 0,
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
    *slots.get(1).expect("the run holds two slots") = {.offset_bytes = 0,
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

    MemoryPlan plan = make_manual_plan(slots, 0, 2);

    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);

    CRUCIBLE_TEST_ASSERT(kTest, pool.is_initialized());
    CRUCIBLE_TEST_ASSERT(kTest, pool.pool_base().is_none());
    CRUCIBLE_TEST_ASSERT(kTest, pool.pool_bytes() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, pool.num_slots() == 2);

    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_ptr(SlotId{0}, pv).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_ptr(SlotId{1}, pv).is_none());

    alignas(256) char buf_a[1024];
    alignas(256) char buf_b[2048];
    pool.register_external(SlotId{0}, host_data(buf_a), pv);
    pool.register_external(SlotId{1}, host_data(buf_b), pv);
    CRUCIBLE_TEST_ASSERT(kTest, host_ptr(pool.slot_ptr(SlotId{0}, pv)) == buf_a);
    CRUCIBLE_TEST_ASSERT(kTest, host_ptr(pool.slot_ptr(SlotId{1}, pv)) == buf_b);

    ::fixy::check::pass(kTest, "  test_all_external: PASSED\n");
}

static void test_reinit() {
    ::fixy::BumpArena arena{4096};
    ::fixy::View<TensorSlot> const slots_a = arena.array(::foundation::effects::testing::test().alloc, 1, TensorSlot{});
    *slots_a.get(0).expect("the run holds one slot") = {.offset_bytes = 0,
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

    MemoryPlan plan_a = make_manual_plan(slots_a, 512, 0);

    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan_a);
    CRUCIBLE_TEST_ASSERT(kTest, pool.pool_bytes() == 512);
    const ::fixy::Option<::fixy::RunAddress> old_base = pool.pool_base();

    pool.destroy();
    CRUCIBLE_TEST_ASSERT(kTest, !pool.is_initialized());

    ::fixy::View<TensorSlot> const slots_b = arena.array(::foundation::effects::testing::test().alloc, 2, TensorSlot{});
    *slots_b.get(0).expect("the run holds two slots") = {.offset_bytes = 0,
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
    *slots_b.get(1).expect("the run holds two slots") = {.offset_bytes = 1024,
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

    MemoryPlan plan_b = make_manual_plan(slots_b, 3072, 0);
    pool.init(::foundation::effects::testing::test().alloc, plan_b);
    auto pv_b = pool.mint_initialized_view(kVigilForeground);

    CRUCIBLE_TEST_ASSERT(kTest, pool.is_initialized());
    CRUCIBLE_TEST_ASSERT(kTest, pool.pool_bytes() == 3072);
    CRUCIBLE_TEST_ASSERT(kTest, pool.num_slots() == 2);
    // The old base is deliberately not compared against the new one: an
    // allocator is free to hand the same address back after the release.
    (void)old_base;

    auto* base = host_ptr<char>(pool.pool_base());
    CRUCIBLE_TEST_ASSERT(kTest, host_ptr(pool.slot_ptr(SlotId{0}, pv_b)) == base);
    CRUCIBLE_TEST_ASSERT(kTest, host_ptr(pool.slot_ptr(SlotId{1}, pv_b)) == base + 1024);

    ::fixy::check::pass(kTest, "  test_reinit: PASSED\n");
}

// The plan here comes from the real planner, so the offsets are whatever
// it chooses and the assertions never name one.  The lifetimes are
// picked so that slot 2 outlives slot 1's death and can legitimately be
// placed in the space slot 1 vacated.
static void test_integration_with_sweep_line() {
    auto test = ::foundation::effects::testing::test();
    crucible::BackgroundThread bt{::foundation::effects::testing::test().alloc};

    constexpr uint32_t N = 4;
    ::fixy::BumpArena sweep_arena{size_t{1} << 16};
    ::fixy::View<TensorSlot> const slots = sweep_arena.array(test.alloc, N, TensorSlot{});
    *slots.get(0).expect("the run holds four slots") = {.offset_bytes = 0,
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
    *slots.get(1).expect("the run holds four slots") = {.offset_bytes = 0,
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
    *slots.get(2).expect("the run holds four slots") = {.offset_bytes = 0,
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
    *slots.get(3).expect("the run holds four slots") = {.offset_bytes = 0,
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

    crucible::MemoryPlan plan_storage{};
    const crucible::MemoryPlan& plan = *bt.compute_memory_plan(test.alloc, plan_storage, sweep_arena, slots)
                                            .expect("the slots of the test fit in 64 bits");
    CRUCIBLE_TEST_ASSERT(kTest, plan.pool_bytes > 0);

    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);

    CRUCIBLE_TEST_ASSERT(kTest, pool.is_initialized());
    CRUCIBLE_TEST_ASSERT(kTest, pool.pool_bytes() == plan.pool_bytes);

    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_ptr(SlotId{3}, pv).is_none());

    alignas(256) char fake_param[512];
    pool.register_external(SlotId{3}, host_data(fake_param), pv);
    CRUCIBLE_TEST_ASSERT(kTest, host_ptr(pool.slot_ptr(SlotId{3}, pv)) == fake_param);

    // Slot 0 is alive alongside slot 1 and then alongside slot 2, while
    // slots 1 and 2 are never alive at the same time.  So only the pairs
    // that overlap are required to stay isolated.

    // Both slots are alive here.
    auto* p0 = host_ptr<uint8_t>(pool.slot_ptr(SlotId{0}, pv));
    auto* p1 = host_ptr<uint8_t>(pool.slot_ptr(SlotId{1}, pv));
    std::memset(p0, 0x11, 1024);
    std::memset(p1, 0x22, 2048);
    for (uint32_t i = 0; i < 1024; i++)
        CRUCIBLE_TEST_ASSERT(kTest, p0[i] == 0x11);
    for (uint32_t i = 0; i < 2048; i++)
        CRUCIBLE_TEST_ASSERT(kTest, p1[i] == 0x22);

    // Slot 1 is dead by now, so writing slot 2 is allowed to overwrite
    // its bytes and nothing below reads them again.
    auto* p2 = host_ptr<uint8_t>(pool.slot_ptr(SlotId{2}, pv));
    std::memset(p2, 0x33, 1024);
    // Slot 0 is still alive, so its bytes must survive that write.
    for (uint32_t i = 0; i < 1024; i++)
        CRUCIBLE_TEST_ASSERT(kTest, p0[i] == 0x11);
    for (uint32_t i = 0; i < 1024; i++)
        CRUCIBLE_TEST_ASSERT(kTest, p2[i] == 0x33);

    ::fixy::check::pass(kTest, "  test_integration_with_sweep_line: PASSED\n");
}

// The pool is memory of the host, so each pooled slot is on the host
// device, also when the plan records a different device for the slot.  A
// registered slot keeps the device that its owner states.
static void test_device_of_slots() {
    const crucible::TensorDevice accelerator{DeviceType::CUDA, 0};
    ::fixy::BumpArena arena{4096};
    ::fixy::View<TensorSlot> const slots = arena.array(::foundation::effects::testing::test().alloc, 2, TensorSlot{});
    *slots.get(0).expect("the run holds two slots") = {.offset_bytes = 0,
                                                       .nbytes = 256,
                                                       .birth_op = OpIndex{0},
                                                       .death_op = OpIndex{3},
                                                       .dtype = ScalarType::Float,
                                                       .device_type = DeviceType::CUDA,
                                                       .device_idx = 0,
                                                       .layout = Layout::Strided,
                                                       .is_external = false,
                                                       .pad = {},
                                                       .slot_id = SlotId{0},
                                                       .pad2 = {}};
    *slots.get(1).expect("the run holds two slots") = {.offset_bytes = 0,
                                                       .nbytes = 256,
                                                       .birth_op = OpIndex{0},
                                                       .death_op = OpIndex{3},
                                                       .dtype = ScalarType::Float,
                                                       .device_type = DeviceType::CUDA,
                                                       .device_idx = 0,
                                                       .layout = Layout::Strided,
                                                       .is_external = true,
                                                       .pad = {},
                                                       .slot_id = SlotId{1},
                                                       .pad2 = {}};

    MemoryPlan plan = make_manual_plan(slots, 256, 1);
    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);

    const crucible::TensorData pooled = pool.slot_data(SlotId{0}, pv).expect("slot 0 is pooled");
    CRUCIBLE_TEST_ASSERT(kTest, pooled.device() == crucible::kHostDevice);
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::some(pooled.address()) == pool.slot_ptr(SlotId{0}, pv));
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_data(SlotId{1}, pv).is_none());

    ::fixy::View<unsigned char> const fake_param =
        arena.array(::foundation::effects::testing::test().alloc, 256, static_cast<unsigned char>(0));
    const ::fixy::RunAddress param = ::fixy::mint_run_address(fake_param, 0).expect("the buffer holds 256 bytes");
    pool.register_external(SlotId{1}, crucible::TensorData{param, accelerator}, pv);
    CRUCIBLE_TEST_ASSERT(kTest,
                         pool.slot_data(SlotId{1}, pv) == ::fixy::some(crucible::TensorData{param, accelerator}));
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_ptr(SlotId{1}, pv) == ::fixy::some(param));

    ::fixy::check::pass(kTest, "  test_device_of_slots: PASSED\n");
}

// The slot-bounds checks in PoolAllocator::init are CRUCIBLE_FATAL_INVARIANT
// rather than contract clauses, so that they hold whatever the contract
// evaluation semantic is set to for the translation unit doing the including.
// The pointer they guard later becomes a memcpy destination.
//
// The guard ends the process by design, so the init runs in a child through
// expect_dies of the Check family.  Running this with NDEBUG defined is the
// point of it: the same body under a bare assert returns normally and hands
// out the pointer.
static void expect_abort_on_init(::fixy::TestRunnerCtx const& test, const TensorSlot& slot, uint64_t pool_bytes,
                                 ::fixy::TextView refusal) {
    ::fixy::BumpArena arena{4096};
    ::fixy::View<TensorSlot> const slots = arena.array(::foundation::effects::testing::test().alloc, 1, slot);
    MemoryPlan plan = make_manual_plan(slots, pool_bytes, 0);
    ::fixy::check::expect_dies(
        test,
        [&plan] noexcept {
            PoolAllocator pool;
            pool.init(::foundation::effects::testing::test().alloc, plan);
        },
        refusal);
}

static void test_slot_bounds_guard_is_armed(::fixy::TestRunnerCtx const& test) {
    TensorSlot past_end = {.offset_bytes = 512,
                           .nbytes = 1024,  // 512 + 1024 runs 512 bytes past a 1024-byte pool
                           .birth_op = OpIndex{0},
                           .death_op = OpIndex{1},
                           .dtype = ScalarType::Float,
                           .device_type = DeviceType::CPU,
                           .device_idx = 0,
                           .layout = Layout::Strided,
                           .is_external = false,
                           .pad = {},
                           .slot_id = SlotId{0},
                           .pad2 = {}};
    expect_abort_on_init(
        test, past_end, 1024,
        ::fixy::TextView{"PoolAllocator: slot 0 does not fit the pool: offset=512 nbytes=1024 pool_bytes=1024"});

    TensorSlot misaligned = {.offset_bytes = 1,  // not a multiple of PoolAllocator::ALIGNMENT
                             .nbytes = 16,
                             .birth_op = OpIndex{0},
                             .death_op = OpIndex{1},
                             .dtype = ScalarType::Float,
                             .device_type = DeviceType::CPU,
                             .device_idx = 0,
                             .layout = Layout::Strided,
                             .is_external = false,
                             .pad = {},
                             .slot_id = SlotId{0},
                             .pad2 = {}};
    expect_abort_on_init(
        test, misaligned, 4096,
        ::fixy::TextView{"PoolAllocator: slot 0 does not fit the pool: offset=1 nbytes=16 pool_bytes=4096"});

    ::fixy::check::pass(test, "  test_slot_bounds_guard_is_armed: PASSED\n");
}

// The column of bytes.  A pooled slot reads its window of the pool, and an
// external slot reads the View that its owner registered.  An external slot
// with an address alone, a slot of no byte and the id with no slot read no
// View.  The address of each slot is the first byte of its View.
static void test_slot_views() {
    ::fixy::BumpArena arena{4096};
    ::fixy::View<TensorSlot> const slots = arena.array(::foundation::effects::testing::test().alloc, 3, TensorSlot{});
    *slots.get(0).expect("the run holds three slots") = {.offset_bytes = 256,
                                                         .nbytes = 64,
                                                         .birth_op = OpIndex{0},
                                                         .death_op = OpIndex{1},
                                                         .dtype = ScalarType::Float,
                                                         .device_type = DeviceType::CPU,
                                                         .device_idx = 0,
                                                         .layout = Layout::Strided,
                                                         .is_external = false,
                                                         .pad = {},
                                                         .slot_id = SlotId{0},
                                                         .pad2 = {}};
    *slots.get(1).expect("the run holds three slots") = {.offset_bytes = 0,
                                                         .nbytes = 32,
                                                         .birth_op = OpIndex{0},
                                                         .death_op = OpIndex{1},
                                                         .dtype = ScalarType::Float,
                                                         .device_type = DeviceType::CPU,
                                                         .device_idx = 0,
                                                         .layout = Layout::Strided,
                                                         .is_external = true,
                                                         .pad = {},
                                                         .slot_id = SlotId{1},
                                                         .pad2 = {}};
    *slots.get(2).expect("the run holds three slots") = {.offset_bytes = 512,
                                                         .nbytes = 0,
                                                         .birth_op = OpIndex{0},
                                                         .death_op = OpIndex{1},
                                                         .dtype = ScalarType::Float,
                                                         .device_type = DeviceType::CPU,
                                                         .device_idx = 0,
                                                         .layout = Layout::Strided,
                                                         .is_external = false,
                                                         .pad = {},
                                                         .slot_id = SlotId{2},
                                                         .pad2 = {}};
    MemoryPlan plan = make_manual_plan(slots, 1024, 1);

    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);

    const auto is_count = [](::fixy::View<float const> const& values) noexcept { return values.size() == 16; };
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_view<float>(SlotId{0}, pv).is_some_and(is_count));
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_view<float>(SlotId{1}, pv).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_view<float>(SlotId{2}, pv).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_view<float>(SlotId::none(), pv).is_none());

    // The address of pooled slot 0 is the first byte of its View, the byte
    // of the pool at its offset.  Pooled slot 2 holds no byte, so its address
    // is the byte of the pool at its offset, and its View is empty.
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_view<std::uint8_t>(SlotId{0}, pv)
                                    .is_some_and([&pool, &pv](::fixy::View<std::uint8_t const> const& bytes) noexcept {
                                        return bytes.size() == 64
                                            && pool.slot_ptr(SlotId{0}, pv)
                                                   == ::fixy::mint_run_address(pool.bytes(), 256);
                                    }));
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_ptr(SlotId{2}, pv) == ::fixy::mint_run_address(pool.bytes(), 512));

    ::fixy::Layout const layout =
        ::fixy::mint_layout(::fixy::ByteCount{64},
                            ::fixy::mint_alignment(::fixy::ByteCount{64}).expect("64 is a power of two"))
            .expect("64 bytes at an alignment of 64 is a layout");
    ::foundation::AlignedBytes owner =
        ::foundation::mint_aligned_bytes_zeroed(::foundation::effects::testing::test().alloc, layout);
    pool.register_external_view(SlotId{1}, owner.view(), pv);
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_view<float>(SlotId{1}, pv).is_some_and(is_count));
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_ptr(SlotId{1}, pv) == ::fixy::mint_run_address(owner.view(), 0));

    // A registration by address alone gives the slot no bytes.
    pool.register_external(
        SlotId{1},
        crucible::TensorData{::fixy::mint_run_address(owner.view(), 0).expect("the block holds 64 bytes"),
                             crucible::kHostDevice},
        pv);
    CRUCIBLE_TEST_ASSERT(kTest, pool.slot_view<float>(SlotId{1}, pv).is_none());

    ::fixy::check::pass(kTest, "  test_slot_views: PASSED\n");
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_pool_allocator:\n");
    test_basic_init();
    test_external_registration();
    test_write_read_isolation();
    test_all_external();
    test_reinit();
    test_integration_with_sweep_line();
    test_device_of_slots();
    test_slot_views();
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_slot_bounds_guard_is_armed(test);
    ::fixy::check::pass(test, "test_pool_allocator: all tests passed\n");
    return 0;
}

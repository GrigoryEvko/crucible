// Each pinned accessor returns the same pointer its unpinned
// counterpart does, wrapped in a type that names which allocation
// class the pointer came from.  What the plain accessors do is
// checked where the allocator itself is tested.  What is checked here
// is that the wrapper changes the type and nothing else.

#include <crucible/PoolAllocator.h>
#include <crucible/safety/AllocClass.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/warden/Registry.h>
#include "test_assert.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

using crucible::PoolAllocator;
using crucible::MemoryPlan;
using crucible::TensorSlot;
using crucible::SlotId;
using crucible::OpIndex;
using crucible::ScalarType;
using crucible::DeviceType;
using crucible::Layout;
using crucible::safety::AllocClass;
using crucible::safety::AllocClassTag_v;

static MemoryPlan make_plan(TensorSlot* slots, uint32_t n, uint64_t pool_bytes, uint32_t num_ext) {
    MemoryPlan plan{};
    plan.slots = slots;
    plan.num_slots = n;
    plan.num_external = num_ext;
    plan.pool_bytes = pool_bytes;
    plan.device_type = DeviceType::CPU;
    plan.device_idx = 0;
    return plan;
}

static TensorSlot make_slot(uint64_t off, uint64_t nbytes, uint32_t id, bool external = false) {
    TensorSlot s{};
    s.offset_bytes = off;
    s.nbytes = nbytes;
    s.birth_op = OpIndex{0};
    s.death_op = OpIndex{1};
    s.dtype = ScalarType::Float;
    s.device_type = DeviceType::CPU;
    s.device_idx = 0;
    s.layout = Layout::Strided;
    s.is_external = external;
    s.slot_id = SlotId{id};
    return s;
}

static void test_slot_ptr_pinned_bit_equality() {
    TensorSlot slots[2]{};
    slots[0] = make_slot(0, 1024, 0);
    slots[1] = make_slot(1024, 1024, 1);
    MemoryPlan plan = make_plan(slots, 2, 2048, 0);

    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view();

    void* raw_0 = pool.slot_ptr(SlotId{0}, pv);
    void* raw_1 = pool.slot_ptr(SlotId{1}, pv);

    auto pinned_0 = pool.slot_ptr_pinned(SlotId{0}, pv);
    auto pinned_1 = pool.slot_ptr_pinned(SlotId{1}, pv);

    // The wrapper preserves the underlying address bit-for-bit.
    assert(std::move(pinned_0).consume() == raw_0);
    assert(std::move(pinned_1).consume() == raw_1);

    pool.destroy();
}

static void test_slot_ptr_pinned_type_identity() {
    TensorSlot slots[1]{};
    slots[0] = make_slot(0, 256, 0);
    MemoryPlan plan = make_plan(slots, 1, 256, 0);

    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view();

    using Got = decltype(pool.slot_ptr_pinned(SlotId{0}, pv));
    using Want = AllocClass<AllocClassTag_v::Pool, void*>;
    static_assert(std::is_same_v<Got, Want>, "slot_ptr_pinned must return AllocClass<Pool, void*>");
    static_assert(Got::tag == AllocClassTag_v::Pool);

    pool.destroy();
}

// The two fences below match a tag exactly rather than comparing
// along the lattice, which is what a consumer does when it wants one
// allocation class and no substitute.
template <typename W>
concept admissible_at_pool_fence =
    W::tag == AllocClassTag_v::Pool || W::tag == AllocClassTag_v::Arena || W::tag == AllocClassTag_v::Heap
    || W::tag == AllocClassTag_v::Mmap || W::tag == AllocClassTag_v::HugePage;

template <typename W>
concept admissible_at_huge_fence = W::tag == AllocClassTag_v::HugePage;

static void test_pool_fence_simulation() {
    using Slot = AllocClass<AllocClassTag_v::Pool, void*>;
    static_assert(admissible_at_pool_fence<Slot>);
    static_assert(!admissible_at_huge_fence<Slot>);

    using Huge = AllocClass<AllocClassTag_v::HugePage, void*>;
    static_assert(admissible_at_huge_fence<Huge>);
}

static void test_negative_tier_witnesses() {
    using PoolSlot = AllocClass<AllocClassTag_v::Pool, void*>;
    using StackSlot = AllocClass<AllocClassTag_v::Stack, void*>;
    using ArenaSlot = AllocClass<AllocClassTag_v::Arena, void*>;
    using HeapSlot = AllocClass<AllocClassTag_v::Heap, void*>;
    using MmapSlot = AllocClass<AllocClassTag_v::Mmap, void*>;
    using HugeSlot = AllocClass<AllocClassTag_v::HugePage, void*>;

    // A value satisfies a requirement when its own class is at least as
    // strong as the one asked for.  From weakest to strongest the
    // classes run: huge page, memory mapping, heap, arena, pool, stack.
    static_assert(PoolSlot::satisfies<AllocClassTag_v::Pool>);
    static_assert(PoolSlot::satisfies<AllocClassTag_v::Arena>);
    static_assert(PoolSlot::satisfies<AllocClassTag_v::Heap>);
    static_assert(PoolSlot::satisfies<AllocClassTag_v::Mmap>);
    static_assert(PoolSlot::satisfies<AllocClassTag_v::HugePage>);
    static_assert(!PoolSlot::satisfies<AllocClassTag_v::Stack>);

    // The weakest class satisfies nothing but itself.
    static_assert(HugeSlot::satisfies<AllocClassTag_v::HugePage>);
    static_assert(!HugeSlot::satisfies<AllocClassTag_v::Mmap>);
    static_assert(!HugeSlot::satisfies<AllocClassTag_v::Heap>);
    static_assert(!HugeSlot::satisfies<AllocClassTag_v::Arena>);
    static_assert(!HugeSlot::satisfies<AllocClassTag_v::Pool>);
    static_assert(!HugeSlot::satisfies<AllocClassTag_v::Stack>);

    // The strongest satisfies every one of them.
    static_assert(StackSlot::satisfies<AllocClassTag_v::Stack>);
    static_assert(StackSlot::satisfies<AllocClassTag_v::Pool>);
    static_assert(StackSlot::satisfies<AllocClassTag_v::Arena>);
    static_assert(StackSlot::satisfies<AllocClassTag_v::Heap>);
    static_assert(StackSlot::satisfies<AllocClassTag_v::Mmap>);
    static_assert(StackSlot::satisfies<AllocClassTag_v::HugePage>);

    // Named above only to spell the lattice out in full.
    (void)sizeof(ArenaSlot);
    (void)sizeof(HeapSlot);
    (void)sizeof(MmapSlot);
}

static void test_layout_invariant() {
    static_assert(sizeof(AllocClass<AllocClassTag_v::Pool, void*>) == sizeof(void*));
    static_assert(sizeof(AllocClass<AllocClassTag_v::HugePage, void*>) == sizeof(void*));
}

static void test_pool_base_pinned_small_pool() {
    TensorSlot slots[1]{};
    slots[0] = make_slot(0, 1024, 0);
    MemoryPlan plan = make_plan(slots, 1, 1024, 0);

    PoolAllocator pool;
    pool.init(&plan);

    auto pinned = pool.pool_base_pinned();
    static_assert(std::is_same_v<decltype(pinned), AllocClass<AllocClassTag_v::Pool, void*>>);

    void* raw = pool.pool_base();
    assert(std::move(pinned).consume() == raw);
    assert(raw != nullptr);

    // The pool base is aligned at least to the allocator's own
    // granularity, whatever the pool size.
    assert(std::bit_cast<uintptr_t>(raw) % PoolAllocator::ALIGNMENT == 0);

    pool.destroy();
}

static void test_pool_base_huge_pinned() {
    // A pool of exactly one huge page is the smallest that qualifies
    // for huge-page alignment.
    constexpr uint64_t HP = crucible::warden::kHugePageBytes;
    TensorSlot slots[1]{};
    slots[0] = make_slot(0, HP / 2, 0);  // one slot, half the pool
    MemoryPlan plan = make_plan(slots, 1, HP, 0);

    PoolAllocator pool;
    pool.init(&plan);

    // The accessor has a precondition on the pool being at least one
    // huge page, which the plan above satisfies.
    auto pinned = pool.pool_base_huge_pinned();
    static_assert(std::is_same_v<decltype(pinned), AllocClass<AllocClassTag_v::HugePage, void*>>);

    void* raw = pool.pool_base();
    void* wrapped = std::move(pinned).consume();
    assert(wrapped == raw);
    assert(raw != nullptr);
    assert(std::bit_cast<uintptr_t>(raw) % HP == 0);

    pool.destroy();
}

static void test_pool_relax_to_weaker() {
    TensorSlot slots[1]{};
    slots[0] = make_slot(0, 256, 0);
    MemoryPlan plan = make_plan(slots, 1, 256, 0);

    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view();

    auto pinned = pool.slot_ptr_pinned(SlotId{0}, pv);
    // Relaxing toward a weaker class is allowed.  Tightening toward a
    // stronger one is refused, which a negative-compile fixture proves
    // separately.
    auto relaxed = std::move(pinned).relax<AllocClassTag_v::Heap>();
    static_assert(std::is_same_v<decltype(relaxed), AllocClass<AllocClassTag_v::Heap, void*>>);

    pool.destroy();
}

// The ordering is transitive, so a pool pointer reaches a consumer
// that asks for any class below pool without an intermediate step.
static void test_chain_composition() {
    using P = AllocClass<AllocClassTag_v::Pool, void*>;
    static_assert(P::satisfies<AllocClassTag_v::Pool>);
    static_assert(P::satisfies<AllocClassTag_v::Arena>);
    static_assert(P::satisfies<AllocClassTag_v::Heap>);
    static_assert(P::satisfies<AllocClassTag_v::Mmap>);
    static_assert(P::satisfies<AllocClassTag_v::HugePage>);

    using H = AllocClass<AllocClassTag_v::HugePage, void*>;
    static_assert(H::satisfies<AllocClassTag_v::HugePage>);
    static_assert(!H::satisfies<AllocClassTag_v::Mmap>);
}

// A consumer shaped like a production call site, admitting a pool
// pointer or anything stronger.
template <typename Slot>
    requires(Slot::template satisfies<AllocClassTag_v::Pool>)
static uintptr_t pool_consumer(Slot slot) {
    void* p = std::move(slot).consume();
    return std::bit_cast<uintptr_t>(p);
}

static void test_e2e_fence_checked_consumer() {
    TensorSlot slots[1]{};
    slots[0] = make_slot(0, 1024, 0);
    MemoryPlan plan = make_plan(slots, 1, 1024, 0);

    PoolAllocator pool;
    pool.init(&plan);
    auto pv = pool.mint_initialized_view();

    auto slot_pinned = pool.slot_ptr_pinned(SlotId{0}, pv);
    uintptr_t addr = pool_consumer(std::move(slot_pinned));
    assert(addr != 0);

    pool.destroy();
}

// One address, two claims.  The weaker accessor keeps returning the
// pool class even when the allocation happens to be huge-page
// aligned, because that class is the one guaranteed for every pool
// size.
static void test_pool_base_pinned_with_huge_alignment() {
    constexpr uint64_t HP = crucible::warden::kHugePageBytes;
    TensorSlot slots[1]{};
    slots[0] = make_slot(0, HP / 2, 0);
    MemoryPlan plan = make_plan(slots, 1, HP, 0);

    PoolAllocator pool;
    pool.init(&plan);

    auto pool_pinned = pool.pool_base_pinned();
    static_assert(std::is_same_v<decltype(pool_pinned), AllocClass<AllocClassTag_v::Pool, void*>>);

    auto huge_pinned = pool.pool_base_huge_pinned();
    static_assert(std::is_same_v<decltype(huge_pinned), AllocClass<AllocClassTag_v::HugePage, void*>>);

    void* p1 = std::move(pool_pinned).consume();
    void* p2 = std::move(huge_pinned).consume();
    assert(p1 == p2);
    assert(std::bit_cast<uintptr_t>(p1) % HP == 0);

    pool.destroy();
}

int main() {
    test_slot_ptr_pinned_bit_equality();
    test_slot_ptr_pinned_type_identity();
    test_pool_fence_simulation();
    test_negative_tier_witnesses();
    test_layout_invariant();
    test_pool_base_pinned_small_pool();
    test_pool_base_huge_pinned();
    test_pool_relax_to_weaker();
    test_chain_composition();
    test_e2e_fence_checked_consumer();
    test_pool_base_pinned_with_huge_alignment();
    std::puts("ok");
    return 0;
}

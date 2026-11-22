// Each pinned accessor returns the same pointer its unpinned
// counterpart does, wrapped in a type that names which allocation
// class the pointer came from.  What the plain accessors do is
// checked where the allocator itself is tested.  What is checked here
// is that the wrapper changes the type and nothing else.

#include <crucible/PoolAllocator.h>
#include <crucible/warden/Registry.h>
#include <fixy/Bands.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Fixture.h>
#include <foundation/effects/Effect.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

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
using ::fixy::AllocClass;
using ::fixy::AllocClassTag_v;
using ::fixy::band_tier_v;
using ::fixy::satisfies_v;

// The pool is minted live on the thread that holds a Vigil's producer
// claim.  These tests run the pool without a Vigil, so they take that
// context from the test door.
static constexpr crucible::VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<crucible::Vigil>();

static MemoryPlan make_plan(::fixy::View<TensorSlot> slots, uint64_t pool_bytes, uint32_t num_ext) {
    MemoryPlan plan{};
    plan.slots = slots;
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
    ::fixy::BumpArena arena{4096};
    ::fixy::View<TensorSlot> const slots = ::fixy::check::run_of(
        kTest, ::foundation::effects::testing::test().alloc, arena, make_slot(0, 1024, 0), make_slot(1024, 1024, 1));
    MemoryPlan plan = make_plan(slots, 2048, 0);

    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);

    const ::fixy::Option<::fixy::RunAddress> raw_0 = pool.slot_ptr(SlotId{0}, pv);
    const ::fixy::Option<::fixy::RunAddress> raw_1 = pool.slot_ptr(SlotId{1}, pv);

    auto pinned_0 = pool.slot_ptr_pinned(SlotId{0}, pv);
    auto pinned_1 = pool.slot_ptr_pinned(SlotId{1}, pv);

    // The wrapper preserves the underlying address and its device.
    CRUCIBLE_TEST_ASSERT(kTest, std::move(pinned_0).consume() == raw_0);
    CRUCIBLE_TEST_ASSERT(kTest, std::move(pinned_1).consume() == raw_1);

    pool.destroy();
}

static void test_slot_ptr_pinned_type_identity() {
    ::fixy::BumpArena arena{4096};
    ::fixy::View<TensorSlot> const slots =
        ::fixy::check::run_of(kTest, ::foundation::effects::testing::test().alloc, arena, make_slot(0, 256, 0));
    MemoryPlan plan = make_plan(slots, 256, 0);

    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);

    using Got = decltype(pool.slot_ptr_pinned(SlotId{0}, pv));
    using Want = AllocClass<AllocClassTag_v::Pool, ::fixy::Option<::fixy::RunAddress>>;
    static_assert(std::is_same_v<Got, Want>, "slot_ptr_pinned must return AllocClass<Pool, Option<RunAddress>>");
    static_assert(band_tier_v<Got> == AllocClassTag_v::Pool);

    pool.destroy();
}

// The two fences below match a tier exactly rather than comparing
// along the lattice, which is what a consumer does when it wants one
// allocation class and no substitute.
template <typename W>
concept admissible_at_pool_fence = band_tier_v<W> == AllocClassTag_v::Pool || band_tier_v<W> == AllocClassTag_v::Arena
                                || band_tier_v<W> == AllocClassTag_v::Heap || band_tier_v<W> == AllocClassTag_v::Mmap
                                || band_tier_v<W> == AllocClassTag_v::HugePage;

template <typename W>
concept admissible_at_huge_fence = band_tier_v<W> == AllocClassTag_v::HugePage;

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
    using HugeSlot = AllocClass<AllocClassTag_v::HugePage, void*>;

    // A value satisfies a requirement when its own class is at least as
    // strong as the one asked for.  From weakest to strongest the
    // classes run: huge page, memory mapping, heap, arena, pool, stack.
    static_assert(satisfies_v<PoolSlot, AllocClassTag_v::Pool>);
    static_assert(satisfies_v<PoolSlot, AllocClassTag_v::Arena>);
    static_assert(satisfies_v<PoolSlot, AllocClassTag_v::Heap>);
    static_assert(satisfies_v<PoolSlot, AllocClassTag_v::Mmap>);
    static_assert(satisfies_v<PoolSlot, AllocClassTag_v::HugePage>);
    static_assert(!satisfies_v<PoolSlot, AllocClassTag_v::Stack>);

    // The weakest class satisfies nothing but itself.
    static_assert(satisfies_v<HugeSlot, AllocClassTag_v::HugePage>);
    static_assert(!satisfies_v<HugeSlot, AllocClassTag_v::Mmap>);
    static_assert(!satisfies_v<HugeSlot, AllocClassTag_v::Heap>);
    static_assert(!satisfies_v<HugeSlot, AllocClassTag_v::Arena>);
    static_assert(!satisfies_v<HugeSlot, AllocClassTag_v::Pool>);
    static_assert(!satisfies_v<HugeSlot, AllocClassTag_v::Stack>);

    // The strongest satisfies every one of them.
    static_assert(satisfies_v<StackSlot, AllocClassTag_v::Stack>);
    static_assert(satisfies_v<StackSlot, AllocClassTag_v::Pool>);
    static_assert(satisfies_v<StackSlot, AllocClassTag_v::Arena>);
    static_assert(satisfies_v<StackSlot, AllocClassTag_v::Heap>);
    static_assert(satisfies_v<StackSlot, AllocClassTag_v::Mmap>);
    static_assert(satisfies_v<StackSlot, AllocClassTag_v::HugePage>);
}

static void test_layout_invariant() {
    static_assert(sizeof(AllocClass<AllocClassTag_v::Pool, void*>) == sizeof(void*));
    static_assert(sizeof(AllocClass<AllocClassTag_v::HugePage, void*>) == sizeof(void*));
    static_assert(sizeof(PoolAllocator::PinnedAddress) == sizeof(::fixy::Option<::fixy::RunAddress>));
    static_assert(sizeof(PoolAllocator::HugePinnedAddress) == sizeof(::fixy::Option<::fixy::RunAddress>));
}

static void test_pool_base_pinned_small_pool() {
    ::fixy::BumpArena arena{4096};
    ::fixy::View<TensorSlot> const slots =
        ::fixy::check::run_of(kTest, ::foundation::effects::testing::test().alloc, arena, make_slot(0, 1024, 0));
    MemoryPlan plan = make_plan(slots, 1024, 0);

    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan);

    auto pinned = pool.pool_base_pinned();
    static_assert(std::is_same_v<decltype(pinned), PoolAllocator::PinnedAddress>);

    const ::fixy::Option<::fixy::RunAddress> raw = pool.pool_base();
    CRUCIBLE_TEST_ASSERT(kTest, std::move(pinned).consume() == raw);
    CRUCIBLE_TEST_ASSERT(kTest, raw.is_some());

    // The pool base is aligned at least to the allocator's own
    // granularity, whatever the pool size.
    CRUCIBLE_TEST_ASSERT(kTest, is_aligned<PoolAllocator::ALIGNMENT>(pool.pool_base().expect("the pool holds bytes")));

    pool.destroy();
}

static void test_pool_base_huge_pinned() {
    // A pool of exactly one huge page is the smallest that qualifies
    // for huge-page alignment.
    constexpr uint64_t HP = crucible::warden::kHugePageBytes;
    ::fixy::BumpArena arena{4096};
    // one slot, half the pool
    ::fixy::View<TensorSlot> const slots =
        ::fixy::check::run_of(kTest, ::foundation::effects::testing::test().alloc, arena, make_slot(0, HP / 2, 0));
    MemoryPlan plan = make_plan(slots, HP, 0);

    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan);

    // The accessor has a precondition on the pool being at least one
    // huge page, which the plan above satisfies.
    auto pinned = pool.pool_base_huge_pinned();
    static_assert(std::is_same_v<decltype(pinned), PoolAllocator::HugePinnedAddress>);

    const ::fixy::Option<::fixy::RunAddress> raw = pool.pool_base();
    const ::fixy::Option<::fixy::RunAddress> wrapped = std::move(pinned).consume();
    CRUCIBLE_TEST_ASSERT(kTest, wrapped == raw);
    CRUCIBLE_TEST_ASSERT(kTest, raw.is_some());
    CRUCIBLE_TEST_ASSERT(kTest, is_aligned<HP>(pool.pool_base().expect("the pool holds bytes")));

    pool.destroy();
}

static void test_pool_relax_to_weaker() {
    ::fixy::BumpArena arena{4096};
    ::fixy::View<TensorSlot> const slots =
        ::fixy::check::run_of(kTest, ::foundation::effects::testing::test().alloc, arena, make_slot(0, 256, 0));
    MemoryPlan plan = make_plan(slots, 256, 0);

    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);

    auto pinned = pool.slot_ptr_pinned(SlotId{0}, pv);
    // Relaxing toward a weaker class is allowed.  Tightening toward a
    // stronger one is refused, which a negative-compile fixture proves
    // separately.
    auto relaxed = ::fixy::relax<AllocClassTag_v::Heap>(std::move(pinned));
    static_assert(
        std::is_same_v<decltype(relaxed), AllocClass<AllocClassTag_v::Heap, ::fixy::Option<::fixy::RunAddress>>>);

    pool.destroy();
}

// The ordering is transitive, so a pool pointer reaches a consumer
// that asks for any class below pool without an intermediate step.
static void test_chain_composition() {
    using P = AllocClass<AllocClassTag_v::Pool, void*>;
    static_assert(satisfies_v<P, AllocClassTag_v::Pool>);
    static_assert(satisfies_v<P, AllocClassTag_v::Arena>);
    static_assert(satisfies_v<P, AllocClassTag_v::Heap>);
    static_assert(satisfies_v<P, AllocClassTag_v::Mmap>);
    static_assert(satisfies_v<P, AllocClassTag_v::HugePage>);

    using H = AllocClass<AllocClassTag_v::HugePage, void*>;
    static_assert(satisfies_v<H, AllocClassTag_v::HugePage>);
    static_assert(!satisfies_v<H, AllocClassTag_v::Mmap>);
}

// A consumer shaped like a production call site, admitting a pool
// address or anything stronger.
template <typename Slot>
    requires(satisfies_v<Slot, AllocClassTag_v::Pool>)
static ::fixy::Option<::fixy::RunAddress> pool_consumer(Slot slot) {
    return std::move(slot).consume();
}

static void test_e2e_fence_checked_consumer() {
    ::fixy::BumpArena arena{4096};
    ::fixy::View<TensorSlot> const slots =
        ::fixy::check::run_of(kTest, ::foundation::effects::testing::test().alloc, arena, make_slot(0, 1024, 0));
    MemoryPlan plan = make_plan(slots, 1024, 0);

    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan);
    auto pv = pool.mint_initialized_view(kVigilForeground);

    auto slot_pinned = pool.slot_ptr_pinned(SlotId{0}, pv);
    const ::fixy::Option<::fixy::RunAddress> data = pool_consumer(std::move(slot_pinned));
    CRUCIBLE_TEST_ASSERT(kTest, data.is_some());

    pool.destroy();
}

// One address, two claims.  The weaker accessor keeps returning the
// pool class even when the allocation happens to be huge-page
// aligned, because that class is the one guaranteed for every pool
// size.
static void test_pool_base_pinned_with_huge_alignment() {
    constexpr uint64_t HP = crucible::warden::kHugePageBytes;
    ::fixy::BumpArena arena{4096};
    ::fixy::View<TensorSlot> const slots =
        ::fixy::check::run_of(kTest, ::foundation::effects::testing::test().alloc, arena, make_slot(0, HP / 2, 0));
    MemoryPlan plan = make_plan(slots, HP, 0);

    PoolAllocator pool;
    pool.init(::foundation::effects::testing::test().alloc, plan);

    auto pool_pinned = pool.pool_base_pinned();
    static_assert(std::is_same_v<decltype(pool_pinned), PoolAllocator::PinnedAddress>);

    auto huge_pinned = pool.pool_base_huge_pinned();
    static_assert(std::is_same_v<decltype(huge_pinned), PoolAllocator::HugePinnedAddress>);

    const ::fixy::Option<::fixy::RunAddress> p1 = std::move(pool_pinned).consume();
    const ::fixy::Option<::fixy::RunAddress> p2 = std::move(huge_pinned).consume();
    CRUCIBLE_TEST_ASSERT(kTest, p1 == p2);
    CRUCIBLE_TEST_ASSERT(kTest, is_aligned<HP>(pool.pool_base().expect("the pool holds bytes")));

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
    ::fixy::check::pass(kTest, "ok\n");
    return 0;
}

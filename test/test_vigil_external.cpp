// The data address of an external slot follows the live op of each iteration.
//
// Op 0 of each iteration reads a tensor that no op of the region writes, so
// the region gives it an external slot.  The tensor is a new batch of the
// data loader in each iteration, at a new address.  The activation binds the
// slot to the address of the recording.  Each compiled dispatch of a later
// iteration then binds it to the address of the live op, so input_ptr of op 0
// gives the address of the batch of that iteration.

#include "vigil_rig.h"

#include <crucible/Vigil.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "test_harness.h"

#include <cstddef>
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace test_vigil {

namespace {

using divrig::K;
using divrig::NUM_OPS;

// The family tag of the arena of the memory below.
struct[[= ::foundation::brand::arena_family_brand{}]] MemoryArena final {};

// The arena of the memory that the data addresses of the tensors point into.
// The Vigil reads an address as a key, and gives it back through input_ptr,
// and never reads through it.  Each test takes its memory as a run of this
// arena, which lives until the end of the process, and gives the View of the
// run to each helper.
::fixy::BumpArena<MemoryArena> g_memory_arena =
    ::fixy::mint_bump_arena<MemoryArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{16384});

// The memory of one test: a run of 16384 bytes of the arena.
[[nodiscard]] ::fixy::ArenaRun<std::byte, MemoryArena> take_memory() {
    return g_memory_arena.array(::foundation::effects::testing::test().alloc, 16384, std::byte{0});
}

// The offset of the batch of iteration iter, at an address of its own.
[[nodiscard]] uint32_t batch_offset(uint32_t iter) noexcept { return iter * 64; }

// The offset of the output of op op of iteration iter.
[[nodiscard]] uint32_t activation_offset(uint32_t iter, uint32_t op) noexcept { return 4096 + (iter * 16 + op) * 64; }

// A tensor of 16 floats less `first`, at element `first` of the storage at
// the offset of memory.  A tensor past the first element of its storage is
// a view.
[[nodiscard]] crucible::TensorMeta meta_at(::fixy::View<std::byte> memory, uint32_t offset, uint32_t first = 0) {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = ::crucible::tensor_dim(16 - int64_t{first});
    meta.strides[0] = ::crucible::tensor_dim(1);
    meta.dtype = crucible::ScalarType::Float;
    meta.device_type = crucible::DeviceType::CPU;
    meta.device_idx = 0;
    meta.layout = crucible::Layout::Strided;
    meta.storage_offset = first;
    if (first != 0) meta.flags = crucible::meta_flags::IS_VIEW;
    meta.data_ptr =
        crucible::external_data_ptr(&*memory.get(offset + first * sizeof(float)).expect("the offset is in the memory"));
    return meta;
}

// The address of the batch of iteration iter.
[[nodiscard]] ::fixy::Option<::fixy::RunAddress> batch_address(::fixy::View<std::byte> memory, uint32_t iter) {
    return ::fixy::mint_run_address(memory, batch_offset(iter));
}

// The element of the batch that op 1 reads, when op 1 reads a view of the
// batch, or zero when op 1 reads the output of op 0.  kNestResult names the
// shape of a nest: op 0, of depth 1, writes the new result R of op 1 in
// place, as the copy_ of addmm, and op 1 reads the batch and returns R.
constexpr uint32_t kOutputOfOp0 = 0;
constexpr uint32_t kViewFirst = 4;
constexpr uint32_t kNestResult = UINT32_MAX;

// Op op_idx of iteration iter.  Op 0 reads the batch of the iteration, and
// each other op reads the output of the op before it.  When op1_first is not
// zero, op 1 reads the view of the batch from element op1_first.
divrig::OpData op_reading_batch(::fixy::View<std::byte> memory, uint32_t iter, uint32_t op_idx, uint32_t op1_first) {
    divrig::OpData made;
    made.entry.schema_hash = divrig::SCHEMA[op_idx];
    made.entry.shape_hash = divrig::SHAPE[op_idx];
    made.entry.num_inputs = 1;
    made.entry.num_outputs = 1;
    made.n_metas = 2;
    if (op1_first == kNestResult && op_idx < 2) {
        const crucible::TensorMeta result = meta_at(memory, activation_offset(iter, 1));
        const crucible::TensorMeta input = op_idx == 0 ? result : meta_at(memory, batch_offset(iter));
        made.metas.set(::fixy::index<0>(), input);
        made.metas.set(::fixy::index<1>(), result);
        return made;
    }
    const bool is_view_read = op_idx == 1 && op1_first != kOutputOfOp0;
    const crucible::TensorMeta input = op_idx == 0  ? meta_at(memory, batch_offset(iter))
                                     : is_view_read ? meta_at(memory, batch_offset(iter), op1_first)
                                                    : meta_at(memory, activation_offset(iter, op_idx - 1));
    made.metas.set(::fixy::index<0>(), input);
    const crucible::TensorMeta output = meta_at(memory, activation_offset(iter, op_idx));
    made.metas.set(::fixy::index<1>(), output);
    return made;
}

crucible::DispatchResult dispatch(crucible::Vigil& vigil, ::fixy::View<std::byte> memory, uint32_t iter,
                                  uint32_t op_idx, uint32_t op1_first = kOutputOfOp0) {
    const divrig::OpData made = op_reading_batch(memory, iter, op_idx, op1_first);
    const crucible::OpDepth depth =
        op1_first == kNestResult && op_idx == 0 ? crucible::OpDepth{1} : crucible::OpDepth{};
    return vigil.dispatch_op(crucible::test::certify_synthetic_op(made.entry, made.metas, made.n_metas),
                             crucible::ScopeHash{}, crucible::CallsiteHash{}, depth);
}

// Records two whole iterations and the head of a third, which publish a
// region, and aligns iteration 3 so that its op K replays.
void publish_and_align(crucible::Vigil& vigil, ::fixy::View<std::byte> memory, uint32_t op1_first) {
    using crucible::DispatchResult;
    for (uint32_t iter = 0; iter < 2; ++iter) {
        for (uint32_t op = 0; op < NUM_OPS; ++op)
            CRUCIBLE_TEST_ASSERT(kTest,
                                 dispatch(vigil, memory, iter, op, op1_first).action == DispatchResult::Action::RECORD);
    }
    for (uint32_t op = 0; op < K; ++op)
        CRUCIBLE_TEST_ASSERT(kTest, dispatch(vigil, memory, 2, op, op1_first).action == DispatchResult::Action::RECORD);
    crucible::test::flush_and_wait_region_published(kTest, vigil);

    for (uint32_t op = 0; op < K; ++op)
        CRUCIBLE_TEST_ASSERT(kTest, dispatch(vigil, memory, 3, op, op1_first).action == DispatchResult::Action::RECORD);
    CRUCIBLE_TEST_ASSERT(kTest, vigil.context().is_compiled());
    for (uint32_t op = K; op < NUM_OPS; ++op)
        CRUCIBLE_TEST_ASSERT(kTest,
                             dispatch(vigil, memory, 3, op, op1_first).action == DispatchResult::Action::COMPILED);
}

}  // namespace

void test_external_follows_the_live_op() {
    using crucible::DispatchResult;
    auto memory_run = take_memory();
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<std::byte> const memory = memory_run.view();
    crucible::Vigil vigil;
    const auto fg = vigil.mint_producer_context();
    publish_and_align(vigil, memory, kOutputOfOp0);

    // Each later iteration reads a new batch, and the slot follows it.
    for (uint32_t iter = 4; iter < 7; ++iter) {
        CRUCIBLE_TEST_ASSERT(kTest, dispatch(vigil, memory, iter, 0).action == DispatchResult::Action::COMPILED);
        CRUCIBLE_TEST_ASSERT(kTest, vigil.input_ptr(fg, 0) == batch_address(memory, iter)
                                        && "the external slot holds the address of the batch of the live op");
        for (uint32_t op = 1; op < NUM_OPS; ++op)
            CRUCIBLE_TEST_ASSERT(kTest, dispatch(vigil, memory, iter, op).action == DispatchResult::Action::COMPILED);
    }

    ::fixy::check::pass(kTest, "  test_external_follows_the_live_op: PASSED\n");
}

// Op 0 reads the batch, and op 1 reads the view of the batch from element 4.
// The two reads have one external slot, the storage of the batch, so the
// bind at each read gives the slot the address of the first byte of the
// batch, and not the data pointer of the view.
void test_views_of_an_external_bind_its_storage() {
    using crucible::DispatchResult;
    auto memory_run = take_memory();
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<std::byte> const memory = memory_run.view();
    crucible::Vigil vigil;
    const auto fg = vigil.mint_producer_context();
    publish_and_align(vigil, memory, kViewFirst);

    for (uint32_t iter = 4; iter < 7; ++iter) {
        CRUCIBLE_TEST_ASSERT(kTest,
                             dispatch(vigil, memory, iter, 0, kViewFirst).action == DispatchResult::Action::COMPILED);
        CRUCIBLE_TEST_ASSERT(kTest, vigil.input_ptr(fg, 0) == batch_address(memory, iter));
        CRUCIBLE_TEST_ASSERT(kTest,
                             dispatch(vigil, memory, iter, 1, kViewFirst).action == DispatchResult::Action::COMPILED);
        CRUCIBLE_TEST_ASSERT(kTest, vigil.input_ptr(fg, 0) == batch_address(memory, iter)
                                        && "the view binds the slot to the first byte of its storage");
        for (uint32_t op = 2; op < NUM_OPS; ++op)
            CRUCIBLE_TEST_ASSERT(kTest, dispatch(vigil, memory, iter, op, kViewFirst).action
                                            == DispatchResult::Action::COMPILED);
    }

    ::fixy::check::pass(kTest, "  test_views_of_an_external_bind_its_storage: PASSED\n");
}

// Op 0 of depth 1 reads R before any op of the region writes it, and op 1
// returns R.  R is a slot of the region, so the replay gives op 0 the bytes
// of the pool for R, and the bind of the externals does not give the slot
// the address of the live R.
void test_internal_slot_of_a_nest_keeps_its_pool_bytes() {
    using crucible::DispatchResult;
    auto memory_run = take_memory();
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<std::byte> const memory = memory_run.view();
    crucible::Vigil vigil;
    const auto fg = vigil.mint_producer_context();
    publish_and_align(vigil, memory, kNestResult);

    for (uint32_t iter = 4; iter < 7; ++iter) {
        CRUCIBLE_TEST_ASSERT(kTest,
                             dispatch(vigil, memory, iter, 0, kNestResult).action == DispatchResult::Action::COMPILED);
        const ::fixy::Option<::fixy::RunAddress> planned = vigil.input_ptr(fg, 0);
        CRUCIBLE_TEST_ASSERT(kTest, planned.is_some() && "the pool holds the bytes of R");
        CRUCIBLE_TEST_ASSERT(kTest, !(planned == ::fixy::mint_run_address(memory, activation_offset(iter, 1)))
                                        && "no bind gives the internal slot of R the address of the live R");
        CRUCIBLE_TEST_ASSERT(kTest,
                             dispatch(vigil, memory, iter, 1, kNestResult).action == DispatchResult::Action::COMPILED);
        CRUCIBLE_TEST_ASSERT(kTest, vigil.input_ptr(fg, 0) == batch_address(memory, iter)
                                        && "the batch is still an external slot");
        for (uint32_t op = 2; op < NUM_OPS; ++op)
            CRUCIBLE_TEST_ASSERT(kTest, dispatch(vigil, memory, iter, op, kNestResult).action
                                            == DispatchResult::Action::COMPILED);
    }

    ::fixy::check::pass(kTest, "  test_internal_slot_of_a_nest_keeps_its_pool_bytes: PASSED\n");
}

// The door of an external address reads the plan of the region that replays.
// It takes the one external slot, the batch, and it refuses each pooled slot
// and an id outside the plan, so each pooled slot keeps its window.
void test_register_external_checks_the_plan() {
    using crucible::DispatchResult;
    using crucible::ExternalRefusal;
    using crucible::SlotId;
    auto memory_run = take_memory();
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<std::byte> const memory = memory_run.view();
    crucible::Vigil vigil;
    const auto fg = vigil.mint_producer_context();
    publish_and_align(vigil, memory, kOutputOfOp0);

    const crucible::TensorData given{batch_address(memory, 9).expect("the batch is in the memory"),
                                     crucible::kHostDevice};
    const crucible::DagRef<crucible::RegionNode const> region =
        vigil.context().active_region().expect("the Vigil replays a region");
    const crucible::DagRef<crucible::MemoryPlan const> plan =
        crucible::PlanLink{region->plan}.expect("the region has a plan");
    auto pv = vigil.context().pool().mint_initialized_view(fg);
    uint32_t position = 0;
    uint32_t num_external = 0;
    for (const crucible::TensorSlot& slot : plan->slots.view()) {
        const SlotId sid{position};
        const ::fixy::Option<::fixy::RunAddress> planned = vigil.context().pool().slot_ptr(sid, pv);
        const ::fixy::Option<ExternalRefusal> refusal = vigil.register_external(fg, sid, given).err();
        const ::fixy::Option<::fixy::RunAddress> held = vigil.context().pool().slot_ptr(sid, pv);
        if (slot.is_external) {
            CRUCIBLE_TEST_ASSERT(kTest, refusal.is_none() && held == ::fixy::some(given.address()));
            ++num_external;
        } else {
            CRUCIBLE_TEST_ASSERT(kTest, refusal == ::fixy::some(ExternalRefusal::PooledSlot) && held == planned);
        }
        ++position;
    }
    CRUCIBLE_TEST_ASSERT(kTest, num_external == 1 && position > num_external);
    CRUCIBLE_TEST_ASSERT(kTest, vigil.register_external(fg, SlotId{position}, given).err()
                                    == ::fixy::some(ExternalRefusal::OutsidePlan));

    // The replay binds the batch of the live op again.
    for (uint32_t op = 0; op < NUM_OPS; ++op)
        CRUCIBLE_TEST_ASSERT(kTest, dispatch(vigil, memory, 4, op).action == DispatchResult::Action::COMPILED);
    CRUCIBLE_TEST_ASSERT(kTest, dispatch(vigil, memory, 5, 0).action == DispatchResult::Action::COMPILED);
    CRUCIBLE_TEST_ASSERT(kTest, vigil.input_ptr(fg, 0) == batch_address(memory, 5));

    ::fixy::check::pass(kTest, "  test_register_external_checks_the_plan: PASSED\n");
}

}  // namespace test_vigil

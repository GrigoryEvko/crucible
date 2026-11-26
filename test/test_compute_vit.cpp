// One transformer block plus a classification head, computed through the
// pool allocator and checked against a direct CPU reference:
//
//   X[B,S,D] → LayerNorm → Q,K,V projections → SDPA → output projection
//   → residual add → LayerNorm → FFN(mm+relu+mm) → residual add
//   → CLS token extract → linear head → softmax → Y[B,N_CLS]
//
// The op sequence is picked to stress sweep-line offset assignment rather
// than the arithmetic: three-input ops, residual edges whose producer and
// consumer are not adjacent, two activation widths, and a gather. Q, K and
// V are all alive at once during SDPA, and the slots freed after attention
// have to be reused for the FFN activations.
//
// The test is several source files of one executable, so that no
// translation unit holds the whole run:
//
//   compute_vit.h                 the shared part
//   this file                     the plan, the replay loop, the checks
//                                 and main
//   ..._reference.cpp             the weights and the CPU reference pass
//   ..._attention.cpp             ops 0 to 6 of one compiled iteration
//   ..._head.cpp                  ops 7 to 14 of one compiled iteration

#include "compute_vit.h"

#include "cpu_kernels.h"

#include <crucible/BackgroundThread.h>
#include <crucible/CrucibleContext.h>
#include <foundation/effects/Effect.h>

#include "entry_arrays.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Fixture.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;
using namespace test_compute_vit;

// The views of the replay chain are minted on the thread that holds a
// Vigil's producer claim.  This test drives the context without a Vigil,
// so it takes that context from the test door.
static constexpr VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<Vigil>();

int main() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::report(::fixy::Sink::Out, "test_compute_vit:\n");

    Weights weights;
    fill_weights(weights);

    ::fixy::BumpArena<DagArena> arena =
        ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{4096});
    auto slots_run = arena.array(test.alloc, N_SLOTS, TensorSlot{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TensorSlot, N_SLOTS> const slots =
        slots_run.view().window<N_SLOTS>(0).expect("the run holds N_SLOTS slots");

    auto make_ext = [](uint32_t id, uint64_t sz) -> TensorSlot {
        return {.offset_bytes = 0,
                .nbytes = sz,
                .birth_op = OpIndex{0},
                .death_op = OpIndex{N_OPS},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = true,
                .pad = {},
                .slot_id = SlotId{id},
                .pad2 = {}};
    };
    auto make_int = [](uint32_t id, uint64_t sz, uint32_t birth, uint32_t death) -> TensorSlot {
        return {.offset_bytes = 0,
                .nbytes = sz,
                .birth_op = OpIndex{birth},
                .death_op = OpIndex{death},
                .dtype = ScalarType::Float,
                .device_type = DeviceType::CPU,
                .device_idx = 0,
                .layout = Layout::Strided,
                .is_external = false,
                .pad = {},
                .slot_id = SlotId{id},
                .pad2 = {}};
    };

    // Write the slot at the index.
    const auto put_slot = [&slots](::foundation::core::Index<N_SLOTS> index, TensorSlot const& slot) noexcept {
        slots.at(index) = slot;
    };

    put_slot(::foundation::core::index<SL_X>(), make_ext(SL_X, SZ_BSD));
    put_slot(::foundation::core::index<SL_G1>(), make_ext(SL_G1, SZ_D));
    put_slot(::foundation::core::index<SL_B1>(), make_ext(SL_B1, SZ_D));
    put_slot(::foundation::core::index<SL_WQ>(), make_ext(SL_WQ, SZ_DD));
    put_slot(::foundation::core::index<SL_WK>(), make_ext(SL_WK, SZ_DD));
    put_slot(::foundation::core::index<SL_WV>(), make_ext(SL_WV, SZ_DD));
    put_slot(::foundation::core::index<SL_WOUT>(), make_ext(SL_WOUT, SZ_DD));
    put_slot(::foundation::core::index<SL_G2>(), make_ext(SL_G2, SZ_D));
    put_slot(::foundation::core::index<SL_B2>(), make_ext(SL_B2, SZ_D));
    put_slot(::foundation::core::index<SL_WFF1>(), make_ext(SL_WFF1, SZ_DFF));
    put_slot(::foundation::core::index<SL_WFF2>(), make_ext(SL_WFF2, SZ_FFD));
    put_slot(::foundation::core::index<SL_WHEAD>(), make_ext(SL_WHEAD, SZ_DNCLS));

    put_slot(::foundation::core::index<SL_NORM1>(), make_int(SL_NORM1, SZ_BSD, 0, 3));
    put_slot(::foundation::core::index<SL_Q>(), make_int(SL_Q, SZ_BSD, 1, 4));
    put_slot(::foundation::core::index<SL_K>(), make_int(SL_K, SZ_BSD, 2, 4));
    put_slot(::foundation::core::index<SL_V>(), make_int(SL_V, SZ_BSD, 3, 4));
    put_slot(::foundation::core::index<SL_ATTN>(), make_int(SL_ATTN, SZ_BSD, 4, 5));
    put_slot(::foundation::core::index<SL_PROJ>(), make_int(SL_PROJ, SZ_BSD, 5, 6));
    put_slot(::foundation::core::index<SL_RES1>(), make_int(SL_RES1, SZ_BSD, 6, 11));
    put_slot(::foundation::core::index<SL_NORM2>(), make_int(SL_NORM2, SZ_BSD, 7, 8));
    put_slot(::foundation::core::index<SL_FF1>(), make_int(SL_FF1, SZ_BSDFF, 8, 9));
    put_slot(::foundation::core::index<SL_RELU>(), make_int(SL_RELU, SZ_BSDFF, 9, 10));
    put_slot(::foundation::core::index<SL_FF2>(), make_int(SL_FF2, SZ_BSD, 10, 11));
    // The last real consumer of RES2 is op 12, but the check after the loop
    // reads it too. Declaring death at the final op stops the sweep line
    // from handing its storage to a later activation.
    put_slot(::foundation::core::index<SL_RES2>(), make_int(SL_RES2, SZ_BSD, 11, 14));
    put_slot(::foundation::core::index<SL_CLS>(), make_int(SL_CLS, SZ_BD, 12, 13));
    put_slot(::foundation::core::index<SL_LOGITS>(), make_int(SL_LOGITS, SZ_BNCLS, 13, 14));
    put_slot(::foundation::core::index<SL_PROBS>(), make_int(SL_PROBS, SZ_BNCLS, 14, 14));

    const DagRef<MemoryPlan> plan_storage = arena.make<MemoryPlan>(test.alloc);
    crucible::PlanScratch sweep_scratch{::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 16}};
    BackgroundThread::PlanTarget const target{};
    sweep_scratch.arena.with_epoch(
        [target, alloc = test.alloc, plan_storage, &slots_run](crucible::PlanScratch::Epoch& epoch) noexcept {
            static_cast<void>(BackgroundThread::compute_memory_plan(alloc, target, plan_storage, epoch, slots_run)
                                  .expect("the slots of the test fit in 64 bits"));
        });
    // The plan is in the arena of the slots.
    crucible::MemoryPlan& plan = *plan_storage;

    ::fixy::report(::fixy::Sink::Out, "  plan: pool={} bytes, {} slots ({} external)\n",
                   static_cast<unsigned long>(plan.pool_bytes), plan.num_slots(), plan.num_external);

    // The slot ids of each op, the ops and the region are in this arena.
    ::fixy::BumpArena<DagArena> runs =
        ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 16});
    const auto slots_of = [&](auto... slot_numbers) noexcept {
        return ::fixy::check::run_of(kTest, test.alloc, runs, SlotId{slot_numbers}...);
    };

    struct OpDef {
        SchemaHash schema;
        ShapeHash shape;
        ::fixy::ArenaRun<SlotId, DagArena> in;
        ::fixy::ArenaRun<SlotId, DagArena> out;
    };
    const ::foundation::core::FixedArray<OpDef, N_OPS> defs{
        OpDef{.schema = H_LN1, .shape = S_LN1, .in = slots_of(SL_X, SL_G1, SL_B1), .out = slots_of(SL_NORM1)},
        OpDef{.schema = H_MMQ, .shape = S_MMQ, .in = slots_of(SL_NORM1, SL_WQ), .out = slots_of(SL_Q)},
        OpDef{.schema = H_MMK, .shape = S_MMK, .in = slots_of(SL_NORM1, SL_WK), .out = slots_of(SL_K)},
        OpDef{.schema = H_MMV, .shape = S_MMV, .in = slots_of(SL_NORM1, SL_WV), .out = slots_of(SL_V)},
        OpDef{.schema = H_SDPA, .shape = S_SDPA, .in = slots_of(SL_Q, SL_K, SL_V), .out = slots_of(SL_ATTN)},
        OpDef{.schema = H_MMOUT, .shape = S_MMOUT, .in = slots_of(SL_ATTN, SL_WOUT), .out = slots_of(SL_PROJ)},
        OpDef{.schema = H_ADD1, .shape = S_ADD1, .in = slots_of(SL_PROJ, SL_X), .out = slots_of(SL_RES1)},
        OpDef{.schema = H_LN2, .shape = S_LN2, .in = slots_of(SL_RES1, SL_G2, SL_B2), .out = slots_of(SL_NORM2)},
        OpDef{.schema = H_MMFF1, .shape = S_MMFF1, .in = slots_of(SL_NORM2, SL_WFF1), .out = slots_of(SL_FF1)},
        OpDef{.schema = H_RELU, .shape = S_RELU, .in = slots_of(SL_FF1), .out = slots_of(SL_RELU)},
        OpDef{.schema = H_MMFF2, .shape = S_MMFF2, .in = slots_of(SL_RELU, SL_WFF2), .out = slots_of(SL_FF2)},
        OpDef{.schema = H_ADD2, .shape = S_ADD2, .in = slots_of(SL_FF2, SL_RES1), .out = slots_of(SL_RES2)},
        OpDef{.schema = H_IDXSEL, .shape = S_IDXSEL, .in = slots_of(SL_RES2), .out = slots_of(SL_CLS)},
        OpDef{.schema = H_MMHEAD, .shape = S_MMHEAD, .in = slots_of(SL_CLS, SL_WHEAD), .out = slots_of(SL_LOGITS)},
        OpDef{.schema = H_SOFTMAX, .shape = S_SOFTMAX, .in = slots_of(SL_LOGITS), .out = slots_of(SL_PROBS)},
    };

    // One frame on the arena of the runs makes the ops and the region, so the
    // region links ops of its own arena.
    const DagRef<RegionNode> region =
        ::foundation::core::with_arena_frame(runs, [alloc = test.alloc, &defs, &plan_storage](auto& frame) noexcept {
            auto ops_run = frame.template build_array<TraceEntry>(
                alloc, ::fixy::ElementCount{N_OPS}, [](::fixy::RunStart) noexcept { return TraceEntry{}; });
            ::fixy::View<TraceEntry, N_OPS> const ops =
                ops_run.view().template window<N_OPS>(0).expect("the run holds N_OPS ops");
            for (auto [op, def] : ops.walk().zip(defs.view().walk())) {
                op->schema_hash = def->schema;
                op->shape_hash = def->shape;
                crucible::test::set_input_slots(alloc, frame, *op, def->in);
                crucible::test::set_output_slots(alloc, frame, *op, def->out);
            }

            const auto made = mint_dag_node<RegionNode>(alloc, frame);
            made->kind = TraceNodeKind::REGION;
            frame.store(made, &RegionNode::ops, ops_run);
            made->plan = make_plan_link(plan_storage);
            return made.weaken();
        });

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    auto context_key = crucible::mint_context_key(::foundation::effects::testing::test());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_compiled());

    Reference reference;
    compute_reference(weights, reference);

    // The blocks of the weights are in the scope for the time of the loop, so no
    // View of them stays in the pool after it.  The window of each slot starts at
    // the first byte of the block at the position of the slot in the list.
    const ::fixy::FixedArray<SlotId, 12> slot_ids{SlotId{SL_X},  SlotId{SL_G1},   SlotId{SL_B1},   SlotId{SL_WQ},
                                                  SlotId{SL_WK}, SlotId{SL_WV},   SlotId{SL_WOUT}, SlotId{SL_G2},
                                                  SlotId{SL_B2}, SlotId{SL_WFF1}, SlotId{SL_WFF2}, SlotId{SL_WHEAD}};
    ::fixy::FixedArray<::foundation::AlignedBytes, 12> blocks{
        std::move(weights.X),     std::move(weights.gamma1), std::move(weights.beta1), std::move(weights.W_q),
        std::move(weights.W_k),   std::move(weights.W_v),    std::move(weights.W_out), std::move(weights.gamma2),
        std::move(weights.beta2), std::move(weights.W_ff1),  std::move(weights.W_ff2), std::move(weights.W_head)};
    const auto take_block = [&blocks](std::size_t position) noexcept {
        return blocks.exchange(::fixy::Index<12>::check(position).expect("the list holds twelve blocks"),
                               ::foundation::AlignedBytes{});
    };
    const auto make_window = [&slot_ids](std::size_t position) noexcept {
        return ExternalWindow{.slot = *slot_ids.get(position).expect("the list holds twelve slots"),
                              .block = BlockPosition{::fixy::narrow<uint32_t>(position).expect("below twelve")}};
    };
    ExternalBlocks weight_blocks{
        .blocks = ::fixy::mint_heap_array_with<::foundation::AlignedBytes>(test.alloc, 12, take_block),
        .windows = ::fixy::mint_heap_array_with<ExternalWindow>(test.alloc, 12, make_window)};
    const auto replay_iterations = [](CompiledScope& scoped) noexcept {
        for (int iter = 0; iter < 50; iter++) {
            run_attention_ops(scoped);
            run_ffn_and_head_ops(scoped);
        }
    };
    // The body gives the answer alone: a result of a body of the scope holds no
    // owner of bytes, so the blocks end in the body after the scope.
    const auto replayed =
        ctx.with_compiled(context_key, [&weight_blocks, &replay_iterations](CompiledScope& scope) noexcept {
            auto [answer, held] = scope.with_external_blocks(std::move(weight_blocks), replay_iterations);
            return answer.is_ok() && held.blocks.size() == 12;
        });
    CRUCIBLE_TEST_ASSERT(kTest, replayed == ::fixy::some(true));

    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 50);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 0);

    auto pv = ctx.pool().mint_initialized_view(kVigilForeground);
    const ::fixy::View<float const> pool_probs =
        ctx.pool().slot_view<float>(SlotId{SL_PROBS}, pv).expect("the probabilities are a pooled slot of floats");
    const ::fixy::View<float const> ref_probs = reference.probs.view();
    const auto probs_at = [&pool_probs](crucible::cpu::Offset position) noexcept {
        return *pool_probs.get(position.raw()).expect("the probabilities hold B * N_CLS floats");
    };

    float max_err = 0.0f;
    for (auto [got, want] : pool_probs.walk().try_zip(ref_probs.walk()).expect("the two runs hold B * N_CLS floats")) {
        float err = ::fixy::fp::abs(*got - *want);
        max_err = ::fixy::max(max_err, err);
    }

    ::fixy::report(::fixy::Sink::Out, "  max_error vs reference: {}\n", static_cast<double>(max_err));
    CRUCIBLE_TEST_ASSERT(kTest, max_err < 1e-5f && "ViT output diverged from reference");

    for (std::size_t b = 0; b < B; b++) {
        float row_sum = 0.0f;
        for (std::size_t j = 0; j < N_CLS; j++) {
            float v = probs_at(crucible::cpu::Offset{b * N_CLS + j});
            CRUCIBLE_TEST_ASSERT(kTest, v >= 0.0f && v <= 1.0f);
            row_sum += v;
        }
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::fp::abs(row_sum - 1.0f) < 1e-5f && "softmax row does not sum to 1");
    }

    const ::fixy::View<float const> pool_res2 =
        ctx.pool().slot_view<float>(SlotId{SL_RES2}, pv).expect("the residual is a pooled slot of floats");
    const ::fixy::View<float const> ref_res2 = reference.res2.view();
    float max_res2_err = 0.0f;
    for (auto [got, want] : pool_res2.walk().try_zip(ref_res2.walk()).expect("the two runs hold B * S * D floats")) {
        float err = ::fixy::fp::abs(*got - *want);
        max_res2_err = ::fixy::max(max_res2_err, err);
    }
    ::fixy::report(::fixy::Sink::Out, "  resid2 max_error: {}\n", static_cast<double>(max_res2_err));
    CRUCIBLE_TEST_ASSERT(kTest, max_res2_err < 1e-5f && "residual output diverged from reference");

    ::fixy::report(::fixy::Sink::Out, "  probs[0] = [{}, {}, {}]\n",
                   static_cast<double>(probs_at(crucible::cpu::Offset{0})),
                   static_cast<double>(probs_at(crucible::cpu::Offset{1})),
                   static_cast<double>(probs_at(crucible::cpu::Offset{2})));
    ::fixy::report(::fixy::Sink::Out, "  probs[1] = [{}, {}, {}]\n",
                   static_cast<double>(probs_at(crucible::cpu::Offset{3})),
                   static_cast<double>(probs_at(crucible::cpu::Offset{4})),
                   static_cast<double>(probs_at(crucible::cpu::Offset{5})));
    ::fixy::report(::fixy::Sink::Out, "  50 iterations, {} ops/iter, compiled_iters={}\n", N_OPS,
                   ctx.compiled_iterations());

    ::fixy::check::pass(kTest, "test_compute_vit: PASSED\n");
    return 0;
}

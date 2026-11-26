// A four-operation forward pass carrying real numbers through the
// planned memory pool: a matrix multiply, a rectifier, a second
// matrix multiply, and a softmax.
//
// The planner is free to give two slots the same offset when their
// lifetimes do not overlap, which is the case for the two matrix
// multiply outputs here.  The result is therefore checked against a
// reference computed straight through, which holds whether or not the
// planner reused anything.

#include "cpu_kernels.h"

#include <crucible/BackgroundThread.h>
#include <crucible/CrucibleContext.h>
#include <foundation/effects/Effect.h>

#include "entry_arrays.h"
#include "plan_rig.h"
#include <crucible/Philox.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Fixture.h>
#include <foundation/AlignedBytes.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// The layout of count floats at an alignment of 64.
[[nodiscard]] ::fixy::Layout make_float_layout(::fixy::ElementCount count) {
    return ::fixy::mint_layout(::fixy::ByteCount{count.raw() * sizeof(float)},
                               ::fixy::mint_alignment(::fixy::ByteCount{64}).expect("64 is a power of two"))
        .expect("a block of floats of the test is a layout");
}

// A block of count floats on the host, each drawn from the Philox stream of
// Key into [-0.5, 0.5).  The float at position i comes from the first word
// of the block of the counter i, so each run and each host draws the same
// floats.
template <std::uint64_t Key>
[[nodiscard]] ::foundation::AlignedBytes make_weights(::fixy::ElementCount count) {
    using Start = ::foundation::core::side::WindowStart;
    using Length = ::foundation::core::side::WindowLength;
    ::foundation::AlignedBytes block =
        ::foundation::mint_aligned_bytes_zeroed(::foundation::effects::testing::test().alloc, make_float_layout(count));
    ::fixy::FixedList<::foundation::ByteWindow, 1> windows{};
    CRUCIBLE_TEST_ASSERT(kTest, windows
                                    .append(::foundation::ByteWindow{
                                        ::fixy::side_as<Start>(::fixy::ByteCount{0}),
                                        ::fixy::side_as<Length>(::fixy::ByteCount{count.raw() * sizeof(float)})})
                                    .is_ok());
    auto [drawn, back] = ::foundation::with_mut_windows<float>(std::move(block), windows, [](auto& views) noexcept {
        std::uint64_t draw = 0;
        std::move(views).drain([&draw](auto view) noexcept {
            for (float& element : view) {
                const ::fixy::Philox4x32::Counter word = ::fixy::Philox4x32::generate(draw, Key);
                element = crucible::Philox::to_uniform(word.at(::fixy::index<0>())) - 0.5f;
                ++draw;
            }
        });
    });
    CRUCIBLE_TEST_ASSERT(kTest, drawn.is_ok());
    return std::move(back);
}

}  // namespace

using namespace crucible;
using ::foundation::core::side::LeftFactor;
using ::foundation::core::side::RightFactor;
using cpu::make_dim;
using cpu::make_input;
using cpu::read_input;
using cpu::write_output;
namespace roles = cpu::side;

// The views of the replay chain are minted on the thread that holds a
// Vigil's producer claim.  This test drives the context without a Vigil,
// so it takes that context from the test door.
static constexpr VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<Vigil>();

static constexpr int BATCH = 2;
static constexpr int IN_DIM = 4;
static constexpr int HIDDEN = 8;
static constexpr int OUT_DIM = 3;

// One hash per operation kind, and one per operation, both distinct
// so a mismatch names which one drifted.
static const SchemaHash H_MM1{0x100};
static const SchemaHash H_RELU{0x200};
static const SchemaHash H_MM2{0x300};
static const SchemaHash H_SOFTMAX{0x400};

static const ShapeHash S_MM1{0x1100};
static const ShapeHash S_RELU{0x1200};
static const ShapeHash S_MM2{0x1300};
static const ShapeHash S_SOFTMAX{0x1400};

// Slot IDs — 3 external (params/input) + 4 internal (activations)
static constexpr uint32_t SL_X = 0;  // input [2,4]
static constexpr uint32_t SL_W1 = 1;  // param [4,8]
static constexpr uint32_t SL_W2 = 2;  // param [8,3]
static constexpr uint32_t SL_MM1 = 3;  // mm1 output [2,8]
static constexpr uint32_t SL_REL = 4;  // relu output [2,8]
static constexpr uint32_t SL_MM2 = 5;  // mm2 output [2,3]
static constexpr uint32_t SL_SM = 6;  // softmax output [2,3]
static constexpr uint32_t N_SLOTS = 7;
static constexpr uint32_t N_OPS = 4;

// Replays the four operations of one iteration, each with its kernel on the
// View of its inputs and the ViewMut of its output.  The kernel takes the Views
// of the inputs by value, read before the frame of the output.  The trailing
// names say which slot each index reaches, which the index alone does not.
static void run_iteration(CompiledScope& scope) {
    auto s = scope.advance(H_MM1, S_MM1).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    write_output(kTest, scope,
                 [left = read_input(scope, InputPosition{0}),  // X
                  right = read_input(scope, InputPosition{1})](auto mm1_out) noexcept {  // W1
                     cpu::mm(kTest, make_input<LeftFactor>(left), make_input<RightFactor>(right), std::move(mm1_out),
                             make_dim<roles::Rows>(BATCH), make_dim<roles::Columns>(HIDDEN),
                             make_dim<roles::Inner>(IN_DIM));
                 });

    s = scope.advance(H_RELU, S_RELU).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    write_output(kTest, scope, [input = read_input(scope, InputPosition{0})](auto relu_out) noexcept {  // mm1_out
        cpu::relu(kTest, input, std::move(relu_out), make_dim<roles::Count>(BATCH * HIDDEN));
    });

    s = scope.advance(H_MM2, S_MM2).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
    write_output(kTest, scope,
                 [left = read_input(scope, InputPosition{0}),  // relu_out
                  right = read_input(scope, InputPosition{1})](auto mm2_out) noexcept {  // W2
                     cpu::mm(kTest, make_input<LeftFactor>(left), make_input<RightFactor>(right), std::move(mm2_out),
                             make_dim<roles::Rows>(BATCH), make_dim<roles::Columns>(OUT_DIM),
                             make_dim<roles::Inner>(HIDDEN));
                 });

    // The last operation of an iteration completes it rather than matching.
    s = scope.advance(H_SOFTMAX, S_SOFTMAX).status();
    CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::COMPLETE);
    write_output(kTest, scope, [input = read_input(scope, InputPosition{0})](auto softmax_out) noexcept {  // mm2_out
        cpu::softmax(kTest, input, std::move(softmax_out), make_dim<roles::Rows>(BATCH),
                     make_dim<roles::Width>(OUT_DIM));
    });
}

int main() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::report(::fixy::Sink::Out, "test_compute:\n");

    // A fixed key for each block, so the same numbers flow through on every
    // run and the reference comparison below is reproducible.
    ::foundation::AlignedBytes x_block = make_weights<42>(::fixy::ElementCount{BATCH * IN_DIM});
    ::foundation::AlignedBytes w1_block = make_weights<43>(::fixy::ElementCount{IN_DIM * HIDDEN});
    ::foundation::AlignedBytes w2_block = make_weights<44>(::fixy::ElementCount{HIDDEN * OUT_DIM});

    // An external slot brings its own memory, registered separately,
    // and lives for the whole iteration.  An internal slot is given an
    // offset by the planner, and its lifetime runs from the operation
    // that produces it to the last one that reads it.  The test fills the
    // slots in a run of this arena, and the plan holds a planned copy of
    // them.  The ops, their slot ids, their zero metadata, their producers,
    // the plan and the region also live in this arena.
    ::fixy::BumpArena<DagArena> runs =
        ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 16});
    auto slots_run = runs.array(test.alloc, N_SLOTS, TensorSlot{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<TensorSlot, N_SLOTS> const slots =
        slots_run.view().window<N_SLOTS>(0).expect("the run holds N_SLOTS slots");

    slots.at(::foundation::core::index<SL_X>()) = {.offset_bytes = 0,
                                                   .nbytes = BATCH * IN_DIM * 4,
                                                   .birth_op = OpIndex{0},
                                                   .death_op = OpIndex{N_OPS},
                                                   .dtype = ScalarType::Float,
                                                   .device_type = DeviceType::CPU,
                                                   .device_idx = 0,
                                                   .layout = Layout::Strided,
                                                   .is_external = true,
                                                   .pad = {},
                                                   .slot_id = SlotId{SL_X},
                                                   .pad2 = {}};
    slots.at(::foundation::core::index<SL_W1>()) = {.offset_bytes = 0,
                                                    .nbytes = IN_DIM * HIDDEN * 4,
                                                    .birth_op = OpIndex{0},
                                                    .death_op = OpIndex{N_OPS},
                                                    .dtype = ScalarType::Float,
                                                    .device_type = DeviceType::CPU,
                                                    .device_idx = 0,
                                                    .layout = Layout::Strided,
                                                    .is_external = true,
                                                    .pad = {},
                                                    .slot_id = SlotId{SL_W1},
                                                    .pad2 = {}};
    slots.at(::foundation::core::index<SL_W2>()) = {.offset_bytes = 0,
                                                    .nbytes = HIDDEN * OUT_DIM * 4,
                                                    .birth_op = OpIndex{0},
                                                    .death_op = OpIndex{N_OPS},
                                                    .dtype = ScalarType::Float,
                                                    .device_type = DeviceType::CPU,
                                                    .device_idx = 0,
                                                    .layout = Layout::Strided,
                                                    .is_external = true,
                                                    .pad = {},
                                                    .slot_id = SlotId{SL_W2},
                                                    .pad2 = {}};

    slots.at(::foundation::core::index<SL_MM1>()) = {.offset_bytes = 0,
                                                     .nbytes = BATCH * HIDDEN * 4,
                                                     .birth_op = OpIndex{0},
                                                     .death_op = OpIndex{1},
                                                     .dtype = ScalarType::Float,
                                                     .device_type = DeviceType::CPU,
                                                     .device_idx = 0,
                                                     .layout = Layout::Strided,
                                                     .is_external = false,
                                                     .pad = {},
                                                     .slot_id = SlotId{SL_MM1},
                                                     .pad2 = {}};
    slots.at(::foundation::core::index<SL_REL>()) = {.offset_bytes = 0,
                                                     .nbytes = BATCH * HIDDEN * 4,
                                                     .birth_op = OpIndex{1},
                                                     .death_op = OpIndex{2},
                                                     .dtype = ScalarType::Float,
                                                     .device_type = DeviceType::CPU,
                                                     .device_idx = 0,
                                                     .layout = Layout::Strided,
                                                     .is_external = false,
                                                     .pad = {},
                                                     .slot_id = SlotId{SL_REL},
                                                     .pad2 = {}};
    slots.at(::foundation::core::index<SL_MM2>()) = {.offset_bytes = 0,
                                                     .nbytes = BATCH * OUT_DIM * 4,
                                                     .birth_op = OpIndex{2},
                                                     .death_op = OpIndex{3},
                                                     .dtype = ScalarType::Float,
                                                     .device_type = DeviceType::CPU,
                                                     .device_idx = 0,
                                                     .layout = Layout::Strided,
                                                     .is_external = false,
                                                     .pad = {},
                                                     .slot_id = SlotId{SL_MM2},
                                                     .pad2 = {}};
    slots.at(::foundation::core::index<SL_SM>()) = {.offset_bytes = 0,
                                                    .nbytes = BATCH * OUT_DIM * 4,
                                                    .birth_op = OpIndex{3},
                                                    .death_op = OpIndex{3},
                                                    .dtype = ScalarType::Float,
                                                    .device_type = DeviceType::CPU,
                                                    .device_idx = 0,
                                                    .layout = Layout::Strided,
                                                    .is_external = false,
                                                    .pad = {},
                                                    .slot_id = SlotId{SL_SM},
                                                    .pad2 = {}};

    // One frame on the arena of the runs makes the ops, their runs, the plan
    // and the region, so the region links ops and a plan of its own arena.
    // The planner plans a copy of the slots in the frame.
    const ::fixy::View<TensorSlot const> planned_slots = slots_run.view();
    const DagRef<RegionNode> region = ::foundation::core::with_arena_frame(runs, [alloc = test.alloc,
                                                                                  planned_slots](auto& frame) noexcept {
        auto op0_in = ::fixy::check::run_of(kTest, alloc, frame, SlotId{SL_X}, SlotId{SL_W1}).weaken();
        auto op0_out = ::fixy::check::run_of(kTest, alloc, frame, SlotId{SL_MM1}).weaken();

        auto op1_in = ::fixy::check::run_of(kTest, alloc, frame, SlotId{SL_MM1}).weaken();
        auto op1_out = ::fixy::check::run_of(kTest, alloc, frame, SlotId{SL_REL}).weaken();

        auto op2_in = ::fixy::check::run_of(kTest, alloc, frame, SlotId{SL_REL}, SlotId{SL_W2}).weaken();
        auto op2_out = ::fixy::check::run_of(kTest, alloc, frame, SlotId{SL_MM2}).weaken();

        auto op3_in = ::fixy::check::run_of(kTest, alloc, frame, SlotId{SL_MM2}).weaken();
        auto op3_out = ::fixy::check::run_of(kTest, alloc, frame, SlotId{SL_SM}).weaken();

        auto ops_run = frame.template build_array<TraceEntry>(alloc, ::fixy::ElementCount{N_OPS},
                                                              [](::fixy::RunStart) noexcept { return TraceEntry{}; });
        // A plain View of an arena run: the View carries no brand of its arena.
        ::fixy::View<TraceEntry, N_OPS> const ops =
            ops_run.view().template window<N_OPS>(0).expect("the run holds N_OPS ops");

        ops.at(::fixy::index<0>()).schema_hash = H_MM1;
        ops.at(::fixy::index<0>()).shape_hash = S_MM1;
        crucible::test::set_input_slots(alloc, frame, ops.at(::fixy::index<0>()), op0_in);
        crucible::test::set_output_slots(alloc, frame, ops.at(::fixy::index<0>()), op0_out);

        ops.at(::fixy::index<1>()).schema_hash = H_RELU;
        ops.at(::fixy::index<1>()).shape_hash = S_RELU;
        crucible::test::set_input_slots(alloc, frame, ops.at(::fixy::index<1>()), op1_in);
        crucible::test::set_output_slots(alloc, frame, ops.at(::fixy::index<1>()), op1_out);

        ops.at(::fixy::index<2>()).schema_hash = H_MM2;
        ops.at(::fixy::index<2>()).shape_hash = S_MM2;
        crucible::test::set_input_slots(alloc, frame, ops.at(::fixy::index<2>()), op2_in);
        crucible::test::set_output_slots(alloc, frame, ops.at(::fixy::index<2>()), op2_out);

        ops.at(::fixy::index<3>()).schema_hash = H_SOFTMAX;
        ops.at(::fixy::index<3>()).shape_hash = S_SOFTMAX;
        crucible::test::set_input_slots(alloc, frame, ops.at(::fixy::index<3>()), op3_in);
        crucible::test::set_output_slots(alloc, frame, ops.at(::fixy::index<3>()), op3_out);

        const auto plan =
            crucible::test::plan_in_frame(alloc, frame, planned_slots).expect("the slots of the test fit in 64 bits");
        const auto made = mint_dag_node<RegionNode>(alloc, frame);
        made->kind = TraceNodeKind::REGION;
        frame.store(made, &RegionNode::ops, ops_run);
        frame.store(made, &RegionNode::plan, plan);
        return made.weaken();
    });
    const crucible::MemoryPlan& plan = *region->plan.expect("the region holds its plan");

    ::fixy::report(::fixy::Sink::Out, "  plan: pool={} bytes, {} slots ({} external)\n",
                   static_cast<unsigned long>(plan.pool_bytes), plan.num_slots(), plan.num_external);

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    auto context_key = crucible::mint_context_key(::foundation::effects::testing::test());
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(context_key, *region));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_compiled());

    // The blocks of the external slots are in the scope for the time of the
    // loop, so no View of them stays in the pool after it.  A move keeps the
    // bytes of a block where they are.  The context stays compiled for the
    // whole loop, so one compiled scope holds it.
    ::fixy::FixedArray<ExternalWindow, 3> const windows{
        ExternalWindow{.slot = SlotId{SL_X}, .block = BlockPosition{0}},
        ExternalWindow{.slot = SlotId{SL_W1}, .block = BlockPosition{1}},
        ExternalWindow{.slot = SlotId{SL_W2}, .block = BlockPosition{2}}};
    ::fixy::FixedArray<::fixy::Option<::fixy::RunAddress>, 3> const addresses{
        ::fixy::mint_run_address(x_block.view(), 0), ::fixy::mint_run_address(w1_block.view(), 0),
        ::fixy::mint_run_address(w2_block.view(), 0)};
    const auto replay_iterations = [&windows, &addresses](CompiledScope& scoped) noexcept {
        // A read-only view, so that even these checks reach the pool the way
        // the hot path does.
        PoolAllocator const& pool = scoped.context().pool();
        auto const pv = pool.mint_initialized_view(kVigilForeground);
        for (auto const index : windows.indices()) {
            CRUCIBLE_TEST_ASSERT(kTest, pool.slot_ptr(windows.at(index).slot, pv) == addresses.at(index));
        }
        for (int iter = 0; iter < 100; iter++)
            run_iteration(scoped);
    };
    ExternalBlocks blocks{.blocks = ::fixy::mint_heap_array_with<::foundation::AlignedBytes>(
                              test.alloc, 3,
                              [&x_block, &w1_block, &w2_block](std::size_t position) noexcept {
                                  return std::move(position == 0 ? x_block : position == 1 ? w1_block : w2_block);
                              }),
                          .windows = ::fixy::mint_heap_array_copy<ExternalWindow>(test.alloc, windows.view())};
    // The scope gives the blocks back into their list, and the body gives the
    // answer alone: a result of a body of the scope holds no owner of bytes.
    const auto replayed = ctx.with_compiled(context_key, [&blocks, &replay_iterations](CompiledScope& scope) noexcept {
        auto [answer, held] = scope.with_external_blocks(std::move(blocks), replay_iterations);
        blocks = std::move(held);
        return answer.is_ok();
    });
    CRUCIBLE_TEST_ASSERT(kTest, replayed == ::fixy::some(true) && blocks.blocks.size() == 3);
    x_block = blocks.blocks.exchange(0, ::foundation::AlignedBytes{});
    w1_block = blocks.blocks.exchange(1, ::foundation::AlignedBytes{});
    w2_block = blocks.blocks.exchange(2, ::foundation::AlignedBytes{});

    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 100);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 0);

    auto const pv = ctx.pool().mint_initialized_view(kVigilForeground);
    const ::fixy::View<float const> x_floats =
        ::foundation::lifetime::start_as_array<float>(::fixy::as_const(x_block).view());
    const ::fixy::View<float const> w1_floats =
        ::foundation::lifetime::start_as_array<float>(::fixy::as_const(w1_block).view());
    const ::fixy::View<float const> w2_floats =
        ::foundation::lifetime::start_as_array<float>(::fixy::as_const(w2_block).view());

    ::fixy::FixedArray<float, BATCH * HIDDEN> const ref_mm1 =
        ::fixy::build_fixed_array<float, BATCH * HIDDEN>([&x_floats, &w1_floats](auto mm1) noexcept {
            cpu::mm(kTest, make_input<LeftFactor>(x_floats), make_input<RightFactor>(w1_floats), std::move(mm1),
                    make_dim<roles::Rows>(BATCH), make_dim<roles::Columns>(HIDDEN), make_dim<roles::Inner>(IN_DIM));
        });
    ::fixy::FixedArray<float, BATCH * HIDDEN> const ref_relu =
        ::fixy::build_fixed_array<float, BATCH * HIDDEN>([&ref_mm1](auto relu) noexcept {
            cpu::relu(kTest, ref_mm1.view(), std::move(relu), make_dim<roles::Count>(BATCH * HIDDEN));
        });
    ::fixy::FixedArray<float, BATCH * OUT_DIM> const ref_mm2 =
        ::fixy::build_fixed_array<float, BATCH * OUT_DIM>([&ref_relu, &w2_floats](auto mm2) noexcept {
            cpu::mm(kTest, make_input<LeftFactor>(ref_relu.view()), make_input<RightFactor>(w2_floats), std::move(mm2),
                    make_dim<roles::Rows>(BATCH), make_dim<roles::Columns>(OUT_DIM), make_dim<roles::Inner>(HIDDEN));
        });
    ::fixy::FixedArray<float, BATCH * OUT_DIM> const ref_sm =
        ::fixy::build_fixed_array<float, BATCH * OUT_DIM>([&ref_mm2](auto sm) noexcept {
            cpu::softmax(kTest, ref_mm2.view(), std::move(sm), make_dim<roles::Rows>(BATCH),
                         make_dim<roles::Width>(OUT_DIM));
        });

    const ::fixy::View<float const> pool_sm =
        ctx.pool().slot_view<float>(SlotId{SL_SM}, pv).expect("the softmax output is a pooled slot of floats");
    const ::fixy::View<float const> ref_floats = ref_sm.view();
    const auto pool_at = [&pool_sm](cpu::Offset position) noexcept {
        return *pool_sm.get(position.raw()).expect("the softmax output holds BATCH * OUT_DIM floats");
    };

    float max_err = 0.0f;
    for (auto [got, want] :
         pool_sm.walk().try_zip(ref_floats.walk()).expect("the two runs hold BATCH * OUT_DIM floats")) {
        float err = ::fixy::fp::abs(*got - *want);
        max_err = ::fixy::max(max_err, err);
    }

    ::fixy::report(::fixy::Sink::Out, "  max_error vs reference: {}\n", static_cast<double>(max_err));
    CRUCIBLE_TEST_ASSERT(kTest, max_err < 1e-5f && "softmax output diverged from reference");

    for (std::size_t b = 0; b < BATCH; b++) {
        float row_sum = 0.0f;
        for (std::size_t j = 0; j < OUT_DIM; j++) {
            float v = pool_at(cpu::Offset{b * OUT_DIM + j});
            CRUCIBLE_TEST_ASSERT(kTest, v >= 0.0f && v <= 1.0f);
            row_sum += v;
        }
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::fp::abs(row_sum - 1.0f) < 1e-5f && "softmax row doesn't sum to 1");
    }

    ::fixy::report(::fixy::Sink::Out, "  softmax[0] = [{}, {}, {}]\n", static_cast<double>(pool_at(cpu::Offset{0})),
                   static_cast<double>(pool_at(cpu::Offset{1})), static_cast<double>(pool_at(cpu::Offset{2})));
    ::fixy::report(::fixy::Sink::Out, "  softmax[1] = [{}, {}, {}]\n", static_cast<double>(pool_at(cpu::Offset{3})),
                   static_cast<double>(pool_at(cpu::Offset{4})), static_cast<double>(pool_at(cpu::Offset{5})));
    ::fixy::report(::fixy::Sink::Out, "  100 iterations, {} ops/iter, compiled_iters={}\n", N_OPS,
                   ctx.compiled_iterations());

    ::fixy::check::pass(kTest, "test_compute: PASSED\n");
    return 0;
}

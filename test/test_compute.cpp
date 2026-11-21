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
    const auto drawn = ::foundation::with_mut_windows<float>(block, windows, [](auto& views) noexcept {
        std::uint64_t draw = 0;
        for (auto& view : views) {
            for (float& element : view) {
                const ::fixy::Philox4x32::Counter word = ::fixy::Philox4x32::generate(draw, Key);
                element = crucible::Philox::to_uniform(word.at(::fixy::index<0>())) - 0.5f;
                ++draw;
            }
        }
    });
    CRUCIBLE_TEST_ASSERT(kTest, drawn.is_ok());
    return block;
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
    // that produces it to the last one that reads it.  The plan borrows the
    // slots.  The slots, the ops, their slot ids, their zero metadata and
    // their producers are arrays of this arena.
    ::fixy::BumpArena runs{size_t{1} << 16};
    ::fixy::View<TensorSlot, N_SLOTS> const slots =
        runs.array(test.alloc, N_SLOTS, TensorSlot{}).window<N_SLOTS>(0).expect("the run holds N_SLOTS slots");

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

    BackgroundThread bt{::foundation::effects::testing::test().alloc};
    crucible::MemoryPlan plan_storage{};
    ::fixy::BumpArena sweep_arena{size_t{1} << 16};
    crucible::MemoryPlan& plan = *bt.compute_memory_plan(test.alloc, plan_storage, sweep_arena, slots)
                                      .expect("the slots of the test fit in 64 bits");

    ::fixy::report(::fixy::Sink::Out, "  plan: pool={} bytes, {} slots ({} external)\n",
                   static_cast<unsigned long>(plan.pool_bytes), plan.num_slots(), plan.num_external);

    ::fixy::View<SlotId> const op0_in = ::fixy::check::run_of(kTest, test.alloc, runs, SlotId{SL_X}, SlotId{SL_W1});
    ::fixy::View<SlotId> const op0_out = ::fixy::check::run_of(kTest, test.alloc, runs, SlotId{SL_MM1});

    ::fixy::View<SlotId> const op1_in = ::fixy::check::run_of(kTest, test.alloc, runs, SlotId{SL_MM1});
    ::fixy::View<SlotId> const op1_out = ::fixy::check::run_of(kTest, test.alloc, runs, SlotId{SL_REL});

    ::fixy::View<SlotId> const op2_in = ::fixy::check::run_of(kTest, test.alloc, runs, SlotId{SL_REL}, SlotId{SL_W2});
    ::fixy::View<SlotId> const op2_out = ::fixy::check::run_of(kTest, test.alloc, runs, SlotId{SL_MM2});

    ::fixy::View<SlotId> const op3_in = ::fixy::check::run_of(kTest, test.alloc, runs, SlotId{SL_MM2});
    ::fixy::View<SlotId> const op3_out = ::fixy::check::run_of(kTest, test.alloc, runs, SlotId{SL_SM});

    ::fixy::View<TraceEntry, N_OPS> const ops =
        runs.array(test.alloc, N_OPS, TraceEntry{}).window<N_OPS>(0).expect("the run holds N_OPS ops");

    ops.at(::fixy::index<0>()).schema_hash = H_MM1;
    ops.at(::fixy::index<0>()).shape_hash = S_MM1;
    crucible::test::set_input_slots(test.alloc, runs, ops.at(::fixy::index<0>()), op0_in);
    crucible::test::set_output_slots(test.alloc, runs, ops.at(::fixy::index<0>()), op0_out);

    ops.at(::fixy::index<1>()).schema_hash = H_RELU;
    ops.at(::fixy::index<1>()).shape_hash = S_RELU;
    crucible::test::set_input_slots(test.alloc, runs, ops.at(::fixy::index<1>()), op1_in);
    crucible::test::set_output_slots(test.alloc, runs, ops.at(::fixy::index<1>()), op1_out);

    ops.at(::fixy::index<2>()).schema_hash = H_MM2;
    ops.at(::fixy::index<2>()).shape_hash = S_MM2;
    crucible::test::set_input_slots(test.alloc, runs, ops.at(::fixy::index<2>()), op2_in);
    crucible::test::set_output_slots(test.alloc, runs, ops.at(::fixy::index<2>()), op2_out);

    ops.at(::fixy::index<3>()).schema_hash = H_SOFTMAX;
    ops.at(::fixy::index<3>()).shape_hash = S_SOFTMAX;
    crucible::test::set_input_slots(test.alloc, runs, ops.at(::fixy::index<3>()), op3_in);
    crucible::test::set_output_slots(test.alloc, runs, ops.at(::fixy::index<3>()), op3_out);

    RegionNode region{};
    region.kind = TraceNodeKind::REGION;
    region.ops = ops;
    region.plan = make_plan_link(plan);

    CrucibleContext ctx{::foundation::effects::testing::test().alloc};
    CRUCIBLE_TEST_ASSERT(kTest, ctx.activate(region));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.is_compiled());

    // The view is minted once, because the context stays compiled for
    // the whole loop.  Passing it to every call costs nothing at run
    // time and makes each call site state, in its own types, that it
    // is only reachable in compiled mode.
    auto cv = ctx.mint_compiled_view(kVigilForeground);

    ctx.register_external_view(SlotId{SL_X}, x_block.view(), cv).expect("the block holds the bytes of its slot");
    ctx.register_external_view(SlotId{SL_W1}, w1_block.view(), cv).expect("the block holds the bytes of its slot");
    ctx.register_external_view(SlotId{SL_W2}, w2_block.view(), cv).expect("the block holds the bytes of its slot");

    // A read-only view, so that even these checks reach the pool the
    // way the hot path does.
    auto pv = ctx.pool().mint_initialized_view(kVigilForeground);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.pool().slot_ptr(SlotId{SL_X}, pv) == ::fixy::mint_run_address(x_block.view(), 0));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.pool().slot_ptr(SlotId{SL_W1}, pv) == ::fixy::mint_run_address(w1_block.view(), 0));
    CRUCIBLE_TEST_ASSERT(kTest, ctx.pool().slot_ptr(SlotId{SL_W2}, pv) == ::fixy::mint_run_address(w2_block.view(), 0));

    for (int iter = 0; iter < 100; iter++) {
        // The trailing names below say which slot each index reaches,
        // which the index alone does not.
        auto s = ctx.advance(H_MM1, S_MM1, cv).status();
        CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
        write_output(kTest, ctx, cv, [&ctx, &cv](::fixy::View<float> mm1_out) noexcept {
            cpu::mm(kTest, make_input<LeftFactor>(read_input(ctx, cv, InputPosition{0})),  // X
                    make_input<RightFactor>(read_input(ctx, cv, InputPosition{1})),  // W1
                    mm1_out, make_dim<roles::Rows>(BATCH), make_dim<roles::Columns>(HIDDEN),
                    make_dim<roles::Inner>(IN_DIM));
        });

        s = ctx.advance(H_RELU, S_RELU, cv).status();
        CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
        write_output(kTest, ctx, cv, [&ctx, &cv](::fixy::View<float> relu_out) noexcept {
            cpu::relu(kTest, read_input(ctx, cv, InputPosition{0}),  // mm1_out
                      relu_out, make_dim<roles::Count>(BATCH * HIDDEN));
        });

        s = ctx.advance(H_MM2, S_MM2, cv).status();
        CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::MATCH);
        write_output(kTest, ctx, cv, [&ctx, &cv](::fixy::View<float> mm2_out) noexcept {
            cpu::mm(kTest, make_input<LeftFactor>(read_input(ctx, cv, InputPosition{0})),  // relu_out
                    make_input<RightFactor>(read_input(ctx, cv, InputPosition{1})),  // W2
                    mm2_out, make_dim<roles::Rows>(BATCH), make_dim<roles::Columns>(OUT_DIM),
                    make_dim<roles::Inner>(HIDDEN));
        });

        // The last operation of an iteration completes it rather
        // than matching.
        s = ctx.advance(H_SOFTMAX, S_SOFTMAX, cv).status();
        CRUCIBLE_TEST_ASSERT(kTest, s == ReplayStatus::COMPLETE);
        write_output(kTest, ctx, cv, [&ctx, &cv](::fixy::View<float> softmax_out) noexcept {
            cpu::softmax(kTest, read_input(ctx, cv, InputPosition{0}),  // mm2_out
                         softmax_out, make_dim<roles::Rows>(BATCH), make_dim<roles::Width>(OUT_DIM));
        });
    }

    CRUCIBLE_TEST_ASSERT(kTest, ctx.compiled_iterations() == 100);
    CRUCIBLE_TEST_ASSERT(kTest, ctx.diverged_count() == 0);

    ::fixy::FixedArray<float, BATCH * HIDDEN> ref_mm1{};
    ::fixy::FixedArray<float, BATCH * HIDDEN> ref_relu{};
    ::fixy::FixedArray<float, BATCH * OUT_DIM> ref_mm2{};
    ::fixy::FixedArray<float, BATCH * OUT_DIM> ref_sm{};
    const ::fixy::View<float const> x_floats = ctx.pool().slot_view<float>(SlotId{SL_X}, pv).expect("X is registered");
    const ::fixy::View<float const> w1_floats =
        ctx.pool().slot_view<float>(SlotId{SL_W1}, pv).expect("W1 is registered");
    const ::fixy::View<float const> w2_floats =
        ctx.pool().slot_view<float>(SlotId{SL_W2}, pv).expect("W2 is registered");

    ref_mm1.with_mut([&x_floats, &w1_floats](auto mm1) noexcept {
        cpu::mm(kTest, make_input<LeftFactor>(x_floats), make_input<RightFactor>(w1_floats), mm1.view(),
                make_dim<roles::Rows>(BATCH), make_dim<roles::Columns>(HIDDEN), make_dim<roles::Inner>(IN_DIM));
    });
    ref_relu.with_mut([&ref_mm1](auto relu) noexcept {
        cpu::relu(kTest, ref_mm1.view(), relu.view(), make_dim<roles::Count>(BATCH * HIDDEN));
    });
    ref_mm2.with_mut([&ref_relu, &w2_floats](auto mm2) noexcept {
        cpu::mm(kTest, make_input<LeftFactor>(ref_relu.view()), make_input<RightFactor>(w2_floats), mm2.view(),
                make_dim<roles::Rows>(BATCH), make_dim<roles::Columns>(OUT_DIM), make_dim<roles::Inner>(HIDDEN));
    });
    ref_sm.with_mut([&ref_mm2](auto sm) noexcept {
        cpu::softmax(kTest, ref_mm2.view(), sm.view(), make_dim<roles::Rows>(BATCH), make_dim<roles::Width>(OUT_DIM));
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

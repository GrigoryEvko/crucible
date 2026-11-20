#include <crucible/CostModel.h>

#include <fixy/Refined.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;
namespace eff = ::foundation::effects;

// The cost model admits a context that claims no effect.  The test runs on
// the foreground context, whose row is empty.
static constexpr auto pure_ctx = eff::testing::foreground();

[[nodiscard]] static constexpr ValidRegsPerThread regs(uint16_t count) {
    return ::fixy::mint_refined<valid_regs_per_thread>(count);
}

// The preset figures are transcribed from published vendor numbers, so a
// one-percent band catches a transcription error without failing on the
// rounding in those numbers.
static bool approx(float actual, float expected, float tol = 0.01f) {
    if (std::fabs(expected) < 1e-9f) return std::fabs(actual) < tol;
    return std::fabs(actual - expected) / std::fabs(expected) < tol;
}

static void test_preset_sanity() {
    auto b200 = blackwell_b200();
    CRUCIBLE_TEST_ASSERT(kTest, b200.num_sms == 128);
    CRUCIBLE_TEST_ASSERT(kTest, b200.warp_size.value() == 32);
    CRUCIBLE_TEST_ASSERT(kTest, b200.max_warps_per_sm == 64);
    CRUCIBLE_TEST_ASSERT(kTest, b200.max_threads_per_sm() == 32 * 64);
    CRUCIBLE_TEST_ASSERT(kTest, b200.total_threads() == 128ULL * 32 * 64);
    // The rates of the presets are whole numbers below 2^53, so a double
    // holds each one exactly.
    CRUCIBLE_FATAL_INVARIANT(b200.peak_fp16 == FlopRate{1.125e15});
    CRUCIBLE_FATAL_INVARIANT(b200.hbm_bw == ByteRate{8.0e12});

    auto h100 = hopper_h100();
    CRUCIBLE_TEST_ASSERT(kTest, h100.num_sms == 132);
    CRUCIBLE_FATAL_INVARIANT(h100.peak_fp16 == FlopRate{9.9e14});
    CRUCIBLE_FATAL_INVARIANT(h100.hbm_bw == ByteRate{3.35e12});
    CRUCIBLE_TEST_ASSERT(kTest, h100.sm_version == 90);

    auto mi300 = mi300x();
    CRUCIBLE_TEST_ASSERT(kTest, mi300.warp_size.value() == 64);  // AMD wavefront
    CRUCIBLE_FATAL_INVARIANT(mi300.peak_fp16 == FlopRate{1.3e15});

    auto a100 = ampere_a100();
    CRUCIBLE_TEST_ASSERT(kTest, a100.num_sms == 108);
    CRUCIBLE_FATAL_INVARIANT(a100.peak_fp8 == FlopRate{0.0});  // A100 predates FP8

    ::fixy::check::pass(kTest, "  test_preset_sanity:             PASSED\n");
}

// The bits of a double, so a test compares two numbers bit for bit.
[[nodiscard]] static std::uint64_t bits_of_number(double value) { return ::fixy::bit_cast<std::uint64_t>(value); }

static void test_ridge_point() {
    auto hw = blackwell_b200();
    // The ridge is the peak over the bandwidth, in flops per byte.
    // 1.125 * 10^15 / (8 * 10^12) = 140.625, which a double holds exactly.
    CRUCIBLE_FATAL_INVARIANT(bits_of_number(hw.compute_ridge_point(ScalarType::Half).raw()) == bits_of_number(140.625));
    // 9 * 10^13 / (8 * 10^12) = 11.25, exactly.
    CRUCIBLE_FATAL_INVARIANT(bits_of_number(hw.compute_ridge_point(ScalarType::Float).raw()) == bits_of_number(11.25));

    // 9.9 * 10^14 / (3.35 * 10^12) is about 295.5.  The ridge sits much
    // higher here because this part has less bandwidth relative to its peak.
    auto h100 = hopper_h100();
    CRUCIBLE_FATAL_INVARIANT(approx(static_cast<float>(h100.compute_ridge_point(ScalarType::Half).raw()), 295.52f));

    // A part with no bandwidth has the ridge 0.
    CRUCIBLE_FATAL_INVARIANT(HardwareProfile{}.compute_ridge_point(ScalarType::Half) == FlopsPerByte{0.0});

    ::fixy::check::pass(kTest, "  test_ridge_point:               PASSED\n");
}

// Each FP8 type is rated at the FP8 peak, and the FP4 type at the FP4 peak.
// The old dispatch named four FP8 types and sent Float8_e8m0fnu and
// Float4_e2m1fn_x2 to its default arm, the FP32 peak.
static void test_peak_of_each_narrow_type() {
    auto const hw = blackwell_b200();
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::Float8_e5m2) == hw.peak_fp8);
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::Float8_e4m3fn) == hw.peak_fp8);
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::Float8_e5m2fnuz) == hw.peak_fp8);
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::Float8_e4m3fnuz) == hw.peak_fp8);
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::Float8_e8m0fnu) == hw.peak_fp8
                                    && "the scale type of a block-scaled format is rated at the FP8 peak");
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::Float4_e2m1fn_x2) == hw.peak_fp4
                                    && "the FP4 type is rated at the FP4 peak");
    ::fixy::check::pass(kTest, "  test_peak_of_each_narrow_type:  PASSED\n");
}

// Each other type has its own arm: an 8-bit integer at the INT8 peak, a
// complex type at the peak of its real type, a wider integer and a bool at
// the FP32 peak, and no type at no rate.
static void test_peak_of_each_other_type() {
    auto const hw = blackwell_b200();
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::Byte) == hw.peak_int8);
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::Char) == hw.peak_int8);
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::ComplexHalf) == hw.peak_fp16);
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::ComplexFloat) == hw.peak_fp32);
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::ComplexDouble) == hw.peak_fp64);
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::Short) == hw.peak_fp32);
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::Int) == hw.peak_fp32);
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::Long) == hw.peak_fp32);
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::Bool) == hw.peak_fp32);
    CRUCIBLE_TEST_ASSERT(kTest, hw.select_peak_rate(ScalarType::Undefined) == FlopRate{0.0});
    ::fixy::check::pass(kTest, "  test_peak_of_each_other_type:   PASSED\n");
}

// evaluate_cost against the same formula on plain doubles, bit for bit.
// The memory time is the latency plus the bytes over the bandwidth per
// nanosecond, and the compute time is the flops over the peak per
// nanosecond times the two utilisations.
static void test_evaluate_cost() {
    auto const hw = blackwell_b200();
    KernelConfig const cfg{};
    CostBreakdown const cost =
        evaluate_cost(pure_ctx, FlopCount{1024}, ByteCount{2048}, ElementCount{4096}, ScalarType::Float, cfg, hw);
    double const occupancy = static_cast<double>(
        sm_occupancy(pure_ctx, cfg.regs_per_thread, cfg.smem_bytes, cfg.warps_per_block, hw).value());
    double const memory = 350.0 + 2048.0 / 8000.0;
    double const compute = 1024.0 / ((90000.0 * 1.0) * occupancy);
    CRUCIBLE_FATAL_INVARIANT(bits_of_number(cost.memory_ns.raw()) == bits_of_number(memory));
    CRUCIBLE_FATAL_INVARIANT(bits_of_number(cost.compute_ns.raw()) == bits_of_number(compute));
    CRUCIBLE_FATAL_INVARIANT(bits_of_number(cost.total_ns.raw()) == bits_of_number(memory + 3000.0));
    CRUCIBLE_FATAL_INVARIANT(bits_of_number(cost.arithmetic_intensity.raw()) == bits_of_number(0.5));
    CRUCIBLE_FATAL_INVARIANT(cost.flops == FlopCount{1024} && cost.bytes == ByteCount{2048});
    CRUCIBLE_FATAL_INVARIANT(cost.bottleneck == CostBreakdown::Bottleneck::LAUNCH);

    // No bytes give the intensity 0, and a part with no rate gives no time.
    CostBreakdown const empty = evaluate_cost(pure_ctx, FlopCount{1024}, ByteCount{0}, ElementCount{4096},
                                              ScalarType::Float, cfg, HardwareProfile{});
    CRUCIBLE_FATAL_INVARIANT(empty.arithmetic_intensity == FlopsPerByte{0.0});
    CRUCIBLE_FATAL_INVARIANT(empty.compute_ns == Nanoseconds{0.0} && empty.memory_ns == Nanoseconds{0.0});

    ::fixy::check::pass(kTest, "  test_evaluate_cost:             PASSED\n");
}

static void test_wave_efficiency() {
    auto hw = blackwell_b200();
    const uint64_t tpw = hw.num_sms * hw.warp_size.value();  // 128 × 32 = 4096

    // The return is a refined float, so building it already checks the
    // [0, 1] bound.  A formula that leaves that range aborts on
    // construction, before any assertion below is reached.
    CRUCIBLE_TEST_ASSERT(kTest, approx(wave_efficiency(pure_ctx, 0, hw).value(), 0.0f));
    CRUCIBLE_TEST_ASSERT(kTest, approx(wave_efficiency(pure_ctx, tpw, hw).value(), 1.0f));
    CRUCIBLE_TEST_ASSERT(kTest, approx(wave_efficiency(pure_ctx, tpw / 2, hw).value(), 0.5f));
    const float expect = static_cast<float>(tpw + 1) / static_cast<float>(2 * tpw);
    CRUCIBLE_TEST_ASSERT(kTest, approx(wave_efficiency(pure_ctx, tpw + 1, hw).value(), expect));
    ::fixy::check::pass(kTest, "  test_wave_efficiency:           PASSED\n");
}

static void test_sm_occupancy() {
    auto hw = blackwell_b200();

    // The return is refined the same way.  A numerator that escapes the
    // thread ceiling aborts here, at the construction site, rather than
    // travelling on as a plausible-looking ratio.
    //
    // 32 regs/thread → 65536 / 32 = 2048 threads = full occupancy.
    CRUCIBLE_TEST_ASSERT(kTest, approx(sm_occupancy(pure_ctx, regs(32), 0, 8, hw).value(), 1.0f));
    // 64 regs/thread → 1024 threads = 50% occupancy.
    CRUCIBLE_TEST_ASSERT(kTest, approx(sm_occupancy(pure_ctx, regs(64), 0, 8, hw).value(), 0.5f));
    // 128 regs/thread → 512 threads = 25% occupancy.
    CRUCIBLE_TEST_ASSERT(kTest, approx(sm_occupancy(pure_ctx, regs(128), 0, 8, hw).value(), 0.25f));
    // 255 is the type's ceiling: 65536 / 255 = 257.0…, rounded down to 256
    // threads at warp granularity.  The matrix stops here because a higher
    // register count cannot be constructed at all.
    CRUCIBLE_TEST_ASSERT(kTest, approx(sm_occupancy(pure_ctx, regs(255), 0, 8, hw).value(), 0.125f));

    ::fixy::check::pass(kTest, "  test_sm_occupancy:              PASSED\n");
}

static void test_validate_config() {
    auto hw = blackwell_b200();

    KernelConfig ok{};
    ok.tile_m = 128;
    ok.tile_n = 128;
    ok.tile_k = 32;
    ok.pipeline_stages = 3;
    ok.warps_per_block = 8;
    ok.smem_bytes = 64 * 1024;
    ok.regs_per_thread = regs(64);
    ok.vec_width = 4;
    CRUCIBLE_TEST_ASSERT(kTest, validate_config(ok, hw));

    KernelConfig bad_smem = ok;
    bad_smem.smem_bytes = 1024 * 1024;  // 1 MB, over the 228 KB the part has
    CRUCIBLE_TEST_ASSERT(kTest, !validate_config(bad_smem, hw));

    KernelConfig no_warps = ok;
    no_warps.warps_per_block = 0;
    CRUCIBLE_TEST_ASSERT(kTest, !validate_config(no_warps, hw));

    KernelConfig too_wide = ok;
    too_wide.warps_per_block = 64;  // 64 × 32 = 2048 threads, over the 1024 cap
    CRUCIBLE_TEST_ASSERT(kTest, !validate_config(too_wide, hw));

    KernelConfig zero_tile = ok;
    zero_tile.tile_m = 0;
    CRUCIBLE_TEST_ASSERT(kTest, !validate_config(zero_tile, hw));

    KernelConfig deep = ok;
    deep.pipeline_stages = 8;
    CRUCIBLE_TEST_ASSERT(kTest, !validate_config(deep, hw));
    deep.pipeline_stages = 0;
    CRUCIBLE_TEST_ASSERT(kTest, !validate_config(deep, hw));

    ::fixy::check::pass(kTest, "  test_validate_config:           PASSED\n");
}

static void test_fusion_benefit() {
    // Two kernels of 5 µs each fuse into one of 6 µs once the round trip
    // through memory between them is gone: 4 µs saved, 10/6 speedup.
    auto fb = compute_fusion_benefit(pure_ctx,
                                     /*unfused_ns=*/10000.0,
                                     /*fused_ns=*/6000.0,
                                     /*saved_bytes=*/1024000,
                                     /*saved_launches=*/1);
    CRUCIBLE_TEST_ASSERT(kTest, approx(static_cast<float>(fb.saved_ns), 4000.0f));
    CRUCIBLE_TEST_ASSERT(kTest, approx(fb.speedup, 10.0f / 6.0f));
    CRUCIBLE_TEST_ASSERT(kTest, fb.saved_launches == 1);
    ::fixy::check::pass(kTest, "  test_fusion_benefit:            PASSED\n");
}

// One concept for each evaluator, so each gate is read on its own.
template <class Ctx>
concept TakesWaveEfficiency =
    requires(Ctx const& ctx, HardwareProfile const& hw) { wave_efficiency(ctx, uint64_t{0}, hw); };
template <class Ctx>
concept TakesSmOccupancy = requires(Ctx const& ctx, HardwareProfile const& hw, ValidRegsPerThread r) {
    sm_occupancy(ctx, r, uint32_t{0}, uint16_t{8}, hw);
};
template <class Ctx>
concept TakesEvaluateCost = requires(Ctx const& ctx, KernelConfig const& cfg, HardwareProfile const& hw) {
    evaluate_cost(ctx, FlopCount{1}, ByteCount{1}, ElementCount{1}, ScalarType::Float, cfg, hw);
};
template <class Ctx>
concept TakesEvaluateCostDefault = requires(Ctx const& ctx, HardwareProfile const& hw) {
    evaluate_cost(ctx, FlopCount{1}, ByteCount{1}, ElementCount{1}, ScalarType::Float, hw);
};
template <class Ctx>
concept TakesFusionBenefit = requires(Ctx const& ctx) { compute_fusion_benefit(ctx, 10.0, 5.0, uint64_t{64}, 1u); };

template <class Ctx>
concept TakesEveryEvaluator = TakesWaveEfficiency<Ctx> && TakesSmOccupancy<Ctx> && TakesEvaluateCost<Ctx>
                           && TakesEvaluateCostDefault<Ctx> && TakesFusionBenefit<Ctx>;

template <class Ctx>
concept TakesAnyEvaluator = TakesWaveEfficiency<Ctx> || TakesSmOccupancy<Ctx> || TakesEvaluateCost<Ctx>
                         || TakesEvaluateCostDefault<Ctx> || TakesFusionBenefit<Ctx>;

using BgIoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO>>;
using AllocOnlyCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Alloc>>;
using NarrowedBgCtx = eff::ExecCtx<eff::Bg, eff::Row<>>;

static_assert(CtxFitsCostModel<std::remove_cvref_t<decltype(pure_ctx)>> && CtxFitsCostModel<NarrowedBgCtx>);
static_assert(!CtxFitsCostModel<BgIoCtx> && !CtxFitsCostModel<AllocOnlyCtx> && !CtxFitsCostModel<int>);
static_assert(TakesEveryEvaluator<std::remove_cvref_t<decltype(pure_ctx)>> && TakesEveryEvaluator<NarrowedBgCtx>);
static_assert(!TakesAnyEvaluator<BgIoCtx> && !TakesAnyEvaluator<AllocOnlyCtx>);

static void test_pure_context_gate() {
    // A background context with a wider row reaches the evaluators only
    // after it narrows to the empty row.
    const BgIoCtx bg_io{eff::testing::bg()};
    const NarrowedBgCtx narrowed = bg_io.in_row<eff::Row<>>();

    auto hw = blackwell_b200();
    KernelConfig cfg{};
    CRUCIBLE_TEST_ASSERT(kTest, approx(wave_efficiency(narrowed, 0, hw).value(), 0.0f));
    CRUCIBLE_TEST_ASSERT(
        kTest,
        approx(sm_occupancy(narrowed, cfg.regs_per_thread, cfg.smem_bytes, cfg.warps_per_block, hw).value(), 0.5f));
    const CostBreakdown explicit_cfg =
        evaluate_cost(narrowed, FlopCount{1024}, ByteCount{2048}, ElementCount{4096}, ScalarType::Float, cfg, hw);
    const CostBreakdown default_cfg =
        evaluate_cost(narrowed, FlopCount{1024}, ByteCount{2048}, ElementCount{4096}, ScalarType::Float, hw);
    CRUCIBLE_FATAL_INVARIANT(bits_of_number(explicit_cfg.total_ns.raw()) == bits_of_number(default_cfg.total_ns.raw()));
    CRUCIBLE_TEST_ASSERT(kTest, compute_fusion_benefit(narrowed, 10000.0, 6000.0, 1024000, 1).saved_launches == 1);

    ::fixy::check::pass(kTest, "  test_pure_context_gate:         PASSED\n");
}

int main() {
    test_preset_sanity();
    test_ridge_point();
    test_peak_of_each_narrow_type();
    test_peak_of_each_other_type();
    test_evaluate_cost();
    test_wave_efficiency();
    test_sm_occupancy();
    test_validate_config();
    test_fusion_benefit();
    test_pure_context_gate();
    ::fixy::check::pass(kTest, "test_cost_model: 8 groups, all passed\n");
    return 0;
}

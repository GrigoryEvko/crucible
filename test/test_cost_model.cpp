#include <crucible/CostModel.h>

#include <fixy/Refined.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include "test_assert.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <type_traits>

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
    assert(b200.num_sms == 128);
    assert(b200.warp_size.value() == 32);
    assert(b200.max_warps_per_sm == 64);
    assert(b200.max_threads_per_sm() == 32 * 64);
    assert(b200.total_threads() == 128ULL * 32 * 64);
    assert(approx(b200.peak_fp16, 1125.0f));
    assert(approx(b200.hbm_bw, 8000.0f));

    auto h100 = hopper_h100();
    assert(h100.num_sms == 132);
    assert(approx(h100.peak_fp16, 990.0f));
    assert(approx(h100.hbm_bw, 3350.0f));
    assert(h100.sm_version == 90);

    auto mi300 = mi300x();
    assert(mi300.warp_size.value() == 64);  // AMD wavefront
    assert(approx(mi300.peak_fp16, 1300.0f));

    auto a100 = ampere_a100();
    assert(a100.num_sms == 108);
    assert(approx(a100.peak_fp8, 0.0f));  // A100 predates FP8

    std::printf("  test_preset_sanity:             PASSED\n");
}

static void test_ridge_point() {
    auto hw = blackwell_b200();
    // Ridge is (peak_tflops × 1000) / hbm_bw, in FLOP per byte.
    // (1125 × 1000) / 8000 = 140.625
    assert(approx(hw.ridge_point(ScalarType::Half), 140.625f));
    // (90 × 1000) / 8000 = 11.25
    assert(approx(hw.ridge_point(ScalarType::Float), 11.25f));

    // (990 × 1000) / 3350 is about 295.5.  The ridge sits much higher here
    // because this part has less bandwidth relative to its peak.
    auto h100 = hopper_h100();
    assert(approx(h100.ridge_point(ScalarType::Half), 295.52f));

    std::printf("  test_ridge_point:               PASSED\n");
}

static void test_wave_efficiency() {
    auto hw = blackwell_b200();
    const uint64_t tpw = hw.num_sms * hw.warp_size.value();  // 128 × 32 = 4096

    // The return is a refined float, so building it already checks the
    // [0, 1] bound.  A formula that leaves that range aborts on
    // construction, before any assertion below is reached.
    assert(approx(wave_efficiency(pure_ctx, 0, hw).value(), 0.0f));
    assert(approx(wave_efficiency(pure_ctx, tpw, hw).value(), 1.0f));
    assert(approx(wave_efficiency(pure_ctx, tpw / 2, hw).value(), 0.5f));
    const float expect = static_cast<float>(tpw + 1) / static_cast<float>(2 * tpw);
    assert(approx(wave_efficiency(pure_ctx, tpw + 1, hw).value(), expect));
    std::printf("  test_wave_efficiency:           PASSED\n");
}

static void test_sm_occupancy() {
    auto hw = blackwell_b200();

    // The return is refined the same way.  A numerator that escapes the
    // thread ceiling aborts here, at the construction site, rather than
    // travelling on as a plausible-looking ratio.
    //
    // 32 regs/thread → 65536 / 32 = 2048 threads = full occupancy.
    assert(approx(sm_occupancy(pure_ctx, regs(32), 0, 8, hw).value(), 1.0f));
    // 64 regs/thread → 1024 threads = 50% occupancy.
    assert(approx(sm_occupancy(pure_ctx, regs(64), 0, 8, hw).value(), 0.5f));
    // 128 regs/thread → 512 threads = 25% occupancy.
    assert(approx(sm_occupancy(pure_ctx, regs(128), 0, 8, hw).value(), 0.25f));
    // 255 is the type's ceiling: 65536 / 255 = 257.0…, rounded down to 256
    // threads at warp granularity.  The matrix stops here because a higher
    // register count cannot be constructed at all.
    assert(approx(sm_occupancy(pure_ctx, regs(255), 0, 8, hw).value(), 0.125f));

    std::printf("  test_sm_occupancy:              PASSED\n");
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
    assert(validate_config(ok, hw));

    KernelConfig bad_smem = ok;
    bad_smem.smem_bytes = 1024 * 1024;  // 1 MB, over the 228 KB the part has
    assert(!validate_config(bad_smem, hw));

    KernelConfig no_warps = ok;
    no_warps.warps_per_block = 0;
    assert(!validate_config(no_warps, hw));

    KernelConfig too_wide = ok;
    too_wide.warps_per_block = 64;  // 64 × 32 = 2048 threads, over the 1024 cap
    assert(!validate_config(too_wide, hw));

    KernelConfig zero_tile = ok;
    zero_tile.tile_m = 0;
    assert(!validate_config(zero_tile, hw));

    KernelConfig deep = ok;
    deep.pipeline_stages = 8;
    assert(!validate_config(deep, hw));
    deep.pipeline_stages = 0;
    assert(!validate_config(deep, hw));

    std::printf("  test_validate_config:           PASSED\n");
}

static void test_fusion_benefit() {
    // Two kernels of 5 µs each fuse into one of 6 µs once the round trip
    // through memory between them is gone: 4 µs saved, 10/6 speedup.
    auto fb = compute_fusion_benefit(
        pure_ctx,
        /*unfused_ns=*/10000.0,
        /*fused_ns=*/6000.0,
        /*saved_bytes=*/1024000,
        /*saved_launches=*/1);
    assert(approx(static_cast<float>(fb.saved_ns), 4000.0f));
    assert(approx(fb.speedup, 10.0f / 6.0f));
    assert(fb.saved_launches == 1);
    std::printf("  test_fusion_benefit:            PASSED\n");
}

// One concept for each evaluator, so each gate is read on its own.
template <class Ctx>
concept TakesWaveEfficiency = requires(Ctx const& ctx, HardwareProfile const& hw) {
    wave_efficiency(ctx, uint64_t{0}, hw);
};
template <class Ctx>
concept TakesSmOccupancy = requires(Ctx const& ctx, HardwareProfile const& hw, ValidRegsPerThread r) {
    sm_occupancy(ctx, r, uint32_t{0}, uint16_t{8}, hw);
};
template <class Ctx>
concept TakesEvaluateCost = requires(Ctx const& ctx, KernelConfig const& cfg, HardwareProfile const& hw) {
    evaluate_cost(ctx, uint64_t{1}, uint64_t{1}, uint64_t{1}, ScalarType::Float, cfg, hw);
};
template <class Ctx>
concept TakesEvaluateCostDefault = requires(Ctx const& ctx, HardwareProfile const& hw) {
    evaluate_cost(ctx, uint64_t{1}, uint64_t{1}, uint64_t{1}, ScalarType::Float, hw);
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
    assert(approx(wave_efficiency(narrowed, 0, hw).value(), 0.0f));
    assert(approx(sm_occupancy(narrowed, cfg.regs_per_thread, cfg.smem_bytes, cfg.warps_per_block, hw).value(), 0.5f));
    const CostBreakdown explicit_cfg = evaluate_cost(narrowed, 1024, 2048, 4096, ScalarType::Float, cfg, hw);
    const CostBreakdown default_cfg = evaluate_cost(narrowed, 1024, 2048, 4096, ScalarType::Float, hw);
    assert(approx(static_cast<float>(explicit_cfg.total_ns), static_cast<float>(default_cfg.total_ns)));
    assert(compute_fusion_benefit(narrowed, 10000.0, 6000.0, 1024000, 1).saved_launches == 1);

    std::printf("  test_pure_context_gate:         PASSED\n");
}

int main() {
    test_preset_sanity();
    test_ridge_point();
    test_wave_efficiency();
    test_sm_occupancy();
    test_validate_config();
    test_fusion_benefit();
    test_pure_context_gate();
    std::printf("test_cost_model: 7 groups, all passed\n");
    return 0;
}

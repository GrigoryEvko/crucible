// Tests for CostModel.h — roofline math + preset sanity + constraints.
//
// Validates:
//   - Preset hardware profiles match vendor specs within 1%.
//   - Ridge-point formula (peak_tflops * 1e3 / hbm_bw) — FLOP/byte.
//   - Wave efficiency: zero elements → 0; exactly filled → 1.0.
//   - SM occupancy: register-limited vs shared-memory-limited.
//   - validate_config rejects invalid kernel configs.
//   - Fusion benefit arithmetic.

#include <crucible/CostModel.h>

#include "test_assert.h"

#include <cmath>
#include <cstdio>

using namespace crucible;

// Within-1% tolerance check for float equality.
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
    // Ridge = (peak_tflops × 1000) / hbm_bw
    // B200 FP16: (1125 × 1000) / 8000 = 140.625 FLOP/byte
    assert(approx(hw.ridge_point(ScalarType::Half), 140.625f));
    // B200 FP32: (90 × 1000) / 8000 = 11.25 FLOP/byte
    assert(approx(hw.ridge_point(ScalarType::Float), 11.25f));

    // H100 FP16: (990 × 1000) / 3350 ≈ 295.5 FLOP/byte — memory-bound floor
    // is much higher because H100 has less HBM bandwidth relative to peak.
    auto h100 = hopper_h100();
    assert(approx(h100.ridge_point(ScalarType::Half), 295.52f));

    std::printf("  test_ridge_point:               PASSED\n");
}

static void test_wave_efficiency() {
    auto hw = blackwell_b200();
    const uint64_t tpw = hw.num_sms * hw.warp_size.value();  // 128 × 32 = 4096

    // Zero elements → 0.
    assert(approx(wave_efficiency(0, hw), 0.0f));
    // Exactly one wave filled → 1.0.
    assert(approx(wave_efficiency(tpw, hw), 1.0f));
    // Half a wave → 0.5.
    assert(approx(wave_efficiency(tpw / 2, hw), 0.5f));
    // N + 1 elements → (N+1) / (2 × tpw).
    const float expect = static_cast<float>(tpw + 1)
                       / static_cast<float>(2 * tpw);
    assert(approx(wave_efficiency(tpw + 1, hw), expect));
    std::printf("  test_wave_efficiency:           PASSED\n");
}

static void test_sm_occupancy() {
    auto hw = blackwell_b200();

    // 32 regs/thread → 65536 / 32 = 2048 threads = full occupancy.
    assert(approx(sm_occupancy(ValidRegsPerThread{uint16_t{32}}, 0, 8, hw), 1.0f));
    // 64 regs/thread → 1024 threads = 50% occupancy.
    assert(approx(sm_occupancy(ValidRegsPerThread{uint16_t{64}}, 0, 8, hw), 0.5f));
    // 128 regs/thread → 512 threads = 25% occupancy.
    assert(approx(sm_occupancy(ValidRegsPerThread{uint16_t{128}}, 0, 8, hw), 0.25f));
    // Boundary edge: 255 = ValidRegsPerThread cap, computes
    // 65536 / 255 = 257.0… → 256 threads after warp-granularity
    // round-down (32-thread warp width).  Confirms the type-system
    // ceiling is reachable without contract violation and that the
    // arithmetic at the edge produces the expected occupancy.
    assert(approx(sm_occupancy(ValidRegsPerThread{uint16_t{255}}, 0, 8, hw), 0.125f));

    // Per WRAP-CostModel-3 (#897 + audit follow-up), the formerly tested
    // case `sm_occupancy(256, ...)` is structurally impossible — the
    // ValidRegsPerThread ctor's `pre(bounded_above<255>(v))` rejects 256
    // at construction (semantic=enforce → handle_contract_violation; in
    // constexpr → ill-formed per P1494R5).  See companion HS14 fixtures
    // test/safety_neg/neg_costmodel_regs_per_thread_overflow.cpp and
    // test/safety_neg/neg_costmodel_regs_per_thread_max_uint16.cpp for
    // the witnesses that the type system rejects 256 and UINT16_MAX
    // respectively.

    std::printf("  test_sm_occupancy:              PASSED\n");
}

static void test_validate_config() {
    auto hw = blackwell_b200();

    KernelConfig ok{};
    ok.tile_m = 128; ok.tile_n = 128; ok.tile_k = 32;
    ok.pipeline_stages = 3; ok.warps_per_block = 8;
    ok.smem_bytes = 64 * 1024; ok.regs_per_thread = ValidRegsPerThread{uint16_t{64}}; ok.vec_width = 4;
    assert(validate_config(ok, hw));

    // C1: smem too large.
    KernelConfig bad_smem = ok;
    bad_smem.smem_bytes = 1024 * 1024;  // 1 MB > 228 KB
    assert(!validate_config(bad_smem, hw));

    // C3: zero warps.
    KernelConfig no_warps = ok;
    no_warps.warps_per_block = 0;
    assert(!validate_config(no_warps, hw));

    // C4: > 1024 threads.
    KernelConfig too_wide = ok;
    too_wide.warps_per_block = 64;  // 64 × 32 = 2048 threads > 1024
    assert(!validate_config(too_wide, hw));

    // C5–C7: zero tile.
    KernelConfig zero_tile = ok;
    zero_tile.tile_m = 0;
    assert(!validate_config(zero_tile, hw));

    // C8: pipeline depth out of range.
    KernelConfig deep = ok;
    deep.pipeline_stages = 8;
    assert(!validate_config(deep, hw));
    deep.pipeline_stages = 0;
    assert(!validate_config(deep, hw));

    std::printf("  test_validate_config:           PASSED\n");
}

static void test_fusion_benefit() {
    // Unfused: two kernels, 5 µs each = 10 µs.  Fused: 6 µs (no HBM
    // round-trip between them).  Should report 4 µs saved, 1.67× speedup.
    auto fb = compute_fusion_benefit(
        /*unfused_ns=*/10'000.0,
        /*fused_ns=*/  6'000.0,
        /*saved_bytes=*/1'024'000,
        /*saved_launches=*/1);
    assert(approx(static_cast<float>(fb.saved_ns), 4'000.0f));
    assert(approx(fb.speedup, 10.0f / 6.0f));
    assert(fb.saved_launches == 1);
    std::printf("  test_fusion_benefit:            PASSED\n");
}

int main() {
    test_preset_sanity();
    test_ridge_point();
    test_wave_efficiency();
    test_sm_occupancy();
    test_validate_config();
    test_fusion_benefit();
    std::printf("test_cost_model: 6 groups, all passed\n");
    return 0;
}

#pragma once

// The units are chosen so that no formula here carries a conversion factor.
// Bandwidth is in GB/s, which is identically bytes per nanosecond, so a byte
// count divided by a bandwidth is already a time. Throughput is in TFLOPS,
// which is FLOPs per nanosecond scaled by 1e3. Every time is nanoseconds and
// every size is bytes.

#include <crucible/Types.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/fixy/Wrap.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace crucible {

// 255 is the per-thread register ceiling on every shipped backend, so a value
// past it names no real hardware.
using ValidRegsPerThread = fixy::wrap::Refined<fixy::wrap::bounded_above<uint16_t{255}>, uint16_t>;
static_assert(sizeof(ValidRegsPerThread) == sizeof(uint16_t),
              "ValidRegsPerThread must be the same size as the value it wraps");

// The number of threads a scheduler dispatches together: a warp, a wavefront,
// or a sub-group depending on the vendor.
//
// It is always a power of two because schedulers fan lanes out through
// bit-mask shifts on per-lane predicate registers, and no documented ISA can
// represent any other width. The widest shipped width is 64, and 128 leaves
// one doubling of headroom. Past that the per-warp register pressure is
// intractable on any plausible silicon.
using ValidWarpSize =
    fixy::wrap::Refined<fixy::wrap::all_of<fixy::wrap::power_of_two, fixy::wrap::bounded_above<uint16_t{128}>>,
                        uint16_t>;
static_assert(sizeof(ValidWarpSize) == sizeof(uint16_t), "ValidWarpSize must be the same size as the value it wraps");

// A dimensionless ratio that both producers below construct in [0, 1] by
// construction, and that the two stored fields adopt so the bound survives
// storage. A value outside the range would put a kernel in an occupancy bucket
// that does not exist, or drive a predicted time past the hardware peak, so
// the bound is checked once where the value is made and assumed everywhere
// after.
using ValidUtilization = fixy::wrap::Refined<fixy::wrap::in_range<0.0f, 1.0f>, float>;
static_assert(sizeof(ValidUtilization) == sizeof(float),
              "ValidUtilization must be the same size as the value it wraps");

// Nominal figures from vendor specifications. Calibration replaces them with
// measured values at startup, so nothing here is authoritative at run time.
// Fields run from the fastest level of the memory hierarchy to the slowest.

struct HardwareProfile {
    uint32_t num_sms = 0;  // streaming multiprocessors, or compute units on AMD
    ValidWarpSize warp_size{uint16_t{32}};
    uint16_t max_warps_per_sm = 64;

    uint32_t regs_per_sm = 65536;  // 32-bit registers

    // The receiving end of the register check below. Typing it too means a
    // preset or a deserialised snapshot cannot raise the ceiling past what any
    // hardware supports and let an over-wide kernel config through.
    ValidRegsPerThread max_regs_per_thread{uint16_t{255}};
    uint16_t pad0 = 0;

    uint32_t smem_per_sm = 233472;  // shared memory, or LDS on AMD
    uint32_t tmem_per_sm = 0;  // tensor memory, absent on most parts
    uint32_t l1_per_sm = 262144;  // may share its budget with shared memory

    uint64_t l2_bytes = 0;
    uint64_t hbm_bytes = 0;

    // GB/s, which is identically bytes per nanosecond.
    float smem_bw_per_sm = 0;
    float l2_bw = 0;
    float hbm_bw = 0;

    float smem_latency = 20;
    float l2_latency = 200;
    float hbm_latency = 400;
    float launch_ns = 3000;

    // Whole-chip TFLOPS. Tensor-core rate where the precision has one, scalar
    // ALU rate otherwise. Zero means the part does not implement the format.
    float peak_fp64 = 0;
    float peak_fp32 = 0;
    float peak_tf32 = 0;
    float peak_fp16 = 0;
    float peak_bf16 = 0;
    float peak_fp8 = 0;
    float peak_fp4 = 0;
    float peak_int8 = 0;

    uint32_t sm_version = 0;

    [[nodiscard]] constexpr uint32_t max_threads_per_sm() const {
        return static_cast<uint32_t>(warp_size.value()) * max_warps_per_sm;
    }

    [[nodiscard]] constexpr uint64_t total_threads() const {
        return static_cast<uint64_t>(num_sms) * max_threads_per_sm();
    }

    [[nodiscard]] constexpr float peak_tflops(ScalarType dtype) const {
        switch (dtype) {
            case ScalarType::Double:
                return peak_fp64;
            case ScalarType::Float:
                return peak_fp32;
            case ScalarType::Half:
                return peak_fp16;
            case ScalarType::BFloat16:
                return peak_bf16;
            case ScalarType::Float8_e5m2:
            case ScalarType::Float8_e4m3fn:
            case ScalarType::Float8_e5m2fnuz:
            case ScalarType::Float8_e4m3fnuz:
                return peak_fp8;
            default:
                return peak_fp32;
        }
    }

    // The arithmetic intensity, in FLOPs per byte, at which compute and memory
    // are balanced. A kernel below the ridge is memory-bound and one above it
    // is compute-bound.
    [[nodiscard]] constexpr float ridge_point(ScalarType dtype) const {
        float peak = peak_tflops(dtype);
        return (hbm_bw > 0) ? (peak * 1e3f) / hbm_bw : 0.0f;
    }
};

[[nodiscard]] constexpr HardwareProfile blackwell_b200() {
    HardwareProfile hw{};
    hw.num_sms = 128;
    hw.warp_size = ValidWarpSize{uint16_t{32}};
    hw.max_warps_per_sm = 64;
    hw.regs_per_sm = 65536;
    hw.max_regs_per_thread = ValidRegsPerThread{uint16_t{255}};
    hw.smem_per_sm = 233472;
    hw.tmem_per_sm = 65536;
    hw.l1_per_sm = 262144;
    hw.l2_bytes = 50ULL << 20;
    hw.hbm_bytes = 192ULL << 30;
    hw.smem_bw_per_sm = 1000;
    hw.l2_bw = 12000;
    hw.hbm_bw = 8000;
    hw.smem_latency = 20;
    hw.l2_latency = 150;
    hw.hbm_latency = 350;
    hw.launch_ns = 3000;
    hw.peak_fp64 = 45;
    hw.peak_fp32 = 90;
    hw.peak_tf32 = 225;
    hw.peak_fp16 = 1125;  // dense rate, not the sparsity-doubled figure
    hw.peak_bf16 = 1125;
    hw.peak_fp8 = 2250;
    hw.peak_fp4 = 4500;
    hw.peak_int8 = 2250;
    hw.sm_version = 100;
    return hw;
}

[[nodiscard]] constexpr HardwareProfile hopper_h100() {
    HardwareProfile hw{};
    hw.num_sms = 132;
    hw.warp_size = ValidWarpSize{uint16_t{32}};
    hw.max_warps_per_sm = 64;
    hw.regs_per_sm = 65536;
    hw.max_regs_per_thread = ValidRegsPerThread{uint16_t{255}};
    hw.smem_per_sm = 233472;
    hw.tmem_per_sm = 0;
    hw.l1_per_sm = 262144;
    hw.l2_bytes = 50ULL << 20;
    hw.hbm_bytes = 80ULL << 30;
    hw.smem_bw_per_sm = 800;
    hw.l2_bw = 9000;
    hw.hbm_bw = 3350;
    hw.smem_latency = 20;
    hw.l2_latency = 200;
    hw.hbm_latency = 400;
    hw.launch_ns = 4000;
    hw.peak_fp64 = 34;
    hw.peak_fp32 = 67;
    hw.peak_tf32 = 495;
    hw.peak_fp16 = 990;
    hw.peak_bf16 = 990;
    hw.peak_fp8 = 1979;
    hw.peak_fp4 = 0;
    hw.peak_int8 = 1979;
    hw.sm_version = 90;
    return hw;
}

[[nodiscard]] constexpr HardwareProfile mi300x() {
    HardwareProfile hw{};
    hw.num_sms = 304;
    hw.warp_size = ValidWarpSize{uint16_t{64}};
    hw.max_warps_per_sm = 32;
    hw.regs_per_sm = 65536;
    hw.max_regs_per_thread = ValidRegsPerThread{uint16_t{255}};
    hw.smem_per_sm = 65536;
    hw.tmem_per_sm = 0;
    hw.l1_per_sm = 131072;
    hw.l2_bytes = 256ULL << 20;
    hw.hbm_bytes = 192ULL << 30;
    hw.smem_bw_per_sm = 600;
    hw.l2_bw = 7000;
    hw.hbm_bw = 5300;
    hw.smem_latency = 25;
    hw.l2_latency = 180;
    hw.hbm_latency = 380;
    hw.launch_ns = 4000;
    hw.peak_fp64 = 81;
    hw.peak_fp32 = 164;
    hw.peak_tf32 = 0;
    hw.peak_fp16 = 1300;
    hw.peak_bf16 = 1300;
    hw.peak_fp8 = 2600;
    hw.peak_fp4 = 0;
    hw.peak_int8 = 2600;
    hw.sm_version = 0;  // this vendor has no equivalent version number
    return hw;
}

[[nodiscard]] constexpr HardwareProfile ampere_a100() {
    HardwareProfile hw{};
    hw.num_sms = 108;
    hw.warp_size = ValidWarpSize{uint16_t{32}};
    hw.max_warps_per_sm = 64;
    hw.regs_per_sm = 65536;
    hw.max_regs_per_thread = ValidRegsPerThread{uint16_t{255}};
    hw.smem_per_sm = 167936;
    hw.tmem_per_sm = 0;
    hw.l1_per_sm = 196608;
    hw.l2_bytes = 40ULL << 20;
    hw.hbm_bytes = 80ULL << 30;
    hw.smem_bw_per_sm = 600;
    hw.l2_bw = 6000;
    hw.hbm_bw = 2039;
    hw.smem_latency = 25;
    hw.l2_latency = 200;
    hw.hbm_latency = 450;
    hw.launch_ns = 5000;
    hw.peak_fp64 = 19;
    hw.peak_fp32 = 19;
    hw.peak_tf32 = 156;
    hw.peak_fp16 = 312;
    hw.peak_bf16 = 312;
    hw.peak_fp8 = 0;
    hw.peak_fp4 = 0;
    hw.peak_int8 = 624;
    hw.sm_version = 80;
    return hw;
}

struct KernelConfig {
    uint16_t tile_m = 128;  // output tile rows per threadblock
    uint16_t tile_n = 128;  // output tile columns per threadblock
    uint16_t tile_k = 32;  // reduction depth per step
    uint8_t pipeline_stages = 3;
    uint8_t warps_per_block = 8;
    uint32_t smem_bytes = 0;
    ValidRegsPerThread regs_per_thread{uint16_t{64}};
    uint8_t vec_width = 4;  // elements per vectorised load or store
    uint8_t pad0 = 0;
};

[[nodiscard]] constexpr bool validate_config(const KernelConfig& cfg, const HardwareProfile& hw) {
    if (cfg.smem_bytes > hw.smem_per_sm) return false;
    if (cfg.regs_per_thread.value() > hw.max_regs_per_thread.value()) return false;
    if (cfg.warps_per_block == 0) return false;
    // 1024 threads per block is the hardware ceiling on every target.
    if (static_cast<uint32_t>(cfg.warps_per_block) * hw.warp_size.value() > 1024) return false;
    if (cfg.tile_m == 0 || cfg.tile_n == 0 || cfg.tile_k == 0) return false;
    if (cfg.pipeline_stages == 0 || cfg.pipeline_stages > 7) return false;
    return true;
}

struct CostBreakdown {
    double compute_ns = 0;
    double memory_ns = 0;
    double launch_ns = 0;
    double total_ns = 0;

    uint64_t flops = 0;
    uint64_t bytes = 0;  // traffic at the bottleneck level of the hierarchy

    float arithmetic_intensity = 0;
    ValidUtilization wave_efficiency{0.0f};
    ValidUtilization occupancy{0.0f};

    enum class Bottleneck : uint8_t {
        COMPUTE,
        MEMORY,
        LAUNCH,  // dispatch dominates, which means the kernel wants fusing
        UNDERUTIL,  // too few elements to fill this part
    } bottleneck = Bottleneck::LAUNCH;
};

// The fraction of dispatched threads doing useful work. Threads arrive in
// waves the width of the whole chip, so a final partial wave leaves some
// multiprocessors idle for its entire duration.

template <typename CallerRow = ::crucible::effects::Row<>>
    requires ::crucible::effects::Subrow<CallerRow, ::crucible::effects::Row<>>
[[nodiscard]] constexpr ValidUtilization wave_efficiency(uint64_t elements, const HardwareProfile& hw) {
    uint64_t tpw = static_cast<uint64_t>(hw.num_sms) * hw.warp_size.value();
    if (tpw == 0 || elements == 0) return ValidUtilization{0.0f};
    uint64_t waves = (elements + tpw - 1) / tpw;
    // The ceiling division makes the denominator an upper bound on the
    // numerator, and round-to-nearest is monotonic on non-negative operands,
    // so the quotient is in range and the wrapper's check cannot fire. It is
    // there to catch an inversion introduced later, such as a swapped
    // numerator or an off-by-one in the tile width.
    return ValidUtilization{static_cast<float>(elements) / static_cast<float>(waves * tpw)};
}

// The fraction of the multiprocessor's warp slots that can be resident at
// once, whichever of register pressure and shared-memory pressure binds first.

template <typename CallerRow = ::crucible::effects::Row<>>
    requires ::crucible::effects::Subrow<CallerRow, ::crucible::effects::Row<>>
[[nodiscard]] constexpr ValidUtilization sm_occupancy(ValidRegsPerThread regs_per_thread, uint32_t smem_per_block,
                                                      uint16_t warps_per_block, const HardwareProfile& hw) {
    const uint16_t warp_v = hw.warp_size.value();
    uint32_t max_threads = static_cast<uint32_t>(warp_v) * hw.max_warps_per_sm;
    if (max_threads == 0) return ValidUtilization{0.0f};

    // How many threads the register file holds. Zero registers is the only
    // case the type does not already exclude, so it is guarded here.
    const uint16_t regs_v = regs_per_thread.value();
    uint32_t reg_limited = max_threads;
    if (regs_v > 0) {
        reg_limited = hw.regs_per_sm / regs_v;
        // Threads are only ever scheduled a whole warp at a time.
        reg_limited = (reg_limited / warp_v) * warp_v;
    }

    uint32_t smem_limited = max_threads;
    if (smem_per_block > 0) {
        uint32_t blocks = hw.smem_per_sm / smem_per_block;
        uint32_t tpb = static_cast<uint32_t>(warps_per_block) * warp_v;
        smem_limited = blocks * tpb;
    }

    uint32_t actual = std::min({reg_limited, smem_limited, max_threads});
    // The third operand of the minimum bounds the numerator by the
    // denominator, so the quotient is in range and the wrapper's check cannot
    // fire. It catches a later regression, such as dropping the warp-
    // granularity round-down above so that the register limit exceeds the
    // thread ceiling.
    return ValidUtilization{static_cast<float>(actual) / static_cast<float>(max_threads)};
}

// Takes flops, bytes and element counts rather than a graph node, so the cost
// model stays independent of the graph representation.

template <typename CallerRow = ::crucible::effects::Row<>>
    requires ::crucible::effects::Subrow<CallerRow, ::crucible::effects::Row<>>
[[nodiscard]] inline CostBreakdown evaluate_cost(uint64_t flops, uint64_t bytes, uint64_t elements, ScalarType dtype,
                                                 const KernelConfig& cfg, const HardwareProfile& hw) {
    CostBreakdown cb;
    cb.flops = flops;
    cb.bytes = bytes;
    cb.launch_ns = hw.launch_ns;

    cb.arithmetic_intensity = (bytes > 0) ? static_cast<float>(flops) / static_cast<float>(bytes) : 0.0f;

    cb.wave_efficiency = wave_efficiency<CallerRow>(elements, hw);
    cb.occupancy = sm_occupancy<CallerRow>(cfg.regs_per_thread, cfg.smem_bytes, cfg.warps_per_block, hw);

    // The factor of 1e3 turns TFLOPS into FLOPs per nanosecond. The two
    // utilisation tests below filter exactly the structural zeros, since
    // neither value can be out of range.
    float peak = hw.peak_tflops(dtype);
    if (peak > 0 && cb.wave_efficiency.value() > 0 && cb.occupancy.value() > 0) {
        double effective = static_cast<double>(peak) * 1e3 * static_cast<double>(cb.wave_efficiency.value())
                         * static_cast<double>(cb.occupancy.value());
        cb.compute_ns = static_cast<double>(flops) / effective;
    }

    if (hw.hbm_bw > 0) {
        cb.memory_ns =
            static_cast<double>(hw.hbm_latency) + static_cast<double>(bytes) / static_cast<double>(hw.hbm_bw);
    }

    // Compute and memory overlap, so the slower of the two sets the pace.
    cb.total_ns = std::max(cb.compute_ns, cb.memory_ns) + cb.launch_ns;

    if (cb.launch_ns > std::max(cb.compute_ns, cb.memory_ns) * 2.0)
        cb.bottleneck = CostBreakdown::Bottleneck::LAUNCH;
    else if (cb.wave_efficiency.value() < 0.1f)
        cb.bottleneck = CostBreakdown::Bottleneck::UNDERUTIL;
    else if (cb.compute_ns > cb.memory_ns)
        cb.bottleneck = CostBreakdown::Bottleneck::COMPUTE;
    else
        cb.bottleneck = CostBreakdown::Bottleneck::MEMORY;

    return cb;
}

template <typename CallerRow = ::crucible::effects::Row<>>
    requires ::crucible::effects::Subrow<CallerRow, ::crucible::effects::Row<>>
[[nodiscard]] inline CostBreakdown evaluate_cost(uint64_t flops, uint64_t bytes, uint64_t elements, ScalarType dtype,
                                                 const HardwareProfile& hw) {
    KernelConfig default_cfg{};
    return evaluate_cost<CallerRow>(flops, bytes, elements, dtype, default_cfg, hw);
}

// Fusion wins on two counts: intermediates stay in registers or shared memory
// instead of making a round trip to memory, and one dispatch replaces several.

struct FusionBenefit {
    double unfused_ns = 0;
    double fused_ns = 0;
    double saved_ns = 0;
    uint64_t saved_bytes = 0;  // intermediates that no longer reach memory
    uint32_t saved_launches = 0;
    float speedup = 0;  // above one when fusing wins
};

template <typename CallerRow = ::crucible::effects::Row<>>
    requires ::crucible::effects::Subrow<CallerRow, ::crucible::effects::Row<>>
[[nodiscard]] inline FusionBenefit compute_fusion_benefit(double unfused_ns, double fused_ns, uint64_t saved_bytes,
                                                          uint32_t saved_launches) {
    FusionBenefit fb;
    fb.unfused_ns = unfused_ns;
    fb.fused_ns = fused_ns;
    fb.saved_ns = unfused_ns - fused_ns;
    fb.saved_bytes = saved_bytes;
    fb.saved_launches = saved_launches;
    fb.speedup = (fused_ns > 0) ? static_cast<float>(unfused_ns / fused_ns) : 0.0f;
    return fb;
}

}  // namespace crucible

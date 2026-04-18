#pragma once

// CostModel: Axiom-style hardware specification + constraint-based cost.
//
// Every parameter and formula is designed for dual use:
//   1. C++ evaluator: fast cost computation for compile-time decisions
//   2. Z3 SMT encoding: mechanical translation to SMT-LIB2
//
// Z3 mapping:
//   HardwareProfile fields  → (define-const ...) axioms
//   KernelConfig fields     → (declare-const ...) variables
//   validate_config()       → (assert ...) constraints
//   evaluate_cost()         → (minimize ...) objective
//
// Unit conventions (chosen so formulas need no conversion factors):
//   Bandwidth: GB/s  = bytes/nanosecond  (1 GB/s = 10^9 B/s = 1 B/ns)
//   Throughput: TFLOPS × 1e3 = FLOPS/nanosecond
//   Time: nanoseconds throughout
//   Memory: bytes throughout (except per-SM quantities in KB for readability)
//
// The "fat GPU" problem: modern chips (B200: 128 SMs, 262K threads) are
// so parallel that small ops use <1% of silicon. Kernel launch overhead
// (3-5μs) dominates. Fusion amortizes one launch across many ops.
// The cost model exposes this: launch_ns >> compute_ns for small ops.

#include <crucible/Types.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace crucible {

// ═══════════════════════════════════════════════════════════════════
// HardwareProfile: Measurable hardware specifications (Z3 axioms)
//
// Every field is directly benchmarkable or available in vendor specs.
// Meridian (L16) calibrates these at startup with real measurements.
// Z3 encoding: each field becomes (define-const name Type value).
//
// Fields are ordered by memory hierarchy level, from fastest (registers)
// to slowest (HBM). This mirrors the roofline model's bandwidth cascade.
// ═══════════════════════════════════════════════════════════════════

struct HardwareProfile {
  // ── Compute fabric ─────────────────────────────────────────────
  // Z3: (define-const num_sms Int 128)
  uint32_t num_sms = 0;             // Streaming multiprocessors (NVIDIA) or CUs (AMD)
  uint16_t warp_size = 32;          // Threads per warp (32 NVIDIA) or wavefront (64 AMD)
  uint16_t max_warps_per_sm = 64;   // Max concurrent warps per SM

  // ── Register file (per SM) ─────────────────────────────────────
  // Z3: (define-const regs_per_sm Int 65536)
  uint32_t regs_per_sm = 65536;     // 32-bit registers per SM (256KB = 65536 × 4B)
  uint16_t max_regs_per_thread = 255; // Hardware cap on registers per thread
  uint16_t pad0 = 0;

  // ── On-chip memory (bytes per SM) ──────────────────────────────
  // Z3: (define-const smem_per_sm Int 233472)
  uint32_t smem_per_sm = 233472;    // Shared memory (228KB on Blackwell, 228KB on Hopper)
  uint32_t tmem_per_sm = 0;         // Tensor memory (64KB on Blackwell, 0 otherwise)
  uint32_t l1_per_sm = 262144;      // L1 cache (256KB typical, may share budget with smem)

  // ── Global memory hierarchy ────────────────────────────────────
  // Z3: (define-const l2_bytes Int 52428800)
  uint64_t l2_bytes = 0;            // L2 cache total (50MB B200, 256MB MI300X)
  uint64_t hbm_bytes = 0;           // HBM total (192GB B200, 80GB H100)

  // ── Bandwidth (GB/s = bytes/ns) ────────────────────────────────
  // Each level: sustained throughput. GB/s = B/ns (unit identity).
  // Z3: (define-const hbm_bw Real 8000.0)  ; 8000 bytes/ns = 8 TB/s
  float smem_bw_per_sm = 0;         // Shared memory bandwidth per SM
  float l2_bw = 0;                  // L2 cache bandwidth (aggregate, all SMs)
  float hbm_bw = 0;                 // HBM bandwidth (aggregate)

  // ── Latency (ns) ───────────────────────────────────────────────
  // Z3: (define-const hbm_latency Real 400.0)
  float smem_latency = 20;          // Shared memory access (~20ns)
  float l2_latency = 200;           // L2 cache access (~200ns)
  float hbm_latency = 400;          // HBM access (~400ns)
  float launch_ns = 3000;           // Kernel dispatch overhead (~3μs)

  // ── Peak throughput (TFLOPS, whole chip) ────────────────────────
  // Tensor core ops at matching precision; scalar ALUs otherwise.
  // Z3: (define-const peak_fp16 Real 1125.0)  ; 1125 TFLOPS
  float peak_fp64 = 0;              // FP64 (scalar or tensor core)
  float peak_fp32 = 0;              // FP32 (scalar)
  float peak_tf32 = 0;              // TF32 (tensor core, Ampere+)
  float peak_fp16 = 0;              // FP16 (tensor core)
  float peak_bf16 = 0;              // BF16 (tensor core)
  float peak_fp8 = 0;               // FP8 (tensor core, Hopper+)
  float peak_fp4 = 0;               // FP4 (tensor core, Blackwell+)
  float peak_int8 = 0;              // INT8 (tensor core)

  // ── SM architecture version ────────────────────────────────────
  uint32_t sm_version = 0;          // e.g. 90 (Hopper), 100 (Blackwell)

  // ── Derived quantities (not Z3 axioms — computed from axioms) ──

  [[nodiscard]] constexpr uint32_t max_threads_per_sm() const {
    return static_cast<uint32_t>(warp_size) * max_warps_per_sm;
  }

  [[nodiscard]] constexpr uint64_t total_threads() const {
    return static_cast<uint64_t>(num_sms) * max_threads_per_sm();
  }

  // Peak throughput for a given ScalarType.
  // Z3: (define-fun peak_for_dtype ((dtype ScalarType)) Real ...)
  [[nodiscard]] constexpr float peak_tflops(ScalarType dtype) const {
    switch (dtype) {
      case ScalarType::Double:      return peak_fp64;
      case ScalarType::Float:       return peak_fp32;
      case ScalarType::Half:        return peak_fp16;
      case ScalarType::BFloat16:    return peak_bf16;
      case ScalarType::Float8_e5m2:
      case ScalarType::Float8_e4m3fn:
      case ScalarType::Float8_e5m2fnuz:
      case ScalarType::Float8_e4m3fnuz:
                                    return peak_fp8;
      default:                      return peak_fp32;
    }
  }

  // Roofline ridge point: arithmetic intensity (FLOP/byte) where
  // compute and memory are balanced. Below → memory-bound. Above → compute-bound.
  //
  // ridge = peak_flops_per_ns / hbm_bytes_per_ns
  //       = (peak_tflops * 1e3) / hbm_bw
  //
  // Z3: (define-fun ridge ((dtype ScalarType)) Real
  //       (/ (* (peak_for_dtype dtype) 1000.0) hbm_bw))
  [[nodiscard]] constexpr float ridge_point(ScalarType dtype) const {
    float peak = peak_tflops(dtype);
    return (hbm_bw > 0) ? (peak * 1e3f) / hbm_bw : 0.0f;
  }
};

// ═══════════════════════════════════════════════════════════════════
// Hardware presets: nominal values from vendor specs.
// Meridian (L16) replaces these with measured values at startup.
//
// Z3: presets are just default axiom assignments. Meridian overrides
// them with calibrated values before Z3 solves.
// ═══════════════════════════════════════════════════════════════════

// NVIDIA Blackwell B200 (sm_100) — 2024/2025
[[nodiscard]] constexpr HardwareProfile blackwell_b200() {
  HardwareProfile hw{};
  hw.num_sms = 128;
  hw.warp_size = 32;
  hw.max_warps_per_sm = 64;
  hw.regs_per_sm = 65536;
  hw.max_regs_per_thread = 255;
  hw.smem_per_sm = 233472;        // 228KB
  hw.tmem_per_sm = 65536;         // 64KB tensor memory
  hw.l1_per_sm = 262144;          // 256KB
  hw.l2_bytes = 50ULL << 20;      // 50MB
  hw.hbm_bytes = 192ULL << 30;    // 192GB HBM3e
  hw.smem_bw_per_sm = 1000;       // ~1 TB/s per SM
  hw.l2_bw = 12000;               // ~12 TB/s aggregate
  hw.hbm_bw = 8000;               // 8 TB/s
  hw.smem_latency = 20;
  hw.l2_latency = 150;
  hw.hbm_latency = 350;
  hw.launch_ns = 3000;
  hw.peak_fp64 = 45;
  hw.peak_fp32 = 90;
  hw.peak_tf32 = 225;
  hw.peak_fp16 = 1125;            // No sparsity
  hw.peak_bf16 = 1125;
  hw.peak_fp8 = 2250;
  hw.peak_fp4 = 4500;
  hw.peak_int8 = 2250;
  hw.sm_version = 100;
  return hw;
}

// NVIDIA Hopper H100 SXM (sm_90) — 2023
[[nodiscard]] constexpr HardwareProfile hopper_h100() {
  HardwareProfile hw{};
  hw.num_sms = 132;
  hw.warp_size = 32;
  hw.max_warps_per_sm = 64;
  hw.regs_per_sm = 65536;
  hw.max_regs_per_thread = 255;
  hw.smem_per_sm = 233472;        // 228KB
  hw.tmem_per_sm = 0;             // No tensor memory
  hw.l1_per_sm = 262144;          // 256KB
  hw.l2_bytes = 50ULL << 20;      // 50MB
  hw.hbm_bytes = 80ULL << 30;     // 80GB HBM3
  hw.smem_bw_per_sm = 800;
  hw.l2_bw = 9000;
  hw.hbm_bw = 3350;               // 3.35 TB/s
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
  hw.peak_fp4 = 0;                // Not supported
  hw.peak_int8 = 1979;
  hw.sm_version = 90;
  return hw;
}

// AMD Instinct MI300X (gfx942) — 2024
[[nodiscard]] constexpr HardwareProfile mi300x() {
  HardwareProfile hw{};
  hw.num_sms = 304;               // Compute Units
  hw.warp_size = 64;              // Wavefront size
  hw.max_warps_per_sm = 32;       // Max wavefronts per CU
  hw.regs_per_sm = 65536;         // 256KB VGPR per CU
  hw.max_regs_per_thread = 255;
  hw.smem_per_sm = 65536;         // 64KB LDS per CU
  hw.tmem_per_sm = 0;
  hw.l1_per_sm = 131072;          // 128KB L1 vector cache per CU
  hw.l2_bytes = 256ULL << 20;     // 256MB Infinity Cache
  hw.hbm_bytes = 192ULL << 30;    // 192GB HBM3
  hw.smem_bw_per_sm = 600;
  hw.l2_bw = 7000;
  hw.hbm_bw = 5300;               // 5.3 TB/s
  hw.smem_latency = 25;
  hw.l2_latency = 180;
  hw.hbm_latency = 380;
  hw.launch_ns = 4000;
  hw.peak_fp64 = 81;
  hw.peak_fp32 = 164;
  hw.peak_tf32 = 0;               // No TF32 on AMD
  hw.peak_fp16 = 1300;
  hw.peak_bf16 = 1300;
  hw.peak_fp8 = 2600;
  hw.peak_fp4 = 0;
  hw.peak_int8 = 2600;
  hw.sm_version = 0;              // AMD doesn't use sm_version
  return hw;
}

// NVIDIA Ampere A100 SXM (sm_80) — 2020, still widely deployed
[[nodiscard]] constexpr HardwareProfile ampere_a100() {
  HardwareProfile hw{};
  hw.num_sms = 108;
  hw.warp_size = 32;
  hw.max_warps_per_sm = 64;
  hw.regs_per_sm = 65536;
  hw.max_regs_per_thread = 255;
  hw.smem_per_sm = 167936;        // 164KB
  hw.tmem_per_sm = 0;
  hw.l1_per_sm = 196608;          // 192KB
  hw.l2_bytes = 40ULL << 20;      // 40MB
  hw.hbm_bytes = 80ULL << 30;     // 80GB HBM2e
  hw.smem_bw_per_sm = 600;
  hw.l2_bw = 6000;
  hw.hbm_bw = 2039;               // ~2 TB/s
  hw.smem_latency = 25;
  hw.l2_latency = 200;
  hw.hbm_latency = 450;
  hw.launch_ns = 5000;
  hw.peak_fp64 = 19;
  hw.peak_fp32 = 19;
  hw.peak_tf32 = 156;
  hw.peak_fp16 = 312;
  hw.peak_bf16 = 312;
  hw.peak_fp8 = 0;                // Not supported
  hw.peak_fp4 = 0;
  hw.peak_int8 = 624;
  hw.sm_version = 80;
  return hw;
}

// ═══════════════════════════════════════════════════════════════════
// KernelConfig: Variables that Z3 optimizes.
//
// For a given (operation shape, hardware), Z3 finds the config that
// minimizes cost subject to hardware constraints.
//
// Z3 encoding: each field becomes (declare-const name Type).
// ═══════════════════════════════════════════════════════════════════

struct KernelConfig {
  uint16_t tile_m = 128;           // Output tile rows per threadblock
  uint16_t tile_n = 128;           // Output tile cols per threadblock
  uint16_t tile_k = 32;            // Reduction tile depth per step
  uint8_t pipeline_stages = 3;     // Async copy pipeline depth (1-7)
  uint8_t warps_per_block = 8;     // Warps per threadblock (1-32)
  uint32_t smem_bytes = 0;         // Shared memory allocation (bytes)
  uint16_t regs_per_thread = 64;   // Estimated registers per thread
  uint8_t vec_width = 4;           // Elements per vectorized load/store
  uint8_t pad0 = 0;
};

// ═══════════════════════════════════════════════════════════════════
// Constraints: validate_config() → Z3 (assert ...) declarations
//
// Each constraint has a C1..C8 label for traceability between the
// C++ implementation and the Z3 encoding. A config is feasible iff
// all constraints hold.
// ═══════════════════════════════════════════════════════════════════

// Z3:
//   (assert (<= smem_bytes smem_per_sm))                          ; C1
//   (assert (<= regs_per_thread max_regs_per_thread))             ; C2
//   (assert (>= warps_per_block 1))                               ; C3
//   (assert (<= (* warps_per_block warp_size) 1024))              ; C4
//   (assert (> tile_m 0))                                         ; C5
//   (assert (> tile_n 0))                                         ; C6
//   (assert (> tile_k 0))                                         ; C7
//   (assert (and (>= pipeline_stages 1) (<= pipeline_stages 7)))  ; C8
[[nodiscard]] constexpr bool validate_config(
    const KernelConfig& cfg, const HardwareProfile& hw) {
  // C1: shared memory fits in per-SM budget
  if (cfg.smem_bytes > hw.smem_per_sm) return false;
  // C2: register count within hardware limit
  if (cfg.regs_per_thread > hw.max_regs_per_thread) return false;
  // C3: at least one warp per block
  if (cfg.warps_per_block == 0) return false;
  // C4: threads per block ≤ hardware max (1024 on NVIDIA, 1024 on AMD)
  if (static_cast<uint32_t>(cfg.warps_per_block) * hw.warp_size > 1024) return false;
  // C5-C7: tile sizes are positive
  if (cfg.tile_m == 0 || cfg.tile_n == 0 || cfg.tile_k == 0) return false;
  // C8: pipeline stages in valid range
  if (cfg.pipeline_stages == 0 || cfg.pipeline_stages > 7) return false;
  return true;
}

// ═══════════════════════════════════════════════════════════════════
// CostBreakdown: Result of cost evaluation
//
// Decomposes total cost into compute, memory, and launch components.
// The bottleneck classification tells the optimizer what to attack.
// ═══════════════════════════════════════════════════════════════════

struct CostBreakdown {
  double compute_ns = 0;           // FLOPs / (peak × wave_eff × occupancy)
  double memory_ns = 0;            // latency + bytes / bandwidth
  double launch_ns = 0;            // Kernel dispatch overhead
  double total_ns = 0;             // max(compute, memory) + launch

  uint64_t flops = 0;             // Total floating-point operations
  uint64_t bytes = 0;             // Total memory traffic (at bottleneck level)

  float arithmetic_intensity = 0;  // FLOP/byte (roofline X-axis)
  float wave_efficiency = 0;       // Thread utilization [0,1]
  float occupancy = 0;             // SM occupancy [0,1]

  enum class Bottleneck : uint8_t {
    COMPUTE,     // compute_ns > memory_ns (GPU doing useful work)
    MEMORY,      // memory_ns > compute_ns (bandwidth starved)
    LAUNCH,      // launch_ns dominates (tiny kernel — FUSE IT)
    UNDERUTIL,   // wave_efficiency < 10% (too few elements for this GPU)
  } bottleneck = Bottleneck::LAUNCH;
};

// ═══════════════════════════════════════════════════════════════════
// Wave efficiency: fraction of SM threads doing useful work.
//
// Threads are dispatched in waves of num_sms × warp_size.
// If elements don't fill the last wave, some SMs idle.
//
// B200 example — [32,64] tensor = 2048 elements:
//   threads_per_wave = 128 × 32 = 4096
//   waves = ceil(2048 / 4096) = 1
//   efficiency = 2048 / (1 × 4096) = 0.5 → 50% wasted
//
// Z3: (define-fun wave_eff ((elems Int) (hw HardwareProfile)) Real
//       (let ((tpw (* num_sms warp_size)))
//         (/ (to_real elems)
//            (* (to_real (ceil_div elems tpw)) (to_real tpw)))))
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] constexpr float wave_efficiency(
    uint64_t elements, const HardwareProfile& hw) {
  uint64_t tpw = static_cast<uint64_t>(hw.num_sms) * hw.warp_size;
  if (tpw == 0 || elements == 0) return 0.0f;
  uint64_t waves = (elements + tpw - 1) / tpw;
  return static_cast<float>(elements) / static_cast<float>(waves * tpw);
}

// ═══════════════════════════════════════════════════════════════════
// SM occupancy: fraction of max warps that can run concurrently.
//
// Limited by register pressure AND shared memory pressure.
// Fusion increases register pressure (more intermediates alive)
// but eliminates HBM traffic. The tradeoff is almost always
// worth it: even 12.5% occupancy beats a separate kernel launch.
//
// B200 examples:
//   32 regs/thread → 65536/32 = 2048 threads = 64 warps = 100%
//   64 regs/thread → 65536/64 = 1024 threads = 32 warps = 50%
//   128 regs/thread → 65536/128 = 512 threads = 16 warps = 25%
//   255 regs/thread → 65536/255 = 256 threads = 8 warps = 12.5%
//
// Z3: (define-fun occupancy ((regs Int) (smem Int) (hw HardwareProfile)) Real
//       (let ((reg_limited (div regs_per_sm regs))
//             (smem_limited (ite (> smem 0) (* (div smem_per_sm smem) warps_per_block warp_size) max_threads))
//             (max_t (* max_warps_per_sm warp_size)))
//         (/ (to_real (min reg_limited smem_limited max_t)) (to_real max_t))))
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] constexpr float sm_occupancy(
    uint16_t regs_per_thread, uint32_t smem_per_block,
    uint16_t warps_per_block, const HardwareProfile& hw) {
  uint32_t max_threads = static_cast<uint32_t>(hw.warp_size) * hw.max_warps_per_sm;
  if (max_threads == 0) return 0.0f;

  // Register-limited: how many threads can fit in the register file
  uint32_t reg_limited = max_threads;
  if (regs_per_thread > 0) {
    reg_limited = hw.regs_per_sm / regs_per_thread;
    reg_limited = (reg_limited / hw.warp_size) * hw.warp_size; // warp granularity
  }

  // Shared-memory-limited: how many blocks fit, × threads per block
  uint32_t smem_limited = max_threads;
  if (smem_per_block > 0) {
    uint32_t blocks = hw.smem_per_sm / smem_per_block;
    uint32_t tpb = static_cast<uint32_t>(warps_per_block) * hw.warp_size;
    smem_limited = blocks * tpb;
  }

  uint32_t actual = std::min({reg_limited, smem_limited, max_threads});
  return static_cast<float>(actual) / static_cast<float>(max_threads);
}

// ═══════════════════════════════════════════════════════════════════
// Cost evaluation: the objective function Z3 minimizes.
//
// total_ns = max(compute_ns, memory_ns) + launch_ns
//
// where:
//   compute_ns = flops / (peak_tflops × 1e3 × wave_eff × occupancy)
//   memory_ns  = hbm_latency + bytes / hbm_bw
//
// Z3: (minimize (+ (ite (> compute_ns memory_ns) compute_ns memory_ns)
//                  launch_ns))
//
// Parameters are primitive values (flops, bytes, elements) so this
// function is independent of Graph.h. Integration with GraphNode
// happens at a higher level (the caller extracts flops/bytes).
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline CostBreakdown evaluate_cost(
    uint64_t flops, uint64_t bytes, uint64_t elements,
    ScalarType dtype,
    const KernelConfig& cfg,
    const HardwareProfile& hw) {
  CostBreakdown cb;
  cb.flops = flops;
  cb.bytes = bytes;
  cb.launch_ns = hw.launch_ns;

  // Arithmetic intensity (roofline X-axis)
  cb.arithmetic_intensity = (bytes > 0)
      ? static_cast<float>(flops) / static_cast<float>(bytes) : 0.0f;

  // Wave efficiency
  cb.wave_efficiency = wave_efficiency(elements, hw);

  // SM occupancy
  cb.occupancy = sm_occupancy(cfg.regs_per_thread, cfg.smem_bytes,
                               cfg.warps_per_block, hw);

  // Compute time: flops / (effective throughput)
  // peak_tflops × 1e3 converts TFLOPS → FLOPS/ns
  float peak = hw.peak_tflops(dtype);
  if (peak > 0 && cb.wave_efficiency > 0 && cb.occupancy > 0) {
    double effective = static_cast<double>(peak) * 1e3
                     * cb.wave_efficiency * cb.occupancy;
    cb.compute_ns = static_cast<double>(flops) / effective;
  }

  // Memory time: latency + transfer
  // hbm_bw is in GB/s = bytes/ns (unit identity)
  if (hw.hbm_bw > 0) {
    cb.memory_ns = hw.hbm_latency
                 + static_cast<double>(bytes) / hw.hbm_bw;
  }

  // Total = max(compute, memory) + launch
  cb.total_ns = std::max(cb.compute_ns, cb.memory_ns) + cb.launch_ns;

  // Classify bottleneck
  if (cb.launch_ns > std::max(cb.compute_ns, cb.memory_ns) * 2.0)
    cb.bottleneck = CostBreakdown::Bottleneck::LAUNCH;
  else if (cb.wave_efficiency < 0.1f)
    cb.bottleneck = CostBreakdown::Bottleneck::UNDERUTIL;
  else if (cb.compute_ns > cb.memory_ns)
    cb.bottleneck = CostBreakdown::Bottleneck::COMPUTE;
  else
    cb.bottleneck = CostBreakdown::Bottleneck::MEMORY;

  return cb;
}

// Convenience: evaluate with default kernel config (for quick estimation)
[[nodiscard]] inline CostBreakdown evaluate_cost(
    uint64_t flops, uint64_t bytes, uint64_t elements,
    ScalarType dtype,
    const HardwareProfile& hw) {
  KernelConfig default_cfg{};
  return evaluate_cost(flops, bytes, elements, dtype, default_cfg, hw);
}

// ═══════════════════════════════════════════════════════════════════
// Fusion benefit: compare fused vs unfused cost.
//
// Fusion removes intermediate HBM traffic (intermediates stay in
// registers/smem) and amortizes kernel launch overhead. The saved
// bytes are the intermediates that no longer touch HBM.
//
// Z3 can maximize this: (maximize (- unfused_total fused_total))
// ═══════════════════════════════════════════════════════════════════

struct FusionBenefit {
  double unfused_ns = 0;           // Sum of individual kernel costs
  double fused_ns = 0;             // Cost of one fused kernel
  double saved_ns = 0;             // unfused - fused
  uint64_t saved_bytes = 0;        // Intermediate bytes kept out of HBM
  uint32_t saved_launches = 0;     // Eliminated kernel dispatches
  float speedup = 0;               // unfused / fused (>1.0 = fusion wins)
};

[[nodiscard]] inline FusionBenefit compute_fusion_benefit(
    double unfused_ns, double fused_ns,
    uint64_t saved_bytes, uint32_t saved_launches) {
  FusionBenefit fb;
  fb.unfused_ns = unfused_ns;
  fb.fused_ns = fused_ns;
  fb.saved_ns = unfused_ns - fused_ns;
  fb.saved_bytes = saved_bytes;
  fb.saved_launches = saved_launches;
  fb.speedup = (fused_ns > 0) ? static_cast<float>(unfused_ns / fused_ns) : 0.0f;
  return fb;
}

} // namespace crucible

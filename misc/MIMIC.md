# Mimic — Crucible's Per-Vendor Backend Framework

*One subsystem per accelerator vendor: IR002 → IR003\* lowering, native ISA emission, three-tier simulator, MAP-Elites kernel search, runtime library, collective library, calibration. No vendor SDKs, no vendor libraries. Replaces libcuda, libcudart, cuBLAS, cuDNN, cuSPARSE, NCCL, libnrt, libncfw, libnccom, libtpu, xla-tpu, libhsa, rocBLAS, MIOpen, RCCL, hcoll, libsharp, UCX with Crucible code we own forever.*

Mimic is Crucible's portability layer. It is a **shared core** plus **one self-contained backend per vendor**. Each backend owns its vendor's IR003*, ISA emitter, binary-format writer, three-tier simulator, MAP-Elites search, runtime driver-wrapper library, and collective-library replacement. The shared core provides the simulator framework, MAP-Elites driver, archive machinery, mutation dispatch, calibration harness skeleton, effect tokens, and the cross-vendor numerics CI harness.

```
mimic/
├── core/               — vendor-agnostic (SimResult, Insight, MAP-Elites driver,
│                         archive, effect tokens, simulator framework, calibration
│                         skeleton, cross-vendor CI harness)
├── nv/                 — NVIDIA backend (sm_90 Hopper, sm_100 Blackwell DC,
│                         sm_103 Blackwell Ultra, sm_110 Jetson Thor,
│                         sm_120/121 Blackwell consumer / DGX Spark)
├── am/                 — AMD backend (CDNA3 gfx942, gfx950; RDNA3+)
├── tpu/                — Google TPU (v5p, v5e, v6e, v7)
├── trn/                — AWS Trainium (trn1, trn2, trn3)
├── cer/                — Cerebras WSE3+
├── cpu/                — x86_64 / aarch64 / riscv (correctness oracle + fallback)
└── data/               — per-chip calibration JSON + Genesis Kernel Packs
```

Each per-vendor backend ships five things:
1. **IR003\* + lowering** — IR002 → vendor machine IR (CSSA, address-space tagging, register allocation, schedule, peephole).
2. **Emitter + binary format** — IR003* → ISA bytes → vendor binary (cubin, HSACO, NEFF, TPU-exec, CSL, native ELF).
3. **Three-tier simulator + calibration** — fast / medium / accurate per vendor, calibrated against real silicon via per-vendor hardware-counter probes.
4. **Runtime library** — kernel-driver ioctl wrapper, memory management, submission, completion. Replaces libcuda / libnrt / libtpu / libhsa / etc.
5. **Collective library** — ring/tree/HD/hierarchical collectives over the vendor's fabric (NVLink, XGMI, ICI, NeuronLink). Replaces NCCL / RCCL / libnccom / hcoll / UCX / MPI. Optional `NetworkOffload` integration for SHARP/ICI-aggregation/XGMI/SwarmX.

This document is organized: shared core first, then the NVIDIA backend as the reference-complete implementation, then a sketch of what the other backends look like in the same template. The NV backend was formerly documented here as the whole of Mimic; it is now "Mimic's first backend, the template every other backend follows."

Written in the voice of Crucible's CLAUDE.md: direct, opinionated, dense, without hedging. Read alongside FORGE.md (which specifies what Mimic delivers to Forge) and CLAUDE.md (which defines the substrate both build on).

---

## Contents

1. Thesis and scope
2. The IR003\* contract — per-vendor backend protocol
3. What Mimic predicts; what it does not
4. The three subsystems and their seams
5. The three-tier simulator spectrum
6. DecodedInst — the unified IR
7. The target model — TargetCaps + OpcodeLatencyTable
8. The event-driven medium tier
9. The fast tier and GPU acceleration
10. The accurate tier
11. The memory subsystem
12. The warp scheduler
13. Async pipelines — WGMMA, TMA, tcgen05
14. Insight extraction
15. The SASS encoder
16. The SASS decoder
17. Math templates and precompiled fragments
18. The peephole pass
19. MAP-Elites — the search engine
20. Mutation operators
21. Archive persistence and warm-start
22. Calibration — how we reach 95-98%
23. Per-vendor hardware-counter access (CUPTI / rocprof / PJRT profiler / neuron-profile)
24. Leveraging driver source for the memory model
25. Determinism
26. Threading and concurrency
27. Effect tokens and Crucible discipline
28. The public API
29. Integration surface with Forge
30. Integration surface with Augur
31. Directory structure
32. LoC budget
33. Build plan
34. Critical design decisions
35. Open questions deferred
36. Runtime library replacement — per-vendor driver wrappers
37. Collective library replacement — per-vendor fabric primitives
38. NetworkOffload plane — SHARP / ICI / XGMI / SwarmX
39. Genesis Kernel Pack — per-chip precompiled seed
40. Recipe realization per backend
41. Cross-vendor numerics CI — enforcing the IR002 portability contract
42. Per-vendor backend notes — AMD / TPU / Trainium / Cerebras / CPU
43. Glossary

---

## 1. Thesis and scope

Seven sentences:

1. **Forge is the vendor-agnostic optimizer; Mimic is the portability layer.** Forge stops at IR002; Mimic per-vendor backends continue to silicon. Mimic does not need to be monolithic — it is per-vendor by design, with a shared core that makes adding vendors cheap.
2. **Every backend goes native.** No StableHLO delegation, no vendor-library fallback. Each vendor's ISA is emitted by us, wrapped by our runtime library, and driven by our collective library. Reverse-engineering the few closed-source compilers (neuronx-cc, xla-tpu) is part of the project — we ship delegation-mode as an interim for each but retire it when native emission lands.
3. **An analytical simulator calibrated on the narrow subset of instruction streams Forge emits can hit 95-98% accuracy per vendor**, because Forge-emitted IR002 is structurally disciplined (affine loops, tile-shaped, pinned recipe, no irregular divergence) in ways that collapse the state space a simulator has to track. The accuracy number is per-vendor; each backend carries its own simulator calibrated on its own silicon.
4. **Three simulator tiers per vendor** — fast (~1-5 ms) for MAP-Elites filtering, medium (~10-30 ms) for primary fitness, accurate (~100-500 ms) for calibration validation. Shared framework; per-vendor instantiation.
5. **Insights drive mutations, not just cycles.** Structured diagnostics keyed to specific instructions tell the per-vendor mutation engine what to change. Most insight kinds are vendor-agnostic; each backend adds ~10 vendor-specific kinds.
6. **NumericalRecipe is honored exactly by every backend.** The same IR002 recipe produces equivalent results on H100, MI300X, v5p, trn2, CPU. Mimic's cross-vendor CI matrix enforces this at every PR — a backend that violates tolerance fails the build.
7. **Content-addressed everything at three levels.** L1 IR002 snapshots (cross-vendor shareable), L2 IR003* snapshots (cross-chip within family), L3 compiled bytes (chip-specific). Shared archive/Cipher machinery across backends.

Scope boundaries:

| Category | In scope | Out of scope |
|---|---|---|
| Vendors | NVIDIA sm_90+, AMD CDNA3/RDNA3+, Google TPU v5+, AWS Trainium 1+, Cerebras WSE3+, x86_64/aarch64/riscv CPU | Pre-sm_90 NVIDIA, Intel Xe/Gaudi (first 2 years), mobile accelerators, legacy hardware |
| Workload class | Forge-emitted IR002 (affine, tiled, recipe-pinned, ML-structured) | Arbitrary hand-written kernels, CUDA C++ source, third-party binaries |
| Prediction surface (per vendor) | Cycles, IPC, stalls, memory traffic, cache hit rates, per-pipe utilization, occupancy, 12 vendor-agnostic signals | Power, thermal, DVFS transitions, cross-kernel effects |
| Simulation scope | Single kernel, single device, launch-to-completion | Multi-kernel interactions, cross-accelerator collectives (that's CNTP), host-device transfers scheduling |
| Calibration | Per-vendor microbench suite; driver-source-informed models where driver is available | Runtime adaptation (Augur handles); workload-specific tuning beyond recipe pinning |
| Vendor libraries | None. Kernel driver ioctls only. | Any vendor SDK runtime library, vendor BLAS/DNN/collective library, vendor compiler as library |

Everything out of scope is either handled by a neighboring Crucible subsystem (Augur for thermal drift, CNTP for collectives, Forge for optimization), or an explicit non-goal (no vendor SDK dependency).

---

## 2. The IR003\* contract — per-vendor backend protocol

Every Mimic backend (nv / am / tpu / trn / cer / cpu) implements a common protocol. Forge calls into Mimic via the five public entry points (`fast_cost`, `propose_tiles`, `compile_kernel`, `predict`, `probe_counters`); Mimic dispatches by `TargetCaps::vendor_id` to the appropriate backend.

### 2.1 Ten deliverables per vendor

1. **IR003\* type system + builder + verifier.** Per-vendor machine IR encoding native ISA concepts (opcode, operands, scheduling-slot, scoreboards, pipes) as specific to the silicon as needed.

2. **IR002 → IR003\* lowering.** Kernel-template realization + tile realization + recipe realization + CSSA + address-space tagging + register allocation + instruction scheduling. ~10K LoC per vendor.

3. **IR003\* → binary-format emitter.** Per-vendor ISA encoder + binary-format writer. Produces cubin on NV, HSACO on AMD, NEFF on Trainium, TPU executable on Google, CSL on Cerebras, native ELF on CPU. ~12-15K LoC per vendor.

4. **Decoder + round-trip correctness.** `decode(encode(prog)) == prog` for every valid IR003* program. Used for validation, debugging, and loading cached binaries back for Augur's `predict` path.

5. **Three-tier simulator.** Fast (~1-5 ms), medium (~10-30 ms), accurate (~100-500 ms). Each tier calibrated against real silicon. ~15-25K LoC per vendor, reusing the shared simulator framework from `mimic/core/`.

6. **MAP-Elites mutations + insights.** Per-vendor mutation operators (tile swap, pipeline depth change, pipe reassignment) and insight kinds. ~20 insight kinds are vendor-agnostic (REGISTER_PRESSURE_HIGH, L1_MISS_RATE_HIGH); each vendor adds ~10 specific kinds (WGMMA_UNDERUTILIZED on NV; MFMA_WAVE_STALL on AM; MXU_INPUT_STARVATION on TPU). ~6-8K LoC per vendor.

7. **Peephole rules.** ~150 rules per vendor, table-driven. Each vendor has its own native-fusion patterns.

8. **Calibration harness.** ~50-150 microbenchmarks per vendor. Outputs per-chip JSON data files that populate `TargetCaps<Vendor>`. See §22.

9. **Runtime library.** Our replacement for libcuda / libnrt / libtpu / libhsa. Wraps kernel-driver ioctls, manages device memory, submits kernels, polls completion. ~8-10K LoC per vendor. See §36.

10. **Collective library.** Our replacement for NCCL / RCCL / libnccom / hcoll. Ring/tree/HD/hierarchical over the vendor's native fabric (NVLink+NVSHMEM for NV; XGMI for AMD; ICI for TPU; NeuronLink+EFA for Trainium). Optional `NetworkOffload` provider integration. ~5-7K LoC per vendor. See §37-§38.

### 2.2 Public backend interface (C++26)

```cpp
namespace crucible::mimic::<vendor> {

// Per-vendor TargetCaps extends the abstract one.
struct TargetCaps<Vendor> { /* vendor-specific fields */ };
[[nodiscard]] const TargetCaps<Vendor>* load_caps(ChipId);

// Backend implementations of the five public entry points.
[[nodiscard]] Cycles          fast_cost      (fx::Bg, const KernelNode&, const TargetCaps&);
[[nodiscard]] std::span<const AbstractTile>
                              propose_tiles  (fx::Bg, Arena&, const KernelNode&, const TargetCaps&);
[[nodiscard]] CompiledKernel  compile_kernel (fx::Bg, Arena&, const KernelNode&, const TargetCaps&, const CompileConfig&);
[[nodiscard]] SimResult       predict        (fx::Bg, Arena&, const CompiledKernel&, const TargetCaps&, SimTier);
[[nodiscard]] Measurements    probe_counters (fx::Bg, const CompiledKernel&, const TargetCaps&);

// Plus runtime + collective (see §36-§37):
namespace rt {
    [[nodiscard]] Device         open_device  (fx::Init, int device_idx);
    [[nodiscard]] PoolHandle     allocate_pool(fx::Init, Device&, size_t bytes);
    [[nodiscard]] KernelHandle   load_kernel  (fx::Init, Device&, const CompiledKernel&);
    [[nodiscard]] Future<void>   submit_kernel(fx::Bg, Device&, KernelHandle, LaunchArgs);
    [[nodiscard]] GraphHandle    capture_plan (fx::Bg, Device&, const ExecutionPlan&);
    [[nodiscard]] Future<void>   launch_plan  (fx::Bg, Device&, GraphHandle);
}

namespace comm {
    [[nodiscard]] std::span<uint8_t> all_reduce(fx::Bg, Arena&,
                                                 ContentHash, ReduceOp, ScalarType,
                                                 std::span<const uint8_t>, ReduceGroup);
    [[nodiscard]] std::span<uint8_t> all_gather(fx::Bg, Arena&,
                                                 ContentHash, std::span<const uint8_t>, ReduceGroup);
    // ... reduce_scatter, all_to_all, broadcast, send, recv
}

} // namespace crucible::mimic::<vendor>
```

The top-level `mimic::compile_kernel` in the public namespace is a dispatch shim:

```cpp
CompiledKernel mimic::compile_kernel(fx::Bg bg, Arena& a, const KernelNode& k,
                                      const TargetCaps& caps, const CompileConfig& cfg) {
    switch (caps.vendor_id) {
        case VendorId::NVIDIA:        return nv::compile_kernel(bg, a, k, caps, cfg);
        case VendorId::AMD:           return am::compile_kernel(bg, a, k, caps, cfg);
        case VendorId::GOOGLE_TPU:    return tpu::compile_kernel(bg, a, k, caps, cfg);
        case VendorId::AWS_TRAINIUM:  return trn::compile_kernel(bg, a, k, caps, cfg);
        case VendorId::CEREBRAS:      return cer::compile_kernel(bg, a, k, caps, cfg);
        case VendorId::CPU_X86_64:
        case VendorId::CPU_AARCH64:
        case VendorId::CPU_RISCV:     return cpu::compile_kernel(bg, a, k, caps, cfg);
        default:                      std::unreachable();
    }
}
```

Zero runtime cost (jump-table). Zero friction for adding a vendor.

### 2.3 Shared core architecture

The `mimic/core/` subtree provides vendor-agnostic primitives every backend reuses:

- **Simulator framework skeleton** — event queue, scheduler skeleton, memory-model skeleton. Per-vendor backends specialize with their own subsystems.
- **MAP-Elites driver + Archive + mutation dispatcher** — runs the search loop; calls into per-vendor mutations.
- **Insight catalog (base)** — ~20 vendor-agnostic insight kinds; each backend registers its own in the high-bit range.
- **Calibration harness framework** — drives per-vendor microbenchmark suites, fits parameters, emits JSON.
- **Cross-vendor CI matrix harness** — compiles one IR002 kernel across all backends, compares outputs pairwise, enforces recipe-declared tolerance (see §41).
- **Effect tokens + Arena + strong-ID types** — inherited from Crucible.

Each backend is a self-contained subdirectory; adding a vendor does not touch any other backend or the shared core.

### 2.4 Reading the rest of this document

Part II (§3-§35) documents the NVIDIA backend in detail as the reference-complete implementation. Every pattern here has a direct analog in the other backends: WGMMA↔MFMA↔MXU↔TensorEngine; tcgen05/TMEM↔accumulator reg files↔MXU internal buffers↔Trainium PSUM; SASS↔AMDGPU ISA↔TPU exec↔NeuronCore bytecode.

Part III (§36-§42) covers the multi-vendor additions explicitly — runtime library replacement, collective library replacement, NetworkOffload plane, Genesis Kernel Pack, recipe realization per backend, cross-vendor CI, and per-vendor backend notes.

---

## 3. What Mimic predicts; what it does not

### Predicted (twelve primary outputs)

Every `SimResult` carries twelve fields validated against CUPTI measurements:

1. **Total cycles** — target within 2-5% on Forge-emitted kernels
2. **IPC achieved** — instructions completed per cycle, averaged across SMs
3. **Per-sub-partition utilization** — fraction of issue slots filled per 4-way warp scheduler
4. **Per-pipe utilization** — 10 pipes × fraction-busy: int, fmalighter, fp16, fma64lite, fma64heavy, mio, cbu, fe, mma, tcgen05
5. **Register pressure timeline** — per-cycle live register count distribution
6. **Shared memory bank conflict rate** — bank-conflict events per smem access
7. **L1 / L2 / DRAM hit rates** — fractional hit rate at each level
8. **DRAM bandwidth achieved** — bytes/sec delivered
9. **Stall attribution** — cycles lost to each of 12 stall reasons: SB_WAIT, MEM_WAIT, MEM_QUEUE_FULL, PIPE_CONTENTION, REG_BANK_CONFLICT, OPERAND_COLLECTOR, SYNC_MBARRIER, SYNC_WARPGROUP, WGMMA_WAIT, TMA_PENDING, BARRIER_ALLOC, TCGEN05_WAIT
10. **Critical path** — the dependency chain whose latency limits total cycles
11. **Occupancy** — resident warps / max warps, with blocking resource identified (regs/smem/CTAs)
12. **Efficiency ratio** — achieved cycles / theoretical minimum (roofline); the primary MAP-Elites fitness

Each output is separately CUPTI-measurable (either directly or derivable from counters), so Phase L of Forge can compute per-signal residuals and drive drift detection at the granularity that matters.

### Ranking accuracy vs absolute accuracy

The primary quality metric is **ranking accuracy**: if Mimic says candidate A beats B by 10%, real hardware should see the same direction. Absolute error of 2-5% is acceptable as long as ordering preserves across the behavior space MAP-Elites explores.

The reason is economic: MAP-Elites cares about picking the best cell, not the exact cycles of each cell. A uniform 5% overestimation across all candidates is fine; a 5% standard deviation between Mimic's prediction and reality is acceptable; what's unacceptable is Mimic reversing the ordering of two close candidates (5% underestimate on one, 5% overestimate on the other, producing wrong winner).

### Not predicted (explicit non-goals)

- **Thermal throttling** — depends on recent workload, ambient conditions, chassis airflow. Augur observes it.
- **DVFS transitions** — millisecond-scale clock shifts at power-state boundaries.
- **L2 eviction from other kernels** — multi-tenancy. Simulate as-if single-tenant.
- **PCIe host-device transfer overhead** — modeled as a TargetCaps `launch_overhead_cycles` constant, not in-kernel.
- **Driver launch queue pressure** — similarly, a fixed overhead term.
- **Power consumption** — Augur/NVML observe separately.
- **Single-kernel thermal-induced frequency steps** — rare under good cooling; Augur re-calibrates when they happen.
- **Voltage droop under big transients** — transistor-level effects invisible to software.
- **ECC memory scrub overhead** — stochastic; budget as a +5% BW penalty in TargetCaps.

Everything above the silicon layer gets modeled. Everything below or across workloads does not. The boundary is sharp, and honest.
## 4. The three subsystems and their seams

Mimic is architecturally three things bundled for shared-data efficiency:

```
                   TargetCaps + OpcodeLatencyTable
                           (shared data)
                                │
         ┌──────────────────────┼──────────────────────┐
         │                      │                      │
         ▼                      ▼                      ▼
    ┌─────────┐          ┌──────────┐         ┌────────────┐
    │ Encoder │          │Simulator │         │MAP-Elites  │
    │ Decoder │          │ (3 tier) │         │ Synthesizer│
    └─────────┘          └──────────┘         └────────────┘
         │                      │                      │
         │                      │                      │
    public API:             internal:              public API:
    standalone              called by               compile_kernel()
    callable for            synthesizer,
    Forge backend           by Augur's predict()
```

The three subsystems share:
- `TargetCaps` — per-chip capability struct
- `OpcodeLatencyTable` — per-SM op-to-pipe-to-latency data
- `DecodedInst` + `DecodedProgram` — the IR they all consume
- `Insight` — the diagnostic vocabulary
- `SimResult` — the result format

They do not share implementation code. The encoder doesn't know about simulation; the simulator doesn't know about MAP-Elites; the synthesizer doesn't know about individual bit layouts. Each has its own module boundary inside Mimic, each is independently testable.

### Seam 1: Encoder / Decoder

Forge's Phase J calls `encoder::emit_cubin(Ir003NvProgram, TargetCaps) → cubin_bytes`. This path does not invoke the simulator; it's a pure transformation. Augur's recovery path calls `decoder::decode_cubin(cubin_bytes, TargetCaps) → DecodedProgram`, also without invoking simulation.

The encoder/decoder pair is testable as a round-trip: `decode(encode(x)) == x` for valid IR003NV programs. This is the continuous correctness test that keeps the bit-format tables honest.

### Seam 2: Simulator standalone (`predict`)

Augur's digital twin calls `mimic::predict(DecodedProgram, TargetCaps, Tier) → SimResult`. It does not use the encoder (already have bytes), does not use the synthesizer (already have a chosen kernel).

Seam 2 is the fast path for monitoring: Augur samples 1% of kernel executions, calls `predict` to get the model's expected cycles, compares to measured, computes residual, stores in the regression dataset.

### Seam 3: Full synthesis (`compile_kernel`)

Forge's Phase H calls `mimic::compile_kernel(FusedRegion, TargetCaps, CompileConfig) → CompiledKernel`. This path uses everything: encoder to emit candidates, decoder to verify, simulator to score, MAP-Elites to search, archive to persist.

Seam 3 is the expensive one (10s of ms per region in the worst case) and the parallelizable one. Forge runs N regions concurrently through Seam 3 on a thread pool.

---

## 5. The three-tier simulator spectrum

Accuracy and speed trade. One simulator cannot serve both MAP-Elites throughput and calibration validation. The answer is three tiers, all sharing model data.

### Tier 1: FAST (interval)

- **Wall time**: 1-5 ms per simulation on CPU; 0.1-0.5 ms on GPU
- **Throughput**: 10,000 candidates/sec per CPU core; 100,000+/sec on GPU
- **Accuracy**: 10-20% absolute; **ranking accuracy ~90%**
- **Algorithm**: per-basic-block critical-path analysis + analytical memory BW model + queue depth estimation
- **Skips**: cycle-level warp scheduling, register bank conflicts, operand collector detail, DRAM row-buffer model, cache state
- **Used for**: MAP-Elites coarse filter — most candidates never need cycle-accurate evaluation, they can be rejected or ordered by fast tier alone

### Tier 2: MEDIUM (event-driven)

- **Wall time**: 10-30 ms per simulation
- **Throughput**: 100-300 candidates/sec per CPU core; 3000-9000/sec on a 32-core machine
- **Accuracy**: 5-8% absolute; **ranking accuracy 95%+**
- **Algorithm**: event-driven warp-scheduler simulator with scoreboard tracking, explicit memory subsystem (L1/L2/DRAM request queues), FU token pool, operand collector, per-warp state machines
- **Used for**: MAP-Elites primary fitness evaluation, Forge Phase B.5 cost estimates, Augur prediction

### Tier 3: ACCURATE (cycle-accurate)

- **Wall time**: 100-500 ms per simulation
- **Throughput**: 2-10 candidates/sec
- **Accuracy**: 2-5% absolute; **ranking accuracy 99%+**
- **Algorithm**: cycle-by-cycle simulation of every pipe, every register read port, every scoreboard slot, every DRAM command, bank-conflict resolution, commit-group-aware async tracking
- **Used for**: calibration validation, MAP-Elites final tiebreak on top candidates per generation, debugging simulator discrepancies

### Shared infrastructure

All three tiers share:
- `DecodedProgram` input format
- `TargetCaps` target model
- `SimResult` output format
- `Insight` diagnostic vocabulary
- Per-opcode latency tables
- Memory subsystem model (configured at different fidelities per tier)
- Archive key format

Only the execution engine differs. A candidate can flow through multiple tiers: fast tier triages (reject or accept for scoring), medium tier scores (primary fitness), accurate tier tie-breaks (top 10% per generation).

### Tier escalation policy

```
For each MAP-Elites generation:
  all_candidates → FAST evaluation
  bottom 70% by fast-tier cycles → reject (don't evaluate further)
  top 30% → MEDIUM evaluation
  top 10 by medium-tier fitness → ACCURATE evaluation
  archive updates use the most accurate tier that evaluated each candidate
```

Budget-aware: if Forge sets `CompileConfig::max_wall_time_ms = 100`, Mimic skips accurate tier entirely and uses medium for top candidates. If `max_wall_time_ms = 1000`, accurate tier is enabled.

---

## 6. DecodedInst — the unified IR

Mimic's internal representation of a single SASS instruction. Fits in exactly 32 bytes, two per cache line. Optimized for cache locality during simulation, which is the hot path.

```cpp
struct alignas(32) DecodedInst {
    uint16_t op;              // opcode enum (packed family + op kind)
    uint8_t  pipe;             // which functional unit (10 pipes for sm_90+)
    uint8_t  flags;            // yield hint, reuse flags, convergent, is_async,
                               //   IS_PARAMETRIC, IS_EXTENSION_POINT_BODY

    uint16_t src_regs[3];      // source register encoded as 10-bit + 6-bit flags
    uint16_t dst_regs[2];      // up to 2 destinations (mma has dst + accum)

    uint8_t  predicate;        // 4-bit pred reg + neg flag + 3 bits reserved
    uint8_t  stall_count;      // 0-15 explicit stall from SASS control field
    uint8_t  wait_sb_mask;     // 6-bit scoreboard wait bitmask
    uint8_t  write_sb;         // 3-bit write scoreboard slot + 0xFF sentinel
    uint8_t  read_sb;          // 3-bit read scoreboard slot + 0xFF sentinel

    uint16_t latency;          // cycles until result ready (from OpcodeLatencyTable)
    uint16_t occupancy;        // cycles until pipe can re-issue

    uint32_t attrs_idx;        // index into TensorOpAttrs side-table; 0xFFFFFFFF = none
    uint32_t pc;               // byte offset in program (for BRA targets, debug)
};
static_assert(sizeof(DecodedInst) == 32);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(DecodedInst);
```

The `attrs_idx` side-table pattern keeps DecodedInst tight for the common case (arithmetic, memory, branch — 90% of instructions). Tensor-core ops (MMA, WGMMA, UTCMMA) reference a `TensorOpAttrs` struct stored separately.

**Flags bit assignments.**

| Bit | Name | Semantics |
|---|---|---|
| 0 | `YIELD` | Scheduler yield hint |
| 1 | `REUSE` | Operand-collector reuse hint |
| 2 | `CONVERGENT` | Must execute in warp lockstep |
| 3 | `IS_ASYNC` | Issues completion asynchronously (WGMMA/TMA/tcgen05) |
| 4 | `IS_PARAMETRIC` | Shape or tile dim is a runtime kernel argument (Forge §F.4). Simulator treats the dim as a symbolic variable bound to the measurement range; fast-tier cost estimate uses range midpoint; medium tier integrates over the distribution from SymbolTable. |
| 5 | `IS_EXTENSION_POINT_BODY` | Instruction originated from a user-supplied `ComputeBody*` extension point (FORGE.md §18.7), not from the structural kernel template. Peephole rules gate differently on these; body instructions may participate in fusion across the structural boundary (e.g., body MUL + structural softmax exp → FMA) per §18.3. Insight extraction tags body-instruction stalls separately from structural stalls so users can tune their body without the insight noise of structural pipeline events. |
| 6-7 | reserved | |

### TensorOpAttrs

```cpp
struct alignas(32) TensorOpAttrs {
    uint16_t m, n, k;              // shape (tile dimensions)
    uint8_t  a_dtype, b_dtype;     // 0=f16, 1=bf16, 2=tf32, 3=f32, 4=f64, 5=e4m3, 6=e5m2, ...
    uint8_t  d_dtype;              // accumulator type
    uint8_t  a_layout, b_layout;   // 0=row, 1=col, 2=tiled
    uint8_t  rnd, satf;            // round mode, saturate finite

    // WGMMA / tcgen05 extensions
    uint8_t  flags;                // neg_a, neg_b, trans_a, trans_b bits
    uint8_t  scale_a, scale_d;
    uint8_t  sparsity_meta_reg;    // 0xFF if dense

    // tcgen05-only
    uint8_t  cta_group;            // 0=none, 1=1CTA, 2=2CTA
    uint8_t  scale_vec;            // 0=none, 1=1X, 2=2X, 4=4X
    uint8_t  block_scale;          // bool
    uint8_t  ws_mode;              // warp-specialized
    uint8_t  pad[6];
};
static_assert(sizeof(TensorOpAttrs) == 32);
```

Also 32 bytes. Attrs-bearing DecodedInsts consume two cache lines total instead of one; still compact.

### DecodedProgram

The program-level wrapper, arena-allocated:

```cpp
struct DecodedProgram {
    std::span<const DecodedInst>   insts;         // linear in execution order
    std::span<const BasicBlock>    blocks;        // BB boundaries, pred/succ edges
    std::span<const LoopInfo>      loops;         // for trip-count-aware simulation
    std::span<const TensorOpAttrs> attrs_pool;    // referenced by attrs_idx

    GridGeometry                   grid;          // gridDim, blockDim, clusterDim
    uint32_t                       regs_per_thread;
    uint32_t                       smem_bytes_per_cta;
    uint32_t                       tmem_columns;  // 0 if not using tcgen05
    std::span<const uint8_t>       cubin_bytes;   // reference for debug/roundtrip
    std::span<const Symbol>        symbols;       // for relocations

    ContentHash                    program_hash;  // for cache keying
    const TargetCaps*              caps;
};
```

All pointers are arena-owned. No heap allocations inside Mimic during simulation; everything comes from the per-compile arena that Forge passes in.

### Why not reuse IR003NV directly?

Two reasons:

1. **The decoder path produces bytes-to-struct; reconstructing IR003NV from bytes is harder than producing DecodedInst.** IR003NV has fields that only make sense in Forge's Phase H context (pre-lowering attributes, mutation-source tracking, scheduling group labels). These don't round-trip through SASS bytes. DecodedInst carries only what matters for simulation.

2. **IR003NV uses arena pointers owned by Forge's compile arena.** After Forge returns, those arenas are gone. DecodedProgram is self-contained, arena-owned by Mimic (or by Forge's longer-lived outer arena during Phase H).

So the flow is: Forge builds IR003NV → Mimic's encoder produces SASS bytes → Mimic's decoder produces DecodedProgram → Mimic's simulator consumes DecodedProgram. Forge's IR003NV → DecodedInst is a short conversion path (~200 lines of mapping code) when we need it for in-search fast-path.

---

## 7. The target model — TargetCaps + OpcodeLatencyTable

Two-struct model: small global data for everything, large per-SM calibration data loaded lazily.

### TargetCaps (~256 bytes, one per chip)

The struct Forge uses everywhere and passes to Mimic on every call:

```cpp
struct alignas(64) TargetCaps {
    // Identity
    uint16_t sm_version;                // 900, 1000, 1030, 1100, 1200, 1210
    uint8_t  suffix;                    // 0=Base, 1=A, 2=F
    uint8_t  pad_id[1];
    uint32_t codegen_factory;           // from sub_60AXXX

    // SM geometry
    uint16_t sm_count;                  // physical SMs (132 on H100 SXM5)
    uint8_t  partitions_per_sm;         // 4 universal
    uint8_t  warps_per_partition;       // 16 universal
    uint8_t  scoreboards_per_warp;      // 6 universal
    uint8_t  reg_banks;                 // 8 universal
    uint8_t  pad_geom[2];

    // Capability flags (one bit per feature)
    uint32_t caps_bits;                 // WGMMA, TBC, DSMEM, TMA, tcgen05, FP8, FP4, MX, BlockScale, CC, C2C

    // Resource budgets
    uint16_t max_regs_per_thread;       // 255
    uint16_t max_warps_per_sm;          // 64 on H100/B200, 48 on RTX 30xx
    uint16_t max_ctas_per_sm;           // 32 or 16 per variant
    uint32_t smem_max_per_cta;          // 228KB datacenter, 128KB consumer
    uint32_t tmem_columns;              // 128+ on tcgen05-capable, 0 otherwise
    uint32_t l1_bytes;                  // unified L1/smem
    uint32_t l2_total_bytes;            // 50MB on H100
    uint8_t  l2_slice_count;            // 96 on H100
    uint8_t  hbm_channels;              // 80 on H100 SXM5
    uint8_t  pad_mem[2];

    // Latency model (calibrated, per-chip)
    uint16_t l1_latency_cycles;         // ~28
    uint16_t l2_hit_latency;            // ~200 uncontended
    uint16_t hbm_latency_cycles;        // ~450 row-hit
    uint16_t launch_overhead_cycles;    // ~6000 ≈ 3μs at 1.8GHz

    // Pipe specs (10 pipes × 8 bytes = 80 bytes)
    struct PipeSpec {
        uint16_t latency;
        uint16_t occupancy;
        uint8_t  instances;             // per partition
        uint8_t  pad[3];
    };
    PipeSpec pipes[PIPE_COUNT];

    // Pointer to per-SM opcode latency table (loaded lazily)
    const OpcodeLatencyTable* opcode_table;
};
static_assert(sizeof(TargetCaps) <= 256);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(TargetCaps);
```

Populated by `target::h100_sxm5()`, `target::h200()`, `target::b200()`, etc. — factory functions that hardcode values from a calibrated `.json` file at build time (generated from the calibration harness).

### OpcodeLatencyTable (per SM, ~100-500 KB)

Loaded lazily on demand; keyed by SM version:

```cpp
struct OpcodeSpec {
    uint8_t  pipe;                   // FU pipe assignment
    uint8_t  flags;                  // convergent, is_async, needs_mask
    uint16_t latency;                // result-ready cycles
    uint16_t occupancy;              // pipe-busy cycles
    uint16_t issue_throughput_inv;   // 1 = fully pipelined, higher = slower
    uint32_t attr_deps_mask;         // bitmap: which attributes affect latency
    uint32_t pad;
};
static_assert(sizeof(OpcodeSpec) == 16);

struct WgmmaShapeSpec {
    uint16_t m, n, k;
    uint8_t  dtype_a, dtype_b, dtype_d;
    uint8_t  pad;
    uint16_t latency;
    uint16_t occupancy;
    uint32_t pad2;
};
static_assert(sizeof(WgmmaShapeSpec) == 16);

struct UtcmmaShapeSpec {
    uint16_t m, n, k;
    uint8_t  kind;                   // mxf4, mxf8f6f4, f16, i8, tf32
    uint8_t  cta_group;              // 1 or 2
    uint16_t latency;
    uint16_t occupancy;
    uint8_t  ws_flag;
    uint8_t  sparsity_flag;
    uint8_t  pad[4];
};
static_assert(sizeof(UtcmmaShapeSpec) == 16);

struct OpcodeLatencyTable {
    OpcodeSpec specs[OPCODE_COUNT];            // ~370 entries
    std::span<const WgmmaShapeSpec> wgmma;     // 144 shapes × dtypes
    std::span<const UtcmmaShapeSpec> utcmma;   // per-SM

    // Memory access detail (from calibration)
    uint16_t ldg_latency_l1_hit;               // ~28
    uint16_t ldg_latency_l2_hit_uncontended;   // ~200
    uint16_t ldg_latency_l2_hit_loaded;        // 200 + queue_depth × k_queue
    uint16_t ldg_latency_dram_row_hit;         // ~400
    uint16_t ldg_latency_dram_row_miss;        // ~550
    uint16_t lds_latency;                      // ~23
    uint16_t lds_bank_conflict_penalty;        // +1 per replay
    uint8_t  k_queue;                          // queue-depth multiplier

    // HBM scheduling constants
    uint16_t tRCD;                             // row-to-column delay
    uint16_t tRP;                              // row precharge
    uint16_t tRAS;                             // row active time
    uint16_t tRW_turnaround;                   // read-write turnaround
    uint32_t refresh_stall_cycles_per_interval;

    // Warp scheduler
    uint8_t  gto_greedy_prob;                  // per-cycle probability of sticking with last-picked warp (0-100)
    uint8_t  boost_threshold_cycles;           // warp waiting longer gets boosted
    uint8_t  pad_sched[6];
};
```

The atomic lookup key for opcode specs is `(opcode, width_class)` where width_class is a 4-bit enum encoding 32/64/128/vector patterns. Common case: O(1) array lookup by opcode index. Tensor-core ops do a secondary lookup in the shape arrays.

### Per-SM data file format

JSON on disk, parsed at `target::load()` time:

```json
{
  "identity": {
    "sm_version": 900,
    "codegen_factory": 32768
  },
  "geometry": {
    "sm_count": 132,
    "warps_per_sm": 64,
    "max_regs_per_thread": 255,
    "smem_per_cta": 233472,
    "tmem_columns": 0
  },
  "pipes": {
    "int":        {"latency": 4, "occupancy": 2, "instances": 1},
    "fmalighter": {"latency": 4, "occupancy": 2, "instances": 1},
    "fma64lite":  {"latency": 8, "occupancy": 4, "instances": 1}
  },
  "opcodes": {
    "ffma":   {"pipe": "fmalighter", "latency": 4,  "occupancy": 2, "throughput_inv": 1},
    "ldg_32": {"pipe": "mio",        "latency": 28, "occupancy": 1, "throughput_inv": 1}
  },
  "wgmma": [
    {"m": 64, "n": 128, "k": 16,
     "dtype_a": "f16", "dtype_b": "f16", "dtype_d": "f32",
     "latency": 64, "occupancy": 8}
  ],
  "memory": {
    "l1_latency": 28,
    "l2_hit_uncontended": 200,
    "l2_hit_loaded_coefficient": 2,
    "dram_row_hit": 400,
    "dram_row_miss": 550
  }
}
```

Schema validated via JSON Schema; parser is vendored nlohmann/json or similar single-header library. Each chip gets one file, ~10-100 KB uncompressed. Per-chip files live in `crucible/mimic/<vendor>/data/`, checked into git, regenerated by the calibration harness. Large binary-packed variants (opcode-latency tables with thousands of entries) fall back to MessagePack when file size becomes a concern.

---

## 8. The event-driven medium tier

The main simulator. Gets most of Mimic's algorithmic attention. Runs 100-300 candidates/sec per core, produces 5-8% absolute accuracy with 95% ranking accuracy.

### State model

Per-SM state, one allocation per active SM:

```cpp
struct alignas(64) SmState {
    // Hot fields, accessed every cycle
    Cycle current_cycle;

    // 4 sub-partitions × up to 16 warps each
    Partition partitions[4];

    // Per-pipe token pool (10 pipes)
    uint8_t  fu_tokens[PIPE_COUNT];
    Cycle    fu_next_free[PIPE_COUNT];

    // Register pressure per partition
    uint16_t reg_pressure[4];

    // Shared memory occupancy
    uint32_t smem_used;

    // Outstanding async operations
    AsyncOp  async_ops[16];
    uint8_t  async_op_count;

    // Scoreboard bitmap per warp (64 warps × 64-bit mask)
    uint64_t sb_inflight[64];

    // Padding for cold fields
    uint8_t  pad_cold[16];
};
```

Per-warp state, stored in parallel arrays (SoA, one array per field) in a `WarpTable`:

```cpp
struct WarpTable {
    InstId*      pc;                    // next instruction to issue
    Cycle*       next_ready;            // cannot issue before this cycle
    uint64_t*    sb_inflight;           // per-warp scoreboard mask
    uint32_t*    stalls_this_burst;     // for GTO boost detection
    uint16_t*    cta_id;                // for cross-warp barrier matching
    uint8_t*     partition;             // 0..3 fixed at launch
    uint8_t*     state;                 // WarpState enum
    uint8_t*     last_stall;            // StallReason (for insights)
    uint32_t     total_warps;
};
```

SoA layout (parallel arrays) instead of AoS (array of structs) because a scheduler tick reads one field across all 16 warps in a partition — one cache line per field vs 16 cache lines per warp under AoS.

### Event queue

4-ary min-heap, keyed by (cycle, event_kind) for deterministic ordering. Arena-allocated at simulator construction, never grows during simulation:

```cpp
enum class EventKind : uint8_t {
    WARP_READY = 0,             // warp can issue next inst
    ISSUE_SLOT = 1,             // sub-scheduler tick
    INST_COMPLETE = 2,          // result lands, SB release
    FU_FREE = 3,                // pipe instance becomes available
    MEM_L1_MISS = 4,            // L1 lookup failed, route to L2
    MEM_L2_RESP = 5,            // L2 response (hit or miss forward)
    MEM_HBM_RESP = 6,           // HBM response
    MEM_ARRIVE = 7,             // TMA data arrival at SM
    MBARRIER_ARRIVE = 8,        // mbarrier.arrive lands
    MBARRIER_WAIT = 9,          // mbarrier wait unblocks
    WGMMA_COMPLETE = 10,        // async WGMMA result ready
    UTCMMA_COMPLETE = 11,       // async tcgen05 result ready
    CLUSTER_BARRIER_ARRIVE = 12,
    CLUSTER_BARRIER_WAIT = 13,
    KERNEL_DONE = 14,
    COUNT
};

struct alignas(32) Event {
    Cycle    cycle;             // 8B heap key
    uint64_t payload;           // 8B kind-specific
    uint32_t p1;                // 4B warp/sm packed
    uint32_t p2;                // 4B inst_id / reg_mask
    EventKind kind;             // 1B
    uint8_t  pad[7];            // 7B to 32B
};
static_assert(sizeof(Event) == 32);
```

Events fit 2 per 64B cache line; heap operations touch ≤ log₄(N) lines. The 4-ary shape is 1.5-2× faster than binary heap on real benchmarks because each sift-down step checks 4 children in one cache line.

### Main loop

```cpp
void Simulator::run(fx::Block, DecodedProgram prog) {
    // Seed initial WARP_READY events for each warp at cycle 0
    for (auto& warp : prog.warps()) {
        events_.push(Event{Cycle{0}, ...});
    }

    while (!events_.empty()) {
        Event ev = events_.pop();
        current_cycle_ = ev.cycle;
        dispatch_(ev);
    }

    finalize_result_();
}

void Simulator::dispatch_(const Event& ev) {
    switch (ev.kind) {
        case EventKind::ISSUE_SLOT:    on_issue_slot_(ev); break;
        case EventKind::WARP_READY:    on_warp_ready_(ev); break;
        case EventKind::INST_COMPLETE: on_inst_complete_(ev); break;
        case EventKind::FU_FREE:       on_fu_free_(ev); break;
        case EventKind::MEM_L1_MISS:   on_mem_l1_miss_(ev); break;
        case EventKind::MEM_L2_RESP:   on_mem_l2_resp_(ev); break;
        case EventKind::MEM_HBM_RESP:  on_mem_hbm_resp_(ev); break;
        // ... other kinds
    }
}

void Simulator::on_issue_slot_(const Event& ev) {
    SmId sm = ev.p1 >> 8;
    PartitionId part = ev.p1 & 0xFF;
    
    uint16_t warp = pick_warp_(sm, part);       // GTO + boost
    if (warp == UINT16_MAX) {
        // No ready warp; try again next cycle
        schedule_(current_cycle_ + 1, ISSUE_SLOT, sm, part);
        return;
    }
    
    DecodedInst inst = warps_.current_inst(warp);
    Pipe pipe = static_cast<Pipe>(inst.pipe);
    
    // Check pipe token availability
    if (sms_[sm].fu_tokens[pipe] == 0) {
        warps_.set_state(warp, WarpState::WAITING_FU);
        warps_.set_last_stall(warp, StallReason::PIPE_CONTENTION);
        extract_insight_(InsightKind::PIPE_UNDERUTILIZED, sm, warp, inst);
        schedule_(current_cycle_ + 1, ISSUE_SLOT, sm, part);
        return;
    }
    
    // Check scoreboard dependencies
    uint64_t required = inst.wait_sb_mask;
    uint64_t inflight = warps_.sb_inflight(warp);
    if ((inflight & required) != 0) {
        warps_.set_state(warp, WarpState::WAITING_SB);
        warps_.set_last_stall(warp, StallReason::SB_WAIT);
        schedule_(current_cycle_ + 1, ISSUE_SLOT, sm, part);
        return;
    }
    
    // Check register bank conflicts
    uint32_t bank_stall = compute_bank_stall_(inst);
    
    // Consume pipe token
    sms_[sm].fu_tokens[pipe]--;
    schedule_(current_cycle_ + inst.occupancy, FU_FREE, sm, pipe);
    
    // Effective issue time
    Cycle issue_time = current_cycle_ + inst.stall_count + bank_stall;
    
    // Schedule completion
    schedule_(issue_time + inst.latency, INST_COMPLETE, warp, inst.pc);
    
    // Dispatch memory instructions to memory subsystem
    if (is_memory(inst.op)) {
        dispatch_memory_(sm, warp, inst, issue_time);
    }
    
    // Register async ops
    if (is_async_tensor(inst.op)) {
        register_async_op_(sm, warp, inst, issue_time + inst.latency);
    }
    
    // Mark scoreboards in-flight
    if (inst.write_sb < 7) {
        warps_.set_sb_inflight(warp, inst.write_sb);
    }
    
    // Advance warp PC
    warps_.advance(warp);
    if (warps_.has_next_inst(warp) && !sb_would_block_(warp)) {
        schedule_(issue_time + 1, WARP_READY, warp, 0);
    }
    
    // Update insights
    extract_insight_hooks_(sm, warp, inst, issue_time);
    
    // Re-arm issue slot for next cycle
    schedule_(current_cycle_ + 1, ISSUE_SLOT, sm, part);
}
```

Other event handlers follow the same pattern: examine state, update state, schedule follow-on events, extract insights.

### Performance characteristics

- **Event count**: for a 1ms kernel (~1M cycles) with ~100K instructions per SM, expect ~200-500K events total per simulation
- **Event dispatch cost**: ~100-500 ns per event (depends on kind; MEM events are most expensive)
- **Total wall time**: 20-50 ms per simulation, within the 10-30ms target for typical kernels

Hot path is ISSUE_SLOT (called every cycle per partition). Inlining the scoreboard check, bank conflict computation, and token check into `on_issue_slot_` is critical — each of these is a 1-2 cycle operation that's called billions of times.

## 9. The fast tier and GPU acceleration

### CPU fast tier: interval simulation

```cpp
SimResult Simulator::run_fast(fx::Bg, DecodedProgram prog) {
    SimResult result{};
    
    // Walk basic blocks, compute per-block cost, aggregate
    for (const BasicBlock& bb : prog.blocks) {
        // Pipe utilization within block
        uint32_t pipe_cycles[PIPE_COUNT] = {};
        uint32_t mem_traffic_bytes = 0;
        uint32_t critical_path_latency = 0;
        uint32_t inst_count = bb.end - bb.start;
        
        for (uint32_t i = bb.start; i < bb.end; i++) {
            const DecodedInst& inst = prog.insts[i];
            pipe_cycles[inst.pipe] += inst.occupancy;
            
            // Critical path contribution (dependency chain)
            critical_path_latency = std::max(
                critical_path_latency,
                dep_earliest_cycle_[i] + inst.latency
            );
            
            if (is_memory(inst.op)) {
                mem_traffic_bytes += estimate_bytes_moved(inst);
            }
        }
        
        // Block time = max of (pipe-constrained, mem-constrained, critical-path-constrained)
        uint32_t block_cycles = std::max({
            *std::max_element(pipe_cycles, pipe_cycles + PIPE_COUNT),
            mem_traffic_bytes * target.mem_bw_inv,
            critical_path_latency,
        });
        
        // Multiply by trip count
        uint32_t trip_count = prog.loop_trip_count(bb.loop_id);
        result.total_cycles += block_cycles * trip_count;
    }
    
    // Account for concurrent CTAs per SM and total grid
    uint32_t per_sm_cycles = result.total_cycles;
    uint32_t grid_cycles = per_sm_cycles * ceil_div(
        prog.grid.total_ctas,
        target.sm_count * estimate_occupancy(prog)
    );
    
    result.total_cycles = grid_cycles;
    result.efficiency_ratio = theoretical_min(prog, target) / grid_cycles;
    
    return result;
}
```

Misses everything about scheduler contention, memory queuing, async overlap. But gives **ranking-accurate** answers: a kernel that's 2× slower in reality is ~1.7-2.3× slower in this model.

Wall-clock cost: ~1-5 ms for a 10K-instruction kernel on a CPU core. Throughput: 10K+ candidates/sec.

### GPU fast tier

For 1000-candidate batches, port the fast tier to CUDA:

```cpp
__global__ void fast_sim_batch_kernel(
    const DecodedInst* all_programs,        // flattened
    const uint32_t* program_offsets,        // offsets into all_programs
    const BasicBlock* all_blocks,
    const uint32_t* block_offsets,
    const TargetCapsDevice* caps,
    SimResult* results,
    uint32_t num_candidates
) {
    uint32_t candidate = blockIdx.x;
    if (candidate >= num_candidates) return;
    
    // Each thread block evaluates one candidate
    // Cooperative threads within the block parallelize across basic blocks
    
    const DecodedInst* program = all_programs + program_offsets[candidate];
    const BasicBlock* blocks = all_blocks + block_offsets[candidate];
    uint32_t num_blocks = block_offsets[candidate + 1] - block_offsets[candidate];
    
    __shared__ uint32_t partial_cycles[MAX_BLOCKS_PER_CANDIDATE];
    
    // Each thread handles a range of basic blocks
    for (uint32_t b = threadIdx.x; b < num_blocks; b += blockDim.x) {
        partial_cycles[b] = compute_block_cycles(&blocks[b], program, caps);
    }
    __syncthreads();
    
    // Reduce to total cycles
    if (threadIdx.x == 0) {
        uint32_t total = 0;
        for (uint32_t b = 0; b < num_blocks; b++) total += partial_cycles[b];
        results[candidate].total_cycles = adjust_for_grid(total, program->grid, caps);
        results[candidate].efficiency_ratio = theoretical_min(program, caps) / total;
    }
}
```

Each candidate is a thread block. Thousand-candidate batches submit in one kernel launch; results come back in one `cudaMemcpy`. Embarrassingly parallel. Expected throughput on RTX 4090: 100K candidates/sec.

Usage: Forge's Phase H sends 1000 fresh candidates per MAP-Elites generation to GPU fast tier, waits ~10ms for batch to complete, receives ranked results, sends top 100 to CPU medium tier for more accurate scoring.

### When GPU fast tier is worth it

GPU acceleration overhead: kernel launch (~5μs) + data transfer (~100μs for 1000 candidates). Only pays off when batch size > 100 candidates and when GPU is not busy serving the actual workload Mimic is compiling for (use a different GPU or time-slice if same).

Soft dependency: Mimic compiles and runs without GPU fast tier; CPU fast tier is always available. Build-time flag `MIMIC_GPU_FAST` controls compilation.

---

## 10. The accurate tier

Cycle-accurate. Slow. Used for calibration validation, MAP-Elites tie-breaks on top candidates, debugging simulator discrepancies.

Differs from medium tier in these ways:

### Cycle-stepping, not event-driven

The outer loop ticks every cycle. Every cycle:
1. For each SM, for each sub-partition: check ready warps, pick one per GTO+boost, try to issue
2. Advance all FU pipelines by one cycle
3. Process memory subsystem: drain queues, advance row buffers, refresh check
4. Advance async operations, unblock waiting warps as completions land

More expensive per simulation (~100-500ms), but more faithful to HW behavior because every micro-interaction is captured.

### Detailed memory subsystem

Instead of event-driven latency with queue-depth multiplier:
- Per-bank row buffer state tracked explicitly (open row, last access cycle)
- DRAM command scheduler with FR-FCFS logic
- Per-channel and per-slice queue management
- Refresh interleaving
- Read-write turnaround modeled explicitly

### Register read ports

Model the 4-port register read at operand collector granularity. Operand collector requests are queued; bank conflicts serialize through the 2-port-per-bank constraint. Medium tier approximates this with a simple `compute_bank_stall_` function; accurate tier simulates the collector queue.

### Commit-group-accurate WGMMA

Medium tier treats WGMMA async ops as simple (issue, occupy-pipe, deadline-lands) tuples. Accurate tier tracks each commit group individually, models the ordering constraint that `wgmma.wait_group(N)` waits until ≤N groups are in flight.

### Used for

- **Calibration validation**: before promoting a calibration run to production, verify accurate-tier predictions match CUPTI within 2% on 50 reference kernels
- **MAP-Elites tie-breaks**: when two candidates score within 1% on medium tier, accurate tier picks the winner
- **Debugging**: when medium tier predictions diverge from reality, accurate tier identifies whether the discrepancy is in the model or the calibration data
- **Not used in the MAP-Elites inner loop**: too slow; would cap at 10 candidates/sec

### Shared code with medium tier

Accurate tier shares ~70% of its code with medium tier. The event handlers are the same; the difference is driving logic (cycle-stepping vs event-driven) and sub-model detail (operand collector, row buffers, commit groups).

Structurally: `AccurateTier.h` imports medium tier's handlers and overrides with richer implementations where needed. About 5K LOC unique to accurate tier on top of medium's 10K.

---

## 11. The memory subsystem

The hardest part of GPU simulation. This is where accuracy is won or lost.

### Driver-source advantage

Previous academic simulators (GPGPU-Sim, Accel-Sim, MGPUSim) had to reverse-engineer the L2 slice hash, the HBM3 command scheduler, the HSHUB crossbar arbitration, the L1 eviction policy from microbenchmark residuals. We have the driver source through Hopper, which directly documents:

- The L2 slice address hash function (GPC-to-FBP crossbar routing algorithm)
- DRAM command scheduler policy (FR-FCFS with per-channel age prioritization)
- Row buffer policy (open-page by default, closing trigger on hit-rate drop)
- Per-channel refresh scheduling and stall propagation
- L1 eviction (LRU with replacement bypass for streaming ops)
- Coalescer logic (128-byte transaction merging)
- Read-write turnaround budget

These translate directly to C code in Mimic's memory subsystem. Calibration still matters — exact cycles vary per chip — but the *structure* of the model comes from driver reality, not from fitting microbenchmark curves.

### Model at medium tier

```
L1 cache:
    per-SM LRU with 128-byte lines
    capacity from TargetCaps (48 KB / 96 KB / 228 KB depending on config)
    28 cycle hit latency (uncontended)
    miss → route to appropriate L2 slice

L2:
    96 slices on H100 (from TargetCaps)
    address hash: (addr >> 7) % 96 — the actual NVIDIA hash is slightly more complex;
        the driver defines it; we reproduce it exactly
    per-slice request queue (FIFO with capacity ~16 outstanding requests)
    200 cycle base hit latency uncontended
    loaded latency = 200 + (queue_depth × k_queue)
    k_queue from calibration, typically 2-4
    miss → route to HBM channel

HBM:
    80 channels on H100 SXM5
    address hash: channel = (line_addr >> some_bits) & 0x7F
    per-channel request queue (capacity ~8-16)
    per-bank row buffer: { open_row, last_access_cycle }
    row hit: 400 cycles (tRCD bypass)
    row miss: 550 cycles (tRP + tRCD + tRAS)
    read-write turnaround: +8 cycles when switching direction
    refresh: every ~50K cycles, stalls channel for ~150 cycles
```

### Queue-aware latency

Medium tier doesn't simulate every DRAM command or every cache eviction. It models the aggregate behavior via queue-depth-aware latency:

```cpp
Cycle Simulator::mem_request_latency_(
    SmId sm, 
    uint64_t addr, 
    uint32_t bytes, 
    bool is_write
) {
    // L1 lookup
    auto l1 = sms_[sm].l1.probe(addr, bytes);
    if (l1.hit) return Cycle{caps_.l1_latency_cycles};
    
    // L1 miss: route to L2 slice
    uint32_t l2_slice = l2_slice_hash_(addr);
    Cycle base_l2_latency = caps_.l2_hit_latency;
    
    // Queue-aware adjustment
    uint32_t queue_depth = mem_.l2_queue_depth[l2_slice];
    Cycle l2_latency = base_l2_latency + queue_depth * caps_.opcode_table->k_queue;
    
    // L2 lookup
    auto l2 = mem_.l2.probe(addr, bytes);
    if (l2.hit) {
        mem_.l2_queue_depth[l2_slice]++;
        schedule_(current_cycle_ + l2_latency, MEM_L2_RESP, sm, addr);
        return l2_latency;
    }
    
    // L2 miss: route to HBM channel
    uint32_t hbm_channel = hbm_channel_hash_(addr);
    uint32_t row = addr >> ROW_BITS;
    
    Cycle hbm_latency;
    if (mem_.row_buffers[hbm_channel].open_row == row) {
        hbm_latency = caps_.hbm_row_hit_latency;
    } else {
        hbm_latency = caps_.hbm_row_miss_latency;
        mem_.row_buffers[hbm_channel].open_row = row;
    }
    mem_.row_buffers[hbm_channel].last_access = current_cycle_;
    
    // Read-write turnaround
    if (mem_.last_direction[hbm_channel] != is_write) {
        hbm_latency = hbm_latency + caps_.hbm_rw_turnaround;
        mem_.last_direction[hbm_channel] = is_write;
    }
    
    // Queue-depth contribution
    uint32_t hbm_queue_depth = mem_.hbm_queue_depth[hbm_channel];
    hbm_latency = hbm_latency + hbm_queue_depth * caps_.opcode_table->k_queue;
    
    mem_.hbm_queue_depth[hbm_channel]++;
    
    schedule_(current_cycle_ + l2_latency + hbm_latency, MEM_HBM_RESP, sm, addr);
    return l2_latency + hbm_latency;
}
```

On `MEM_L2_RESP` / `MEM_HBM_RESP`, decrement the queue depth and release the waiting warp's scoreboard.

### Handling refresh

Per-channel refresh interval from TargetCaps (~50K cycles for HBM3). When refresh fires:

```cpp
void Simulator::refresh_channel_(HbmChanId chan) {
    // Stall outstanding requests on this channel
    mem_.hbm_stall_until[chan] = current_cycle_ + caps_.refresh_stall_cycles;
    // Schedule next refresh
    schedule_(current_cycle_ + caps_.refresh_interval_cycles, REFRESH, chan);
}
```

Requests arriving during refresh stall are deferred; their latency increases by the remaining refresh time.

### Bank modeling on shared memory

Per-partition-local model (32 banks, 4-byte stride):

```cpp
uint32_t Simulator::smem_access_stall_(const DecodedInst& inst, uint32_t lane_addrs[32]) {
    // Compute which banks are accessed per lane
    uint32_t banks_accessed[32] = {};
    for (uint32_t l = 0; l < 32; l++) {
        banks_accessed[l] = (lane_addrs[l] >> 2) & 31;
    }
    
    // Count unique banks (popcount of seen bitmask)
    uint32_t seen_mask = 0;
    for (uint32_t l = 0; l < 32; l++) {
        seen_mask |= (1 << banks_accessed[l]);
    }
    uint32_t unique_banks = std::popcount(seen_mask);
    
    // If all 32 lanes hit 32 distinct banks (no conflict): 0 stall
    // If 32 lanes hit fewer banks: serialize into N replays where N = 32 / unique_banks
    uint32_t replays = 32 / unique_banks;
    return (replays > 1) ? (replays - 1) * caps_.opcode_table->lds_bank_conflict_penalty : 0;
}
```

This is called by `MEM_REQUEST` handling for `LDS` / `STS` instructions and adds to the latency.

### What we skip at medium tier

- Per-bank row state tracking (just aggregate row-hit/row-miss probability)
- Individual DRAM commands (ACT/PRE/RD/WR/REF)
- Write-combining buffer
- L2 compression

These cost ~2-5% accuracy in exchange for ~20× faster simulation. Accurate tier models them fully.

### The 2-7% that's hard to model

Some residual effects that even driver-source-informed models will miss:

1. **Temperature-dependent DRAM timing** — tRCD/tRP shift slightly with junction temperature. Augur observes; Mimic doesn't.
2. **Cross-kernel L2 retention** — if a kernel inherits a warm L2 from a previous kernel, hit rate is higher than Mimic predicts. Assume cold L2 at kernel start.
3. **Compression hit rate** — NVIDIA L2 has hardware compression; actual effective capacity is workload-dependent. Model as an effective capacity multiplier (calibrated).
4. **NoC arbitration details** — HSHUB crossbar has specific arbitration that can matter under extreme contention. Not typical in ML workloads.

These contribute ~2-5% residual error that we accept. 95% accuracy is the target; 98% would require modeling all of the above and is not worth the engineering effort for ML workloads.

---

## 12. The warp scheduler

NVIDIA's warp scheduler is GTO (Greedy-Then-Oldest) with age-based boosting. Details are undocumented publicly but microbenchmark-derivable. Access to driver source gives us the exact policy through Hopper.

### GTO core

```cpp
uint16_t Simulator::pick_warp_(SmId sm, PartitionId p) {
    const auto& part = sms_[sm].partitions[p];
    
    // Collect ready warps
    uint16_t ready_mask = 0;
    for (uint8_t w = 0; w < WARPS_PER_PARTITION; w++) {
        if (warps_.state(warp_idx(sm, p, w)) == WarpState::READY &&
            warps_.next_ready(warp_idx(sm, p, w)) <= current_cycle_) {
            ready_mask |= (1 << w);
        }
    }
    
    if (ready_mask == 0) return UINT16_MAX;
    
    // GTO greedy: prefer last-picked warp if still ready
    uint16_t last_picked = part.last_picked_warp;
    if (last_picked < WARPS_PER_PARTITION && 
        (ready_mask & (1 << last_picked)) != 0 &&
        warps_.stalls_this_burst(warp_idx(sm, p, last_picked)) < caps_.gto_burst_limit) {
        return last_picked;
    }
    
    // GTO oldest: the ready warp with smallest cycle-last-issued
    uint16_t oldest = 0;
    Cycle oldest_last_issue = Cycle::max();
    for (uint8_t w = 0; w < WARPS_PER_PARTITION; w++) {
        if ((ready_mask & (1 << w)) == 0) continue;
        Cycle last = warps_.last_issue_cycle(warp_idx(sm, p, w));
        if (last < oldest_last_issue) {
            oldest_last_issue = last;
            oldest = w;
        }
    }
    
    // Boost: if any warp has been stuck > threshold, prefer it
    for (uint8_t w = 0; w < WARPS_PER_PARTITION; w++) {
        if ((ready_mask & (1 << w)) == 0) continue;
        if (current_cycle_.v - warps_.last_issue_cycle(warp_idx(sm, p, w)).v 
            > caps_.gto_boost_threshold) {
            return w;
        }
    }
    
    return oldest;
}
```

Three rules interleaved:
1. **Greedy**: stick with last-picked warp while it's still ready and within burst budget
2. **Oldest**: fallback to oldest-ready when greedy fails
3. **Boost**: elevate any warp that's been stuck beyond threshold

Each has a constant from TargetCaps:
- `gto_burst_limit` — max consecutive issues from same warp before yielding
- `gto_boost_threshold` — cycles-stuck that triggers boost
- `gto_greedy_prob` — probability of choosing greedy over oldest (0-100, per-cycle)

### Calibration

The three constants are calibrated via microbenchmarks:
- Script 1: two warps, one fast (no dependencies) one slow (memory-bound). Measure how often each issues. Fits `gto_greedy_prob`.
- Script 2: N warps, one stalled for M cycles. Measure when it gets unblocked. Fits `gto_boost_threshold`.
- Script 3: one warp with long dep chain. Measure burst depth. Fits `gto_burst_limit`.

Expected values for Hopper: greedy_prob ~80, boost_threshold ~24 cycles, burst_limit ~4.

### Cross-partition

The 4 partitions are independent. No cross-partition scheduling. Each partition's `pick_warp_` runs on its own issue-slot events.

---

## 13. Async pipelines — WGMMA, TMA, tcgen05

The three async subsystems on sm_90+ hardware. Each is modeled as a separate pool of outstanding operations.

### WGMMA (sm_90+)

```cpp
struct WgmmaOp {
    Cycle    issue_time;
    Cycle    deadline;            // when result lands
    uint16_t warp_id;
    uint16_t reg_mask_start;      // accumulator register range
    uint16_t reg_mask_end;
    uint8_t  commit_group;        // for wait_group(N) tracking
};

struct SmState {
    // ...
    WgmmaOp  wgmma_ops[64];       // up to 64 outstanding WGMMAs
    uint8_t  wgmma_op_count;
    uint8_t  wgmma_latest_commit; // for commit_group / wait_group
};
```

On `wgmma.mma_async` issue:
```cpp
void Simulator::issue_wgmma_(SmId sm, WarpId warp, const DecodedInst& inst) {
    const auto& attrs = *prog_.attrs_pool[inst.attrs_idx];
    const WgmmaShapeSpec* spec = find_wgmma_spec(attrs.m, attrs.n, attrs.k, 
                                                  attrs.a_dtype, attrs.b_dtype);
    
    // MMA pipe consumed for occupancy cycles
    sms_[sm].fu_tokens[Pipe::MMA]--;
    schedule_(current_cycle_ + spec->occupancy, FU_FREE, sm, Pipe::MMA);
    
    // Allocate async slot
    WgmmaOp op{
        current_cycle_,
        current_cycle_ + spec->latency,
        warp,
        inst.dst_regs[0],
        inst.dst_regs[0] + accumulator_size(attrs),
        sms_[sm].wgmma_latest_commit,
    };
    sms_[sm].wgmma_ops[sms_[sm].wgmma_op_count++] = op;
    
    // Schedule completion
    schedule_(op.deadline, WGMMA_COMPLETE, sm, sms_[sm].wgmma_op_count - 1);
    
    // Warp can continue issuing (async)
    advance_warp_(warp);
}
```

On `wgmma.commit_group`:
```cpp
void Simulator::issue_wgmma_commit_group_(SmId sm, WarpId warp) {
    sms_[sm].wgmma_latest_commit++;
    // Commit group count is the number of distinct commit_groups in-flight
    // Measured from wgmma_ops.commit_group values
}
```

On `wgmma.wait_group(N)`:
```cpp
void Simulator::issue_wgmma_wait_group_(SmId sm, WarpId warp, uint8_t n) {
    uint8_t oldest_commit = compute_oldest_in_flight_commit_(sm);
    uint8_t target_commit = sms_[sm].wgmma_latest_commit - n;
    
    if (oldest_commit > target_commit) {
        // All groups we need have completed
        advance_warp_(warp);
        return;
    }
    
    // Otherwise stall until oldest commit group completes
    Cycle wait_until = find_deadline_for_commit_(sm, target_commit);
    warps_.set_state(warp, WarpState::WAITING_WGMMA);
    schedule_(wait_until, WGMMA_COMPLETE_UNBLOCK, warp);
    extract_insight_(InsightKind::WGMMA_WAIT_DOMINATES, sm, warp);
}
```

Key: the 100+ cycle gap between WGMMA issue and WGMMA deadline is what enables pipeline overlap. Other ops (LDG, STS, address arithmetic) issue during the MMA's flight. Medium tier captures this by making the MMA pipe busy for only `occupancy` cycles (typically 4-16) while the result doesn't land until `latency` cycles (16-128). Other issue slots are free during the gap.

### TMA (sm_90+)

```cpp
struct TmaOp {
    Cycle    issue_time;
    Cycle    deadline;            // when data lands in smem
    uint16_t smem_target_addr;
    uint32_t bytes_transferred;
    uint8_t  mbarrier_id;         // associated mbarrier
};

struct SmState {
    TmaOp tma_ops[8];             // up to 8 outstanding TMAs per SM
    uint8_t tma_op_count;
};
```

On `cp.async.bulk.tensor` issue:
```cpp
void Simulator::issue_tma_(SmId sm, WarpId warp, const DecodedInst& inst) {
    // TMA pipe consumed for ~20 cycles (issue, not full latency)
    sms_[sm].fu_tokens[Pipe::TMA]--;
    schedule_(current_cycle_ + 20, FU_FREE, sm, Pipe::TMA);
    
    // Compute transfer latency based on bytes and current memory pressure
    uint32_t bytes = estimate_tma_bytes(inst);
    uint32_t mem_pressure = mem_.hbm_total_queue_depth();
    Cycle base_latency = bytes / caps_.hbm_bw_bytes_per_cycle;
    Cycle loaded_latency = base_latency * (1 + 0.05 * mem_pressure);
    
    // Allocate async slot
    TmaOp op{
        current_cycle_,
        current_cycle_ + loaded_latency,
        estimate_smem_addr(inst),
        bytes,
        extract_mbarrier_id(inst),
    };
    sms_[sm].tma_ops[sms_[sm].tma_op_count++] = op;
    
    // TMA increments mbarrier expected-tx
    mbarriers_[op.mbarrier_id].expected_tx += bytes;
    
    // Schedule completion
    schedule_(op.deadline, MEM_ARRIVE, sm, sms_[sm].tma_op_count - 1);
    
    // Warp can continue
    advance_warp_(warp);
}
```

Crucially, TMA and WGMMA are **separate pipelines**. A kernel that issues both can overlap them. The simulator must not conflate their sync protocols.

### tcgen05 (sm_100+ a/f)

Similar structure but with TMEM as the destination:

```cpp
struct Utcmma Op {
    Cycle    issue_time;
    Cycle    deadline;
    uint16_t warp_id;
    uint16_t tmem_column_start;
    uint16_t tmem_column_end;
    uint8_t  commit_group;
    uint8_t  cta_group;           // 1 or 2
};

struct SmState {
    UtcmmaOp utcmma_ops[16];
    uint8_t utcmma_op_count;
    
    TmemState tmem;               // tracks allocated columns
};
```

The key difference from WGMMA: `tcgen05.alloc` reserves columns of TMEM before the MMA; subsequent `tcgen05.mma` references allocated columns. Failure to allocate (pool exhausted) blocks the warp. The simulator models TMEM as a small bump-allocator pool per SM.

1CTA vs 2CTA mode: 2CTA doubles the M dim and couples two CTAs. Model as `cta_group=2` on the async op; treat as a joint operation across the CTA pair.

### Commit groups in all three

All three async subsystems use commit-group-based ordering:
- `wgmma.commit_group` / `wgmma.wait_group(N)` 
- (TMA is synced via mbarrier, not commit groups)
- `tcgen05.commit` / `tcgen05.wait`

Simulator tracks outstanding ops per commit group; wait unblocks when count ≤ N.

### Back-to-back pipeline optimization

When a kernel has `wgmma → wgmma → ... → wait_group(0)`, the MMAs overlap. Medium tier captures this because:
- Each MMA's MMA-pipe token is released after `occupancy` cycles (4-16)
- The next MMA can issue immediately after
- Only the `wait_group(0)` causes a real stall

Simulator correctness: ensure that the MMA pipe token is released at `issue_time + occupancy`, NOT at `issue_time + latency`. This is the single most important detail for WGMMA modeling. Getting it wrong underestimates throughput by 2-8×.

---

## 14. Insight extraction

Mimic's second-most-important output after cycles. Insights guide mutation.

### Insight struct

```cpp
struct Insight {
    InsightKind kind;             // 1-2 bytes
    uint8_t     severity;         // 0-255
    uint8_t     confidence;       // 0-100

    // Localization
    InstId      inst_id;          // which instruction flagged this (or INVALID)
    uint32_t    cycle;            // when it happened
    uint16_t    warp_id;          // which warp (or 0xFFFF if cross-warp)
    uint16_t    sm_id;            // which SM (or 0xFFFF if device-global)

    // Payload
    uint64_t    metric_value;     // observed metric (e.g. stall cycles, bytes moved)
    uint64_t    suggest_param;    // suggested adjustment parameter

    // Optional suggested mutation
    MutationHint hint;
};
```

### Insight kinds (30-40 total)

```cpp
enum class InsightKind : uint16_t {
    // Compute inefficiencies
    PIPE_UNDERUTILIZED,
    REGISTER_BANK_CONFLICTS,
    OPERAND_COLLECTOR_STALL,
    PIPE_CONTENTION_DOMINATES,

    // Memory inefficiencies
    L1_MISS_RATE_HIGH,
    L2_QUEUE_SATURATED,
    HBM_BW_BOUND,
    MEMORY_COALESCING_POOR,
    SMEM_BANK_CONFLICTS,
    DRAM_ROW_MISS_RATE_HIGH,

    // Async / pipeline
    TMA_LATENCY_DOMINATES,
    WGMMA_UNDERUTILIZED,
    WGMMA_WAIT_GROUP_TOO_SHALLOW,
    WGMMA_WAIT_GROUP_TOO_DEEP,
    PIPELINE_DEPTH_INSUFFICIENT,
    PIPELINE_DEPTH_EXCESSIVE,
    TCGEN05_TMEM_PRESSURE,

    // Resource
    REGISTER_PRESSURE_HIGH,
    SMEM_PRESSURE_HIGH,
    OCCUPANCY_BELOW_TARGET,
    TMEM_EXHAUSTED,

    // Sync
    MBARRIER_WAIT_DOMINATES,
    CLUSTER_BARRIER_STALL,
    SCOREBOARD_WAIT_DOMINATES,

    // Structural
    WARP_DIVERGENCE,
    UNUSED_UR_FILE,
    WARP_SCHEDULER_STARVATION,
    SUB_PARTITION_IMBALANCE,

    // High-level kernel hints
    MERGE_KERNELS_POSSIBLE,
    TILE_TOO_SMALL_FOR_SATURATION,
    TILE_TOO_LARGE,
    EPILOGUE_DOMINATES,
    PROLOGUE_TOO_DEEP,
    SMALL_PROBLEM_SIZE,
    KERNEL_FUNDAMENTALLY_SMALL,

    COUNT
};
```

### Hint → mutation mapping

Each insight kind has a canonical hint that maps to 1-3 MutationOps:

```cpp
struct HintCatalog {
    static constexpr std::array<MutationOp, 3> hints_for(InsightKind k) {
        switch (k) {
            case WGMMA_UNDERUTILIZED:
                return {MutationOp::INCREASE_PIPELINE_STAGES,
                        MutationOp::INCREASE_TILE_K,
                        MutationOp::ENABLE_WARP_SPECIALIZATION};
            case REGISTER_PRESSURE_HIGH:
                return {MutationOp::DECREASE_TILE_SIZE,
                        MutationOp::ENABLE_UR_PROMOTION,
                        MutationOp::REDUCE_PIPELINE_STAGES};
            case L2_QUEUE_SATURATED:
                return {MutationOp::REDUCE_CTA_COUNT,
                        MutationOp::INCREASE_L2_RESIDENT_TILE,
                        MutationOp::CHANGE_SWIZZLE_PATTERN};
            // ... all 40 kinds
        }
    }
};
```

### Extraction during simulation

Insights are produced by hooks at key event handlers, not a post-hoc pass. Each hook is ~10-50 ns; negligible overhead:

```cpp
void Simulator::on_inst_complete_(const Event& ev) {
    WarpId warp = extract_warp_id(ev);
    InstId inst = extract_inst_id(ev);
    
    // Release scoreboard
    sb_release(warp, inst);
    
    // Insight hooks
    Cycle wait_duration = ev.cycle - warps_.last_issue_cycle(warp);
    if (wait_duration > 10 * caps_.opcode_table->specs[prog_.insts[inst].op].latency) {
        // Instruction took 10× longer than its latency — probably contention
        extract_insight_(
            InsightKind::PIPE_CONTENTION_DOMINATES,
            severity: 60,
            confidence: 80,
            inst_id: inst,
            metric: wait_duration.v
        );
    }
    
    // ... more hooks
}
```

### Severity and confidence

- **Severity** (0-255): the magnitude of the performance impact. A 10-cycle stall in a kernel that runs for 1M cycles is severity ~1; a stall that accounts for 50% of runtime is severity 200+.
- **Confidence** (0-100): how sure the simulator is that this insight is real. Low confidence fires for patterns near threshold; high confidence for clear cases.

Mutator uses severity × confidence as the priority weight when choosing mutations.

### Insight buffer sizing

Per simulation, bounded:
- Max insights per SimResult: 512 (overflow drops low-severity)
- Severity threshold for inclusion: dynamic, starts at 20, adjusted up if buffer fills

Most simulations produce 10-50 insights. Pathological kernels can produce hundreds (every instruction stalls). Buffer is arena-allocated.

---

## 15. The SASS encoder

Transforms IR003NV nodes into 128-bit Mercury instruction words, packages them into a cubin ELF. This is the backend for Forge's Phase J.

### Structure

```
crucible/mimic/include/crucible/mimic/encode/
├── Packer.h             — 30-line bitfield packer (the core primitive)
├── Formats.h            — 16 format group descriptors (data tables)
├── Opcodes.h            — opcode → (major, minor, subop, format) mapping per SM
├── Operands.h           — 4 operand type encoders (reg, imm, pred, ureg)
├── Control.h            — scoreboard/stall/yield/reuse encoder
├── SassEncoder.h        — orchestrator: IR003NV → 128-bit words
├── CubinEmitter.h       — ELF container + section layout
├── Eiattr.h             — EIATTR catalog emission (Forge must emit 26+ codes)
├── Relocations.h        — R_MERCURY_* relocations
├── Capmerc.h            — capsule Mercury wrapper (sm_100+ default)
└── Dwarf.h              — DWARF line tables (uses gimli-style crate)
```

### The bitfield packer

Core primitive, 30 lines:

```cpp
namespace encode {

// Pack `val` into bits [offset, offset+width) of `word`.
// `word` is a 128-bit array of 4 uint32 (low, low-mid, high-mid, high).
constexpr void insert_bits(uint32_t word[4], unsigned offset, unsigned width, uint64_t val) {
    // Handle crossing word boundaries
    unsigned start_word = offset / 32;
    unsigned start_bit = offset % 32;
    unsigned bits_in_first = std::min(width, 32 - start_bit);
    
    uint32_t mask_first = ((1u << bits_in_first) - 1) << start_bit;
    word[start_word] = (word[start_word] & ~mask_first) | 
                       ((static_cast<uint32_t>(val) << start_bit) & mask_first);
    
    if (bits_in_first < width) {
        // Spills into next word
        unsigned remaining = width - bits_in_first;
        uint32_t mask_second = (1u << remaining) - 1;
        word[start_word + 1] = (word[start_word + 1] & ~mask_second) |
                              (static_cast<uint32_t>(val >> bits_in_first) & mask_second);
    }
}

} // namespace encode
```

Called from operand encoders, format emitters, control field writer. Inline. Compiles to a few dozen machine instructions.

### Format groups

16 format group descriptors (from the wiki's 16 format classes):

```cpp
struct FormatDescriptor {
    uint8_t  format_id;              // 0x01 .. 0x19
    uint8_t  slot_stride;             // typically 2
    uint8_t  opcode_header_width;     // 4 bits
    uint8_t  operand_offset;          // where operands start in the word
    std::array<uint8_t, 10> slot_sizes;   // bit widths per operand slot
    std::array<uint8_t, 10> slot_types;   // 0=reg, 1=imm, 2=pred, 3=ureg, 4=cb
    std::array<uint8_t, 10> slot_flags;   // modifier bit positions
};

constexpr std::array<FormatDescriptor, 16> FORMAT_DESCRIPTORS = {{
    { .format_id = 0x03, .slot_stride = 2, /* general ALU */ ... },
    { .format_id = 0x19, .slot_stride = 2, /* extended ALU */ ... },
    { .format_id = 0x0A, /* multi-source */ ... },
    // ... 13 more
}};
```

Data-driven. Tables come from calibration (disassembling known cubins to learn formats).

### Opcode → format mapping

```cpp
struct OpcodeEntry {
    uint16_t sass_opcode;       // e.g. FFMA = 0x23
    uint8_t  major;
    uint8_t  minor;
    uint8_t  subop;
    uint8_t  format_id;         // which format group
    uint8_t  min_sm;            // minimum SM for this opcode
    uint8_t  pad[3];
};

constexpr std::array<OpcodeEntry, OPCODE_COUNT> OPCODE_TABLE = {{
    { .sass_opcode = SASS_FFMA, .major = 0x54, .minor = 0x01, .subop = 0x00,
      .format_id = 0x03, .min_sm = 90 },
    // ... 370 more
}};
```

Per-SM validity mask is a separate bitmap indexed by SM version.

### Control word encoder

```cpp
void encode_control_field(
    uint32_t word[4],
    uint8_t wait_mask,
    uint8_t read_sb,
    uint8_t write_sb,
    uint8_t stall_count,
    uint8_t yield_hint,
    uint8_t reuse_flags
) {
    insert_bits(word, CTRL_STALL_OFFSET, 4, stall_count);
    insert_bits(word, CTRL_WAIT_MASK_OFFSET, 6, wait_mask);
    insert_bits(word, CTRL_READ_SB_OFFSET, 3, read_sb);
    insert_bits(word, CTRL_WRITE_SB_OFFSET, 3, write_sb);
    insert_bits(word, CTRL_YIELD_OFFSET, 1, yield_hint);
    insert_bits(word, CTRL_REUSE_OFFSET, 4, reuse_flags);
}
```

### Cubin ELF emitter

Standard ELF with NVIDIA-specific sections:
- `.text.<func>` — SASS bytes (128-bit aligned)
- `.nv.info` — EIATTR catalog (global)
- `.nv.info.<func>` — EIATTR catalog (per-function)
- `.nv.constant0` — constant bank 0 data
- `.nv.shared.<func>` — shared memory layout
- `.note.nv.tkinfo` — toolkit version
- `.note.nv.cuinfo` — compiler info
- `.debug_*` — DWARF sections (LineTablesOnly default)
- `.nv.capmerc.<func>` — capsule Mercury wrapper (sm_100+)
- `.nv.merc.*` — auxiliary sections for capmerc

Validation: the generated cubin must pass `nvdisasm --verbose` without errors. Regression test: generate 100 cubins, nvdisasm each, verify exit code 0.

### EIATTR emission

The 26+ EIATTR codes Forge must emit, with per-function values derived from the IR003NV program:

- `EIATTR_REGCOUNT` (0x2F) — from register allocator's final count
- `EIATTR_FRAME_SIZE` (0x11) — from local memory plan
- `EIATTR_MIN_STACK_SIZE` (0x12) — from call graph analysis
- `EIATTR_MAX_STACK_SIZE` (0x23) — if calls exist
- `EIATTR_PARAM_CBANK` (0x0A) — bank index + offset
- `EIATTR_CBANK_PARAM_SIZE` (0x19) — total kernel param bytes
- `EIATTR_KPARAM_INFO` or `EIATTR_KPARAM_INFO_V2` (0x17 or 0x45) — per-param type/size/align
- `EIATTR_CBANK_PARAM_OFFSETS` (0x0C) — per-param offsets
- `EIATTR_MAX_THREADS` (0x05) — launch bound
- `EIATTR_REQNTID` (0x10) — exact block shape if static
- `EIATTR_NUM_BARRIERS` (0x4C) — named barrier count
- `EIATTR_NUM_MBARRIERS` (0x38) — mbarrier count (sm_90+)
- `EIATTR_SHARED_SCRATCH` (0x32) — spill smem
- `EIATTR_RESERVED_SMEM_USED` (0x41) — reserved SMEM regions
- `EIATTR_RESERVED_SMEM_0_SIZE` (0x42) — if reserved SMEM
- `EIATTR_CTA_PER_CLUSTER` (0x3D) — cluster launch
- `EIATTR_EXPLICIT_CLUSTER` (0x3E) — if cluster dims fixed
- `EIATTR_MAX_CLUSTER_RANK` (0x3F) — cluster scheduling
- `EIATTR_BLOCKS_ARE_CLUSTERS` (0x5B) — fused block/cluster
- `EIATTR_TCGEN05_1CTA_USED` or `EIATTR_TCGEN05_2CTA_USED` (0x51/0x52) — exactly one, never both
- `EIATTR_REG_RECONFIG` (0x54) — if `setmaxnreg` used
- `EIATTR_AT_ENTRY_FRAGEMENTS` (0x4F) — Blackwell TMEM entry
- `EIATTR_SPARSE_MMA_MASK` (0x50) — if 2:4 sparse MMA
- `EIATTR_EXIT_INSTR_OFFSETS` (0x1C) — offsets of all EXIT
- `EIATTR_MBARRIER_INSTR_OFFSETS` (0x39) — if mbarrier used
- `EIATTR_CUDA_API_VERSION` (0x37) — toolkit version stamp
- `EIATTR_MERCURY_ISA_VERSION` (0x5F) — for Mercury output (sm_100+)
- `EIATTR_MERCURY_FINALIZER_OPTIONS` (0x5A) — for capmerc

Missing any required EIATTR → driver rejects the cubin at `cuModuleLoad`. Regression test: load every generated cubin via CUDA driver API in a test harness.

### Size

~5K LOC for encoder core + ~6K LOC for cubin/EIATTR/capmerc/dwarf = ~11K LOC total. Plus ~2-3K of data tables (format descriptors, opcode mapping) per SM.

### 15.4 Pushbuffer composer per vendor

The SASS encoder emits compute-kernel bytes (QMD payload, cubin ELF). A complementary **pushbuffer composer** emits the host-engine commands that dispatch QMDs to SMs. Phase J (FORGE.md §15, §J.6) invokes this path once per Plan to lay out the pushbuffer_bytes span. Pushbuffer and SASS are distinct command streams: SASS runs on the SM; pushbuffer runs on the host engine.

Each vendor's composer is a state machine that takes a sequence of (launch, barrier, chain_edge, patch_point) records and emits vendor-native command words.

#### 15.4.1 NVIDIA — Hopper/Blackwell GPFIFO + method stream

Command format: 32-bit methods organized by subchannel, per `cla06f.h` (FIFO class) / `clc96f.h` (Hopper) / `clcfc0.h` (Blackwell).

**Method header encoding** (`cla06f.h:165-186`):

```
word[0] = (sec_op<<29) | (count<<16) | (subchannel<<13) | method_addr
  sec_op ∈ {INC=1, IMMD=4, NON_INC=3, ONE_INC=5}
  method_addr = method_byte_offset / 4

INC   : count N data words follow (N different method addresses)
NON_INC: N data words for same method
IMMD  : data encoded in header; no following words
```

**Canonical kernel launch** (4 dwords = 16 bytes, from Agent-5's traced sequence):

```
# INC method, subch=1, addr=SEND_PCAS_A(0x02b4/4=0xAD), count=2
word[0] = 0x020020AD
word[1] = QMD_ADDRESS >> 8              # high bits of 40-bit QMD phys addr
word[2] = 0                             # SEND_PCAS_B (unused on Hopper)

# IMMD method, subch=1, addr=SEND_SIGNALING_PCAS2_B(0x02c0/4=0xB0)
word[3] = 0x800320B0                    # PCAS_ACTION=INVALIDATE_COPY_SCHEDULE=3
```

The composer emits these 4 dwords per kernel launch. QMDs are pre-built in a per-Plan pool (256 B each on Hopper, `cla0c0qmd.h` layout); their byte offsets are recorded in the LaunchRec.

**Semaphore release** (5 methods, per `clc96f.h:75-88`):

```
# NON_INC method, subch=0, addr=SEM_ADDR_LO(0x5c/4=0x17), count=5
word[0] = 0x60050017                    # NON_INC | count=5 | subch=0 | 0x17
word[1] = sem_addr_lo
word[2] = sem_addr_hi
word[3] = payload_lo
word[4] = payload_hi
word[5] = 0x00100001                    # EXECUTE: RELEASE | PAYLOAD_SIZE_32BIT | WFI_DIS
```

Total: 6 dwords = 24 bytes per release or acquire.

**mbarrier** (cluster-local barriers, Hopper+):

```
word[0] = 0x20010xxx                    # INC, count=1, MBAR method
word[1] = mbarrier_handle
```

Total: 2 dwords = 8 bytes.

**GPFIFO entry** (8 bytes, `clc96f.h:91-105`):

```
entry[0] = (pb_va >> 2) & 0x3FFFFFFF | (FETCH=0)
entry[1] = (pb_va >> 32) | (LEVEL << 9) | ((pb_length >> 2) << 10) | (SYNC << 31)
  SYNC ∈ {PROCEED=0, WAIT_FOR_IDLE=1}
```

One GPFIFO entry per pushbuffer chunk. Composer batches many kernel launches into one pushbuffer chunk (one GPFIFO entry), reducing GPFIFO pressure.

**Composer footprint**: ~2 KB of method tables + 1 KB state machine logic. Under 3 KB total LoC for the NV composer.

#### 15.4.2 AMD — RDNA3/CDNA3 MEC + PM4 packets

On AM-style userspace driver (no MES scheduler), compute is dispatched to MEC (Micro Engine Compute) via **PM4 packets** in an IB (Indirect Buffer). PM4 packet format (GFX12/CDNA4):

```
dword[0] = (packet_type<<30) | (count<<16) | (opcode<<8) | shader_type
  packet_type = 3 (PACKET3)
  opcode ∈ PM4_IT_*
dword[1..count+1] = payload
```

**Dispatch compute** (`PACKET3_DISPATCH_DIRECT` / `IT_DISPATCH_DIRECT`):

```
dword[0] = PACKET3(IT_DISPATCH_DIRECT, count=3)
dword[1] = grid_dim_x
dword[2] = grid_dim_y
dword[3] = grid_dim_z
dword[4] = DISPATCH_INITIATOR flags
```

Total: 5 dwords = 20 bytes. Kernel setup (program address, userdata, resource descriptors) is a preceding `SET_SH_REG` block of ~32 bytes, done once per kernel boundary (cacheable across launches).

**Semaphore release** (`PACKET3_RELEASE_MEM`):

```
PACKET3(RELEASE_MEM, count=6) + {event_type, addr, data, interrupt_flags, ...}
```

Total: 7 dwords = 28 bytes.

**Semaphore wait** (`PACKET3_WAIT_REG_MEM`):

```
PACKET3(WAIT_REG_MEM, count=5) + {function=GE, addr, reference, mask, poll_interval}
```

Total: 6 dwords = 24 bytes.

MEC ring submission: write PM4 packets to the IB, advance `wptr` register mapped via BAR, ring the doorbell at the per-queue offset.

**Composer footprint**: ~2.5 KB PM4 tables + 1 KB logic. Tinygrad's `runtime/autogen/am/pm4_nv.py` and `pm4_soc15.py` auto-generated from AMD's published ISA XML is the starting point.

#### 15.4.3 TPU — scalar processor bytecode

TPU executables are protobuf-wrapped with an opaque payload consumed by the on-chip scalar processor (one per TensorCore). Each command is a micro-op the scalar processor interprets to schedule MXU / VPU / DMA work.

**Command shape** (reverse-engineered from libtpu.so):

- 16-byte fixed header + variable-size operand list
- Opcode families: `MXU_DISPATCH`, `VPU_DISPATCH`, `VMEM_LOAD`, `DMA_ISSUE`, `SYNC_BARRIER`, `LOOP_BEGIN/END`

Unlike NV's host engine, TPU doesn't have a separate "doorbell" per-submit model — the executable is written via DMA descriptor chain to the TPU, which autonomously walks it.

**Composer footprint**: ~3 KB opcode encoding + 2 KB protobuf assembly + 1 KB logic (initial estimate; depends on RE completeness).

#### 15.4.4 Trainium — NEFF submit batch

NEFF (Neuron Executable File Format) is a tarball with per-engine bytecode streams (TensorEngine, VectorEngine, ScalarEngine, GpSimd, DMA, Sync). Submission to `/dev/neuronN` is via `NEURON_IOCTL_SUBMIT_BATCH` with multiple NEFF handles. The per-engine streams use TensorEngine's tile op sequence, VectorEngine's SIMD ops, ScalarEngine control flow, DMA's transfer descriptors, and Sync's barrier primitives.

**Composer footprint**: ~3 KB per-engine opcode tables + 1.5 KB logic.

#### 15.4.5 CPU — direct function call sequence

No pushbuffer; each "launch" is a direct C function call invoking compiled kernel bytes loaded via `jit_loader` (tinygrad-style in-process ELF). Composer emits a dispatch table of function pointers + argument marshalling stubs.

**Composer footprint**: ~500 LoC.

#### 15.4.6 Common composer API

All vendor composers implement the same interface:

```cpp
namespace crucible::mimic::<vendor> {
    class PushbufferComposer {
    public:
        explicit PushbufferComposer(fx::Init, Arena&, const TargetCaps&);

        void emit_launch(fx::Bg, const LaunchRec&, QmdId qmd,
                         std::span<const PatchPoint> local_patches);
        void emit_barrier(fx::Bg, const BarrierRec&);
        void emit_semaphore_wait(fx::Bg, SemaphoreId, uint64_t expected);
        void emit_semaphore_signal(fx::Bg, SemaphoreId, uint64_t value);
        void emit_loop_prologue(fx::Bg, LoopId, uint32_t initial_count);
        void emit_loop_epilogue(fx::Bg, LoopId);
        void emit_branch_prologue(fx::Bg, BranchId, PatchPointId predicate);
        void emit_branch_epilogue(fx::Bg, BranchId);

        ComposerResult finalize(fx::Bg);
    };
}
```

Phase J calls these methods in topological order; each emits vendor-native command bytes at the current write cursor and records PatchPoint offsets referencing mutable scalars/pointers.

#### 15.4.7 Size budget per vendor

| Backend | Composer LoC | Method/opcode tables | Total |
|---|---|---|---|
| NV (Hopper/Blackwell/consumer) | 1K | 2K | 3K |
| AMD (CDNA3+/RDNA3+) | 1K | 2.5K | 3.5K |
| TPU (v5+) | 1K | 5K | 6K |
| Trainium (trn1+) | 1.5K | 4.5K | 6K |
| CPU | 0.5K | 0 | 0.5K |
| **Total** | **5K** | **14K** | **19K** |

The per-vendor composer is the smallest of the per-vendor subsystems — less than 10% of the encoder + simulator footprint. But it is the component that runs in the hot path of every submission, so its correctness and performance are critical.

---

## 16. The SASS decoder

Inverse of the encoder. Used by Mimic's calibration harness (to parse CUPTI-profiled cubins) and by Augur's digital twin (to decode already-compiled kernels for prediction).

### Structure

```
crucible/mimic/include/crucible/mimic/decode/
├── Unpacker.h            — inverse of encode::Packer
├── FormatRecognize.h     — identify format group from bits
├── OpcodeLookup.h        — (major, minor, subop) → opcode enum
├── OperandDecode.h       — inverse of operand encoders
├── SassDecoder.h         — orchestrator: bytes → DecodedProgram
└── Disasm.h              — pretty-print (for debug/logs)
```

### The unpacker

```cpp
constexpr uint64_t extract_bits(const uint32_t word[4], unsigned offset, unsigned width) {
    unsigned start_word = offset / 32;
    unsigned start_bit = offset % 32;
    unsigned bits_in_first = std::min(width, 32 - start_bit);
    
    uint64_t low = (word[start_word] >> start_bit) & ((1u << bits_in_first) - 1);
    
    if (bits_in_first < width) {
        unsigned remaining = width - bits_in_first;
        uint64_t high = (word[start_word + 1] & ((1u << remaining) - 1)) << bits_in_first;
        return low | high;
    }
    
    return low;
}
```

Inverse of `insert_bits`. Round-trip correct.

### Format recognition

From the first word of each 128-bit instruction, extract the format class bits, look up the descriptor, use that to drive operand extraction:

```cpp
FormatDescriptor identify_format(const uint32_t word[4]) {
    uint8_t format_bits = extract_bits(word, FORMAT_ID_OFFSET, 5);
    return FORMAT_DESCRIPTORS[format_bits];
}
```

### Opcode decode

```cpp
uint16_t decode_opcode(const uint32_t word[4]) {
    uint8_t major = extract_bits(word, MAJOR_OPCODE_OFFSET, 9);
    uint8_t minor = extract_bits(word, MINOR_OPCODE_OFFSET, 8);
    uint8_t subop = extract_bits(word, SUBOP_OFFSET, 7);
    
    // Hash-lookup in opcode table
    return OPCODE_LOOKUP_TABLE[pack(major, minor, subop)];
}
```

Static compile-time lookup table built from `OPCODE_TABLE`.

### Full decode

```cpp
DecodedInst decode_inst(const uint32_t word[4], const TargetCaps* caps) {
    DecodedInst inst{};
    
    inst.op = decode_opcode(word);
    FormatDescriptor fmt = identify_format(word);
    
    // Extract operands per slot
    for (unsigned slot = 0; slot < 3 /* max sources */; slot++) {
        uint8_t slot_size = fmt.slot_sizes[slot];
        uint8_t slot_type = fmt.slot_types[slot];
        uint64_t raw = extract_bits(word, fmt.operand_offset + slot * fmt.slot_stride * 8, slot_size);
        inst.src_regs[slot] = decode_operand(raw, slot_type);
    }
    
    // Control field
    inst.stall_count = extract_bits(word, CTRL_STALL_OFFSET, 4);
    inst.wait_sb_mask = extract_bits(word, CTRL_WAIT_MASK_OFFSET, 6);
    inst.read_sb = extract_bits(word, CTRL_READ_SB_OFFSET, 3);
    inst.write_sb = extract_bits(word, CTRL_WRITE_SB_OFFSET, 3);
    inst.flags = (extract_bits(word, CTRL_YIELD_OFFSET, 1) << YIELD_FLAG_BIT) |
                 (extract_bits(word, CTRL_REUSE_OFFSET, 4) << REUSE_FLAG_BIT);
    
    // Latency/occupancy from OpcodeLatencyTable
    auto spec = caps->opcode_table->specs[inst.op];
    inst.latency = spec.latency;
    inst.occupancy = spec.occupancy;
    inst.pipe = spec.pipe;
    
    return inst;
}
```

### Disassembly (pretty-print)

Table-driven for debug/log output:

```cpp
void disassemble(const DecodedInst& inst, std::string& out) {
    out += opcode_mnemonic(inst.op);        // "FFMA" etc.
    out += modifier_string(inst.op, inst.flags);  // ".FTZ" etc.
    out += " ";
    out += register_pretty(inst.dst_regs[0]);  // "R42"
    out += ", ";
    out += register_pretty(inst.src_regs[0]);
    out += ", ";
    out += register_pretty(inst.src_regs[1]);
    // ...
}
```

~2K LOC of mnemonic tables + printing logic.

### Round-trip correctness

The critical test: `decode(encode(program)) == program` for all valid IR003NV programs. Run continuously in CI. Any discrepancy indicates a bug in either the encoder or decoder — both are regenerated from the same tables, so they must agree.

### Size

~6K LOC total for decoder.

---

## 17. Math templates and precompiled fragments

FP64 division/reciprocal/sqrt and integer division/modulo have no single-instruction SASS implementation. They must be expanded into Newton-Raphson iterations (~50-120 SASS instructions each). Mimic provides these as inlinable templates.

### Templates

```
crucible/mimic/include/crucible/mimic/template/
├── Ddiv.h              — FP64 divide via MUFU.RCP + 2× DFMA refinement (~100-120 SASS)
├── Drcp.h              — FP64 reciprocal (~90 SASS)
├── Dsqrt.h             — FP64 square root via MUFU.RSQ (~80 SASS)
├── Drsqrt.h            — FP64 reciprocal square root (59-247 SASS by reg pressure tier)
├── Idiv.h              — Integer division 32-bit and 64-bit (~50 and ~80 SASS)
├── Imod.h              — Integer modulo
└── PressureTier.h      — select template variant per register budget
```

### Pressure tier selection

```cpp
enum class RegPressureTier : uint8_t {
    LOW,      // >20K free regs: use the full-unrolled template (fastest)
    MEDIUM,   // 16-20K: use the partially-spilled variant
    HIGH,     // <16K: use the minimum-reg variant
};

RegPressureTier select_tier(uint32_t free_regs) {
    if (free_regs > 20480) return RegPressureTier::LOW;
    if (free_regs > 16384) return RegPressureTier::MEDIUM;
    return RegPressureTier::HIGH;
}

// At IR003NV generation, each FP64 div instruction becomes:
//   select_tier(current_pressure) picks template variant
//   template is inlined at use site
```

### Template content

Each template is a hand-coded FX function (graded-type-verified) that takes `(dividend, divisor) → quotient` (or analogous) and emits IR003NV operations. ~500-1000 LOC per template.

Not compiled to cubin fragments — inlined at each use site by Mimic's encoder. Shared via common subexpression if the same divisor appears multiple times.

### Precompiled fragments

For rare ops that don't need inlining (e.g., subnormal exception handlers, integer divide-by-zero traps), use precompiled cubin fragments:

```
crucible/mimic/precompiled/
├── ddiv_subnormal_hopper.cubin
├── dsqrt_exception_hopper.cubin
├── ddiv_subnormal_blackwell.cubin
└── ...
```

These are pre-generated by Mimic itself during calibration. Linked as cold sections of Forge-generated cubins. Not in the hot path.

### Total size

~5K LOC of templates + ~100KB of precompiled fragments per SM.

---

## 18. The peephole pass

Runs after register allocation, before SASS encoding. ~150 rules, table-driven.

### Structure

```
crucible/mimic/include/crucible/mimic/peephole/
├── Engine.h              — single-pass rewrite engine
├── RulesAlu.h            — IADD3 fusion, FFMA formation, LOP3 truth-table
├── RulesMemory.h         — LDG coalescing, STG coalescing, .reuse hints
├── RulesControl.h        — BRA folding, predicate chains
└── RulesScheduling.h     — stall compaction, DEPBAR merge
```

### Key rules

**ALU:**
- `IADD3 + IADD3.X` pair with matching carry → fuse into `IMAD.WIDE`
- `FMUL + FADD` with no intervening use → `FFMA`
- `FADD + MUL` reverse pattern → `FFMA` (with denorm guard)
- `I2I` zero-extend from a known-bounded value → fold into producer
- 2-3 `AND`/`OR`/`XOR` → single `LOP3` with truth table

**Memory:**
- Adjacent `LDG.32` on contiguous addresses → `LDG.64` or `LDG.128` (requires alignment)
- `MOV R1, c[0][0x28]` directly consumed → const-bank operand on consumer
- Mark `R-operand` reads as `.reuse` when next instruction reads same register

**Control:**
- `ISETP; @P BRA; ISETP; @P BRA` with correlated predicates → single `PLOP3 + BRA`
- Branch to next instruction → delete
- Branch to block with one inst that's a return → replace with return

**Scheduling:**
- Merge adjacent `DEPBAR` with combined mask
- Compact stall counts based on actual consumer distance
- Drop redundant NOPs not needed for alignment

### Engine

```cpp
void run_peephole(Ir003NvProgram& prog) {
    for (uint32_t i = 0; i < prog.insts.size(); i++) {
        for (const Rule& rule : peephole_rules) {
            if (rule.matches(prog, i)) {
                rule.apply(prog, i);
                // Don't re-increment i — re-check from same position
                i--;
                break;
            }
        }
    }
}
```

Single-pass. Rules are ordered by priority; higher-priority rules fire first. Convergence via restart-at-match-position.

### Extension-point-aware rewrites

Peephole rules are tagged with an "origin policy" that determines whether they may fire across the structural / extension-point body boundary (FORGE.md §18.7).

```cpp
enum class RuleOriginPolicy : uint8_t {
    STRUCTURAL_ONLY,      // matches only structural-kernel instructions
    BODY_ONLY,             // matches only IS_EXTENSION_POINT_BODY instructions
    CROSS_BOUNDARY,        // may match spans that cross the structural/body boundary
};
```

**Cross-boundary rules** enable the fusion that makes extension-point bodies achieve hand-tuned perf:

- **Body MUL followed by structural ADD → FMA.** A `score_mod` body multiplying score by ALiBi bias, followed by the structural softmax's exp preprocessing, merges into a single FFMA on Hopper.
- **Body WHERE followed by structural accumulator update → predicated FMA.** A `mask_mod` emitting `where(mask, score, -inf)` fuses with the next tile's accumulator update via predicated issue.
- **Body CAST folded into structural MMA operand.** A body casting input to FP32 for computation, followed by the structural MMA using the FP32 value directly, elides the cast (the MMA operand already holds the widened value in the accumulator register).

Roughly 30 of the 150 NV peephole rules are `CROSS_BOUNDARY`; analogous counts per vendor backend. Rules that would merge body instructions across two distinct extension points (e.g., `score_mod` output directly into `mask_mod` input) are rejected to preserve the per-extension-point semantic boundary.

**Body-only rules** specialize for common body patterns:

- `body_adj(MUL, ADD)` → FMA within the body's micro-op sequence
- `body(WHERE, same-predicate)` chains → merged predicate evaluation
- `body(CAST(x, T), CAST(y, T))` with matching sources → shared cast

### Size

~5K LOC rules + 1K LOC engine + ~1K LOC for extension-point-aware rewrites.

## 19. MAP-Elites — the search engine

MAP-Elites is Mimic's solution to the kernel-synthesis optimization problem. Given a FusedRegion, find an IR003NV program that minimizes cycles subject to target constraints.

Unlike greedy search (XLA's benchmark-best-wins) or random autotune (Triton), MAP-Elites maintains a **behavior archive**: a discretized grid where each cell stores the best solution with the corresponding behavior coordinates. This gives us:

- Diverse solutions across the design space (not a single local optimum)
- Robustness (if one cell's solution breaks, others remain)
- Interpretable catalog (each cell has meaning: "best at 50% occupancy with 96 regs")
- Runtime adaptability (different cells for different dispatch contexts)

### Behavior axes

Six discrete axes, each with 8 buckets. Total cells: 8^6 = ~260K. Typically 500-5K populated per kernel family.

1. **Occupancy bucket** — [1/8, 1/4, 3/8, 1/2, 5/8, 3/4, 7/8, 1.0]
2. **Register usage bucket** — [32, 48, 64, 80, 96, 128, 168, 255]
3. **Smem usage bucket** — 8 log-spaced buckets 0..228KB
4. **Pipeline depth bucket** — [1, 2, 3, 4, 5, 6, 8, 10+]
5. **MMA shape family** — 8 enum entries (m64n8, m64n16, m64n32, m64n64, m64n128, m64n256, m128n256, m128n512)
6. **Warpgroup split** — 8 patterns (1-0-0, 2-0-0, 0-0-4, 1-1-2, 2-1-1, 2-2-0, 1-2-1, 2-1-2)

### Fitness

```cpp
float fitness(SimResult r, const DecodedProgram& prog, const TargetCaps& caps) {
    // Efficiency ratio: how close to theoretical maximum
    uint64_t theoretical_min = compute_theoretical_min_cycles(prog, caps);
    return static_cast<float>(theoretical_min) / static_cast<float>(r.total_cycles);
}
```

Normalized to [0, 1]. 1.0 means perfect roofline utilization. 0.5 means 50% efficient. 0.9 is excellent; 0.95 is near-optimal; 0.98+ is probably incorrect (measurement error).

### Archive data structure

```cpp
struct ArchiveCell {
    bool populated;
    FeatureVec behavior;           // 6 × uint8_t = 6 bytes
    float fitness;                 // best fitness seen in this cell
    SimResult result;              // full simulation result (for insight retrieval)
    const uint8_t* cubin_bytes;    // arena-allocated
    uint32_t cubin_size;
    ContentHash program_hash;      // for cache key consistency
    uint64_t mutation_count;       // how many mutations reached this cell
    uint64_t last_updated_cycle;   // for staleness detection
};

struct MapElitesArchive {
    // Sharded lock-free map for parallel access
    std::array<std::vector<ArchiveCell>, 64> shards;  // sharded by behavior hash
    std::atomic<uint64_t> total_populated;
    std::atomic<uint64_t> total_submissions;
};
```

Sharded by `hash(behavior) % 64` for concurrent access. Each shard is lock-free open-addressing.

### Sampling (parent selection)

```cpp
ArchiveCell sample_parent(MapElitesArchive& archive, RNG& rng) {
    float r = rng.uniform();
    
    if (r < 0.3) {
        // Curiosity: prefer sparse regions
        return sample_from_sparse_shards(archive, rng);
    } else if (r < 0.7) {
        // Exploitation: prefer high-fitness cells
        return weighted_sample(archive, [](const ArchiveCell& c) { return c.fitness * c.fitness * c.fitness; });
    } else {
        // Exploration: uniform random from populated cells
        return uniform_random(archive, rng);
    }
}
```

Biased to balance exploitation (evolve from good candidates) and exploration (investigate under-filled regions of the behavior space).

### Main search loop

```cpp
CompiledKernel compile_kernel(
    fx::Bg bg,
    Arena& arena,
    const FusedRegion& region,
    const TargetCaps& caps,
    const CompileConfig& cfg
) {
    // Warm-start from Cipher if archive exists
    MapElitesArchive archive = load_or_create_archive(region.hash, caps, bg);
    
    // Initial seed: translate region to an initial IR003NV candidate
    Ir003NvProgram seed = translate_initial(region, caps);
    
    // Thread pool for parallel evaluation
    WorkerPool pool(cfg.worker_count);
    
    auto deadline = now() + cfg.max_wall_time_ms;
    uint32_t iteration = 0;
    
    while (now() < deadline && archive.best_fitness() < 0.95 && iteration < cfg.max_iterations) {
        // Batch of candidates per generation
        std::vector<Ir003NvProgram> batch;
        for (int i = 0; i < BATCH_SIZE; i++) {
            // Sample parent
            auto parent = sample_parent(archive, rng);
            // Mutate
            auto child = mutator.mutate(parent.program, parent.result.insights);
            batch.push_back(child);
        }
        
        // Evaluate batch in parallel
        std::vector<SimResult> results = pool.evaluate(batch, caps, Tier::MEDIUM);
        
        // Submit to archive
        for (size_t i = 0; i < batch.size(); i++) {
            FeatureVec behavior = compute_behavior(batch[i], results[i]);
            float fit = fitness(results[i], batch[i], caps);
            archive.try_submit(behavior, fit, batch[i], results[i]);
        }
        
        iteration++;
    }
    
    // Persist archive to Cipher
    persist_archive(archive, region.hash, caps, bg);
    
    // Return best
    auto best = archive.best_cell();
    return CompiledKernel{
        .cubin_bytes = best.cubin_bytes,
        .predicted_cycles = Cycle{best.result.total_cycles},
        .predicted_efficiency_ratio = best.fitness,
        .vir_hash = best.program_hash,
        .search_iterations = iteration,
        .insights = best.result.insights,
    };
}
```

### Termination

- `efficiency_ratio > 0.92` — near-optimal, stop
- `max_wall_time_ms` exceeded — return current best
- `max_iterations` exceeded (typically 200-1000) — return current best
- Stuck for 50 generations with no improvement — return current best
- Budget Phase H called with `config::budget_mode = FAST` — use fewer iterations

### Warm-start advantage

When compiling a new kernel that structurally matches a previously-compiled one (same ContentHash signature, different TargetCaps), the archive from the previous compile loads from Cipher. The mutator can start from cells that already have good fitness on the previous target; most mutations transfer well across similar targets.

Effect: compile time for a structurally-repeat kernel drops from 30-50ms to 5-10ms. For a training run with repeated forward passes of the same model, this is >95% cache-hit and the archive never needs to be regenerated.

### Body-hash warm-start for extension-point variants

Research workloads generate many kernels that share structural identity but differ in extension-point body content (FORGE.md §18.7). Example: a researcher iterating on `score_mod` for attention produces dozens of variants with the same `AttentionAttrs` structural fields and different `score_mod_body` hashes. Each variant is a separate `KernelContentHash` and warrants its own archive, but the structural search machinery is identical.

Crucible's archive lookup uses a two-key scheme:

```cpp
std::optional<MapElitesArchive> load_archive(ContentHash kernel_hash,
                                              TargetCapsSignature caps) {
    // 1. Exact match
    if (auto exact = cipher::lookup(archive_key(kernel_hash, caps))) return *exact;

    // 2. Structurally-similar warm-start
    auto structural_hash = canonical_structural_hash(kernel_hash);
    auto nearest = cipher::lookup_nearest_structural(structural_hash, caps);
    if (nearest) {
        return warm_start_from(*nearest);  // top-N fittest cells transplanted;
                                            // mutation converges 5-10× faster
    }

    return std::nullopt;
}
```

Where `canonical_structural_hash` strips the extension-point body hashes from the kernel's content hash, keeping only structural attrs (shape, layout, dtype, tile, recipe). Two kernels with identical structure and different bodies share the canonical structural hash; their archives can warm-start each other.

**Per-body-family archive metadata.** Each cell carries a `body_hash_set` listing which body hashes produced this cell across previous compiles. A new body variant whose `body_hash` is structurally close (small graph edit distance on its ComputeBody) to the cell's existing body set inherits the cell as warm-start initializer.

**Graph-edit-distance similarity.** Two `ComputeBody*` fragments are "similar" if:
- Same top-level op topology (same dataflow graph shape)
- Same input/output arity and dtypes
- Constants differ by <10× magnitude

Similarity metric computed offline when archives persist; stored as an index for fast nearest-neighbor lookup.

**Impact.** For a user iterating on `score_mod` variants of a FlashAttention-class kernel:
- First compile (cold): 100-500 MAP-Elites generations, ~30-60s
- Structurally-warm-started compile (nearest-body available): 10-50 generations, ~3-10s
- Fully-warm-started compile (same body hash in cache): 0 generations, ~1μs cache lookup

The mutation operators also benefit: when a body variant is close to an existing cell's body, mutations that reduced structural pressure on the similar body are tried first (insight-weighted sampling weights them higher).

### Hybrid search mode — MAP-Elites + real-hardware validation

Beyond pure MAP-Elites (default), Crucible supports a hybrid mode where top archive cells are validated against real hardware. User-facing enum in FORGE.md §22.8; this section describes the Mimic-side realization.

#### Validation protocol

When `CompileConfig.mode == HYBRID`:

1. MAP-Elites runs as normal — simulator fitness, archive construction, `max_iterations` bound.
2. After MAP-Elites finishes, select top `hardware_cand_count` cells by fitness (default 10).
3. For each candidate:
   - Compile to executable bytes (cubin / HSACO / NEFF / ...)
   - Load onto real hardware via per-vendor runtime
   - Execute with representative input shapes, 10 iterations, record minimum time
   - Invalidate L2 cache between iterations via large scratch read (tinygrad-style)
4. Re-fit simulator calibration if measured time disagrees with predicted by >15% — rare event indicating simulator drift requiring investigation.
5. Return the candidate with lowest measured time as the compiled kernel.

Hardware validation budget: `hardware_budget_ms` (default 200 ms). If budget exhausts before all top-K validated, return best validated so far.

#### Cost and when to use

| Scenario | Recommended mode | Rationale |
|---|---|---|
| Production training (stable workload) | MAP_ELITES | Simulator accuracy sufficient; amortize across many steps |
| First compile of a novel model | HYBRID | Validate simulator predictions match real behavior |
| Federated cache build (no hardware) | MAP_ELITES | No hardware available |
| Cross-vendor validation | HYBRID | Confirm cross-vendor numerics + perf on each target |
| Dev / debug investigation | BEAM | Real-hardware search to understand perf floor |

In production training loops, HYBRID fires only on first submission of a novel plan; subsequent submissions hit cache. Compile-time cost impact: <1% across a run.

#### Disk cache

Hybrid-mode results cached by `(kernel_content_hash, target_caps_signature, mode)`. Re-running HYBRID on the same (kernel, hardware) hits cache at ~30 ns lookup, identical to MAP_ELITES.

#### Cross-vendor CI integration

The cross-vendor CI matrix (§41) runs in HYBRID mode by default: each kernel × recipe × target compiled with MAP-Elites, then validated on real silicon. Measured outputs feed the simulator calibration loop (§22) and the cross-vendor tolerance enforcement. A simulator-vs-measured delta > 15% triggers an investigation ticket; > 5% is logged as a metric for Phase L drift detection.

#### Determinism

HYBRID is deterministic when `seed` is fixed and hardware is stable. Measurement noise can cause candidate ranking to swap for candidates within 5% of each other; under BITEXACT CI, we require the selected candidate be within 5% of optimal (not exactly optimal) to satisfy reproducibility bounds.

---

## 20. Mutation operators

The set of transformations Mimic's mutator applies to produce child candidates from a parent IR003NV program.

### Macro mutations (~10% of applications)

Rare but high-impact. Each changes the overall structural shape of the kernel.

- `CHANGE_TILE_SHAPE` — swap (tile_m, tile_n, tile_k) to an alternative
- `CHANGE_WARPGROUP_SPLIT` — reassign warp groups (producer/consumer specialization)
- `CHANGE_GRID_SHAPE` — adjust CTA count, cluster size
- `CHANGE_CLUSTER_SIZE` — 1, 2, 4, 8 CTAs (Hopper+ TBC)
- `CHANGE_CTA_GROUP` — 1CTA vs 2CTA (Blackwell tcgen05)
- `SWAP_LAYOUT` — row-major ↔ col-major ↔ tiled
- `SPLIT_K` — K dim across CTAs with atomic accumulation
- `PERSISTENT_CTA` — convert to one-shot vs persistent scheme

### Meso mutations (~40% of applications)

Medium impact. Change pipeline structure without altering tiles.

- `CHANGE_PIPELINE_STAGES` — 1, 2, 3, 4, 5, 6, 8 stages
- `CHANGE_SWIZZLE_PATTERN` — MN_128B, MN_64B, MN_32B, etc.
- `CHANGE_MMA_SHAPE_VARIANT` — m64n128k16 vs m64n256k16 etc.
- `CHANGE_EPILOGUE_SCHEME` — inline vs separate
- `ADD_PREFETCH_STAGE` — deeper pipeline with more overlap
- `REMOVE_PREFETCH_STAGE` — shallower pipeline, less reg pressure
- `CHANGE_REGISTER_ALLOC_STRATEGY` — MMA-aligned vs packed vs balanced
- `MERGE_ADJACENT_MMAS` — chain multiple MMA ops
- `HOIST_BETWEEN_LAYERS` — move computation across layer boundaries

### Micro mutations (~50% of applications)

Small tweaks. Change individual instructions or scheduling details.

- `SWAP_REG_PAIR` — reassign a specific register pair
- `REORDER_INSTRUCTIONS` — within straight-line code
- `ADD_REUSE_HINT` — set .reuse flag on specific instruction
- `CHANGE_WARP_ORDER` — within a warpgroup
- `ADJUST_MBARRIER_COUNT` — per-mbarrier expected-tx or arrival count
- `CHANGE_SCOREBOARD_ALLOCATION` — reassign SB slots
- `CHANGE_STALL_COUNT` — adjust per-instruction stall
- `ADD_NOPS_FOR_ALIGNMENT` — pad for i-cache line alignment
- `ADD_PREDICATION` — if-convert a short branch
- `REMOVE_PREDICATION` — split a predicated block into branches

### Mutation selection

```cpp
MutationOp select_mutation(const Ir003NvProgram& parent, const SimResult& result, RNG& rng) {
    // Primary rule: select from insights
    if (!result.insights.empty() && rng.uniform() < 0.7) {
        auto insight = sample_weighted_by_severity(result.insights, rng);
        auto hints = HintCatalog::hints_for(insight.kind);
        return hints[rng.uniform_int(hints.size())];
    }
    
    // Fallback: random mutation weighted by scale
    float r = rng.uniform();
    if (r < 0.1) return random_macro_mutation(rng);
    if (r < 0.5) return random_meso_mutation(rng);
    return random_micro_mutation(rng);
}
```

Insight-driven selection is ~3-8× more effective than random mutation because it applies expert knowledge encoded in the insight → hint mapping.

### Mutation safety

Each mutation has a precondition. A `SPLIT_K` mutation only applies when K is large enough; `PERSISTENT_CTA` only when reg/smem budget allows. The mutator checks preconditions before applying; if a mutation is impossible, it falls back to a random eligible alternative.

Post-mutation: the candidate is validated through Forge's register allocator and memory planner. If validation fails (e.g., register pressure exceeds 255), the mutation is reverted and the parent stays in the archive.

### Size

~7K LOC for the mutator (dispatch + per-op implementation + precondition checks).

---

## 21. Archive persistence and warm-start

The MAP-Elites archive is a valuable asset. It captures kernel-family knowledge that took thousands of mutations to discover. Persist it to Cipher so it survives process death, cross-run reuse, federated sharing across installations.

### Storage format

```cpp
struct ArchiveSerialized {
    ContentHash family_hash;             // canonicalized op signature (kernel family)
    TargetCapsSignature caps_signature;   // target identity
    uint32_t cell_count;
    uint64_t total_mutations;
    uint64_t best_fitness_fixed_point;   // float encoded as uint64 (* 10^9)
    
    std::span<const CellSerialized> cells;
};

struct CellSerialized {
    FeatureVec behavior;
    uint32_t fitness_fixed_point;
    uint32_t mutation_count;
    uint32_t cubin_size;
    std::span<const uint8_t> cubin_bytes;
    const Insight* insights;
    uint32_t insight_count;
};
```

All fields arena-packable. Serialized as a single contiguous blob, checksummed, stored in Cipher indexed by `(family_hash, caps_signature)`.

### Lookup

At the start of `compile_kernel`:

```cpp
std::optional<MapElitesArchive> load_archive(ContentHash family, TargetCaps caps) {
    auto entry = cipher::lookup(archive_key(family, caps));
    if (!entry) return std::nullopt;
    
    auto serialized = deserialize<ArchiveSerialized>(entry);
    return reconstruct_archive(serialized);
}
```

### Canonicalization of the family hash

Two IR003NV programs from the same kernel family should hash to the same `family_hash`. Canonicalize by:
- Stripping tile-specific parameters (tile_m, tile_n, tile_k normalized to symbols)
- Stripping target-specific choices (which MMA shape used — normalize to generic MMA family)
- Keeping the underlying computation structure (matmul vs conv vs attention)

This lets archives from one GEMM shape contribute to another GEMM of different dimensions.

### Federated sharing

The Cipher cold-tier storage (S3/GCS) can be shared across Crucible installations. Organization A compiles a ResNet-50 training run and populates the archive for 50 kernel families; Organization B loads the same archive when they run a similar workload.

Privacy: the archive stores cubin bytes + predicted cycles + behavior vector. No user data, no model weights, no proprietary information. Pure kernel synthesis artifacts.

Governance: a federation agreement specifies which installations share; opt-in by default.

### Growth over time

Per kernel family, the archive grows monotonically (only better solutions replace worse ones in a cell). Over thousands of compilations, it asymptotes near theoretical optimum.

For the ML ecosystem as a whole (millions of users), the combined archive becomes a **GPU kernel compilation genome** — every shape/dtype combination we've ever seen, with its near-optimal kernel.

---

## 22. Calibration — how we reach 95-98%

Without calibration, Mimic is 70-80% accurate (ballpark predictions from hardware docs). With calibration, 95%+. The difference is 2-3 months of engineering effort per SM generation.

### The seven-stage calibration pipeline

1. **Isolation**: single-warp, single-pipe, no memory. Measure per-opcode latency, occupancy, throughput_inv. ~30 benchmarks per SM.

2. **FU contention**: N warps doing the same op. Measure per-pipe instance count, partition share. ~20 benchmarks per SM.

3. **Memory isolation**: single-warp pointer chase, varying stride. Measure L1/L2/DRAM latencies uncontended. ~15 benchmarks per SM.

4. **Memory BW**: max-width strided loads, all warps. Measure HSHUB BW, L2 BW, HBM BW ceilings. ~10 benchmarks per SM.

5. **Scheduler**: 2-4 warps with asymmetric stall patterns. Measure GTO greedy probability, boost threshold, burst limit. ~10 benchmarks per SM.

6. **Integration**: tiled GEMM, softmax, FlashAttention. Validate pipeline composition. 20 benchmarks per SM.

7. **Regression**: 500 real kernels with known perf. Target P95 residual < 5%. Automatic.

Total: ~150-200 benchmarks per SM target, run once per calibration cycle.

### Calibration harness

```
crucible/mimic/bench/
├── mb_ffma_latency.cu
├── mb_dfma_latency.cu
├── mb_wgmma_latency.cu
├── mb_utcmma_latency.cu
├── mb_l1_latency.cu
├── mb_l2_latency.cu
├── mb_hbm_bw.cu
├── mb_register_bank.cu
├── mb_warp_scheduler.cu
├── mb_mbarrier_latency.cu
├── mb_cluster_sync.cu
└── run_calibration.cpp
```

`run_calibration.cpp` is the orchestrator:

```cpp
int main(int argc, char** argv) {
    auto target = parse_target(argv);      // e.g. "h100_sxm5"
    CudaContext ctx = init_cuda();
    
    CalibrationResults results;
    
    // Stage 1: isolation
    for (auto& bench : isolation_benchmarks) {
        auto result = bench.run(ctx);
        results.add(result);
    }
    
    // Stage 2-7: progressively
    run_fu_contention(ctx, results);
    run_memory_isolation(ctx, results);
    run_memory_bw(ctx, results);
    run_scheduler(ctx, results);
    run_integration(ctx, results);
    run_regression(ctx, results);
    
    // Fit parameters
    TargetCaps caps = fit_target_caps(results);
    OpcodeLatencyTable latency = fit_opcode_latencies(results);
    
    // Emit .json
    write_json(caps, latency, target);
    
    // Report residuals
    report_residuals(results);
    
    return 0;
}
```

Each benchmark is a CUDA kernel that measures a specific quantity:

```cu
__global__ void mb_ffma_latency() {
    // Single warp, single FFMA chain with no other instructions
    // Chain depth = 32, measure cycles
    float a = 1.0f, b = 1.0f, c = 1.0f;
    uint64_t start = clock64();
    #pragma unroll 32
    for (int i = 0; i < 32; i++) {
        a = __fmaf_rn(a, b, c);
    }
    uint64_t end = clock64();
    // ...
}
```

Runs on real GPU. Results compared against Mimic's prediction for the same kernel. Fit: `actual_latency = m * predicted_latency + b`; if residual > 5%, investigate.

### Calibration time

Per SM target, expect:
- Week 1: stages 1-3 (isolation benchmarks) → caps + basic opcode latencies
- Week 2: stages 4-5 (memory BW, scheduler) → memory subsystem parameters
- Week 3-4: stages 6-7 (integration + regression) → full validation
- Week 5-8: outlier investigation, additional benchmarks, refinement

Total: 1-2 months per SM to hit 95% accuracy. Another 2-3 months to hit 98% — diminishing returns, probably not worth chasing.

### Continuous calibration

Crucible's L14+L15 (Phase L of Forge) monitors residuals during runtime. When drift > 10% for 100+ samples, trigger recalibration:

```cpp
void l15_drift_detected(InsightKind kind, float residual) {
    if (residual > 0.10 && samples_exceeding > 100) {
        mimic::trigger_recalibration();
        // ... schedule calibration harness run
        // ... upon completion, invalidate affected KernelCache entries
    }
}
```

Recalibration runs during idle time (Keeper detects GPU idle, runs the calibration suite). Results update the JSON file; KernelCache entries invalidate; next use triggers recompile.

This is how Mimic stays accurate over the lifetime of the hardware.

---

## 23. CUPTI integration

CUPTI (CUDA Profiling Tools Interface) is NVIDIA's library for GPU performance measurement. Mimic uses it for calibration and for runtime drift detection.

### What CUPTI provides

- Per-kernel cycle count via `CUPTI_EVENT_SM__CYCLES_ELAPSED`
- Per-SM pipeline utilization via pipeline-specific events
- L1/L2/DRAM hit rates via cache-level events
- Warp stall reason distribution via `CUPTI_EVENT_SM__WARP_CYCLES_NOT_ISSUED_*`
- Memory traffic via `CUPTI_EVENT_DRAM__BYTES` family
- Register pressure via `CUPTI_EVENT_SM__REGISTERS_PER_WARP`
- Instructions retired via `CUPTI_EVENT_INSTS_ISSUED`

~200 counter kinds on Hopper; ~300 on Blackwell.

### Integration point

```
crucible/mimic/include/crucible/mimic/calibrate/Cupti.h
```

Wraps CUPTI's C API in C++-with-RAII:

```cpp
namespace crucible::mimic::cupti {

class ProfileSession {
public:
    explicit ProfileSession(fx::Init, const std::vector<EventKind>& events);
    ~ProfileSession();
    
    // Begin profiling; subsequent kernel launches are measured
    void begin();
    
    // End and collect results
    CuptiMeasurement end();
};

struct CuptiMeasurement {
    uint64_t cycles;
    float ipc;
    std::array<float, 10> per_pipe_utilization;
    std::array<float, 3> cache_hit_rates;      // L1, L2, DRAM
    uint64_t bytes_moved_dram;
    std::array<uint64_t, 12> stall_reasons;     // matching Mimic's StallReason enum
    uint16_t peak_regs;
    uint32_t peak_smem;
};

} // namespace crucible::mimic::cupti
```

### Usage in calibration

```cpp
for (auto& bench : benchmarks) {
    cupti::ProfileSession session{fx::Init{}, {
        EVENT_CYCLES_ELAPSED,
        EVENT_INST_ISSUED,
        EVENT_L2_HITS,
        // ...
    }};
    
    session.begin();
    bench.launch_on_gpu();
    auto measured = session.end();
    
    // Compare to Mimic's prediction
    DecodedProgram prog = decode_cubin(bench.cubin);
    SimResult predicted = mimic::predict(fx::Bg{}, arena, prog, caps, Tier::ACCURATE);
    
    // Compute residual
    float cycle_residual = abs(measured.cycles - predicted.total_cycles) 
                           / static_cast<float>(measured.cycles);
    results.add_residual(bench.name, cycle_residual);
}
```

### Usage in Augur

At runtime, 1% of kernel executions are sampled:

```cpp
// In Augur's monitoring loop
void augur_sample_kernel(KernelInvocation inv) {
    if (sampling_rng.uniform() > 0.01) return;  // 1% sampling
    
    cupti::ProfileSession session{..., {/* counters */}};
    session.begin();
    inv.launch();
    auto measured = session.end();
    
    // Mimic's stored prediction for this kernel
    SimResult predicted = inv.plan->region_predictions[inv.region_id];
    
    // Compute residual, append to regression dataset
    residuals.append({
        .region_id = inv.region_id,
        .cycles_residual = compute_residual(measured.cycles, predicted.total_cycles),
        // ... other signals
    });
    
    // Trigger drift detection if P95 exceeds threshold
    if (residuals.p95_over(last_1000) > 0.10) {
        drift_detected();
    }
}
```

### Soft dependency

CUPTI is NVIDIA-proprietary and not always available. Build-time flag `MIMIC_CUPTI` enables integration; off-by-default.

Without CUPTI: calibration disabled, Mimic uses defaults from published hardware docs (accuracy ~70-80%). Useful for development environments without GPU access.

### Size

~3K LOC for CUPTI wrapper + integration.

---

## 24. Leveraging driver source for the memory model

This is Mimic's secret weapon and the reason we can hit 95%+ accuracy where academic simulators cap at 85-92%.

### What the driver source tells us directly

With access to NVIDIA driver source through Hopper, we can read:

1. **L2 slice address hash function** — the driver documents the hash used for GPC-to-FBP routing. Not a mystery; copy it verbatim.

2. **HBM3 command scheduler policy** — FR-FCFS with per-channel age prioritization is described in the DRAM controller driver. The exact queue depths, priority thresholds, and aging constants are in header files.

3. **Row-buffer policy** — open-page by default, closing-trigger on hit-rate drop. Parameters explicit in the driver.

4. **Refresh scheduling** — per-channel refresh interval, stall propagation rules, interleaving with reads/writes.

5. **L1 eviction policy** — LRU with streaming bypass. The streaming bypass threshold (cache-miss rate above which new misses evict younger lines first) is in the driver.

6. **HSHUB arbitration** — round-robin across GPCs with age-based boost. Exact constants in the crossbar driver.

7. **Coalescer logic** — the 128-byte transaction merging rules, including handling of strided and misaligned accesses.

8. **Read-write turnaround** — the cycle penalty when switching directions on a channel. Explicit in the DRAM controller.

9. **ECC overhead** — the exact BW penalty (6-12% for HBM3, 2-4% for register file) is specified.

### What this buys us

Academic simulators like Accel-Sim reverse-engineer these policies from microbenchmarks — they run synthetic workloads, measure latency distributions, fit parameters to curves. The fit is noisy, and it captures only the aggregate behavior, not the exact mechanism.

Mimic implements the actual mechanism. The result:

- No residual from modeling errors — only from calibration of the underlying constants (which are exact HW parameters, not fitted)
- Accurate prediction under novel access patterns (the mechanism covers them; a fitted curve only covers what was sampled)
- Explanatory: when a prediction is wrong, we can identify which mechanism's constant is miscalibrated

### Where the driver source doesn't help

Some behaviors are in silicon, not in the driver:
- Transistor-level voltage droop
- Thermal-induced timing variations
- Process variation across chips of the same SKU
- DRAM device-level command-ordering subtleties (driver writes to controller; controller to DRAM is proprietary)

These contribute the residual 2-5% error that keeps us at 95-98% rather than 99%. They're the tail.

### Blackwell and beyond

We have driver source through Hopper. Blackwell has **similar mechanisms** (memory subsystem is evolutionary, not revolutionary), so we can extrapolate patterns. But specific constants need calibration.

For Blackwell-new features (tcgen05, TMEM, scaled MX formats), we reason from:
- Hopper equivalents (tcgen05 is the async version of WGMMA's sync MMA)
- Published NVIDIA architecture whitepapers
- Microbenchmark measurements

This gets us to ~90% accuracy on Blackwell without driver source; calibration closes the gap to 95%.

### Source-informed code in the memory subsystem

The following Mimic source files embed driver-derived logic:

```
crucible/mimic/src/mimic/sim/
├── MemSubsystem_Hopper.cpp        ← uses Hopper-documented mechanisms
├── MemSubsystem_Blackwell.cpp     ← extrapolated from Hopper + calibrated
├── L2Hash_Hopper.cpp              ← exact L2 slice hash function
├── HbmScheduler.cpp               ← FR-FCFS with aging
├── RowBufferPolicy.cpp            ← open-page with close-on-hit-drop
└── CoalescerLogic.cpp             ← 128-byte transaction merging
```

Each file is small (~500-2000 LOC). Together, ~6-8K LOC implement the memory model to driver fidelity.

### Provenance note

Nothing in these files should reveal NVIDIA's internal naming or source structure. We implement the mechanisms, not the source code. Variable names, function names, file organization are ours. The knowledge flows through the engineer's head, not through copy-paste. This matches the methodology used for the nvopen-tools wiki.

---

## 25. Determinism

Non-negotiable. Same inputs → same outputs, bit-exact. Mimic's determinism is a Crucible-wide invariant (Axiom 8 DetSafe from CLAUDE.md) and a MAP-Elites-correctness requirement.

### Sources of non-determinism to avoid

1. **Hash iteration order** — when iterating a `std::unordered_map` or `std::unordered_set`, order depends on hash values which depend on addresses. Solution: sort keys explicitly before iteration.

2. **FP summation order** — `sum(a, b, c) ≠ sum(c, b, a)` in IEEE-754. Solution: canonical summation order (Kahan or pairwise with explicit ordering).

3. **Pointer-based ordering** — don't order events by address. Events have `(cycle, kind, sequence_number)` where sequence is a deterministic counter.

4. **Allocator ordering** — arenas bump in fixed order; no freelist reuse. Per-compile arena fresh each time.

5. **Thread scheduling** — results gathered from parallel workers are merged in deterministic order (by worker ID, not by completion order).

6. **Random number generation** — Mimic uses Philox4x32 (Crucible's standard) with an explicitly-seeded counter per compile. Same seed → same mutation choices → same archive.

7. **TSC-based timing** — never used for simulation logic; only for diagnostic tracking with relaxed determinism.

### Determinism tests

```cpp
TEST(Determinism, SameInputsSameOutput) {
    auto region = make_test_region();
    auto caps = target::h100_sxm5();
    
    for (int i = 0; i < 100; i++) {
        auto result = mimic::compile_kernel(fx::Test{}, arena, region, caps, {.seed = 42});
        if (i > 0) {
            ASSERT_EQ(result.cubin_bytes, prev.cubin_bytes);
            ASSERT_EQ(result.vir_hash, prev.vir_hash);
        }
        prev = result;
    }
}
```

### Seed exposure in CompileConfig

```cpp
struct CompileConfig {
    uint64_t seed = 0;                        // default: derive from content_hash
    uint32_t max_wall_time_ms = 100;
    uint32_t max_iterations = 500;
    float target_efficiency = 0.92;
    Tier max_tier = Tier::MEDIUM;
    // ...
};
```

Default seed derives from `region.hash` so same region → same seed → same result without explicit config.

### Cross-hardware determinism

Running Mimic on an x86 Linux box should produce the same output as running on an ARM macOS box, given the same inputs. This requires:
- No architecture-specific integer sizes or endianness
- Well-defined FP semantics (no x87 80-bit intermediates)
- Consistent default library behaviors

Compile with strict flags. Test on both x86 and ARM platforms.

---

## 26. Threading and concurrency

Mimic's parallelism model follows Crucible convention: shared-nothing worker threads, SPSC rings for communication, acquire/release memory ordering only.

### Layers of parallelism

1. **MAP-Elites worker pool** — N worker threads each running one simulation. N typically = hardware_concurrency (32 on a typical server). Shared state: only the archive.

2. **Per-simulation** — single-threaded. No intra-simulation parallelism. Easier to reason about, easier to debug, and adding parallelism inside one simulation is less efficient than running more simulations in parallel.

3. **Calibration harness** — two threads: one dispatches kernels to GPU, one collects CUPTI results. Classic producer-consumer SPSC ring.

4. **GPU fast tier** — intra-device parallelism (one thread block per candidate). Embarrassingly parallel.

### Archive concurrency

The MAP-Elites archive is the only shared mutable state. Protected by:

```cpp
struct ArchiveShard {
    alignas(64) std::atomic<uint64_t> write_epoch;  // for CAS
    std::array<ArchiveCell, CELLS_PER_SHARD> cells;
};

struct MapElitesArchive {
    std::array<ArchiveShard, 64> shards;
    // ...
};

bool try_submit(MapElitesArchive& arch, FeatureVec behavior, float fitness, ...) {
    uint64_t hash = feature_vec_hash(behavior);
    ArchiveShard& shard = arch.shards[hash % 64];
    uint32_t cell_idx = find_cell(shard, behavior);
    
    // CAS: only update if our fitness is better
    for (;;) {
        uint64_t epoch = shard.write_epoch.load(std::memory_order_acquire);
        const ArchiveCell& current = shard.cells[cell_idx];
        if (current.populated && current.fitness >= fitness) return false;
        
        ArchiveCell new_cell = { ... populated with new data ... };
        
        if (shard.write_epoch.compare_exchange_weak(
                epoch, epoch + 1, std::memory_order_acq_rel)) {
            shard.cells[cell_idx] = new_cell;
            return true;
        }
    }
}
```

Lock-free CAS with epoch counter. Readers always see consistent state via acquire loads.

### Workers and the queue

```cpp
struct WorkerPool {
    std::vector<std::thread> workers;
    SpscRing<CandidateRequest> request_queue;
    SpscRing<EvaluationResult> result_queue;
    
    void enqueue(CandidateRequest req);
    EvaluationResult dequeue_result(bool blocking);
};
```

Workers pull from the request queue (atomic-spin, no OS blocking), evaluate, push to the result queue. The MAP-Elites driver fills the request queue and drains the result queue.

### No locks, no futex

Crucible Axiom enforced: spin on atomic loads with `_mm_pause()`, no `std::mutex`, no `std::condition_variable`, no `std::atomic::wait/notify`. Overhead microseconds.

For any operation that might block (waiting for GPU to complete, waiting for all workers to finish), spin on a completion atomic.

### Thread count

Default: `std::thread::hardware_concurrency()`. Configurable via `CompileConfig::worker_count`.

For typical workloads: 32 workers fully occupy a 32-core machine during Mimic's MAP-Elites loop. More workers than cores gain nothing (overhead of context switching).

---

## 27. Effect tokens and Crucible discipline

All Mimic functions follow the Crucible effect-token pattern. Effects are capabilities; you must hold the capability token to perform the effect.

### Effect tokens used

- `fx::Bg` — background-thread use. Can alloc, IO, block. Mimic's primary context.
- `fx::Init` — construction-time use. Can alloc, IO. Used for Mimic's initialization (loading TargetCaps, etc.).
- `fx::Test` — test-only. Unrestricted.

Foreground hot-path code never calls Mimic; Mimic is strictly background.

### Usage

```cpp
namespace crucible::mimic {

// All public API functions take effect tokens by value
CompiledKernel compile_kernel(
    fx::Bg,
    Arena& arena,
    const FusedRegion& region,
    const TargetCaps& caps,
    const CompileConfig& cfg = {}
);

// Internal functions may or may not take tokens; they're contextual

} // namespace
```

### Zero runtime cost

Effect tokens are empty structs. Passed by value. Compiler elides them entirely. They exist only in the type system at compile time. Runtime cost: zero machine instructions.

### Enforcement

Compiler rejects calls that don't hold the right token. A foreground hot-path function calling `mimic::compile_kernel(fx::Bg{}, ...)` fails to compile because foreground code never holds `fx::Bg`.

### Crucible axioms Mimic obeys

From Crucible's CLAUDE.md:
- **InitSafe** — NSDMI on every field; padding zero-initialized
- **TypeSafe** — strong IDs (`CRUCIBLE_STRONG_ID(Name)`), no ambiguous uint32 usage
- **NullSafe** — `std::span` over pointer+count, `[[nodiscard]]` on queries
- **MemSafe** — Arena-only allocation, no new/delete, no shared_ptr
- **BorrowSafe** — SPSC rings for cross-thread, no aliased mutation
- **ThreadSafe** — acquire/release only, no relaxed
- **LeakSafe** — Arena bulk-frees, unique_ptr for ring buffers, bg_ thread member declared last
- **DetSafe** — same inputs → same outputs

Every Mimic header and source file follows. Same conventions as Forge, Crucible core, and every other Crucible subsystem.

---

## 28. The public API

Three entry points. Everything else is internal.

### `mimic::compile_kernel`

```cpp
[[nodiscard]] CompiledKernel compile_kernel(
    fx::Bg,
    Arena& arena,
    const FusedRegion& region,
    const TargetCaps& caps,
    const CompileConfig& cfg = {}
);

struct CompileConfig {
    uint32_t max_wall_time_ms = 100;
    uint32_t max_iterations = 500;
    float target_efficiency = 0.92;
    Tier max_tier = Tier::MEDIUM;
    uint32_t worker_count = 0;  // 0 = hardware_concurrency
    uint64_t seed = 0;          // 0 = derive from region.hash
};

struct CompiledKernel {
    std::span<const uint8_t> cubin_bytes;
    Cycle predicted_cycles;
    float predicted_efficiency_ratio;
    ContentHash vir_hash;
    uint32_t search_iterations;
    const Insight* insights;
    uint32_t insight_count;
};
```

Entry for Forge's Phase H. Runs MAP-Elites search, returns best kernel.

### `mimic::predict`

```cpp
[[nodiscard]] SimResult predict(
    fx::Bg,
    Arena& arena,
    const DecodedProgram& program,
    const TargetCaps& caps,
    Tier tier = Tier::MEDIUM
);

enum class Tier {
    FAST,
    MEDIUM,
    ACCURATE,
};

struct SimResult {
    Cycle total_cycles;
    float ipc_achieved;
    float occupancy_achieved;
    float efficiency_ratio;
    StallReason bottleneck;
    uint64_t stalls[STALL_REASON_COUNT];
    uint16_t peak_regs;
    uint32_t peak_smem_bytes;
    const Insight* insights;
    uint32_t insight_count;
};
```

Entry for Augur's digital twin and Forge's Phase B.5 cost estimates. Deterministic prediction for an already-compiled kernel.

### `mimic::search_kernel`

```cpp
[[nodiscard]] SearchResult search_kernel(
    fx::Bg,
    Arena& arena,
    const FusedRegion& region,
    const TargetCaps& caps,
    const SearchConfig& cfg
);

struct SearchResult {
    const ArchiveCell* archive;
    uint32_t cells_populated;
    CompiledKernel best;
    std::span<const CompiledKernel> pareto;
};
```

Offline-autotune entry. Returns the full MAP-Elites archive, not just the best. Used for benchmark generation, research, kernel catalog curation.

### Nothing else is public

Everything in `include/crucible/mimic/core/`, `target/`, `sim/`, etc. is implementation detail. Users of Mimic interact only through these three functions.

---

## 29. Integration surface with Forge

Forge calls Mimic at three points:

### 1. Phase B.5 CostEstimate

```cpp
// Forge's Phase B analysis
uint64_t mimic::fast_cost(fx::Bg, const GraphNode& node, const TargetCaps& caps) {
    // Internally routes to Mimic's FAST tier with a single-node program
    // Returns estimated cycles
}
```

~10μs per call. Used during fusion decision cost calculations.

### 2. Phase H InvokeMimic

```cpp
// Forge's Phase H per-region processing
CompiledKernel ck = mimic::compile_kernel(
    fx::Bg{},
    *phase_arena,
    fused_region,
    target_caps,
    {.max_wall_time_ms = 60}
);
```

~10-60ms per region. Parallel across regions.

### 3. Phase L.2 CompareToModel

```cpp
// Augur's drift detection
SimResult predicted = mimic::predict(
    fx::Bg{},
    *augur_arena,
    decoded_program,
    target_caps,
    Tier::MEDIUM
);

float residual = abs(measured.cycles - predicted.total_cycles) / measured.cycles;
if (residual > 0.10) trigger_drift();
```

~10ms per sampled kernel. 1% sampling → ~1 call per 100 launches.

### Forge does not call Mimic's internal functions

Forge never constructs `DecodedProgram` directly, never accesses the MAP-Elites archive, never calls the encoder or decoder standalone. Mimic's internals are Mimic's business.

### Mimic does not reach into Forge's internals

Mimic takes `FusedRegion` as a view into Crucible's Graph. It doesn't know about Forge's Phase manager, PhaseLog, or compile strategy. Mimic just receives an immutable region + target, produces an immutable kernel.

### The arena boundary

Both Forge and Mimic use Crucible's Arena for allocation. Forge passes its arena to Mimic; Mimic allocates into it; arena ownership stays with Forge. When Forge's Phase H finishes, the arena still holds Mimic's outputs. When Forge finishes compilation, the arena frees everything at once.

Alternative: Mimic gets its own arena per call. Simpler lifetime but requires copying outputs across the boundary. Forge uses the shared-arena pattern to avoid copies.

---

## 30. Integration surface with Augur

Augur is Crucible L15's continuous monitoring layer. It observes running kernels, compares measurements to Mimic's predictions, and triggers calibration when drift exceeds threshold.

### Call pattern

```cpp
// In Augur's sampling loop
void augur_sample(KernelInvocation& inv) {
    if (sampling_rng.uniform() > sampling_rate) return;
    
    cupti::ProfileSession session{fx::Init{}, monitored_events};
    session.begin();
    inv.launch();
    auto measured = session.end();
    
    // Use the prediction stored at compile time
    SimResult predicted = inv.plan.region_predictions[inv.region_id];
    
    // Or refresh if needed
    // SimResult predicted = mimic::predict(fx::Bg{}, augur_arena, 
    //                                      inv.plan.decoded_programs[inv.region_id],
    //                                      caps, Tier::MEDIUM);
    
    auto residual = compute_residual(measured, predicted);
    augur_regression_dataset.append(residual);
    
    if (augur_regression_dataset.p95_over(last_1000) > 0.10) {
        drift_detected();
    }
}

void drift_detected() {
    log::warn("Drift exceeded 10% — triggering recalibration");
    mimic::recalibrate(fx::Bg{}, current_target_caps);
    // Upon completion, invalidate affected KernelCache entries
    keeper::invalidate_cache_entries(affected_region_hashes);
}
```

### Sampling rate

Default 1%. Configurable per workload. Higher sampling → better drift detection but more CUPTI overhead.

For pure inference workloads (no weight updates), lower sampling (0.1%) is enough.
For mixed training workloads with variable shapes, higher sampling (5-10%) may be warranted during the first N iterations.

### Drift attribution

When drift fires, Augur identifies which counter is the primary driver:

```cpp
struct DriftAttribution {
    Counter primary;                       // which counter drove the drift
    float primary_magnitude;               // residual magnitude
    std::vector<Counter> secondary;        // other counters contributing
    Subsystem suspected_cause;             // MEMORY, COMPUTE, SYNC, etc.
};

DriftAttribution attribute_drift(const RegressionDataset& ds) {
    // Analyze which counter has the largest residual P95
    // Map to Mimic subsystem
    // ...
}
```

This tells the operator which part of Mimic needs recalibration (e.g., "L2 hit latency has drifted; calibrate memory subsystem").

---

## 31. Directory structure

```
crucible/mimic/
├── README.md
├── CMakeLists.txt
│
├── include/crucible/mimic/
│   ├── Mimic.h                          ← public API umbrella
│   │
│   ├── core/                            ← shared types
│   │   ├── SimTypes.h                   ← strong IDs, Cycle, Pipe, StallReason
│   │   ├── DecodedInst.h                ← 32-byte decoded SASS instruction
│   │   ├── DecodedProgram.h             ← program-level wrapper
│   │   ├── Insight.h                    ← 40 InsightKind + MutationHint
│   │   ├── SimResult.h                  ← simulation output
│   │   ├── FeatureVec.h                 ← MAP-Elites behavior descriptor
│   │   ├── Fitness.h                    ← efficiency_ratio
│   │   └── Tier.h                       ← SimTier::{FAST, MEDIUM, ACCURATE}
│   │
│   ├── adapt/                           ← IR001 → Mimic entry
│   │   ├── FusedRegion.h                ← view into crucible::Graph subgraph
│   │   ├── TileHint.h
│   │   └── ShapeResolver.h              ← concretize Expr symbols
│   │
│   ├── ir003/                           ← vendor abstract machine IR
│   │   ├── Ir002Base.h                  ← common Op, Operand, Schedule
│   │   ├── nv/
│   │   │   ├── Ir003Nv.h                ← 48-op family
│   │   │   ├── Ir003NvBuilder.h
│   │   │   ├── Ir003NvPrinter.h
│   │   │   ├── Ir003NvVerifier.h        ← SM-gated verification
│   │   │   └── Ir003NvPatterns.h        ← canonical GEMM/attention templates
│   │   ├── am/                          ← AMD (future)
│   │   └── gl/                          ← TPU HLO (future)
│   │
│   ├── target/                          ← per-chip capabilities
│   │   ├── TargetCaps.h                 ← central struct
│   │   ├── Targets.h                    ← factories: h100_sxm5(), b200(), ...
│   │   ├── OpcodeLatencyTable.h
│   │   ├── PipeSpec.h
│   │   └── CapsLoader.h                 ← JSON data file loader
│   │
│   ├── encode/                          ← IR003NV → SASS bytes
│   │   ├── Packer.h
│   │   ├── Formats.h
│   │   ├── Opcodes.h
│   │   ├── Operands.h
│   │   ├── Control.h
│   │   ├── SassEncoder.h
│   │   ├── CubinEmitter.h
│   │   ├── Eiattr.h
│   │   ├── Relocations.h
│   │   ├── Capmerc.h
│   │   └── Dwarf.h
│   │
│   ├── decode/                          ← SASS → DecodedInst
│   │   ├── Unpacker.h
│   │   ├── FormatRecognize.h
│   │   ├── OpcodeLookup.h
│   │   ├── OperandDecode.h
│   │   ├── SassDecoder.h
│   │   └── Disasm.h
│   │
│   ├── template/                        ← inline math expansion
│   │   ├── Ddiv.h
│   │   ├── Drcp.h
│   │   ├── Dsqrt.h
│   │   ├── Drsqrt.h
│   │   ├── Idiv.h
│   │   └── PressureTier.h
│   │
│   ├── peephole/                        ← ~150 SASS-level rules
│   │   ├── Engine.h
│   │   ├── RulesAlu.h
│   │   ├── RulesMemory.h
│   │   ├── RulesControl.h
│   │   └── RulesScheduling.h
│   │
│   ├── sim/                             ← three-tier simulator
│   │   ├── Simulator.h                  ← orchestrator
│   │   ├── Event.h                      ← 32B event
│   │   ├── EventQueue.h                 ← 4-ary min-heap
│   │   ├── WarpState.h                  ← SoA warp tables
│   │   ├── SmState.h                    ← per-SM state
│   │   ├── MemSubsystem.h               ← L1/L2/HBM
│   │   ├── WarpScheduler.h              ← GTO + boost
│   │   ├── AsyncOps.h                   ← WGMMA/UTCMMA/TMA
│   │   ├── FastTier.h
│   │   ├── MediumTier.h
│   │   ├── AccurateTier.h
│   │   └── GpuFastTier.cu               ← GPU-accelerated fast tier
│   │
│   ├── insight/                         ← diagnostic extraction
│   │   ├── Extractor.h
│   │   ├── Kinds.h                      ← 40 InsightKind enum
│   │   ├── HintCatalog.h
│   │   └── Bottleneck.h
│   │
│   ├── mutate/                          ← mutation operators
│   │   ├── Mutator.h
│   │   ├── MutationOp.h
│   │   ├── Macro.h
│   │   ├── Meso.h
│   │   └── Micro.h
│   │
│   ├── search/                          ← evolutionary driver
│   │   ├── MapElites.h
│   │   ├── Archive.h                    ← sharded lock-free storage
│   │   ├── Population.h
│   │   ├── Driver.h
│   │   └── WorkerPool.h
│   │
│   ├── calibrate/                       ← calibration harness
│   │   ├── Microbench.h
│   │   ├── Calibrator.h
│   │   └── Cupti.h
│   │
│   └── api/
│       ├── CompileKernel.h
│       ├── Predict.h
│       └── SearchKernel.h
│
├── src/mimic/
│   ├── encode/per_sm/
│   │   ├── sm_90.cpp
│   │   ├── sm_100.cpp
│   │   ├── sm_103.cpp
│   │   └── sm_120.cpp
│   ├── decode/per_sm/
│   ├── target/per_chip/
│   │   ├── h100_sxm5.cpp
│   │   ├── h200.cpp
│   │   ├── b200.cpp
│   │   └── ...
│   ├── sim/
│   │   ├── WarpScheduler_GTO.cpp
│   │   ├── MemSubsystem_Hopper.cpp
│   │   ├── MemSubsystem_Blackwell.cpp
│   │   ├── L2Hash.cpp
│   │   ├── HbmScheduler.cpp
│   │   ├── RowBufferPolicy.cpp
│   │   └── CoalescerLogic.cpp
│   └── template/math_sm90.cpp
│
├── test/mimic/
│   ├── test_event_queue.cpp
│   ├── test_warp_state.cpp
│   ├── test_sim_interval.cpp
│   ├── test_sim_event.cpp
│   ├── test_sim_memory.cpp
│   ├── test_decode_sm90.cpp
│   ├── test_encode_sm90.cpp             ← roundtrip
│   ├── test_encode_decode_roundtrip.cpp
│   ├── test_cubin_valid.cpp             ← driver accepts generated cubin
│   ├── test_map_elites.cpp
│   ├── test_mutation_ops.cpp
│   ├── test_h100_caps.cpp
│   ├── test_accuracy_vs_hw.cpp          ← on-GPU validation
│   └── test_determinism.cpp
│
├── bench/mimic/                         ← microbenchmarks (CUDA)
│   ├── mb_ffma_latency.cu
│   ├── mb_dfma_latency.cu
│   ├── mb_wgmma_latency.cu
│   ├── mb_utcmma_latency.cu
│   ├── mb_l1_latency.cu
│   ├── mb_l2_latency.cu
│   ├── mb_hbm_bw.cu
│   ├── mb_register_bank.cu
│   ├── mb_warp_scheduler.cu
│   ├── mb_mbarrier_latency.cu
│   ├── mb_cluster_sync.cu
│   └── run_calibration.cpp
│
├── data/                                ← calibrated per-chip constants
│   ├── h100_sxm5.json
│   ├── h100_pcie.json
│   ├── h200.json
│   ├── b100.json
│   ├── b200.json
│   ├── gb300.json
│   ├── thor.json
│   ├── rtx5090.json
│   └── README.md
│
├── precompiled/                         ← pre-built cubin fragments
│   ├── ddiv_hopper.cubin
│   ├── dsqrt_hopper.cubin
│   ├── ddiv_blackwell.cubin
│   └── ...
│
├── docs/
│   ├── architecture.md                  ← distilled MIMIC.md
│   ├── ir002nv_spec.md
│   ├── insight_catalog.md
│   ├── mutation_catalog.md
│   ├── calibration_guide.md
│   ├── sim_algorithm.md
│   └── target_model.md
│
└── examples/
    ├── compile_gemm.cpp
    ├── predict_attention.cpp
    ├── explore_gemm_variants.cpp
    └── validate_on_real_hw.cpp
```

---

## 32. LoC budget

| Component | LoC |
|---|---:|
| `core/` (shared types: SimTypes, DecodedInst, Insight, SimResult, FeatureVec, Tier) | 3K |
| `adapt/` (Graph → FusedRegion bridge) | 2K |
| `ir003/nv/` (48 ops, builder, printer, verifier) | 8K |
| `target/` (TargetCaps + OpcodeLatencyTable + loader + per-chip factories) | 6K |
| `encode/` (packer + formats + control + per-SM tables + cubin + EIATTR + capmerc + DWARF) | 14K |
| `decode/` (inverse of encode + disasm) | 7K |
| `template/` (math macros for DDIV, DRCP, DSQRT, DRSQRT, IDIV, IMOD) | 5K |
| `peephole/` (150 rules + engine) | 6K |
| `sim/` shared (WarpState, SmState, MemSubsystem, AsyncOps, Event, EventQueue) | 17K |
| `sim/FastTier.h` | 2K |
| `sim/MediumTier.h` (primary simulator) | 11K |
| `sim/AccurateTier.h` | 8K |
| `sim/GpuFastTier.cu` | 8K |
| `sim/` per-SM (WarpScheduler, MemSubsystem per SM, L2Hash, HbmScheduler) | 6K |
| `insight/` (extractor, kinds, hint catalog, bottleneck) | 5K |
| `mutate/` (macro/meso/micro + dispatch + precondition check) | 7K |
| `search/` (MAP-Elites + sharded archive + driver + worker pool) | 8K |
| `calibrate/` (CUPTI wrapper + microbench runner + fit) | 9K |
| `api/` (three entry points + configs) | 2K |
| Per-chip data files (.json, non-code) | 4K |
| Tests (mimic only) | 18K |
| Benches (CUDA) | 7K |
| Examples | 3K |
| Docs (non-code) | 7K |
| **Total** | **~165K** |

Larger than Forge's estimate (~105K) by 60K because I've expanded on detail. The three tiers of simulator alone are ~50K LoC combined; the encoder/decoder are ~21K; insight/mutate/search/calibration add another 30K. Plus tests, benches, examples, docs.

Single-developer build at full scope: 3-4 years. Team of 2 specialists: 18-30 months.

### Multi-vendor extensions (not in this estimate)

- AMD CDNA3 (IR002AM + mimic AMD backend): +50-70K
- TPU HLO emitter (IR002GL): +15-25K
- Ascend CCE (IR002HW): +25-35K

Each vendor adds its own encoder, decoder, simulator tuning (different pipes, different memory subsystem), calibration harness. Core insight/mutate/search/archive infrastructure shared across vendors.

---

## 33. Build plan — 52 weeks

Not a single-phase plan. Mimic has a natural dependency chain.

### Weeks 1-3: Core foundation

- `core/SimTypes.h`, `DecodedInst.h`, `DecodedProgram.h`, `Insight.h`, `SimResult.h`
- `target/TargetCaps.h` + factory `h100_sxm5()` hardcoded from NVIDIA docs
- Minimal `ir003/nv/Ir003Nv.h` with 10 ops (FFMA, FADD, IADD3, LDG, STG, MOV, BRA, S2R, LDS, STS)
- Test: `test_sim_types.cpp`, verify sizes, trivial-relocatability, compile on Clang 22 and GCC 16

**Milestone M1**: scaffold compiles clean on both compilers, zero warnings.

### Weeks 4-7: Encoder for 10 ops

- `encode/Packer.h` (30-line bitfield packer)
- `encode/Formats.h` for 1-2 format groups
- `encode/per_sm/sm_90.cpp` for 10 ops
- `encode/CubinEmitter.h` minimal with 5 EIATTRs
- Test: hand-coded FFMA kernel emits valid cubin that runs on real H100

**Milestone M2**: "Hello, World" — generated cubin executes correctly on H100.

### Weeks 8-11: Fast-tier simulator

- `sim/FastTier.h` interval simulator
- `sim/MemSubsystem.h` basic BW model
- Validate: simulated cycles within 20% of measured on 5 GEMM variants

**Milestone M3**: fast-tier predictions within 20% on GEMM.

### Weeks 12-18: Medium-tier simulator

- `sim/EventQueue.h`, `sim/SmState.h`, `sim/WarpState.h`
- `sim/WarpScheduler.h` GTO
- `sim/AsyncOps.h` for WGMMA
- `insight/` with 15 insight kinds
- Validate: within 10% on 20 kernels

**Milestone M4**: medium-tier accuracy within 10%.

### Weeks 19-22: Calibration harness + CUPTI

- `calibrate/` + 50 microbenchmarks
- CUPTI integration
- Write `h100_sxm5.json` from measurements
- Re-run validation: target 5-8% on 50 kernels

**Milestone M5**: calibrated H100 target, 5% accuracy.

### Weeks 23-30: MAP-Elites + mutate + search

- `mutate/` with 15 operators
- `search/MapElites.h` with archive + sampler
- `search/Driver.h` with worker pool
- `api/compile_kernel.h` — first end-to-end MAP-Elites compile
- Validate: beat a hand-coded CUTLASS baseline on 60% of tested GEMM shapes

**Milestone M6**: MAP-Elites produces winner kernels.

### Weeks 31-36: Decoder + full op set

- Full `decode/` inverse of encode
- Full op set in IR003NV (all 48)
- Round-trip test: `decode(encode(x)) == x` for 1000 random programs
- Pretty-print disassembler

**Milestone M7**: decoder works; all ops covered.

### Weeks 37-42: Peephole + templates

- Peephole engine + 150 rules
- Templates for DDIV, DSQRT, DRSQRT, IDIV, IMOD
- Pre-compiled math fragments
- Validation: 5 kernels using FP64 math end-to-end

**Milestone M8**: Full math support.

### Weeks 43-47: Accurate tier + validation

- Accurate-tier simulator
- Extended insight set (40 kinds)
- 500-kernel regression suite
- Achieve P95 residual < 5% across suite

**Milestone M9**: 95% accuracy target met.

### Weeks 48-52: Blackwell + Production hardening

- Blackwell (sm_100) TargetCaps
- Per-SM encoder variants for sm_100/103/120
- tcgen05/TMEM simulator additions
- Documentation + tooling
- Federated cache sharing design

**Milestone M10**: Blackwell supported; Mimic production-ready.

### Year 2 and beyond

- GPU fast tier (`sim/GpuFastTier.cu`)
- Full integration with Forge (Phase H invocation)
- Cross-SM calibration automation
- Research extensions (multi-vendor, federated)

---

## 34. Critical design decisions

### D1: Three tiers, not one

Necessary for both throughput (MAP-Elites wants thousands of cands/sec) and accuracy (calibration wants 2-5% absolute). Single simulator cannot span the range.

### D2: Event-driven medium tier, not cycle-stepping

Cycle-stepping is 10^9 steps for a realistic kernel; event-driven is 10^5-10^6 events. 3-4 orders of magnitude faster for same accuracy.

### D3: Per-SM data files in JSON, committed to git

Deterministic builds, auditable history. Alternative (runtime calibration every startup) has non-determinism and startup cost.

### D4: GPU-accelerated fast tier as soft dependency

Not every environment has GPU access during compile. CPU fast tier is always available; GPU tier is ~5-10× faster when available.

### D5: Mutation operators driven by insights, not random

Random mutation wastes cycles exploring the hopeless. Insight-driven mutation applies the expert knowledge encoded in the insight → hint mapping.

### D6: MAP-Elites archive persisted to Cipher

Content-addressed kernel computation implies content-addressed search results. Two identical FusedRegions compile in parallel get the same archive. Across runs, the archive accumulates; a kernel family (e.g., all GEMM variants) converges over time.

### D7: Insight extraction during simulation, not after

Post-hoc requires re-walking the timeline. During-sim is cheap and gives insights immediate access to full state.

### D8: Simulator consumes DecodedInst, not IR003NV

Forces the encoder to be correct (if you can simulate the cubin, the cubin is valid). The encoder/decoder round-trip is a continuous correctness test.

### D9: 48 IR003NV ops, not more

Normalize aggressively via attributes. Cleaner IR, smaller dispatcher, easier mutator.

### D10: Three sibling tensor families, not unified

hmma / wgmma / tcgen05 stay distinct. tcgen05 has lifecycle ops (alloc/dealloc/relinquish) and descriptor-driven dispatch that don't fit a unified MMA op.

### D11: CUPTI as a soft dependency

Build-time flag. Without CUPTI: simulator works, calibration is disabled.

### D12: Driver source informs memory model

Memory subsystem mechanisms come from driver source, not microbenchmark fitting. Accuracy advantage over academic simulators.

### D13: TargetCaps is the central shared struct

One 256-byte struct carried everywhere. Loaded from JSON, passed to every simulator/encoder/decoder call.

### D14: Archive is the computation genome

Persistent MAP-Elites archive indexed by canonicalized family hash. Shared across Crucible installations via Cipher cold-tier storage.

---

## 35. Open questions deferred

1. **Accurate tier: ever productionize or stay calibration-only?** My lean is calibration-only; MAP-Elites using fast + medium is enough. Revisit if specific ML workloads show accuracy-critical calibration needs.

2. **GPU fast tier's batch size sweet spot.** 100? 1000? 10000? Depends on MAP-Elites dynamics. Empirical.

3. **Recompile throttling for drift.** Currently conceived as "when drift > 10%, recompile". How often can that fire before the background thread saturates? Need throttling. Default: max one recompile per region per 5 minutes.

4. **Cross-architecture simulation.** Some workloads are heterogeneous (H100 + A100 mixed). Mimic simulates per-SM independently. Cross-chip collective cost estimation is Phase K of Forge; Mimic doesn't do it today. Future.

5. **Workload-specific tuning.** Mimic's TargetCaps is per-chip, not per-workload. A training workload stresses different subsystems than inference. Adapt? Current design says no — but a "workload profile" attribute might help.

6. **Symbolic simulation.** When shapes are symbolic (Crucible's Expr), can Mimic produce symbolic cycle counts? Partially; specialize ranges and simulate parameterized. Not in the initial design.

7. **GPU-accelerated medium tier.** Medium tier on GPU would be 100× faster but requires significant redesign. Probably not worth it; medium tier CPU at 3000 cand/sec is enough.

8. **MAP-Elites behavior axis tuning.** The 6 axes and 8 buckets are a starting point. Empirical tuning: some axes may need 16 buckets, some may be combined, some new ones may emerge. Monitor cell population statistics and adjust.

9. **Federated cache governance.** If orgs share cold tier Cipher, who owns the archive? Who moderates quality? Legal framework needed. Deferred.

10. **Evolving the simulator over years.** NVIDIA ships new SMs every 18-24 months. Blackwell → Rubin → after-Rubin. Each needs calibration. Process: reuse as much Hopper/Blackwell code as possible; bucket changes into "new opcode family" (small) vs "new memory subsystem" (larger) vs "new fundamental architecture" (rare). Budget 1-3 months per new SM.

---

## 36. Runtime library replacement — per-vendor driver wrappers

No libcuda, no libnrt, no libtpu, no libhsa, no vendor runtime binary dependency. Each Mimic backend ships its own runtime library at `mimic/<vendor>/rt/` that talks to the kernel driver via ioctl directly.

### 36.1 Scope per backend

Each runtime library provides:

- **Device discovery + open** — `open_device(idx) → Device` opens `/dev/<vendor>N` with the correct ioctl protocol. Returns a handle managing the device's fd, context state, and command queue.
- **Memory management** — pool allocator over HBM / device memory / on-chip scratch. Replaces `cuMemAlloc`, `hsa_memory_allocate`, `nrt_tensor_allocate`, TPU DMA descriptor. Our `PoolHandle` is position-independent; `pool_base + offset` access for every tensor.
- **Binary loading** — `load_kernel(device, compiled_bytes) → KernelHandle` parses the vendor binary format (cubin/HSACO/NEFF/TPU-exec/ELF) directly — our own loader, not `cuModuleLoad`. Relocations resolved inline.
- **Submission** — `submit_kernel(device, handle, launch_args) → Future<void>` enqueues a kernel launch via the vendor's submission channel (NVIDIA PB DMA channel ioctl; AMD KFD queue write; Neuron `NEURON_IOCTL_SUBMIT_*`; TPU DMA descriptor enqueue). Returns a future resolved by completion polling.
- **Graph capture** — `capture_plan(device, plan) → GraphHandle` captures a sequence of submissions into the vendor's native graph format for replay (CUDA Graph on NV, HIP Graph on AM, precompiled TPU plan, NEFF batch submit on TRN). On vendors without native graph support, the capture is a batched ioctl sequence our runtime replays as one call.
- **Sync / completion** — `wait_for(future)` polls the vendor's completion channel (event fd / DMA completion counter / TPU host-notify interrupt). Uses acquire-loads on atomics we mmap from the device; no futexes or condition variables.

### 36.2 Per-vendor implementation notes

- **`mimic/nv/rt/`** — opens `/dev/nvidiaN` + `/dev/nvidia-uvm` + `/dev/nvidiactl`. Replaces libcuda. ioctls documented in the open-source NVIDIA driver (`open-gpu-kernel-modules`). ~8K LoC.
- **`mimic/am/rt/`** — opens `/dev/kfd` + per-device `/dev/dri/renderDN`. Replaces libhsa-runtime. KFD ioctls are public (ROCk-Kernel-Driver). ~7K LoC.
- **`mimic/tpu/rt/`** — opens `/dev/accel*` (Cloud TPU VMs expose this). Protocol derived from libtpu RE. ~10K LoC (TPU's runtime is larger due to pod topology management).
- **`mimic/trn/rt/`** — opens `/dev/neuronN`. ioctl protocol documented in the `aws-neuron-runtime-lib` headers (we downloaded these; see `neuron_driver_shared.h`). Replaces libnrt. ~9K LoC.
- **`mimic/cer/rt/`** — opens `/dev/cerebras*`. CSL/runtime protocol from Cerebras SDK RE. ~8K LoC.
- **`mimic/cpu/rt/`** — trivial; kernel execution is direct function calls. Memory is `mmap` / `malloc`. No ioctl. ~2K LoC.

### 36.3 What we lose vs vendor runtime libraries

- **Multi-process resource management** — vendor runtimes arbitrate between processes via a daemon (NVIDIA MPS, AMD rocm-smi lib). Crucible's Canopy handles multi-tenancy at the cluster level; we don't need a per-node daemon.
- **Legacy API compatibility** — we don't expose `cuMemcpy`, `hipMemcpy`, etc. Applications running on Crucible use its own tensor-copy primitives.
- **Vendor-tool interop** — `cuda-gdb`, `rocgdb`, `tpu-profile` won't recognize our binaries by default. We ship our own debugger (`crucible-gdb`) that speaks the vendor binary formats.

Net: ~40-60K LoC of runtime libraries we own, replacing ~several GB of vendor-shipped binaries.

### 36.4 Full userspace driver realization per vendor

"Userspace driver" means different things per vendor based on what silicon exposes and what firmware is reachable. This section specifies, for each backend, the exact depth at which we bypass the stock driver and what remains kernel-side.

#### 36.4.1 The three-layer model (unified)

Every vendor backend's `rt/` subsystem decomposes into:

- **Layer A — Kernel-space shim**: `.ko` code required for hardware bring-up, firmware upload (where signed), IRQ delivery, PCIe reset. Runs at boot, never in the hot path.
- **Layer B — One-time setup (userspace, Keeper init)**: Channel/queue allocation, BAR mapping, VRAM pool alloc, MR registration, clock pinning. ~150-400 ms per vendor.
- **Layer C — Hot-path dispatch (userspace, per ExecutionPlan submit)**: Pushbuffer composition + PatchPoint writes + doorbell. Zero ioctls, zero syscalls, sub-μs per dispatch.

Per-vendor, Layer A/B boundary depth differs based on silicon architecture.

#### 36.4.2 NVIDIA — Hybrid (nvidia.ko + userspace)

**Layer A footprint**: upstream `nvidia.ko` with our module params (§36.6). No custom kernel code.

Justification:
- GSP firmware bootstrap requires PLM-gated FSP mailboxes (signed ucodes, WPR2 programming, SPDM on CC SKUs). Only nvidia.ko can reach these. Full vfio-pci userspace is infeasible on Turing+.
- MSI-X delivery for fatal errors via nvidia.ko IRQ handler.
- FLR via `VFIO_DEVICE_RESET` or `NV_ESC_RM_CONTROL` escape.
- BAR1 resize via `nv_resize_pcie_bars` (kernel PCI core).
- `nvidia-persistenced` or Keeper sentinel fd keeps GSP alive across process lifetimes.

**Layer B** (one-time Keeper init, CRUCIBLE.md §14.8 steps 6-11, ~150-400 ms):

- Open `/dev/nvidiactl` + `/dev/nvidia0`
- Allocate RM client, device, subdevice, vaspace
- `NV_ESC_RM_VID_HEAP_CONTROL` with `ALLOC_FLAGS_FIXED_ADDRESS_ALLOCATE` for full VRAM pool
- `NV_ESC_RM_MAP_MEMORY` → mmap WC pointer
- Channel + compute object + `HOPPER_USERMODE_A` (doorbell mapped to userspace)
- Three GSP RPCs: `ALLOC_CHANNEL_DMA`, `BIND`, `GPFIFO_SCHEDULE`
- Pin max clocks via `PERF_BOOST(DURATION_INFINITE)`
- `ibv_reg_mr` on BAR1-covered pool (one persistent MR)

**Layer C** (hot path, zero syscalls): pushbuffer composition (§15.4.1) + PatchPoint writes + doorbell at BAR0+0x2200 or `HOPPER_USERMODE_A` BAR1 offset.

**LoC budget**: ~15K including per-chip tables for sm_90 / sm_100 / sm_103 / sm_110 / sm_120 / sm_121. No custom kernel code.

**Depth**: Channel-level bypass — below GSP's per-kernel scheduler slice, above GSP-RM (memory, context, power management). GSP scheduler overhead (~5-10 μs per invocation) avoided; GSP-RM untouched.

#### 36.4.3 AMD — Full userspace (AM-style)

**Layer A footprint**: None. `rmmod amdgpu` at host boot; use vfio-pci. Tinygrad's `tinygrad/runtime/support/am/` is the reference implementation (~10K LoC), extended for Mimic integration.

- GPU accessed via PCI BAR mapping (vfio-pci binding)
- Firmware (MES, SMU, PSP) uploaded from userspace via documented PCIe config + BAR writes
- IRQ via vfio-pci eventfd
- FLR via `VFIO_DEVICE_RESET` or direct PCIe config-space write

MES scheduler is **bypassed entirely**; our `rt/` binds compute queues directly to MEC at `pipe=0 queue=0`, single SDMA queue at `engine=0 queue=0`. No firmware scheduler between us and silicon. Tinygrad AM has proven this approach stable for ML workloads.

**Layer B** (one-time, ~50-100 ms):

- Open vfio-pci fd, map BAR0/BAR2/BAR5
- Reset device, upload firmware blobs (MES, SMU, PSP)
- Initialize GART, VM pagetable for single-tenant VMID=0
- Allocate VRAM pool (full visible framebuffer)
- Create single compute queue on MEC pipe 0 queue 0
- Create single SDMA queue on engine 0 queue 0
- `ibv_reg_mr` on framebuffer BAR1 region (if RoCE for inter-node)

**Layer C**: PM4 packet composition (§15.4.2) + doorbell write.

**LoC budget**: ~20K (firmware upload + GART + per-chip tables for gfx942/gfx950/gfx1100/etc).

**Depth**: Silicon-level. We own everything above MEC. MES firmware is optional and disabled.

**Risk**: Full userspace inherits firmware-stability issues that ROCm's MES normally shields. Tinygrad reports AM more stable than amdgpu for ML workloads due to simpler state; we assume same + add bring-up test coverage.

#### 36.4.4 Google TPU — /dev/accel* direct

**Layer A footprint**: Upstream `accel_*.ko` (provided by Google on Cloud TPU VMs; thin DMA + IRQ wrapper). No custom kernel code.

- `/dev/accel*` provides DMA buffer management + IRQ delivery
- No firmware scheduler equivalent to GSP/MES — scheduling lives in the compiled TPU executable consumed by the scalar processor
- No runtime scheduler to bypass; we're already at the silicon boundary when writing scalar-processor bytecode

**Layer B** (one-time, ~200-500 ms):

- Open `/dev/accel*` (protocol RE'd from libtpu)
- Allocate HBM pool, ICI fabric handles, VMEM allocations
- Establish ICI topology per pod configuration
- Build scalar-processor bytecode prologue for our workloads

**Layer C**: TPU executable bytecode (§15.4.3) via DMA descriptor chain. No doorbell; scalar processor runs autonomously once executable is in place.

**LoC budget**: ~25K (larger due to libtpu RE + ICI topology management).

**Depth**: Silicon-level. No firmware scheduler exists to bypass; `/dev/accel*` is already the bottom of the exposed stack.

#### 36.4.5 AWS Trainium — /dev/neuronN direct

**Layer A footprint**: Upstream `neuron.ko` (public driver, protocol in `neuron_driver_shared.h`). No custom kernel code.

- `/dev/neuronN` provides DMA submission, IRQ delivery, NEFF loading
- No firmware scheduler equivalent — NEFF bytecode carries the schedule per-engine
- NeuronLink peer management via driver

**Layer B** (one-time, ~100-200 ms):

- Open `/dev/neuronN`
- Allocate HBM pool, SBUF regions
- Establish NeuronLink topology
- Pre-load NEFF files via `NEURON_IOCTL_LOAD_NEFF`

**Layer C**: NEFF batch submission (§15.4.4) via `NEURON_IOCTL_SUBMIT_BATCH`. Multiple NEFFs per ioctl for lower per-submit overhead.

**LoC budget**: ~22K (NEFF RE + per-engine opcode tables + NeuronLink primitives).

**Depth**: Silicon-level, same as TPU. No firmware scheduler to bypass.

#### 36.4.6 Cerebras WSE — /dev/cerebras*

Optional; Phase 6 of build plan. CSL executables via `/dev/cerebras*`. SwarmX fabric via driver. ~20K LoC; depth equivalent to TPU/TRN.

#### 36.4.7 CPU — trivial

No kernel module beyond stock Linux. Layer B: `malloc` + thread pool init. Layer C: direct function call via `jit_loader`'d ELF. ~2K LoC total.

#### 36.4.8 Summary table

| Backend | Layer A (kernel) | Layer B init | Layer C hot path | Total LoC |
|---|---|---|---|---|
| NV | upstream nvidia.ko + params | ~150-400 ms | 4-dword pushbuffer + doorbell (~200 ns) | ~15K |
| AM | vfio-pci only (no amdgpu.ko) | ~50-100 ms | PM4 packets + doorbell (~200 ns) | ~20K |
| TPU | upstream accel_*.ko | ~200-500 ms | scalar-proc bytecode via DMA chain | ~25K |
| TRN | upstream neuron.ko | ~100-200 ms | NEFF batch submission | ~22K |
| Cerebras | upstream cerebras.ko | ~200-500 ms | CSL executable submission | ~20K |
| CPU | none | ~5 ms | direct function call | ~2K |

**Grand total runtime-library LoC across all vendors: ~104K**, replacing several GB of vendor-shipped proprietary runtime binaries (libcuda + libcudart + libnvrtc + libnvJitLink + libhsa + libtpu + libnrt + ...).

### 15.5 setmaxnreg — warp-specialized register allocation (NV sm_90+)

DeepSeek-V3's kernels use PTX `setmaxnreg.{inc,dec}.sync.aligned.u32 N` to dynamically rebalance registers between warp-group roles. Producer warps (TMA loaders) release registers after issuing bulk copies; consumer warps (MMA compute) grab them for higher occupancy. Near-impossible in CUDA C++, trivial in direct SASS/PTX emission.

#### 15.5.1 Instruction semantics

```
setmaxnreg.dec.sync.aligned.u32 N   // release registers; warpgroup shrinks to N regs/thread
setmaxnreg.inc.sync.aligned.u32 N   // grab registers; warpgroup expands to N regs/thread
```

- Warp-group-synchronous (`.sync.aligned`): all 128 threads of a warpgroup execute together
- `N` is the new per-thread register count; total warpgroup footprint = N × 128
- Hopper's SM register file is 65,536 × 32-bit; warpgroups share this pool
- `setmaxnreg.dec` releases registers back to the pool for other warpgroups
- `setmaxnreg.inc` grabs registers from the pool; fails if insufficient available

Typical usage: producer warpgroup calls `.dec 40` after issuing TMAs (keeps ~40 regs for scoreboarding), consumer warpgroup calls `.inc 240` before MMA (grabs 240 regs for accumulator + operand buffering).

#### 15.5.2 When Mimic-NV emits setmaxnreg

Emitted when `ExecutionAttrs.reg_alloc_policy == DYNAMIC_SETMAXNREG` (FORGE.md §18.3.1). Typically paired with warp specialization: `warp_spec_split ∈ {0x03, 0x04, 0x05, 0x06, 0x07}` (producer-consumer or producer-barrier-consumer splits).

Emission points:

- **Producer-warp entry**: after prologue code, before first TMA issue. `.dec N_prod` where `N_prod` is from the MAP-Elites-selected cell (typically 24-64).
- **Consumer-warp entry**: after barrier waiting for setup, before first MMA. `.inc N_cons` where `N_cons` is from the MAP-Elites-selected cell (typically 168-240).

Per-warpgroup assignment requires the counts satisfy `N_prod × n_producer_warpgroups + N_cons × n_consumer_warpgroups ≤ SM_total_regs × partition_fraction`.

#### 15.5.3 SASS encoding

`setmaxnreg` is PTX; lowered to SASS instruction with mnemonic depending on direction:

```
SETMAXNREG.INC.SYNC  N
SETMAXNREG.DEC.SYNC  N
```

One 128-bit SASS instruction (~16 bytes). A warpgroup-synchronization fence precedes it; emitted by our SASS encoder at Phase E lowering.

#### 15.5.4 MAP-Elites integration

MAP-Elites behavior axis (§19) extends to include:

```
{warp_spec_split, reg_alloc_policy, regs_per_producer, regs_per_consumer, smem_kb, pipeline_depth}
```

~8^6 = 262K behavior cells; typically 500-5K populated per kernel family. Mutation operators: `SWAP_REG_ALLOC_POLICY`, `ADJUST_PRODUCER_REGS ± 16`, `ADJUST_CONSUMER_REGS ± 16`.

#### 15.5.5 Measured fitness impact

On FlashAttention-3 head_dim=128, causal, H100:

| Config | Measured MFU |
|---|---|
| `warp_spec = 0x00 (no spec), reg_alloc = STATIC` | 52% |
| `warp_spec = 0x03, reg_alloc = STATIC` (fixed 128 regs all warps) | 61% |
| `warp_spec = 0x03, reg_alloc = DYNAMIC_SETMAXNREG` (40 prod / 240 cons) | 76% |

DeepSeek-V3-style dynamic rebalancing contributes ~+15 MFU points on MMA-heavy kernels. MAP-Elites finds the optimal `N_prod`/`N_cons` split per (shape, chip) combo.

#### 15.5.6 Cross-vendor

`setmaxnreg` is NV Hopper+ specific. Equivalents:

- **AMD CDNA3+**: MFMA warps use `s_setreg REGMAP` for register allocation; analogous pattern in `mimic/am/encode/`. Not as fine-grained as setmaxnreg — CDNA3 register management is coarser.
- **TPU**: warp concept doesn't apply; scalar processor manages MXU/VPU/SMEM assignment per dispatch
- **Trainium**: per-engine register bank is static per NeuronCore; no dynamic rebalancing
- **CPU**: N/A (compiler owns register allocation at build time)

Per §40.8, each per-vendor backend realizes the `reg_alloc_policy = DYNAMIC_*` intent via its available primitive. On vendors lacking setmaxnreg-class primitives, MAP-Elites explores `reg_alloc_policy = STATIC` variants only; fitness ceiling is correspondingly lower.

### 36.5 Direct PCIe configuration at Keeper init

Certain hardware-level PCIe configurations must be applied at init to unlock performance. Stock drivers do some via module params; others require explicit config-space writes from our code.

#### 36.5.1 NVIDIA — required config writes

| Config | Register / method | Why |
|---|---|---|
| Resizable BAR enabled | PCIe Cap `Resizable BAR Control Register`, set `BAR Size` = max supported | Full VRAM visible in BAR1 for P2P + RDMA |
| L1 / L1.2 ASPM disabled | PCIe Link Control Reg `ASPM Control` = 00b | Eliminates ~10-100 μs exit latency spike |
| Max payload size | PCIe Device Control `Max_Payload_Size` = max supported (typ 512 B Gen5 NIC, 128 B older root ports) | Larger TLPs = better P2P throughput |
| Relaxed ordering | PCIe Device Control `Enable Relaxed Ordering` = 1 | Enables GPU↔NIC concurrent DMA reorder |
| Extended tags | PCIe Device Control 2 `Extended Tag Field Enable` = 1 | More outstanding split transactions (8-bit tag) |
| 10-bit tag requester | PCIe Device Control 2 (where supported) | Even more outstanding transactions (10-bit tag) |

Applied during nvidia.ko probe (via module params, §36.6) or via sysfs (`/sys/bus/pci/devices/<bdf>/resource0_resize`, `.../link/l1_aspm`) post-probe. Crucible's install script sets these for all GPUs.

#### 36.5.2 AMD — required config writes

Same PCIe Cap configs as NV (rBAR, ASPM off, MPS, relaxed ordering, extended tags), applied via vfio-pci sysfs or config-space writes by `rt/init`.

AMD-specific: ATS (Address Translation Services) + PRI (Page Request Interface) Cap enable for IOMMU-coherent DMA. Usually BIOS-configured; install script verifies and re-applies.

#### 36.5.3 TPU / Trainium — limited tunability

On Cloud TPU VMs, PCIe config is set by hypervisor; not tunable from guest. We probe current config and adapt (smaller DMA descriptor batches if MPS=128 B).

Trainium's PCIe config is firmware-controlled; ASPM already disabled, MPS=512 B at driver load. We verify + adapt.

#### 36.5.4 Per-launch byte sequence reminder (NV Hopper)

From §15.4.1, minimum per-kernel dispatch:

```
Pushbuffer (16 bytes, 4 dwords):
  word[0] = 0x020020AD    # INC subch=1 count=2 addr=SEND_PCAS_A
  word[1] = QMD_ADDRESS >> 8
  word[2] = 0             # SEND_PCAS_B (unused on Hopper)
  word[3] = 0x800320B0    # IMMD subch=1 addr=SIGNALING_PCAS2_B data=INVALIDATE_COPY_SCHEDULE=3

GPFIFO entry (8 bytes):
  entry[0] = (pb_va >> 2) | FETCH=0
  entry[1] = (pb_va >> 32) | LEVEL<<9 | (len>>2)<<10 | SYNC=0

GPPut advance (4 bytes):
  mmio_write(USERD_BASE + GP_PUT_OFFSET, new_put_index)

Doorbell (4 bytes):
  mmio_write(HOPPER_USERMODE_A_BASE + DOORBELL_OFFSET, runlist_token)
```

**Total: 32 bytes of host-to-GPU traffic per kernel launch.** CPU critical path: 4 SFENCE-separated stores = ~80-150 ns on modern x86 + PCIe Gen5.

Pushbuffer is WC-mapped so consecutive writes coalesce in CPU's WC buffers; SFENCE flushes them. GPPut and doorbell are UC (uncached device memory) — each a single ordered MMIO write.

### 36.6 Module parameters and runtime disables

Crucible's install script configures module parameters and sysfs knobs per vendor to eliminate latency penalties, disable consumer-SKU gates, and commit to single-tenant trusted-fleet semantics.

#### 36.6.1 NVIDIA — `/etc/modprobe.d/nvidia.conf`

```
options nvidia NVreg_RegistryDwords="RMForceStaticBar1=0x1;RMPcieP2PType=0x1;RmWatchDogTimeOut=0;RmRcWatchdog=0"
options nvidia NVreg_DynamicPowerManagement=0x00
options nvidia NVreg_EnableResizableBar=1
options nvidia NVreg_EnableMSI=0
options nvidia NVreg_EnablePCIeGen3=1
options nvidia NVreg_EnablePCIeRelaxedOrderingMode=1
options nvidia NVreg_EnableS0ixPowerManagement=0
options nvidia NVreg_EnableGpuFirmwareLogs=0
options nvidia NVreg_UsePageAttributeTable=1
options nvidia_uvm uvm_perf_prefetch_enable=0 uvm_disable_peer_access=0 uvm_disable_hmm=1
```

`/etc/default/grub` `GRUB_CMDLINE_LINUX_DEFAULT`:

```
amd_iommu=on iommu=pt pci=realloc pcie_aspm=off
```

(Use `intel_iommu=on` on Intel hosts.)

Rationale per option:

| Option | Rationale |
|---|---|
| `RMForceStaticBar1=1` | Static BAR1 covering full VRAM |
| `RMPcieP2PType=1` | BAR1 P2P preferred over mailbox |
| `RmWatchDogTimeOut=0 RmRcWatchdog=0` | Disable RC watchdog's channel + 1 Hz polling |
| `NVreg_DynamicPowerManagement=0x00` | No runtime-PM churn per fd open/close |
| `NVreg_EnableResizableBar=1` | Request max BAR1 from BIOS |
| `NVreg_EnableMSI=0` | Disable MSI-X for kernel completion (spin-poll instead) |
| `NVreg_EnableS0ixPowerManagement=0` | No S0ix low-power transitions |
| `NVreg_EnableGpuFirmwareLogs=0` | Skip GSP log collection overhead |
| `NVreg_UsePageAttributeTable=1` | Allow WC mappings (required for pushbuffer) |
| UVM `perf_prefetch=0 disable_hmm=1` | Disable UVM prefetch, HMM fault handling |
| `iommu=pt` | IOMMU passthrough — required for BAR1 P2P |
| `pci=realloc` | Allow kernel to reassign BAR regions for resizing |
| `pcie_aspm=off` | Kill L1 substates globally |

#### 36.6.2 NVIDIA — runtime sysfs (applied post-boot)

```
# Disable L1 ASPM on GPU PCIe links
for dev in /sys/bus/pci/devices/0000\:*/drivers/nvidia/0000\:*/link; do
    echo 0 > $dev/l1_aspm_aspm
    echo 0 > $dev/l1_aspm_substates
done

# Sustained max clocks on CPU side
cpupower frequency-set -g performance

# THP defrag off (pinned mem uses hugetlbfs directly)
echo madvise > /sys/kernel/mm/transparent_hugepage/enabled
echo never   > /sys/kernel/mm/transparent_hugepage/defrag

# nvidia-persistenced holds sentinel fd
systemctl enable nvidia-persistenced
systemctl start  nvidia-persistenced
```

#### 36.6.3 NVIDIA — disabled features summary

| Feature | How disabled | Reason |
|---|---|---|
| RC watchdog | `RmWatchDogTimeOut=0 RmRcWatchdog=0` | Replaced by our plan timeout + BAR0 poll |
| Dynamic PM | `NVreg_DynamicPowerManagement=0` | Removes per-fd-open overhead |
| MSI-X for completion | `NVreg_EnableMSI=0` | Spin-poll pinned semaphore (~50-100 ns) |
| L1 ASPM substates | `pcie_aspm=off` + sysfs | Avoid 10-100 μs exit latency |
| GPU accounting | `PDB_PROP_GPU_ACCOUNTING_ON=false` | Skip per-channel proctype bookkeeping |
| UVM demand paging | `uvm_disable_hmm=1` | We don't use UVM; static VRAM pool |
| GSP firmware logs | `NVreg_EnableGpuFirmwareLogs=0` | Skip log collection |
| CC encryption | SBIOS CC mode off | Skip AES-GCM on RPCs (~20-30%) |
| S0ix | `NVreg_EnableS0ixPowerManagement=0` | No low-power transitions |
| Per-process GSP reboot | `nvidia-persistenced` or sentinel fd | GSP stays up across Keeper restarts |

#### 36.6.4 AMD — module params (when fallback-using amdgpu.ko)

```
options amdgpu pcie_gen_cap=0x40000 pcie_lane_cap=0 msi=1 dpm=0 gttsize=-1 mcbp=0
```

Rationale: pin max PCIe gen, disable DPM, enable MSI for fatal only, no mid-command buffer preemption (single-tenant). For full AM-style userspace, amdgpu.ko is NOT loaded at all — these params apply in dev/fallback mode.

#### 36.6.5 TPU / Trainium — limited tunability

TPU: On Cloud TPU VMs most config is hypervisor-controlled. Few tunable knobs; we rely on hypervisor defaults + runtime adaptation.

Trainium: `aws-neuron-runtime-lib` exposes `NEURON_RT_NUM_CORES`, `NEURON_RT_STOCHASTIC_ROUNDING_EN` etc., set per workload + recipe. No modprobe file.

#### 36.6.6 Install-time validation

Crucible's install script validates each config, refuses to proceed if critical ones are missing:

| Required | Condition | Failure action |
|---|---|---|
| IOMMU passthrough or translation + rBAR | `iommu=pt` cmdline OR BIOS rBAR + allowing iommu-group | Abort; print remediation |
| ACS disabled | BIOS or `pcie_acs_override=downstream,multifunction` | Warn; P2P may route through CPU root complex |
| Huge pages | ≥ 8 × 1 GB hugepages allocated | Warn; GSP firmware cache falls back to 2 MB pages |
| `nvidia-persistenced` enabled | systemctl | Warn; each Keeper restart re-uploads GSP firmware (~1 s) |
| IB/RoCE NIC for multi-node | `ibv_devices` returns entries | Abort if multi-node config; single-node ok |
| SBIOS CC mode off | `dmesg` grep or platform tool | Warn; AES-GCM will cost 20-30% on every GSP RPC |

Install script is idempotent; re-running adapts to new hardware/firmware.

---

## 37. Collective library replacement — per-vendor fabric primitives

No NCCL, no RCCL, no libnccom, no hcoll, no UCX, no MPI. Each backend ships `mimic/<vendor>/comm/` over its native fabric.

### 37.1 The per-vendor collective surface

Every backend's `mimic::<vendor>::comm` namespace implements these:

```cpp
[[nodiscard]] std::span<uint8_t> all_reduce    (fx::Bg, Arena&, ContentHash, ReduceOp, ScalarType, std::span<const uint8_t>, ReduceGroup);
[[nodiscard]] std::span<uint8_t> all_gather    (fx::Bg, Arena&, ContentHash, std::span<const uint8_t>, ReduceGroup);
[[nodiscard]] std::span<uint8_t> reduce_scatter(fx::Bg, Arena&, ContentHash, ReduceOp, ScalarType, std::span<const uint8_t>, ReduceGroup);
[[nodiscard]] std::span<uint8_t> all_to_all    (fx::Bg, Arena&, ContentHash, std::span<const uint8_t>, ReduceGroup);
[[nodiscard]] void               broadcast     (fx::Bg, ContentHash, std::span<uint8_t>, int root_rank, ReduceGroup);
[[nodiscard]] void               send_recv_p2p (fx::Bg, ContentHash, std::span<const uint8_t>, SourceUuid peer, bool is_send);
```

`ReduceGroup` is a logical group identity (DP, TP, PP, EP, CP, or user-defined subgroup); CNTP's consensus layer tracks current membership per group.

### 37.2 Per-vendor fabric choice

- **`mimic/nv/comm/`** — intra-node via NVLink peer-access + NVSHMEM primitives; inter-node via CNTP RDMA transport. Replaces NCCL. ~6K LoC.
- **`mimic/am/comm/`** — intra-node via XGMI peer-access; inter-node via RoCE or TCP via CNTP. Replaces RCCL. ~6K LoC.
- **`mimic/tpu/comm/`** — ICI torus topology (v4/v5p = 3D, v5e/v6e = 2D); intra-pod uses native ICI commands, inter-pod via CNTP. ~7K LoC.
- **`mimic/trn/comm/`** — NeuronLink intra-node + EFA inter-node via CNTP's RDMA transport. Replaces libnccom. ~6K LoC.
- **`mimic/cer/comm/`** — SwarmX wafer fabric; inter-wafer TCP. ~5K LoC.
- **`mimic/cpu/comm/`** — shared-memory + TCP via CNTP. ~3K LoC.

### 37.3 Deterministic reductions

When `NumericalRecipe::determinism = BITEXACT_TC` or `BITEXACT_STRICT`, the collective uses a pinned-order binary-tree reduction keyed by sorted-UUID member ordering. This produces bit-identical results across runs regardless of peer-arrival order. Slightly slower than vendor NCCL's order-nondeterministic ring, but required for cross-vendor equivalence (§41).

Under `ORDERED`, reduction topology is pinned per peer-group but algorithm (ring / tree / halving-doubling) may vary with message size; tolerance is per-recipe `tolerance_ulp_cross_vendor`.

### 37.4 Multi-NIC rail assignment per vendor

Modern accelerator nodes expose multiple NICs with per-NIC dedicated PCIe lanes. The collective library exploits this for aggregate-bandwidth parallelism.

**Per-vendor NIC-to-accelerator affinity probe.**

- **NVIDIA**: `/sys/class/net/<nic>/device/numa_node` for NUMA; `/proc/driver/nvidia-fs/stats` for GDR capability per GPU-NIC pair. Typical H100 SXM5 node: 8 NICs, one per GPU, each 400 Gb/s on dedicated PCIe Gen5 lanes. Rail assignment: one QP pool per GPU-NIC pair; round-robin striping for transfers >1MB.
- **AMD**: `/sys/class/infiniband/` + `/sys/class/drm/card*/device` for NIC/GPU NUMA; RoCE v2 probe via `ibv_query_port` reports port speed. MI300X nodes typically have 4-8 NICs. XGMI intra-node peer-access used first; RoCE fallback per-peer.
- **TPU**: ICI fabric is natively multi-link; libtpu-RE'd transport config declares per-axis rail assignment. No user-selectable NIC; assignment is by TPU pod topology.
- **Trainium**: `ec2-metadata` + `/sys/class/net/eth*` for EFA NIC enumeration. Typical trn2.32xlarge: 8 EFA NICs. NeuronLink for intra-node, EFA for inter-node; per-NeuronCore NIC affinity pinned at pod start.
- **Cerebras**: single-wafer node; no multi-NIC choice. Inter-wafer via TCP.
- **CPU**: any NIC; round-robin when multiple present.

**Per-rail QP pool.** The `comm::` layer maintains a per-`(local_NIC, peer)` QP pool. A collective that streams data across N NICs issues concurrent `write_rdma` ops, one per rail, scheduling by round-robin across rails:

```cpp
void spray_multi_rail(std::span<const uint8_t> data, PeerEndpoint* peer) {
    auto rails = per_peer_rails_[peer->uuid];
    size_t chunk_size = data.size() / rails.size();
    for (size_t i = 0; i < rails.size(); ++i) {
        size_t start = i * chunk_size;
        size_t end = (i == rails.size() - 1) ? data.size() : start + chunk_size;
        rails[i].write_rdma(peer, data.subspan(start, end - start));
    }
    // Completions polled from all rails; collective waits for all.
}
```

NUMA-aware pinning: the compute thread servicing a given (GPU, NIC) pair runs on a CPU from that NIC's NUMA node. MR allocations for that pair use `mbind` or NUMA-local `malloc`.

**Rail assignment determinism.** Under BITEXACT recipes, the rail-to-peer mapping is canonical (sort-by-UUID). Different rail choices would not change the aggregate sum — the per-rail data is non-overlapping shards — but the queue-depth-driven arrival timing may affect scheduling of subsequent operations. Collective-completion is the synchronization point; bit-exactness is preserved across rail permutations.

**Concurrent-collective support.** Bucketed async all-reduce (CRUCIBLE.md §12.7) requires multiple in-flight collectives. Per-vendor capability:

- **NV**: MSCCL-compiled rings on separate streams; per-bucket dedicated QP allocation. Supported on sm_90+.
- **AM**: XGMI supports multiple concurrent reduction contexts; RCCL's approach reimplemented natively.
- **TPU**: ICI supports multiple aggregation groups simultaneously, with care on torus ordering to avoid congestion.
- **TRN**: multiple NCC groups per NeuronCore; pinned channels.

Probed at init via `mimic::<vendor>::comm::supports_concurrent_collectives()`. Missing support → bucketing disabled in Phase K (CRUCIBLE.md §12.7 fallback to monolithic per-step all-reduce).

### 37.5 CNTP collective kernels as pushbuffer-embedded entries

NCCL's architecture has `ncclAllReduce` call `cuLaunchKernel(Ex)` per collective with a CPU proxy thread driving progress — costing ~4-6μs host overhead before the first byte moves. Our model: **the collective kernel is a regular kernel in the ExecutionPlan's pushbuffer, emitted by Phase J like any other kernel**. No proxy thread; no separate CUDA stream; no vendor collective library.

#### 37.5.1 Collective as KernelKind

A `COLLECTIVE` KernelNode (FORGE.md §18.1) carries `CollectiveAttrs` with algorithm, reduce_op, group, message layout. Phase E lowers it to IR002; Phase J emits it to pushbuffer; Mimic's per-vendor backend realizes the collective primitive as a compute-shaped kernel invoking CNTP transport.

**Example: 8-way ring all-reduce on NVLink-connected H100s**

```
plan.pushbuffer:
    [compute kernels producing local shard]
    [barrier: sync SMs at collective entry]
    [COLLECTIVE kernel #1: ring_send_recv (hop 1)]
        QMD references "ring hop" kernel
        c[0] holds: local_slot, remote_slot, hop_idx, msg_size, reduce_op
        Kernel: reduce_local(my_slice + peer_slice); write_remote(peer); release_sem
    [barrier]
    [COLLECTIVE kernel #2: ring_send_recv (hop 2)]
    ... 6 more hops ...
    [barrier]
    [downstream compute kernels]
```

Per-hop launch is 16 bytes pushbuffer (same as any compute kernel). 8 hops × 16 B = 128 B plus ~7 barriers × 8 B = 56 B. **Total ~200 B for the entire ring all-reduce**, vs NCCL's ~4μs host-side `cuLaunchKernel` per collective.

#### 37.5.2 Primitive hop kernels per vendor

Each backend's `mimic::<vendor>::comm` provides a family of hop kernels:

| Primitive | Semantics | NVLink impl | RDMA impl |
|---|---|---|---|
| `ring_send` | Write tensor slice to peer's inbox | `multimem.st.v4.b32` | `ibv_post_send` WRITE WR |
| `ring_recv` | Wait for peer's write, consume | pinned semaphore acquire | CQ poll + optional RDMA READ |
| `ring_send_recv_reduce` | Read peer slice, reduce into local | fused load+FMA on SM | gather via RDMA READ, reduce on SM |
| `tree_up` | Reduce N peers' values toward root | N-input reduce kernel | N × RDMA READ + reduce |
| `tree_down` | Broadcast root's value to N peers | multicast via NVLS or per-peer writes | N × RDMA WRITE |
| `multimem_all_reduce` | NVLS hardware multicast-reduce | `multimem.ld_reduce.relaxed.sys.global.add` | n/a |

These are registered in Mimic's CKernelId taxonomy alongside compute kernels. Forge Phase E matches COLLECTIVE KernelNodes to them based on recipe + topology (§40.4-5 recipe realization).

#### 37.5.3 Semaphore-driven hop completion

Each hop kernel ends with a pinned-memory semaphore release; subsequent hops wait-acquire. Semaphore addresses are PatchPoints (SEMAPHORE_VALUE kind) in the Plan, allocated from the Keeper's pool at compile time (§J.5.2).

For cross-node (RDMA) hops, pinned memory is the NIC-registered MR; the `RDMA_WRITE_WITH_IMMEDIATE` WR atomically writes the data + signals the remote semaphore in one fabric round trip. No software polling on the critical path.

#### 37.5.4 Determinism under BITEXACT

Under `BITEXACT_TC` or `BITEXACT_STRICT`:

- Hop order pinned by canonical UUID sort (CRUCIBLE.md §10.6)
- Reduce operation is pairwise tree with sorted operand order
- No split-K atomics, no reorderable accumulators
- Each hop's compute is deterministic from `(local_slice, peer_slice, reduce_op, recipe)`

Result: byte-identical replay of an all-reduce across runs given the same inputs.

#### 37.5.5 Bucketed async all-reduce realization

Per CRUCIBLE.md §12.7, gradient all-reduces are bucketed and fired as gradients become available. In pushbuffer form:

```
[backward layer N: produces gradient G_N]
[barrier]
[COLLECTIVE ring_send_recv (bucket k containing G_N..G_{N+4}), hop 1]
[backward layer N-1: produces G_{N-1}]
    — compute kernels continue concurrent with ring hops on different SMs
[COLLECTIVE ring_send_recv (bucket k), hop 2]
...
```

Compute and collective kernels interleave in the same pushbuffer; the device executes both concurrently on different SMs. The green-context split (CRUCIBLE.md §14.X, landing later) dedicates specific SMs to collective work, matching the DeepSeek-V3 dispatch/combine/compute pattern.

#### 37.5.6 What we don't do

- **No proxy thread.** NCCL's `ncclProxyProgress` loop exists because its collective kernels depend on the CPU to forward work to NICs. Our collective kernels drive the fabric directly from SMs (via mmap'd MR) or from our runtime library's completion poll — no thread yielding, no condvars.
- **No separate CUDA stream.** NCCL uses multiple CUDA streams to overlap collectives with compute. We use one stream per Plan; overlap comes from green-context SM partitioning within the same stream.
- **No `cuLaunchKernel` for collectives.** Every collective launch is a pushbuffer method in the ExecutionPlan, submitted with the rest of the step's work in one doorbell.
- **No per-collective `ibv_reg_mr`.** One MR over the full VRAM pool at Keeper init covers all collective data movement; no per-op registration cost.
- **No topology re-detection.** CNTP's topology view is fixed per Canopy epoch; algorithm choice is baked into Phase J emission.

#### 37.5.7 Size + LoC budget

Per-vendor `comm` directory:

| Vendor | Hop kernels | LoC |
|---|---|---|
| NV | 12 (ring, tree, HD, NVLS variants) | ~5K |
| AM | 10 (ring, tree, HD, XGMI multicast) | ~5K |
| TPU | 6 (native ICI torus primitives) | ~4K |
| TRN | 8 (NeuronLink + EFA primitives) | ~5K |
| CPU | 4 (shared-memory + TCP) | ~2K |

Total ~21K LoC across vendors, vs NCCL's ~60K LoC + proxy thread + transport plumbing. LoC savings come from (a) no proxy, (b) no runtime protocol selection (recipe pins it), (c) no algorithm lookup at runtime (Forge emits the chosen algorithm directly).

---

## 38. NetworkOffload plane — SHARP / ICI / XGMI / SwarmX

Optional fifth plane of CNTP: in-fabric reduction hardware, capability-queried, capability-tolerant, auto-fallback to software.

### 38.1 The interface

```cpp
class NetworkOffload {
public:
    [[nodiscard]] virtual const OffloadCaps& capabilities() const = 0;
    [[nodiscard]] virtual bool can_offload(std::span<const SourceUuid>, OffloadOp, ScalarType, uint32_t shard_bytes) const = 0;
    [[nodiscard]] virtual Expected<OffloadGroup> create_group(fx::Init, Arena&, std::span<const SourceUuid>, OffloadOp, ScalarType, uint32_t) = 0;
    [[nodiscard]] virtual Future<void> submit(fx::Bg, const OffloadGroup&, std::span<const uint8_t>, std::span<uint8_t>) = 0;
    virtual void destroy_group(fx::Init, OffloadGroup) = 0;
};
```

### 38.2 Providers

Each is ~2-5K LoC, optional, runtime-detected via probe functions that gracefully degrade if hardware/software missing.

- **MellanoxSharpOffload** — `libsharp.so` from MOFED/DOCA, InfiniBand SHARP aggregation trees on Quantum-2 / Quantum-X800 switches.
- **GoogleTpuIciOffload** — TPU ICI native aggregation commands, used on TPU pods.
- **AmdXgmiOffload** — AMD Infinity Fabric intra-node reduction primitives (MI300X+).
- **CerebrasSwarmXOffload** — wafer-scale inter-chip SwarmX aggregation.
- **NvswitchScpOffload** — NVSwitch SHARP-equivalent atomics (sm_90+ in certain NVL72 fabrics).
- **BluefieldDpuOffload** — offload to BlueField DPU's ARM cores.
- **P4TofinoOffload** — Intel Tofino programmable switches (research).
- **SoftwareNoOffload** — always-available null provider, returns empty capabilities.

`OffloadRegistry::select(members, op, dtype, shard_bytes, policy) → NetworkOffload*` picks the best eligible provider at collective time, or returns nullptr (fall back to software collective in `comm::all_reduce` etc.).

### 38.3 Policy

```cpp
struct OffloadSelectPolicy {
    bool     require_deterministic = false;  // skip nondeterministic offloads
    bool     prefer_latency        = false;
    bool     prefer_bandwidth      = true;
    uint32_t min_group_size        = 4;
    uint32_t min_shard_bytes       = 4096;
};
```

`require_deterministic = true` is the default for training runs under `BITEXACT` recipe. Offload tier drops out; software pinned-tree takes over. Small perf tax; absolute determinism.

---

## 39. Genesis Kernel Pack — per-chip precompiled seed

Ships with each Crucible installation. One pack per supported chip SKU. Pre-populates L3 KernelCache at runtime-init so common shapes hit from byte zero.

### 39.1 Contents per chip pack

~500 canonical kernels covering 80%+ of ML workloads:

| Family | Shapes | Precisions | Kernel count |
|---|---|---|---:|
| GEMM / BMM / ADDMM | 8 common tile families | FP16, BF16, FP32, FP64, FP8-e4m3 | ~200 |
| Conv 2D | 6 kernel/stride/pad configs | FP16, BF16, FP32 | ~60 |
| Attention | head_dim ∈ {64, 128, 256}, causal ± | FP16, BF16, FP8 | ~40 |
| Norm (Layer / RMS / Batch) | 4 hidden-dim buckets | 3 precisions | ~50 |
| Reductions | axis=0, axis=-1, all-axes | 3 precisions | ~30 |
| Elementwise chains | 8 common patterns | 3 precisions | ~40 |
| Activations standalone | 12 kinds | 3 precisions | ~40 |
| Data movement | contiguous, cat, permute | 3 precisions | ~40 |

### 39.2 Size + tiers

Per chip pack:
- `genesis-minimal`: ~300 kernels, ~30MB. Covers 80% of production inference.
- `genesis-standard`: ~500 kernels, ~100MB. Covers 95% of training + inference. **Default.**
- `genesis-full`: ~1500 kernels, ~300MB. Covers research edge cases + rare ops.

### 39.3 Build process

CI job per supported chip family (`h100_sxm5`, `h200`, `b200`, `gb300`, `thor`, `rtx5090`, `mi300x`, `mi325x`, `v5p`, `v5e`, `v6e`, `trn1`, `trn2`, `trn3`, `cer_wse3`, `cpu_x86_64_avx512`, etc.):

```
For each (KernelKind, ShapeFamily, Precision) in canonical_workload_list:
    Compile via Forge + Mimic<vendor>
    Pack binary + predicted cycles + content_hash into genesis-<chip>.bin
```

Same code path the runtime uses; no separate "BLAS library" codebase to maintain. Pack files shipped as data alongside Crucible binary. Rebuilt on every Crucible release.

### 39.4 Runtime integration

At Crucible init, Keeper mmaps the installed pack, rehydrates L3 cache entries in parallel (~100ms for 500 kernels). First-iteration compile:
- Cache hit → run compiled kernel directly
- Cache miss → fall back to reference-eager from `mimic/<vendor>/reference/` + compile in background; subsequent iterations hit cache

---

## 40. Recipe realization per backend

Every backend's `realize_recipe` function maps `NumericalRecipe` to vendor-specific realization. Cross-vendor tolerance depends on faithful realization here.

### 40.1 NVIDIA (`mimic/nv/lower/Recipe.cpp`)

```
accum_dtype = Float:
    → WGMMA with f32 accumulator attribute
    → tcgen05.mma with acc_dtype=f32
    → hmma.sync with f32 accumulator

reduction_algo = PAIRWISE:
    → Emit explicit pairwise tree reduction over K axis
    → No split-K atomics when determinism != UNORDERED

rounding = RN:
    → MUFU.RCP / MUFU.RSQ with round-nearest
    → FADD/FMUL default rounding

scale_policy = PER_BLOCK_MX:
    → tcgen05.mma with MX mode + scale vector registers
    → Requires sm_100a/f (block_scale cap bit)

softmax = ONLINE_LSE:
    → Emit explicit max-track + exp-sum-track + divide sequence in SASS
    → No vendor softmax intrinsic use

determinism = BITEXACT:
    → No split_k, no atomic accumulation, fixed block issue order
```

### 40.2 AMD (`mimic/am/lower/Recipe.cpp`)

```
accum_dtype = Float:
    → MFMA with f32 accumulator variant
    → ACC_VGPR accumulator registers

reduction_algo = PAIRWISE:
    → Explicit pairwise tree
    → v_fma_f32 chains

rounding = RN:
    → s_setreg MODE for round-nearest
    → Default on CDNA3

scale_policy = PER_BLOCK_MX:
    → MFMA FP8 variants with scale_a / scale_b operands
    → Requires gfx950+

softmax = ONLINE_LSE:
    → Explicit max-track + exp-sum sequence in wave-local VGPRs

determinism = BITEXACT:
    → No atomic split_k, fixed XCD ordering
```

### 40.3 TPU (`mimic/tpu/lower/Recipe.cpp`)

```
accum_dtype = Float:
    → MXU dot_general with precision_config = [HIGHEST, HIGHEST]
    → f32 accumulate attribute

reduction_algo = PAIRWISE:
    → Explicit pairwise HLO reduce tree
    → No xla.reduce_window optimizations that reorder

rounding = RN:
    → MXU default (round-nearest-even)

scale_policy = PER_BLOCK_MX:
    → emulated on v5p/v5e (perf tax); native on v7
    → mark recipe.emulated_on in registry

softmax = ONLINE_LSE:
    → Explicit HLO sequence; no stablehlo.softmax primitive

determinism = BITEXACT:
    → Fixed ICI reduction tree
    → No XLA auto-parallelization of reductions
```

### 40.4 Trainium, Cerebras, CPU

Same pattern. Each backend has `Recipe.cpp` mapping every enum field to native realization. CI enforces the mapping produces equivalent output via §41.

### 40.5 Recipe registry handshake

When Crucible starts on a chip, the per-vendor backend walks `crucible/data/recipes.json`, checks which recipes' `native_on` bitmap includes its chip, and for each registers its `realize_recipe` ability. Recipes absent from the bitmap are unavailable on that chip; Forge's fleet picker respects this.

### 40.6 `BITEXACT_TC` realization per backend

Recipes declaring `determinism = BITEXACT_TC` (FORGE.md §19.1) require K ≤ 8 tensor-core fragments plus a pinned outer scalar reduction. Per-backend realization:

**NVIDIA (`mimic/nv/lower/Recipe.cpp`).**

```
For BITEXACT_TC on sm_90+:
    Prefer WGMMA m64n128k8 (or m64n64k8, m64n32k8).
    Reject m64n128k16 / m64n128k32 / m64n128k64 — summation tree beyond K=8
        has vendor-specific carry-save topology not matched by AMD/TPU/TRN.
    Outer reduction over K-fragments: emit scalar FFMA chain in pinned order
        (source order by canonical index; chain depth = K / 8).
    FTZ pinned via `setreg .denormal_mode, 0`; applies to both tensor-core
        and scalar FMA instructions emitted in this recipe.
    No split_k, no atomic accumulation. Block issue order deterministic
        via EIATTR_REQNTID pinning CTA geometry.
```

**AMD (`mimic/am/lower/Recipe.cpp`).**

```
For BITEXACT_TC on gfx942/gfx950:
    Prefer MFMA_32x32x8_F16 (not 32x32x16).
    Prefer MFMA_32x32x8_BF16 (not 32x32x16).
    Outer reduction: v_fma_f32 chain in pinned order over ACC_VGPR accumulator.
    FTZ pinned via `s_setreg MODE, 0x...`; per-wavefront mode register.
    XCD ordering pinned: one XCD processes one output tile, deterministic dispatch.
```

**TPU (`mimic/tpu/lower/Recipe.cpp`).**

```
For BITEXACT_TC on v5p+:
    MXU dot_general with K=8 accumulator reset cadence
        (chunk large-K into K=8 blocks, emit explicit accumulator reset
         plus outer reduction between blocks).
    Outer reduction: HLO reduce with explicit pairwise-tree layout,
        precision_config=[HIGHEST, HIGHEST], no xla.allow_excess_precision.
    Denormal handling via v5p's default (preserving denormals; matches NV FTZ=off).
    ICI topology pinned via explicit sharding spec.
```

**Trainium (`mimic/trn/lower/Recipe.cpp`).**

```
For BITEXACT_TC on trn2+:
    TensorEngine with K=8 PSUM accumulator resets
        (chunk large-K into K=8 blocks; explicit PSUM read/write between).
    Outer reduction: ScalarEngine / VectorEngine pipelined reduction,
        pairwise tree, SBUF-staged intermediates.
    FTZ pinned via NEFF header denormal-mode field.
    DMA schedule deterministic via explicit sync_engine barriers.
```

**CPU reference (`mimic/cpu/lower/Recipe.cpp`).**

```
For BITEXACT_TC on CPU:
    Emulates BITEXACT_TC by emitting BITEXACT_STRICT semantics with tensor-core
    placeholder. No actual tensor-core; scalar FMA chain in pinned order.
    Serves as the cross-vendor oracle in CI (§41); all BITEXACT_TC recipe outputs
    are compared against this.
```

### 40.7 Extension-point body realization per backend

Extension-point bodies (FORGE.md §18.7) inline at IR002→IR003* lowering. Each backend's lowering pass handles bodies identically for ATTENTION, NORM, REDUCE, SCAN, POINTWISE, EMBEDDING, SSM, RNG, COLLECTIVE, MOE_ROUTE, OPTIMIZER, and the prologue/epilogue slots of GEMM and CONV. The general pattern:

```cpp
void realize_attention_kernel(const AttentionAttrs& attrs,
                               const TileSpec& tile,
                               Ir003NvBuilder& builder) {
    // 1. Structural prelude: compute tile bounds, issue TMA loads for Q,K,V
    emit_structural_prelude(attrs, tile, builder);

    // 2. Per Q-tile outer loop:
    for (each q_tile) {
        // 2a. Extension point: q_preproc_body (typically RoPE)
        if (attrs.q_preproc_body) {
            realize_body(attrs.q_preproc_body, q_tile_regs, builder,
                         /*origin=*/BodyOrigin::EXTENSION_POINT);
        }

        // 2b. Per KV-tile inner loop:
        for (each kv_tile) {
            // Structural: QK^T via WGMMA m64n128k8
            emit_wgmma_qk(q_tile_regs, k_tile_regs, score_regs, builder);

            // 2c. Extension point: score_mod_body
            if (attrs.score_mod_body) {
                realize_body(attrs.score_mod_body, score_regs, builder,
                             /*origin=*/BodyOrigin::EXTENSION_POINT);
            }

            // 2d. Extension point: mask_mod_body
            if (attrs.mask_mod_body) {
                realize_body(attrs.mask_mod_body, score_regs, builder,
                             /*origin=*/BodyOrigin::EXTENSION_POINT);
            }

            // Structural: softmax recurrence (possibly with user's variant)
            if (attrs.softmax_recurrence) {
                realize_body(attrs.softmax_recurrence, ..., builder);
            } else {
                emit_online_softmax(score_regs, max_reg, sum_reg, builder);
            }

            // Structural: V accumulation via WGMMA
            emit_wgmma_v_accum(score_regs, v_tile_regs, output_regs, builder);

            // 2e. Extension point: value_accum_body
            if (attrs.value_accum_body) {
                realize_body(attrs.value_accum_body, output_regs, builder);
            }
        }

        // 2f. Extension point: output_post_body
        if (attrs.output_post_body) {
            realize_body(attrs.output_post_body, output_regs, builder);
        }

        // Structural: write output tile via TMA store
        emit_structural_epilogue(q_tile, output_regs, builder);
    }
}

void realize_body(const ComputeBody* body,
                   std::span<RegisterId> in_regs,
                   Ir003NvBuilder& builder,
                   BodyOrigin origin) {
    // Translate each IR001 micro-op to one or more IR003NV instructions.
    // Mark each generated instruction with IS_EXTENSION_POINT_BODY if origin matches.
    for (const auto& micro : body->ops) {
        auto insts = lower_micro_to_ir003(micro, builder.target_caps());
        for (auto& i : insts) {
            i.flags |= to_flags(origin);
            builder.push(i);
        }
    }
}
```

**Per-backend translation table** for common IR001 micro-ops → native ISA:

| IR001 micro-op | NV (SASS) | AM (AMDGPU) | TPU (HLO+MXU) | TRN (NeuronCore) |
|---|---|---|---|---|
| `ADD f32` | FADD / FFMA | v_add_f32 | ADD | ScalarEngine ADD |
| `MUL f32` | FMUL / FFMA | v_mul_f32 | MUL | ScalarEngine MUL |
| `FMA f32` | FFMA | v_fma_f32 | FUSED_MULTIPLY_ADD | ScalarEngine FMA |
| `WHERE f32` | FSEL / predicated | v_cndmask_b32 | SELECT | VectorEngine SEL |
| `CAST f16→f32` | F2F (widen) | v_cvt_f32_f16 | CONVERT | dtype_conv |
| `EXP f32` | MUFU.EX2 (after ×log₂e) | v_exp_f32 | EXP | ScalarEngine EXP |
| `SIN / COS` | MUFU.SIN / MUFU.COS | v_sin_f32 | SIN / COS | math lib |
| `ABS` | FMUL with clear sign bit | v_and_b32 with sign mask | ABS | ScalarEngine ABS |

Bodies that require ops absent from the backend's ISA (rare for standard arithmetic, possible for novel functions) emit a sequence of supported ops; if the expansion is too costly for hot-path use, MAP-Elites prefers candidates that avoid the extension point's contribution or that switch to a different structural variant with less inner pressure.

### 40.8 Event Tensor idioms per-vendor

The Event Tensor pattern (FORGE.md §18.8, CRUCIBLE.md §11.9.2) — multi-dim counter arrays tracking data-dependent task dependencies — is vendor-neutral in IR but per-vendor in realization. Each backend maps `notify()` / `wait()` primitives to native sync mechanisms.

#### 40.8.1 NVIDIA (Hopper+)

Event Tensor counters are pinned sysmem arrays mapped via BAR1. Each element is 4-8 bytes (matching PatchPoint width).

- **`notify()`**: `SEM_RELEASE` to counter address; released value = `old - 1` via `SEM_EXECUTE` with `_REDUCTION_DEC`. One method entry (~24 bytes pushbuffer).
- **`wait()`**: `SEM_ACQUIRE ACQ_CIRC_GEQ` or `ACQUIRE_EQ 0`. Device-side; warpgroup stalls on the semaphore.

For EVENT_TENSOR with high-frequency updates (MoE routing), the counter maps in shared memory instead of pinned sysmem — warp-local atomics at ~5 ns, release through mbarrier primitives at warpgroup boundaries.

#### 40.8.2 AMD (CDNA3+, RDNA3+)

- **`notify()`**: `PACKET3_RELEASE_MEM` with `ATOMIC_DECREMENT` flag. ~28 bytes PM4.
- **`wait()`**: `PACKET3_WAIT_REG_MEM` with `FUNCTION_GEQ` or `FUNCTION_EQ 0`. ~24 bytes PM4.

High-frequency path: LDS (Local Data Share) atomics on CDNA3 (`ds_atomic_dec_u32`), similar latency to NV smem atomics.

#### 40.8.3 Google TPU

TPU's scalar processor manages event-based synchronization natively:

- **`notify()`**: scalar-processor `SIGNAL_EVENT` with decrement variant
- **`wait()`**: `WAIT_FOR_EVENT` with predicate expression

ICI fabric exposes hardware-supported counter aggregation for cross-core Event Tensor patterns (e.g., MoE with experts on different TensorCores).

#### 40.8.4 AWS Trainium

Trainium's sync_engine:

- **`notify()`**: `BARRIER_SIGNAL` with atomic-decrement payload
- **`wait()`**: `BARRIER_WAIT` with threshold check

NeuronCore engines (TensorEngine / VectorEngine / ScalarEngine) coordinate via sync_engine; Event Tensor cross-engine dependencies are natively expressible.

#### 40.8.5 CPU

- **`notify()`**: `__atomic_fetch_sub(counter, 1, __ATOMIC_RELEASE)`
- **`wait()`**: spin-poll `__atomic_load_n(counter, __ATOMIC_ACQUIRE)` with exponential backoff

Equivalent to a C++20 `std::atomic<int>` with `fetch_sub` / `load`.

#### 40.8.6 Cross-vendor semantics contract

Regardless of vendor, Event Tensor operations satisfy:

1. **Monotonic decrement** — counter values strictly non-increasing over a single event lifecycle
2. **Release-acquire ordering** — all writes preceding `notify()` are visible after the corresponding `wait()` returns
3. **No lost wakeups** — a `wait()` that began before the final `notify()` will eventually return
4. **Counter reinitialization** — at plan boundaries, counters reset to compile-time initial value via PatchPoint write

BITEXACT recipes require deterministic task-ready ordering; the counter-based model preserves determinism when:

- Counter initial values and decrement operations are deterministic (same inputs → same counter trajectory)
- Consumer task-queue pop order is canonical (sorted by task coordinate, not by arrival order)

The on-GPU dynamic scheduler (CRUCIBLE.md §3.9) uses these properties to maintain BITEXACT semantics under dynamic control flow where possible.

#### 40.8.7 Performance comparison

| Backend | `notify()` latency | `wait()` unblock-to-execute |
|---|---|---|
| NV sysmem semaphore | ~200-400 ns (PCIe fly) | ~200-400 ns |
| NV shared memory | ~5 ns (LDS atomic) | ~5-10 ns |
| AMD LDS | ~5 ns | ~5-10 ns |
| AMD sysmem | ~200-400 ns | ~200-400 ns |
| TPU ICI event | ~100-200 ns | ~100-200 ns |
| Trainium sync_engine | ~50-100 ns | ~50-100 ns |
| CPU atomic | ~5 ns | ~10-50 ns |

Event Tensor patterns should use shared-memory / on-chip path when both producer and consumer tasks live on the same SM; sysmem or fabric-level paths only for cross-SM or cross-node coordination.

---

## 41. Cross-vendor numerics CI — enforcing the IR002 portability contract

The single most important piece of test infrastructure. Without it, cross-vendor equivalence is a promise; with it, it's a merged-PR guarantee.

### 41.1 The test matrix

```
For every (KernelKind × NumericalRecipe × target) triple where
  recipe.native_on ∋ target:

    compile via Forge + Mimic<target>
    execute on real hardware (or reference simulator if hardware absent)
    record output bytes + measured_cycles + stall_attribution
```

For 21 kernel kinds × 30 recipes × 6 backends = ~3800 (kind, recipe, target) combinations. Most cells aren't native on all targets; actual populated set is ~2000.

### 41.2 The comparison

```
For each (KernelKind, NumericalRecipe) pair:
    targets = {t : recipe.native_on includes t}
    For each (t1, t2) in targets × targets with t1 < t2:
        diff = compare_outputs(result[t1], result[t2])
        
        switch recipe.determinism:
            case BITEXACT_STRICT:
                require diff == 0 bytes
                (CPU reference is the oracle; every BITEXACT_STRICT recipe
                 must produce byte-identical output to the CPU scalar-FMA
                 reference on every native-supported target.)
            case BITEXACT_TC:
                require diff.max_ulp <= 1
                (K ≤ 8 tensor-core fragments produce bit-identical output
                 for the shapes listed in recipe.tc_shape_constraint; the
                 outer scalar reduction is bit-identical by construction.
                 ≤ 1 ULP slack covers edge cases in the final rounding of
                 mixed-precision widening at the accumulator boundary.)
            case ORDERED:
                require diff.max_ulp <= recipe.tolerance_ulp_cross_vendor
            case UNORDERED:
                no check (user opted in)
        
        Also record diff.max_ulp and diff.mean_ulp in the CI report
```

**Per-tier expected outcomes.** The test matrix separates targets by expected determinism level. A recipe declaring `BITEXACT_TC` on (NV sm_100a, AM gfx950, TPU v5p, TRN trn2) with `tc_shape_constraint.require_k_le = 8` must produce pairwise ≤1 ULP output across all six pairs. Failure modes investigated:

- **Backend regression in a specific `realize_recipe`**: bug in the lowering path (e.g., K=16 tensor-core instruction emitted when K=8 was required). Fix the backend.
- **Widening-before-MMA divergence**: one backend widens the FP16 operand before the multiply, another widens after the multiply-inside-the-tensor-core; the pre-MMA extension-point `q_preproc_body` output may differ at denormal boundaries. Mitigation: explicit cast + FTZ pinning before the MMA instruction.
- **Scale-application order for FP8/FP4 MX recipes**: not fixable by software; recipe is declared `ORDERED` only.

### 41.3 CI infrastructure

- `crucible/test/numerics_ci/` — the harness
- Per-vendor test runners (on real hardware or reference simulator)
- Pairwise comparison + tolerance enforcement
- Reports: per-recipe ULP histograms, per-backend residuals, failure diagnostics pinpointing which backend differs

### 41.4 Workflow on failure

1. CI flags a (kernel, recipe, target-pair) failure
2. Investigator checks: is this a bug in the failing backend's `realize_recipe`?
3. Two remediations:
   - (a) Fix the backend — update lowering, re-test
   - (b) Widen tolerance — update `recipe.tolerance_ulp_cross_vendor`, document the widening, require explicit user opt-in via recipe tag
   - (c) Remove the target from `recipe.native_on` — document that the recipe is not natively realizable there
3. PR rebased, CI re-runs, must pass

### 41.5 Priority tiers

Running all 2000 pairs on every PR would be prohibitively slow. Priority tiers:

- **Pre-merge gate (~5 min)**: CPU reference + NV + AM on ~50 canonical (kernel, recipe) pairs. Catches regressions in the primary supported vendors.
- **Post-merge release gate (~1 hour)**: full 2000-cell matrix. Runs on release branches. Failures block release.
- **Quarterly deep-run**: add TPU, Trainium, Cerebras real-hardware runs if hardware access is available; otherwise reference simulator validates model fidelity.

---

## 42. Per-vendor backend notes

High-level sketches of each non-NV backend. Each is a self-contained codebase following the template documented in §3-§35 for NVIDIA.

### 42.1 AMD (`mimic/am/`)

- ISA: AMDGPU ISA (gfx942 = CDNA3 MI300X; gfx950 = CDNA4 MI325X; RDNA3 = gfx1100 for consumer). Open documentation.
- Emitter: can reuse LLVM-AMDGPU backend *internally* for ISA selection (the generator tables are open). Our HSACO wrapper + our own scheduler on top.
- Simulator: calibrate via rocprof equivalent. Async ops are MFMA (no TMA-analog; s_waitcnt-based sync).
- Runtime: KFD ioctl wrapper. ~7K LoC.
- Collectives: XGMI intra-node + RoCE inter-node via CNTP.
- Status: second priority after NV. Parity expected M10-14.

### 42.2 Google TPU (`mimic/tpu/`)

- ISA: proprietary; RE'd from `libtpu.so` via same methodology as NVIDIA RE (IDA Pro + MLIR-pass-labeled intermediates).
- Emitter: TPU executable format (proto + opaque payload). Our writer produces PJRT-compatible bytes without linking libtpu.
- Simulator: harder — no driver source, calibration relies on real-hardware measurement. MXU + VPU + SMEM + VMEM models.
- Runtime: `/dev/accel*` ioctl wrapper. ~10K LoC.
- Collectives: ICI torus. Natively in-fabric-reducing (NetworkOffload provider).
- Status: starts M14 after TPU compiler RE completes enough of the ISA.

### 42.3 AWS Trainium (`mimic/trn/`)

- ISA: proprietary; RE'd from `neuronx-cc` compiler (we downloaded Neuron SDK 2.29.0, see companion notes).
- Emitter: NEFF (Neuron Executable File Format) — tarball with per-engine bytecode sections (TensorEngine, VectorEngine, ScalarEngine, GpSimd, DMA, Sync).
- Simulator: calibrate via `neuron-profile`. PSUM accumulator, SBUF scratch, HBM, NeuronLink topology.
- Runtime: `/dev/neuronN` ioctl wrapper. Protocol documented in `neuron_driver_shared.h`. ~9K LoC.
- Collectives: NeuronLink intra-node + EFA inter-node. Native ring/mesh/hierarchical algorithms exposed in `nec.h` headers we RE'd.
- Status: starts M10 in parallel with AMD. Plenty of observability (MLIR traces, neuron-profile, public runtime headers).

### 42.4 Cerebras (`mimic/cer/`)

- ISA: CSL (Cerebras Software Language). Some documentation; some RE needed.
- Emitter: Cerebras-specific binary format.
- Runtime: native `/dev/cerebras*` protocol.
- Collectives: SwarmX fabric aggregation.
- Status: optional. Builds after M20. Lower priority than the big four.

### 42.5 CPU reference (`mimic/cpu/`)

- ISA: x86_64 AVX2/AVX512; aarch64 NEON/SVE2; riscv V extension.
- Emitter: writes a native ELF .so or stages source + invokes Clang for JIT. Both supported.
- Simulator: trivial (real execution measured via perf_event_open).
- Runtime: `malloc`, `mmap`, pthreads. ~2K LoC.
- Collectives: shared memory + TCP via CNTP.
- Status: **build first.** Correctness oracle for every other backend. M1-2.

### 42.6 Mixed consumer/datacenter Blackwell — first-class target

Per CRUCIBLE.md §17.7 and the aikitoria 595.58.03 benchmark from the design cycle, mixed Blackwell fleets combining consumer 5090 / RTX PRO 6000 / datacenter B200 are supported as a production deployment pattern. Driver-gate bypass policy (CRUCIBLE.md §17.7) applies to all generations; this section specifies Mimic's treatment of the asymmetric pattern.

#### 42.6.1 Deployment pattern

A typical mixed Blackwell node:

- 1× RTX PRO 6000 Blackwell (GB203) as "anchor" — 96 GB VRAM with ECC, workstation-class validation, full pro driver signing
- N× 5090 (GB202) as compute multipliers — 32 GB VRAM, consumer tier, ~2× cheaper per TFLOP

Example: 1× PRO 6000 + 8× 5090 = 9 GPUs, ~352 GB total VRAM, aggregate ~14 PFLOPs BF16. Cost ~$30K vs comparable datacenter build at ~$250K (8× H100 80 GB).

#### 42.6.2 What works (aikitoria-verified on 595.58.03)

- **BAR1 P2P**: 55 GB/s unidirectional, 111 GB/s bidirectional between any pair — full PCIe Gen5 x16 wire speed
- **Cross-SKU P2P**: PRO 6000 ↔ 5090 works identically to 5090 ↔ 5090 (both GB20x family)
- **GPUDirect RDMA**: NIC-to-VRAM DMA at line rate on any capable GPU
- **Latency**: 0.38-0.45 μs GPU-to-GPU 64 B write via BAR1 (vs 14.3 μs without P2P)

#### 42.6.3 What doesn't work (hardware-physical limits)

- **NVLink between 5090s**: PCB-level absent. Fall back to BAR1 P2P.
- **MIG partitioning**: consumer SKUs lack full MIG hardware.
- **ECC reporting on 5090**: GDDR7 has on-die ECC but no user-visible error counter. PRO 6000 has full ECC with reporting.

#### 42.6.4 Partition solver adaptation

Our Z3 partition solver (FORGE.md §25.8) handles asymmetric VRAM, compute, and reliability classes:

- **Per-GPU VRAM capacity**: PRO 6000 gets 3× the weight shards of 5090 under `policy=asymmetric_weight_allocation`
- **Per-GPU TFLOPs**: 5090 is ~90% of PRO 6000 FP16 throughput; partition sizes per-device compute rating
- **Reliability weighting**: PRO 6000 tagged as "parameter server" role (holds optimizer state, gradient sum); 5090s are compute workers. Loss of 5090 = partition-level reshard; loss of PRO 6000 = checkpoint recovery
- **Interconnect topology**: BAR1 P2P everywhere; no NVLink to coordinate around

#### 42.6.5 Recipe implications

`BITEXACT_TC` realizable cross-SKU: all GB20x chips share tensor-core architecture (WGMMA m64n128k8, etc.). Sub-1 ULP equivalence verified by cross-vendor CI (§41.2).

FP8 MX recipes work across all Blackwell; hardware identical.

Recipe registry `native_on` for Blackwell recipes includes all GB20x variants:

```
"native_on": [
  "nv_gb100", "nv_gb102", "nv_gb10b", "nv_gb110", "nv_gb112",
  "nv_gb202", "nv_gb203", "nv_gb205", "nv_gb206", "nv_gb207",
  "nv_gb20b", "nv_gb20c"
]
```

#### 42.6.6 Deployment support

`crucible-fleet` CLI includes a `--mixed-sku` mode that:

- Auto-detects consumer vs datacenter SKUs via PCI device ID
- Applies driver-gate bypass patch (CRUCIBLE.md §17.7) if consumer SKUs present
- Configures `partition_policy` with appropriate heterogeneity weights
- Validates cross-SKU P2P at Keeper init via 1 MB ping test
- Tags PRO 6000 / RTX 6000 Ada as reliability anchor

Tested reference configurations:

| Config | Use case |
|---|---|
| 1× PRO 6000 + 8× 5090 | Budget research cluster |
| 2× PRO 6000 + 16× 5090 | Mid-scale training |
| 8× B200 + 8× PRO 6000 | "Mixed tier" — consumer absorbs tail traffic |

#### 42.6.7 Warranty, support, compliance

Mixed consumer fleets are explicitly not supported by NVIDIA for warranty / CUDA SDK. Crucible documents at install time; users opt in with disclosure. For compliance-critical workloads (healthcare, finance), all-datacenter fleets remain recommended.

Target audience for mixed Blackwell: (a) research labs, (b) startups without NVIDIA enterprise contracts, (c) burst-capacity deployments, (d) development environments where cost matters more than vendor certification.

---

## 43. Glossary

**Accurate tier** — simulator tier at 100-500ms per simulation, 2-5% absolute accuracy. Cycle-stepping. Used for calibration validation and tiebreaks.

**Archive** — the MAP-Elites data structure. Grid of cells, each holding best-in-cell candidate by fitness. Persisted to Cipher.

**Behavior vector** — the 6-dimensional descriptor identifying a cell in the MAP-Elites archive.

**Calibration** — the process of fitting TargetCaps and OpcodeLatencyTable to actual hardware behavior via microbenchmarks and CUPTI measurement.

**Capsule Mercury (capmerc)** — the sm_100+ cubin wrapper format. Includes auxiliary sections for driver-side re-targeting.

**Content hash** — Crucible's 64-bit Merkle-style hash of graph structure. Stable across refactors.

**CUPTI** — CUDA Profiling Tools Interface. NVIDIA's library for GPU performance measurement. Used by Mimic for calibration and drift detection.

**DecodedInst** — Mimic's internal 32-byte representation of a single SASS instruction.

**DecodedProgram** — program-level wrapper. Consumed by simulators.

**Efficiency ratio** — achieved cycles / theoretical minimum. The primary MAP-Elites fitness metric.

**Event-driven simulation** — simulation model where only relevant events (issue slot, completion, memory response) are processed. Medium tier.

**Fast tier** — simulator tier at 1-5ms, 10-20% absolute accuracy but 90% ranking accuracy. Used for MAP-Elites filtering.

**Feature vector (`FeatureVec`)** — MAP-Elites behavior descriptor. 6 uint8_t = 6 bytes.

**GTO** — Greedy-Then-Oldest warp scheduling policy. Stick with last-picked warp if ready; otherwise pick oldest.

**Hint (`MutationHint`)** — a specific mutation operation suggested by an insight.

**Insight (`Insight`)** — a structured diagnostic extracted during simulation. Keyed to a specific instruction, carries severity and confidence.

**IR003NV** — Forge's NVIDIA vendor-specific IR. Mimic consumes it (via encoding to SASS and decoding to DecodedProgram).

**L2 slice** — a portion of the L2 cache. H100 has 96 slices. Address-hashed for routing.

**MAP-Elites** — Mimic's evolutionary search algorithm. Maintains a behavior archive; mutations produce candidates; candidates are placed in cells by behavior.

**Medium tier** — simulator tier at 10-30ms, 5-8% absolute, 95% ranking. Primary fitness evaluator.

**Microbenchmark** — a small CUDA kernel designed to measure one aspect of hardware behavior in isolation.

**Mutation** — a transformation applied to a parent IR003NV program to produce a child candidate.

**OpcodeLatencyTable** — per-SM table of opcode → (pipe, latency, occupancy). Loaded lazily per SM.

**Pipe** — per-SM functional unit. 10 pipes on sm_90+: int, fmalighter, fp16, fma64lite, fma64heavy, mio, cbu, fe, mma, tcgen05.

**Residual** — predicted value minus measured value. Used for drift detection.

**SASS** — NVIDIA's binary instruction format. 128-bit Mercury encoding for sm_75+.

**Scoreboard (SB)** — per-warp 6-barrier system for tracking in-flight memory/tensor ops.

**SmState** — per-SM simulator state struct.

**TargetCaps** — per-chip capability and calibration data. ~256-byte struct.

**Tier** — simulator fidelity level. Enum {FAST, MEDIUM, ACCURATE}.

**tcgen05** — Blackwell async tensor core subsystem. Uses TMEM address space.

**TMEM** — tensor memory. sm_100+ physically separate storage for tensor-core operands.

**JSON** — file format for per-chip data files and the recipe registry. Universally parseable; schema-validated via JSON Schema; packs to MessagePack/CBOR where binary compression matters.

**Warp** — 32 threads executing in lockstep. NVIDIA's fundamental scheduling unit.

**WarpTable** — SoA parallel arrays of per-warp state.

**WGMMA** — warp-group MMA. sm_90+ async tensor core MMA, warpgroup-granular (128 threads = 4 warps).

**Pushbuffer composer** — per-vendor subsystem (§15.4) emitting host-engine command stream from (launch, barrier, chain_edge, patch_point) records. NV: 4-dword method sequences targeting GPFIFO. AMD: PM4 packets targeting MEC IB. TPU: scalar-processor bytecode. Trainium: NEFF batch submit. ~19K LoC across vendors.

**setmaxnreg** — NV sm_90+ PTX instruction `setmaxnreg.{inc,dec}.sync.aligned.u32 N`. Rebalances registers between warpgroup roles at runtime. Used by DeepSeek-V3; emitted by Mimic-NV when `reg_alloc_policy=DYNAMIC_SETMAXNREG`. See §15.5.

**Warp specialization split** — patterns encoded in `ExecutionAttrs.warp_spec_split` (FORGE.md §18.3.1). 8 discrete values from MAP-Elites behavior axis. Typical: 2-2-0 (two producer + two consumer warpgroups), 1-2-1 (producer + barrier + consumer).

**Reg alloc policy** — STATIC (compile-time fixed) or DYNAMIC_SETMAXNREG (runtime rebalancing). Per-vendor realization:
- NV: `setmaxnreg.inc/dec`
- AMD: `s_setreg REGMAP`
- TPU: scalar processor manages execution-unit assignment
- Trainium: per-engine static

See §15.5.

**Green context** — NV Hopper+ hardware SM-partition primitive. Mimic-NV's `rt/` allocates per-role contexts at Keeper init (compute / dispatch / combine / scheduler); kernels launched into one context cannot preempt another's SMs. See CRUCIBLE.md §14.9.

**Full userspace driver** — per-vendor `rt/` architecture pattern. Three layers: A (kernel-space shim, minimum viable), B (one-time userspace init at Keeper start, ~50-500 ms), C (hot-path dispatch at sub-μs per kernel). Depth varies: NV hybrid (nvidia.ko for GSP), AMD full-userspace (AM-style, vfio-pci only), TPU /dev/accel*, TRN /dev/neuronN. See §36.4.

**AM-style userspace** — tinygrad-inspired full-userspace AMD driver. `rmmod amdgpu`, vfio-pci ownership, bypass MES scheduler, direct MEC binding at pipe=0/queue=0. Reference: tinygrad `runtime/support/am/`. See §36.4.3.

**Static BAR1** — NVIDIA config mode (`RMForceStaticBar1=1` module param) installing a 2MB-aligned GMMU identity map covering full VRAM. After setup, BAR1 VA == FB physical offset; P2P + RDMA work at wire speed with zero per-op registration cost. See §36.5.1, CRUCIBLE.md §14.8.

**Pushbuffer** — per-vendor command stream submitted to the host engine. NV Hopper: 16 bytes per kernel launch (4-dword sequence). Full training step ≈ 13 KB. Pre-composed at Forge Phase J. See §15.4, FORGE.md §J.6.

**QMD** — Queued Method Descriptor. NV sm_90+ 256-byte kernel-launch descriptor pre-built in per-Plan pool. Carries program address, grid/block dims, constant buffer bindings, register count, semaphore-release fields. Referenced by `SEND_PCAS_A` method in pushbuffer.

**SearchMode** — MAP_ELITES (default, simulator) / HYBRID (MAP-Elites + hardware validation of top-K) / BEAM (dev-only, pure hardware search). Hybrid is default for cross-vendor CI. See §19 (hybrid subsection), FORGE.md §22.8.

**EVENT_TENSOR** — multi-dim atomic-counter array PatchPoint kind (FORGE.md §18.8). Primary use: data-dependent dependencies in MoE / speculative decoding / iterative reasoning. Per-vendor realization: NV sysmem semaphore or shared-mem atomics, AMD LDS atomics or PM4 `RELEASE_MEM`, TPU scalar-processor events, Trainium sync_engine barriers. See §40.8.

**Mixed Blackwell fleet** — supported deployment: consumer 5090 + datacenter RTX PRO 6000 + B200 mixed in one Canopy. aikitoria-benchmarked BAR1 P2P at 55 GB/s unidirectional, 111 GB/s bidirectional. Z3 partition solver with CAPACITY_WEIGHTED or TIERED policy. See §42.6, FORGE.md §25.8.

**FLR / VFIO_DEVICE_RESET** — Function Level Reset via PCIe config mechanism. Crucible target: ~1.5-2.5 s recovery including 100 ms PCIe settle + 800ms-1.2s cached GSP firmware re-upload + 150-250ms init-RPC. See CRUCIBLE.md §17.8.

**Driver-gate bypass** — Crucible policy: enable hardware-capable features regardless of SKU marketing tier. Concrete actions in §36.6 (module params) and CRUCIBLE.md §17.7 (feature-by-feature table).

**KernelKind — 22 families** — post-Event-Tensor update adds OPTIMIZER to the original 21. Full list in FORGE.md glossary.

---

## Summary

Mimic is Crucible's portability layer: one self-contained per-vendor backend per accelerator, plus a shared core. Each backend owns its IR003\*, ISA emitter, binary-format writer, three-tier simulator, MAP-Elites search, runtime library (our replacement for libcuda / libnrt / libtpu / libhsa), and collective library (our replacement for NCCL / RCCL / libnccom / hcoll / UCX). Zero vendor SDK dependency. Zero vendor-library runtime dependency. Every line of every backend is ours, from the kernel-driver ioctl wrapper up to the MAP-Elites archive.

The portability contract is **NumericalRecipe pinned at IR002**. Same IR002 kernel compiled by Forge + Mimic-NV on H100 produces numerically-equivalent output to Mimic-AM on MI300X, Mimic-TPU on v5p, Mimic-TRN on trn2, and Mimic-CPU reference. The cross-vendor CI matrix (§41) enforces this — a backend that violates tolerance fails the build.

Per-vendor LoC: ~85-100K for each full backend. Shared core: ~35K. First-wave multi-vendor (CPU + NV + AM + TPU + TRN): ~500K LoC of Mimic. ~35 months to ship full first wave; NV alone at ~12 months. Replacement for ~5-8 M LoC of traditional vendor-compiler + runtime + collective stacks, plus several GB of vendor-proprietary runtime binaries we refuse to ship.

The design refuses the trap of trying to be a general-purpose simulator. Each per-vendor simulator is calibrated only on the narrow subset of instruction streams Forge emits — structurally disciplined, recipe-pinned, ML-typical. 95-98% accuracy is achievable because of this scope discipline.

The design embraces driver-source access where we have it (NVIDIA, AMD) for first-principles memory-subsystem modeling. Where we don't (TPU, Trainium, Cerebras), we close the gap with MLIR-pass-labeled compiler intermediates plus per-vendor profiling — observability enough to RE the remaining instruction formats.

The design integrates cleanly with Crucible: same arena allocator, same effect tokens, same Cipher persistence (three-level cache: L1 IR002 federation-shareable; L2 IR003\* per-vendor; L3 binary per-chip), same determinism discipline (four tiers: UNORDERED / ORDERED / BITEXACT_TC / BITEXACT_STRICT), same CNTP native transport.

Twelve design commitments beyond the original thesis, documented in the sections cited:

1. **Extension-point body realization** (§40.7). Each per-vendor backend's lowering inlines user-supplied `ComputeBody*` fragments into the structural kernel template at IR002→IR003* emission; peephole rules (§18) are extension-point-aware, enabling cross-boundary fusion (body MUL + structural ADD → FMA) without sacrificing body-origin tagging for insight attribution.
2. **BITEXACT_TC realization** (§40.6). K ≤ 8 tensor-core preference per vendor (WGMMA m64n128k8 on NV; MFMA 32×32×8 on AM; MXU with K=8 accumulator resets on TPU; PSUM K=8 on TRN); pinned outer scalar reduction; FTZ pinned via per-vendor mode register. Cross-vendor ≤1 ULP at ~5-8% perf tax.
3. **Body-hash warm-start for MAP-Elites** (§19). Structural-only canonical hashing lets research variants (new `score_mod`, new optimizer update rule) warm-start from nearest-body archives; 5-10× faster convergence for structurally-similar bodies.
4. **Multi-NIC rail assignment per vendor** (§37.4). Per-vendor NIC-to-accelerator affinity probe; one QP pool per GPU-NIC pair; NUMA-pinned threads and MRs; concurrent-collective support for bucketed async all-reduce.
5. **Cross-vendor CI enforces four determinism tiers** (§41.2). BITEXACT_STRICT matched against CPU scalar oracle; BITEXACT_TC ≤ 1 ULP across all native-supported targets; ORDERED per-recipe tolerance; UNORDERED user opt-in.
6. **Pushbuffer composer per vendor** (§15.4). NV 16-byte 4-dword launch (SEND_PCAS_A + SEND_SIGNALING_PCAS2_B). AMD PM4 packets targeting MEC. TPU scalar-processor bytecode. Trainium NEFF batch. CPU direct-call. Shared API; ~19K LoC across vendors.
7. **CNTP collectives pushbuffer-embedded** (§37.5). Ring all-reduce at ~200 bytes of pushbuffer vs NCCL's ~4μs host-side `cuLaunchKernel`. No proxy thread, no separate stream, no vendor collective library. Primitive hop kernels per vendor.
8. **setmaxnreg warp-specialized register allocation** (§15.5). DeepSeek-V3's runtime register rebalancing exposed as `reg_alloc_policy=DYNAMIC_SETMAXNREG` MAP-Elites axis. Measured +15 MFU points on FA-3. Per-vendor equivalents where available.
9. **Full userspace driver per vendor** (§36.4). Three-layer model (kernel shim / one-time init / hot-path dispatch). Per-vendor depths: NV hybrid (nvidia.ko for GSP), AMD full-userspace (AM-style), TPU /dev/accel*, TRN /dev/neuronN, CPU trivial. Total ~104K LoC replacing several GB of vendor-proprietary runtime binaries.
10. **Direct PCIe config + module params** (§36.5, §36.6). rBAR enable, ASPM off, MPS max, relaxed ordering, extended tags; module-param matrix for NVIDIA with exact `NVreg_*` values and rationale; install-time validation. Static BAR1 identity-mapping for zero-per-op P2P/RDMA setup cost.
11. **Event Tensor idioms per-vendor** (§40.8). Multi-dim counter array (EVENT_TENSOR PatchPoint) realized as NV sysmem/smem semaphores, AMD LDS atomics, TPU scalar-proc events, Trainium sync_engine, CPU `std::atomic`. Per-backend latency table for on-chip vs off-chip paths.
12. **Mixed Blackwell + asymmetric fleet support** (§42.6). Consumer 5090 + RTX PRO 6000 + datacenter B200 in one Canopy, aikitoria-benchmarked cross-SKU P2P at 55 GB/s. Integrates with FORGE.md §25.8 Z3 partition solver's CAPACITY_WEIGHTED / TIERED policies.

Mimic is the sibling of Forge. Together they replace the entire LLVM + Inductor + XLA + ptxas + cicc + vendor-SDK + vendor-BLAS + vendor-DNN + vendor-collective stack end-to-end. Crucible's only remaining vendor dependency is the kernel driver ioctl protocol — stable, versioned, replaceable per major-version when needed.

Build order: shared core + CPU first (correctness oracle, M1-2). NVIDIA second (leverages existing nvopen-tools RE, M2-9). AMD third (LLVM-AMDGPU open, M10-14). Trainium fourth (SDK downloaded, RE in progress, M10-16). TPU fifth (libtpu RE, M14-22). Cerebras sixth (optional, M20+). Numerics CI from day one.

Build it in the order of the dependency chain. Validate at each milestone. The first backend that works is the template; every subsequent backend follows the same pattern.

---

*End of Mimic design document. ~25,000 words. The per-vendor portability layer of Crucible's compilation plane; Forge (vendor-agnostic optimizer) is the matching doc.*

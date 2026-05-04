# Networking, Cogs, and Frontier Distributed Computing
## Crucible substrate primitives + Fixy DSL composition

**Date:** 2026-05-03
**Author:** Grigory Evko
**Scope:** Frontier-SOTA distributed-training networking stack, decomposed into Crucible substrate primitives and Fixy DSL compositions
**Length:** ~6,400 lines, §0–§37 (single document)
**Status:** Architectural specification — implementation tracked under GAPS-110..184 tasks.

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §0. TL;DR

This document specifies the architecture of Crucible's networking + distributed-compute subsystem and its composition surface in Fixy. It is the result of an extended ultrathinking pass that surfaced ~180 distinct frontier-SOTA capabilities across 17 categories (after stripping ~20 single-machine compute optimizations to a separate doc), plus several load-bearing reframings:

1. **No sealed networking subsystem in Crucible.** The earlier "CNTP as a unified library" framing is wrong. Crucible ships **primitives** in regular C++ — driver wrappers, kernel emitters, telemetry harvesters, gossip mechanisms, FEC encoders, RDMA verb wrappers, NVML control surfaces. Fixy DSL **composes** them under the existing type-theory discipline. The same primitive can serve a synchronous all-reduce, an async DiLoCo outer sync, or a custom user-defined strategy — composition decides, not the primitive.

2. **Cog is the atomic hardware unit, not Relay.** A DGX is too coarse. We model hardware as a hierarchical Cog graph (L0 = GPU die / NIC port / CPU core / DRAM channel / NVMe namespace / NVSwitch / optical transceiver / PSU; L7 = datacenter). Every Cog has identity, TargetCaps, OpcodeLatencyTable, health score, position in the topology graph, power/thermal envelope, wear estimate. Failure isolation, resource budgets, and Mimic backends all operate per-Cog.

3. **Comm goes through IR001 → IR002 → IR003*** identically to compute. All-reduce, send, recv, gossip-round are first-class IR ops with content-addressing, MAP-Elites search, KernelCache federation, and cross-vendor CI. Compute-comm fusion happens at Forge Phase D (FUSE) — send-from-epilogue, reduce-on-recv, compress-before-send.

4. **Resource budgeting is row-typed across shared hardware.** SMs, HBM bandwidth, NVLink lanes, NIC queues, QPs, MRs, switch buffer cells, power, thermal headroom — all consumable resources. The compiler sums declared consumption across concurrently-scheduled ops and refuses to compile if total exceeds the relevant Cog's capacity.

5. **Adaptive MFU is a multi-timescale control loop, not a tunable.** Per-step (~ms) routing adjustments, per-K-step (~10s) recompile decisions, per-event (~minutes) topology rediscovery. The objective is composite MFU — `(compute_MFU)^α × (1 - wear)^γ × (1 - power_overrun)^β × ...` — with operator-picked exponents. Discrete-search optimization over the calibrated cost surface, no external SMT solver.

6. **Eventual-consistency + decoupled algorithms are first-class strategies.** DiLoCo, Streaming DiLoCo, Async DiLoCo, CocktailSGD, SWARM, Hivemind, Gossip-SGD, AD-PSGD, Federated Averaging — each composable in Fixy DSL, each replaceable by the user. Crash-stop session types (GAPS-001) compose naturally with these patterns.

7. **NIC programmability spans 7 layers** from ethtool config to BlueField DPU full-Linux-on-NIC and P4 programmable switches. Each layer becomes a Fixy primitive class; user picks the layer based on latency requirement and hardware available.

**Implementation status:** 1/200 items ships today (the lower-level safety substrate that everything else depends on). 199 items are designed, decomposed, and tracked but not built. Calendar timeline to defensible "most-advanced shipped open-source distributed training stack" claim: 12-18 months focused engineering on the GAPS-110..150+ chain.

**What this doc commits to:** the architectural shape, the primitive boundaries, the composition discipline, the verification matrix, the cross-references between substrate and DSL. It does not commit to specific shipping dates beyond the GAPS-* task ordering.

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §1. Methodology + Verification Discipline

## §1.1 What this document is

A specification document. Each architectural claim is paired with: (a) the file path or task ID where it lives or will live, (b) the verification evidence (currently-shipped / planned / research-frontier), (c) the Fixy composition surface that exposes it to users, (d) the cross-references to dependencies. Honesty discipline applies throughout — claims about "what exists" are verified against the source tree at the time of writing; claims about "what's planned" name the GAPS-* task that owns delivery.

## §1.2 What this document is not

- Not a tutorial. It assumes familiarity with CLAUDE.md substrate, the existing GAPS-001..109 catalog, the Universal Mint Pattern, and the Met(X) row-typing approach.
- Not a marketing document. The honest assessment in §0 — "1/200 items ships, 199 planned" — is the discipline. We do not overclaim shipped status anywhere.
- Not user documentation. The Fixy DSL examples illustrate the composition surface; user-facing docs come later under separate tasks.
- Not a research paper. Where techniques come from published research, the doc cites them; it does not reproduce derivations.

## §1.3 Verification protocol

Every claim of the form "Crucible has X" was verified by:

```
ls include/crucible/<subsystem>/ 2>/dev/null
grep -rn "<symbol>" include/crucible/
wc -l include/crucible/<file>
```

at the time of writing (2026-05-03). Verified counts at that moment:

| Subsystem | Headers | Total LOC |
|---|---|---|
| `include/crucible/sessions/` | 24 | 16,979 |
| `include/crucible/concurrent/` | 26 | 11,568 |
| `include/crucible/safety/` | 89 | (varied) |
| `include/crucible/effects/` | 12 | (varied) |
| `include/crucible/bridges/` | 5 | 1,942 |
| `include/crucible/cntp/` | **0** | **0** |
| `include/crucible/topology/` | **0** | **0** |
| `include/crucible/canopy/` | **0** | **0** |
| `include/crucible/network/` | **0** | **0** |
| `include/crucible/cog/` | **0** | **0** |
| `include/crucible/MerkleDag.h` | 1 | 2,006 |
| `include/crucible/Vigil.h` | 1 | 756 |
| `include/crucible/Cipher.h` | 1 | 855 |
| `include/crucible/TraceGraph.h` | 1 | 201 |

The networking-related directories are entirely greenfield. The existing substrate (sessions, concurrent, safety, effects, bridges) is what every networking primitive will compose from.

## §1.4 Honesty discipline

When this document says:
- **"ships"** → file exists, tests pass, verified at write time
- **"partially ships"** → some headers exist, complete subsystem doesn't
- **"planned (GAPS-N)"** → tracked task exists, no implementation
- **"research-frontier"** → no production system has it, including Crucible
- **"speculative"** → architectural intent only, may not ship

When uncertainty exists about a claim — particularly about external systems (NCCL behavior, kernel flags, hardware specs) — the doc states the uncertainty rather than guessing.

## §1.5 Document conventions

- File paths absolute from repo root: `include/crucible/foo/Bar.h`
- Task IDs: `GAPS-NNN`, `FOUND-XYY`, `SAFEINT-RNN`, etc.
- Sections: `§N.M`
- Lengths: `~LL lines` indicates approximate target during planning; actual may differ
- C++ examples follow CLAUDE.md §XVII naming discipline (telling-word predicates, no single-char identifiers outside loop scope, strong typing)
- Met(X) row notation: `Computation<Row<Effect::A, Effect::B>, T>`
- Concepts gated via `requires`-clauses, never SFINAE
- Universal Mint Pattern: factories named `mint_X(Ctx const&, args...) requires CtxFitsX<X<...>, Ctx>`

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §2. Architectural Reframing

## §2.1 The original mistake

An earlier draft of this work proposed shipping a unified networking subsystem under `include/crucible/cntp/` with internal subsystems for topology, gossip, FEC, congestion control, etc. That framing is **wrong** and is hereby retired. The reasons:

1. **Sealed subsystems are user-hostile.** A user who wants to write FSDPv3 cannot do so in PyTorch because FSDP is hardwired into the framework. Same trap waits for any Crucible user wanting to write a custom CC, custom topology routing, or custom membership protocol if the networking is sealed.

2. **The substrate already exists for composition.** Crucible's session types, permission system, row-typed effects, and Universal Mint Pattern are exactly the discipline that makes user-extensible composition safe at compile time. Building a parallel sealed subsystem inside `cntp/` would duplicate this discipline poorly.

3. **The Cog hierarchy makes "subsystems" the wrong unit.** Networking primitives operate per-Cog (per-NIC, per-switch). A single sealed subsystem cannot reflect this granularity without becoming a thin shim over per-Cog primitives anyway.

4. **Frontier algorithms are a moving target.** DiLoCo did not exist in 2022; CocktailSGD did not exist in 2022; SWARM did not exist in 2022. A sealed subsystem freezes a snapshot of "what's frontier today." Composition lets users add tomorrow's frontier without forking Crucible.

## §2.2 The corrected architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                          User code                              │
│   (declares strategy, recipe, Cog mesh, MFU objective)          │
└───────────────────────┬─────────────────────────────────────────┘
                        │
┌───────────────────────▼─────────────────────────────────────────┐
│                   Fixy DSL composition layer                     │
│   - DistributedStrategy (FSDPv3, DiLoCo, CocktailSGD, ...)      │
│   - CongestionControl (BBRv3, DCTCP, custom)                    │
│   - MembershipPolicy (SWIM, HyParView, custom)                  │
│   - HealthPolicy (φ-accrual + thermal + ECC + wear)             │
│   - PowerPolicy (cap, smooth, phase-DVFS)                       │
│   - RoutingPolicy (ECMP, adaptive, multi-path)                  │
│   - All composable, all replaceable, all type-checked           │
└───────────────────────┬─────────────────────────────────────────┘
                        │
┌───────────────────────▼─────────────────────────────────────────┐
│           Crucible substrate primitives (regular C++26)         │
│                                                                 │
│  cog/         topology graph nodes, hierarchy, identity         │
│  topology/    typed property graph, content-addressing          │
│  cntp/*       transport primitives (RDMA verbs, AF_XDP, QUIC)   │
│  perf/        eBPF loaders, telemetry harvesters                │
│  bridges/     session-bridge primitives (existing + new)        │
│  effects/     row-typed effect carriers (existing)              │
│  sessions/    typed protocols (existing, 24 headers)            │
│  concurrent/  permission-typed channels (existing, 26 headers)  │
│  safety/      8-axiom wrappers (existing, 89 headers)           │
│                                                                 │
│  Driver management, kernel calls, verbs, eBPF programs all in   │
│  regular C++. Type theory enforces correctness at boundaries.   │
└───────────────────────┬─────────────────────────────────────────┘
                        │
┌───────────────────────▼─────────────────────────────────────────┐
│                    Hardware (per Cog)                           │
│   GPU SMs / NIC queues / switch ports / CPU cores / etc.        │
└─────────────────────────────────────────────────────────────────┘
```

The split rule is mechanical:

- **Crucible** owns mechanism — "this primitive can do X". Driver wrappers, syscalls, verbs, kernel emitters, telemetry harvesters, hardware control surfaces, type-theory machinery, concept gates, mint factories.
- **Fixy** owns policy — "given the user's row + workload + Cog set + recipe, do X versus Y when the conditions hold". Strategies, schedulers, optimizers, composition templates.

## §2.3 Why this composes

The Fixy DSL gets stronger guarantees on top of Crucible's existing type discipline:

1. **Session types** ensure protocol correctness. A custom user CC can't violate the protocol it's plugged into — the session type rejects incompatible signatures at the mint site.
2. **Row-typed effects** ensure capability correctness. A user strategy that needs `effects::IO + effects::Block` but is composed into a `Pure` row context is a compile error.
3. **Permission tokens** ensure ownership correctness. User code receiving a `Permission<Tag>` cannot duplicate it; the linear discipline composes through the strategy boundary.
4. **Universal Mint Pattern** ensures construction-time validation. Every cross-tier composition factory is a `mint_X(ctx, ...) requires CtxFitsX<...>` — the requires clause is the soundness gate.
5. **Reflection** allows generic strategy authors to write code that adapts to any Cog set without per-Cog specialization.

## §2.4 The "FSDPv3 problem" — why composition matters

In PyTorch, writing FSDPv3 requires:
- Forking PyTorch
- Modifying `_flat_param.py` (sharding logic, ~3000 LOC)
- Modifying `flat_param_handle.py` (collectives, ~2000 LOC)
- Modifying `c10d` (process groups, C++)
- Modifying NCCL bindings (C++)
- Recompiling
- Maintaining the fork forever

In Fixy, writing a hypothetical FSDPv3 looks like:

```cpp
// User code, no Crucible modification needed:
struct FSDPv3 : fixy::DistributedStrategy {
    template <fixy::ModelLike M, fixy::MeshLike Mesh>
    constexpr auto shard(M&& model, Mesh mesh) const noexcept
        -> fixy::Computation<fixy::Row<fixy::Effect::Bg>, fixy::ShardedModel<M, Mesh>>
    {
        // Per-parameter sharding with custom strategy
        return fixy::for_each_parameter(std::forward<M>(model),
            [mesh](auto& param) {
                if constexpr (fixy::is_attention_param_v<decltype(param)>) {
                    return fixy::shard_2d<Mesh::tp_axis, Mesh::dp_axis>(param);
                } else if constexpr (fixy::is_mlp_param_v<decltype(param)>) {
                    return fixy::shard_along<Mesh::tp_axis>(param);
                } else {
                    return fixy::shard_along<Mesh::dp_axis>(param);
                }
            });
    }

    template <fixy::ShardLike S>
    constexpr auto all_gather(S&& shard) const noexcept
        requires fixy::IsSharded<S>
    {
        return fixy::all_gather<
            schedule = fixy::Schedule::Overlapped,
            chunks = 4,
            sm_budget = fixy::SmBudget<8>{},
            recipe = fixy::recipe::BITEXACT_TC
        >(std::forward<S>(shard));
    }
    // ... reduce_scatter, etc.
};

// Use it:
fixy::train<FSDPv3>(model, dataset, mesh, recipe);
```

The user wrote ~50 lines of C++ DSL. No Crucible changes. The strategy composes with all existing Cogs, all existing transports, all existing congestion control. Type-system verifies it composes correctly. If user's `all_gather` violates a session-type contract (e.g., wrong duality, wrong row), the compile fails at the mint site with a routed diagnostic per the existing safety/Diagnostic.h discipline.

This is the **load-bearing architectural goal**. Every primitive enumerated in this doc is designed to support this composition shape.

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §3. The Cog Substrate

## §3.1 Why Cog replaces Relay

The existing CLAUDE.md describes Relays — "compute nodes inhabited by Crucible daemons, mortal, replaceable." This is correct for the conceptual layer but operationally too coarse. A modern compute node is a heterogeneous mix:

- 8 GPU dies (each independently failing, independently throttling)
- 2 CPU sockets (each with its own NUMA, memory channels, PCIe lanes)
- 8-16 NIC ports (each with its own line rate, queues, QPs)
- 4-8 NVSwitches (each independently failing)
- 30+ NVMe drives (each with independent wear, namespaces, queues)
- 6+ PCIe root complexes (each constraining GPUDirect peer access)
- Multiple PSUs (with independent failure modes and power budgets)
- Various sensors, BMC, fans

Treating "the DGX" as one Relay hides:
- Per-component failure domains
- Per-component resource budgets (SM count per GPU, queue count per NIC, channels per memory controller)
- Per-component health signals (one GPU XID doesn't kill the others; one transceiver flap doesn't kill the others)
- Per-component scheduling decisions (NUMA-local kernel placement, GPUDirect-eligible NIC pairing)
- Per-component wear and reliability tracking

**Cog** is the atomic addressable hardware unit. Every component that can independently fail, throttle, or be scheduled to gets a Cog identity.

## §3.2 The Cog hierarchy (L0..L7)

```
L0 — atomic Cog
     One GPU die / one NIC port / one CPU core / one DRAM channel /
     one NVMe namespace / one optical transceiver / one PSU rail /
     one NVSwitch / one PCIe lane group / one BMC sensor.
     Smallest independently-failing or independently-schedulable unit.

L1 — component Cog
     One GPU package (multi-die: B100 has 2 dies + 8 HBM stacks) /
     one CPU socket (multi-core + memory controllers + LLC) /
     one NIC card (multi-port) / one NVMe drive (multi-namespace) /
     one rack PSU (multi-rail).
     Aggregation that maps to a single physical part you can replace.

L2 — board Cog
     One PCIe root complex (CPU + GPUs + NICs sharing PCIe lanes) /
     one motherboard / one PCB substrate.
     Aggregation bounded by physical interconnect topology.

L3 — chassis Cog
     One server / DGX / OCP node / hyperscale custom box.
     The unit you typically deploy or RMA as one piece.

L4 — rack Cog
     One rack (multiple chassis + ToR switch + rack-level PDU).
     The unit you typically purchase or fault-isolate.

L5 — row Cog
     One row of racks + spine switches.
     Aggregation at network spine layer.

L6 — hall Cog
     One DC hall + aggregation switches.
     Aggregation at DC fabric layer.

L7 — datacenter Cog
     One DC + WAN routers.
     Top-level aggregation.

(L8+ would be region / continent / planet — reserved for federation.)
```

## §3.3 Cog identity

Each Cog has:

```cpp
namespace crucible::cog {

struct CogIdentity {
    // Stable identity (persists across reboots)
    Uuid       uuid;                    // generated at first calibration
    CogLevel   level;                   // L0 .. L7
    CogKind    kind;                    // Gpu, NicPort, CpuCore, ...

    // Hardware fingerprint (changes if firmware updates)
    Tagged<std::string, source::Vendor> vendor;
    Tagged<std::string, source::Vendor> model;
    Tagged<uint64_t, source::Vendor>    firmware_revision;
    Tagged<uint64_t, source::Vendor>    bios_revision;

    // Position in topology graph
    Optional<CogIdentity> parent;       // L0 GPU's parent is L1 GPU package
    std::span<const CogIdentity> children;
    std::span<const TopologyEdge> neighbors_l2;  // direct hardware peers
    std::span<const TopologyEdge> neighbors_l3;  // network peers
};

}  // namespace crucible::cog
```

Identity is content-addressed at the level of `(uuid, firmware_revision, bios_revision)` — when firmware updates, the effective Cog identity for KernelCache lookup changes (because compiled kernels may rely on firmware-specific opcode latencies), but the Cog UUID stays the same for operational tracking.

## §3.4 TargetCaps per Cog

Each Cog publishes its capabilities. For an L0 GPU Cog:

```cpp
namespace crucible::cog {

struct GpuTargetCaps {
    // Compute resources
    Refined<positive, uint16_t> sm_count;        // 132 on H100, 192 on B200
    Refined<positive, uint16_t> warp_schedulers_per_sm;
    Refined<positive, uint32_t> registers_per_sm; // 65536 on H100
    Refined<positive, uint32_t> smem_per_sm_kb;   // 228 on H100
    Refined<positive, uint32_t> l2_size_mb;       // 50 on H100

    // Memory resources
    Refined<positive, uint64_t> hbm_bytes;
    Refined<positive, uint64_t> hbm_bandwidth_gbps;

    // Interconnect
    Refined<positive, uint8_t>  nvlink_lanes;
    Refined<positive, uint64_t> nvlink_bandwidth_gbps;
    Refined<positive, uint8_t>  pcie_gen;
    Refined<positive, uint8_t>  pcie_lanes;

    // Compute throughput (calibrated)
    Tagged<double, source::Calibrated> tflops_fp64;
    Tagged<double, source::Calibrated> tflops_fp32;
    Tagged<double, source::Calibrated> tflops_tf32;
    Tagged<double, source::Calibrated> tflops_fp16;
    Tagged<double, source::Calibrated> tflops_bf16;
    Tagged<double, source::Calibrated> tflops_fp8;
    Tagged<double, source::Calibrated> tops_int8;

    // Power/thermal envelope
    Refined<positive, uint16_t> tdp_watts;
    Refined<positive, uint8_t>  thermal_throttle_celsius;

    // Feature flags
    Bits<GpuFeatures> features;
    // GpuFeatures: TMA, ClusterLaunch, FP8, BF16, TF32, NVLINK_SHARP,
    //              GPUDirect_RDMA, GPUDirect_Storage, MIG, ...
};

struct NicPortTargetCaps {
    // Link
    Refined<positive, uint64_t> line_rate_gbps;
    Refined<positive, uint16_t> mtu_bytes;
    Tagged<std::string, source::Vendor> link_layer;  // Ethernet, InfiniBand

    // Queues
    Refined<positive, uint16_t> max_tx_queues;
    Refined<positive, uint16_t> max_rx_queues;
    Refined<positive, uint32_t> max_qp_count;        // RDMA
    Refined<positive, uint32_t> max_cq_count;        // RDMA
    Refined<positive, uint32_t> max_mr_count;        // RDMA
    Refined<positive, uint32_t> max_mr_size_bytes;

    // Effective capacity (calibrated, updated under load)
    Tagged<double, source::Calibrated> effective_bandwidth_gbps;
    Tagged<double, source::Calibrated> sysctl_throughput_ceiling_gbps;
    Tagged<double, source::Calibrated> bdp_ceiling_gbps;

    // Feature flags
    Bits<NicFeatures> features;
    // NicFeatures: TSO, GSO, GRO, LRO, RSS, RoCE, iWARP, kTLS,
    //              GPUDirect_RDMA, XDP_Native, XDP_Offload, AF_XDP,
    //              SRIOV, Macsec, IPsec, TimestampingHw, ...

    // PCIe affinity
    Refined<positive, uint8_t>  pcie_root_complex_id;
    std::span<const CogIdentity> gpu_direct_peers;  // GPUs on same root
};

}  // namespace crucible::cog
```

Each `TargetCaps` is **per-Cog**, not per-class. Two B200 GPUs in the same chassis can have different `tflops_fp16` if they were measured at different thermal headroom. Manufacturing variation is real and the Cog model captures it.

## §3.5 OpcodeLatencyTable per Cog

Each Cog has a calibrated latency table for the opcodes it supports:

```cpp
namespace crucible::cog {

struct OpcodeLatencyEntry {
    OpcodeId      opcode;
    uint32_t      latency_cycles;       // measured under nominal load
    uint32_t      latency_ns_p50;
    uint32_t      latency_ns_p99;
    uint32_t      latency_ns_p999;
    double        throughput_per_sec;   // sustained, not peak
};

template <CogKind Kind>
struct OpcodeLatencyTable {
    Tagged<std::span<const OpcodeLatencyEntry>, source::Calibrated> entries;
    Stale<double> calibration_age_seconds;  // re-calibrate if stale
};

}  // namespace crucible::cog
```

For GPU Cogs, opcodes include: GEMM at various sizes, SDPA at various seq lens, all_reduce ring/tree at various N and message sizes, NVLink P2P read/write, PCIe peer access, kernel launch, doorbell ring, completion polling.

For NIC Cogs, opcodes include: RDMA WRITE / SEND / READ at various sizes, completion polling, QP create/destroy, MR register/deregister, doorbell ring, TCP send/recv, AF_XDP enqueue/dequeue.

For switch Cogs, opcodes include: per-port forwarding latency, per-flow ACL match, in-network reduction (if SHARP-capable).

These tables feed the Forge Phase J (FUSE) and Phase K (cost estimation) stages. The discrete-search partition optimizer reads them when picking a placement.

## §3.6 Failure isolation per Cog

The Cog hierarchy gives precise failure-isolation semantics:

| Failure | Cog quarantined | Parent affected? | Sibling affected? |
|---|---|---|---|
| GPU XID error | L0 GPU die | L1 package partial degrade | No |
| NIC link down | L0 NIC port | L1 NIC card partial degrade | Other ports OK |
| ECC uncorrectable | L0 DRAM channel | L1 socket partial degrade | Other channels OK |
| NVMe failure | L0 NVMe namespace | L1 drive partial degrade | Other namespaces OK |
| NVSwitch failure | L0 NVSwitch | L3 chassis partial degrade | Half of GPU mesh isolated |
| PSU rail failure | L0 PSU rail | L4 rack partial degrade | Redundant rail covers |
| ToR switch failure | L4 rack | L5 row partial degrade | Other racks rerouted |
| Spine switch failure | L5 row | L6 hall partial degrade | Other rows rerouted |
| DC fiber cut | L7 datacenter | (federation) | Cross-DC reroute |

When an L0 Cog quarantines, its parent's health score drops proportionally (1/N for each child quarantined). When a parent's score drops below threshold, the parent itself enters suspect state and the optimizer routes around it preemptively.

## §3.7 Per-Cog Mimic instance

Each L0 compute Cog (GPU, CPU socket) runs its own Mimic instance. The Mimic instance compiles kernels targeting that specific Cog's `(GpuTargetCaps, OpcodeLatencyTable, firmware_revision, bios_revision)` quadruple. Compiled kernels go into a Cog-local KernelCache plus the federated cache (shared across Cogs of same target_caps_class).

This means:
- A kernel compiled for one B200 may not be byte-identical to a kernel compiled for another B200 (different firmware revisions, different calibrated occupancy choices)
- The KernelCache key includes target_caps_class to enable sharing within compatibility class
- Cross-Cog kernel emission for collectives is one logical IR002 op compiled to N coordinated IR003* kernels (one per participating Cog)

For NIC Cogs, Mimic emits different things: RDMA verb sequences, AF_XDP packet templates, eBPF programs for XDP. Same content-addressing scheme.

## §3.8 Cog wear and lifetime tracking

Every Cog has a wear estimate that accumulates over its operational lifetime:

```cpp
namespace crucible::cog {

struct WearEstimate {
    // Operational counters (monotonic)
    Monotonic<uint64_t> total_uptime_seconds;
    Monotonic<uint64_t> total_compute_cycles;
    Monotonic<uint64_t> total_thermal_cycles;       // count of high→low temp transitions
    Monotonic<uint64_t> total_power_transients;    // count of >50W/ms transitions
    Monotonic<uint64_t> total_ecc_corrected;
    Monotonic<uint64_t> total_xid_errors;

    // Estimated wear (heuristic until vendor calibration data exists)
    Tagged<double, source::Heuristic> capacitor_wear_fraction;  // 0.0 .. 1.0
    Tagged<double, source::Heuristic> die_thermal_wear_fraction;
    Tagged<double, source::Heuristic> nand_program_erase_wear_fraction;  // for NVMe Cogs
    Tagged<double, source::Heuristic> optical_decay_fraction;  // for transceiver Cogs

    // Predicted remaining useful life
    Stale<Duration> estimated_rul;
};

}  // namespace crucible::cog
```

The wear estimate is **honest about its accuracy**. Without vendor-published wear models, our estimates are heuristic. The wear-based scheduling policies treat this as advisory until calibration data exists. The doc commits to:
- Tracking the operational counters precisely (they're observable from telemetry)
- Building empirical wear models over time as field failures correlate with counter trajectories
- Exposing wear-based policies in Fixy DSL even when the underlying estimate is best-effort

## §3.9 Cog DSL surface in Fixy

Users address Cogs by hierarchy + filter:

```cpp
// "All H100 GPUs in datacenter A, healthy, with at least 50GB free HBM"
auto cogs = fixy::cog_query<fixy::CogKind::Gpu>{}
    .where(fixy::cog_attribute::vendor      == "NVIDIA")
    .where(fixy::cog_attribute::model       == "H100-SXM5-80GB")
    .where(fixy::cog_attribute::datacenter  == datacenter_a)
    .where(fixy::cog_attribute::health      >= fixy::Health::Healthy)
    .where(fixy::cog_attribute::hbm_free_gb >= 50)
    .resolve();

// "All NIC ports on the same PCIe root complex as gpu_cog, capable of RDMA"
auto nic_cogs = fixy::cog_query<fixy::CogKind::NicPort>{}
    .where(fixy::cog_attribute::pcie_root == fixy::pcie_root_of(gpu_cog))
    .where(fixy::cog_attribute::features.has(fixy::NicFeatures::RoCE))
    .resolve();
```

Cog queries return `std::span<const CogIdentity>` and are zero-cost — the topology graph is in-process state, queries are filter+lookup. No syscall, no network round-trip.

## §3.10 Crucible substrate vs Fixy DSL split for Cogs

| Concern | Substrate (`crucible/cog/`) | Fixy DSL (`fixy/cog/`) |
|---|---|---|
| Cog identity types | `CogIdentity`, `CogKind`, `CogLevel` | (uses) |
| TargetCaps types | `GpuTargetCaps`, `NicPortTargetCaps`, etc. | (uses) |
| OpcodeLatencyTable | per-vendor population in `mimic/{nv,am,...}` | (uses) |
| WearEstimate | counters + heuristic models | wear-aware policies |
| Topology graph storage | `topology/CogGraph.h` | composition queries |
| Cog discovery | `cog/Discovery.h` (LLDP, PCIe, lspci) | composition into init flow |
| Cog calibration | `cog/Calibrate.h` (microbenchmarks) | scheduling on calibration trigger |
| Cog quarantine | `cog/Quarantine.h` (state machine) | per-deployment quarantine policy |
| Per-Cog Mimic instance | `mimic/CogMimic.h` (per-Cog compiler) | strategy-driven kernel selection |
| Cog query DSL | (none) | `fixy::cog_query<>` |

The pattern: **types and mechanisms in crucible/, queries and policies in fixy/**.

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §4. Resource Budgeting on Shared Hardware

## §4.1 The shared-resource problem

Modern GPU is one big shared-resource pool. H100 has 132 SMs; B200 has 192. Whatever the operation — GEMM, attention, reduction, NVLink P2P read, NIC doorbell ring through GPUDirect — it consumes some subset of those SMs for some duration. Same applies to:

- HBM bandwidth (3.35 TB/s on H100, 8 TB/s on B200, shared by all SMs)
- L2 cache (50 MB on H100, shared)
- Register file (64KB per SM, partitioned among warps)
- Shared memory (228KB per SM)
- NVLink bandwidth (900 GB/s on H100, shared by P2P + NCCL)
- PCIe bandwidth (Gen5 ×16 = 64 GB/s, shared by host transfer + GPUDirect)

When NCCL allocates 32 channels for an all-reduce on a 132-SM GPU, it consumes 24% of compute capacity for the duration of the collective. If overlap with compute is imperfect, that 24% is paid with no benefit. Today's stacks handle this with runtime scheduling — CUDA streams, channel allocation, completion polling. **None of it is type-system-visible.** Two consequences:

1. Bugs are runtime. Over-subscribe SMs across collectives + compute, get throughput collapse no test catches.
2. Optimization is opaque. Choosing channels-per-collective is per-deployment guesswork.

## §4.2 The row-typed budget

Crucible's row-typed effects already encode capabilities. Extending the row vocabulary to include resource consumption lets the compiler reason about budgets.

```cpp
namespace crucible::effects {

// Resource consumption tags carried in the row
template <uint32_t N> struct SmBudget        { static constexpr uint32_t value = N; };
template <uint64_t Bytes> struct HbmBytes    { static constexpr uint64_t value = Bytes; };
template <uint64_t BPS> struct HbmBandwidth  { static constexpr uint64_t value = BPS; };
template <uint64_t BPS> struct NvlinkBandwidth { static constexpr uint64_t value = BPS; };
template <uint32_t N> struct NicQp           { static constexpr uint32_t value = N; };
template <uint32_t N> struct NicMr           { static constexpr uint32_t value = N; };
template <uint32_t Bytes> struct SmemBytes   { static constexpr uint32_t value = Bytes; };
template <uint32_t N> struct RegistersPerWarp { static constexpr uint32_t value = N; };
template <uint64_t Watts> struct PowerWatts  { static constexpr uint64_t value = Watts; };

}  // namespace crucible::effects
```

A compute op declares its consumption in its row:

```cpp
template <auto M, auto N, auto K>
auto gemm(...) -> Computation<
    Row<SmBudget<128>,
        HbmBytes<M*K*2 + K*N*2>,    // input bytes
        HbmBandwidth<M*N*K*2 / 1>,   // estimated bandwidth use
        SmemBytes<48 * 1024>,        // shared memory per CTA
        RegistersPerWarp<48>,
        PowerWatts<400>>,
    Tensor<M, N>>;
```

A network op declares its consumption similarly:

```cpp
template <auto N>
auto all_reduce_ring(...) -> Computation<
    Row<SmBudget<8>,                 // 8 SMs for reduction kernels
        HbmBandwidth<MessageSize / Latency>,
        NvlinkBandwidth<MessageSize * (N-1) / N>,
        NicQp<1>,                    // one QP per peer
        NicMr<1>,                    // one MR for the buffer
        PowerWatts<50>>,
    Tensor<...>>;
```

## §4.3 Compile-time budget verification

When two ops are scheduled concurrently on the same Cog, the row-union sums consumption:

```cpp
template <typename Row1, typename Row2>
struct concurrent_resource_sum {
    static constexpr uint32_t sm = Row1::sm + Row2::sm;
    static constexpr uint64_t hbm_bandwidth = Row1::hbm_bandwidth + Row2::hbm_bandwidth;
    static constexpr uint64_t nvlink_bandwidth = Row1::nvlink_bandwidth + Row2::nvlink_bandwidth;
    static constexpr uint32_t nic_qp = Row1::nic_qp + Row2::nic_qp;
    // ... etc
};

// At compile time, given a target Cog:
template <CogIdentity Cog, typename Row>
concept FitsCog = requires {
    requires Row::sm <= Cog::caps.sm_count;
    requires Row::hbm_bandwidth <= Cog::caps.hbm_bandwidth_gbps * 1e9;
    requires Row::nvlink_bandwidth <= Cog::caps.nvlink_bandwidth_gbps * 1e9;
    requires Row::nic_qp <= Cog::caps.max_qp_count;
    // ...
};
```

When a Fixy strategy schedules concurrent ops and their summed budget exceeds the Cog's caps, compile fails with a routed diagnostic naming the resource that overflowed.

## §4.4 Why not runtime?

Runtime budget enforcement (today's NCCL channel allocation) has three problems:

1. **No cross-stack visibility.** NCCL doesn't know what GEMM the user is running concurrently. The user's compiler doesn't know what NCCL channels are allocated. Each one over-budgets in isolation.
2. **No early failure.** Over-budget conditions manifest as gradual throughput degradation, not loud errors. Bugs hide.
3. **No optimization input.** Without knowing actual budgets, the optimizer can't make informed scheduling decisions.

Compile-time budgeting fixes all three. The cost: every op author must declare consumption. In practice this is a one-time annotation per kernel template that the compiler refines via Mimic's calibration data.

## §4.5 The 21 resource axes

Full enumeration of resources that should appear in the row vocabulary:

| # | Resource | Per-Cog scope | Per-op declaration |
|---|---|---|---|
| 1 | SM count | GPU L0 | `SmBudget<N>` |
| 2 | Warp scheduler slots | GPU L0 | implicit in SmBudget |
| 3 | Register file | GPU L0 | `RegistersPerWarp<N>` |
| 4 | Shared memory | GPU L0 | `SmemBytes<N>` |
| 5 | L2 cache | GPU L0 | `L2Bytes<N>` |
| 6 | HBM capacity | GPU L0 | `HbmBytes<N>` |
| 7 | HBM bandwidth | GPU L0 | `HbmBandwidth<BPS>` |
| 8 | NVLink lanes / bandwidth | GPU L0 | `NvlinkBandwidth<BPS>` |
| 9 | PCIe bandwidth | PCIe root L2 | `PcieBandwidth<BPS>` |
| 10 | NIC TX/RX queues | NIC L0 | `NicQueueBudget<N>` |
| 11 | NIC ring buffer depth | NIC L0 | `NicRingDepth<N>` |
| 12 | NIC QP slots (RDMA) | NIC L0 | `NicQp<N>` |
| 13 | NIC CQ slots (RDMA) | NIC L0 | `NicCq<N>` |
| 14 | NIC MR registrations (RDMA) | NIC L0 | `NicMr<N>` |
| 15 | Switch egress port BW | Switch port | `SwitchEgressBw<BPS>` |
| 16 | Switch buffer cells | Switch | `SwitchBufferCells<N>` |
| 17 | TCAM entries | Switch | `TcamEntries<N>` |
| 18 | CPU cores | CPU socket L1 | `CpuCoreBudget<N>` |
| 19 | LLC cache | CPU socket L1 | `LlcBytes<N>` |
| 20 | Power budget | GPU/NIC/CPU | `PowerWatts<N>` |
| 21 | Thermal headroom | GPU L0 | `ThermalCelsius<N>` |

Plus carbon and rack-power for L4+ Cogs:

| 22 | Rack power | L4 rack | `RackPowerKw<N>` |
| 23 | Carbon intensity | L7 datacenter | `CarbonGramsPerKwh<N>` |

## §4.6 Adaptive budget refinement

Static row-typed budgets need not be conservative. Mimic measures actual consumption at calibration time and refines the kernel's row metadata. A `gemm` declared as `SmBudget<128>` may actually use only 96 SMs in practice; Mimic detects this from the compiled binary's launch parameters and refines the row to `SmBudget<96>`. The refined row is what the optimizer schedules against.

The refinement is per-Cog: the same kernel may use different SM counts on different Cogs depending on calibration. The KernelCache key includes the refined budget, so different budget variants are different cache entries.

## §4.7 Crucible substrate vs Fixy DSL split for budgets

| Concern | Substrate | Fixy DSL |
|---|---|---|
| Resource tag types | `effects/Resources.h` | (uses) |
| Concurrent-row union | `effects/Concurrent.h` | (uses) |
| `FitsCog<Cog, Row>` concept | `cog/FitsCog.h` | (uses) |
| Per-kernel row declaration | template metadata | (uses) |
| Adaptive refinement | Mimic + KernelCache | (uses) |
| Scheduling decisions | (none) | `fixy::concurrent`, `fixy::overlap`, etc. |
| Composite MFU objective | (none) | `fixy::Mfu` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §5. Adaptive MFU — Multi-Timescale Control Loop

## §5.1 The MFU framing

Model FLOPs Utilization (MFU) is the ratio of useful compute delivered to theoretical peak. For the user's 100M model on B200 case, MFU is ~3% — the GPU sits idle 97% of wall-clock time. For well-tuned LLM training on A100/H100, MFU is typically 40-60%. SOTA published numbers reach 70-80% (DeepSeek-V3 on H800 cluster, GPT-4 era Megatron on A100).

But MFU is **not the only objective**. A training run that achieves 80% MFU while burning out GPUs in 6 months is worse than 70% MFU with 3-year hardware lifetime. The objective is composite:

```
composite_MFU =
      (compute_MFU)^α
    × (1 - power_overrun_penalty)^β
    × (1 - wear_acceleration)^γ
    × (1 - thermal_violation_rate)^δ
    × (1 - di_dt_violation_rate)^ε
    × (1 - sla_violation_rate)^ζ
```

Each `^*` is operator-tunable. Research workload: high α, low γ. Long-duration production training: balanced. Reliability-critical: high δ + ε. The exponents go in the Fixy job spec; the optimizer treats the composite as the objective.

## §5.2 Three timescales

The optimizer runs on three independent timescales:

| Timescale | Frequency | Scope | Action |
|---|---|---|---|
| Per-step | ~ms | Within iteration | Augur measures, no recompile, route adjustment via existing kernels |
| Per-K-step | ~10s (configurable) | Across iterations | Re-solve SM allocation, Mimic recompile changed kernels (KernelCache hit common), atomic swap at iteration boundary |
| Per-event | ~minutes | Membership change, Cog quarantine, hardware swap, workload phase change | Topology re-discovery, full Forge re-run, full re-calibration of affected Cogs |

Each timescale is its own Fixy DSL primitive. User can override frequency, change triggers, replace optimizers entirely.

## §5.3 The per-step loop

Per-step adjustments operate within the existing kernel set. No recompile, no Forge invocation. The decisions:

- Which Cog runs which work item (load balancing within current strategy)
- Which path to use for each in-flight collective (multi-path weights)
- Which DVFS level to apply (within operator policy bounds)
- Whether to inject filler ops for power smoothing

Augur ingests telemetry every step:
- Per-Cog: SM utilization, HBM bandwidth utilization, power draw, temperature, clock frequency
- Per-NIC: TX/RX bytes, drop rate, queue depth, RDMA completion rate
- Per-collective: completion time, bytes transferred, peer completion variance
- Per-step: total step time, compute-bound vs comm-bound classification

The per-step optimizer runs in O(microseconds) on the Augur worker thread, emits decisions as updates to the active routing tables.

## §5.4 The per-K-step loop

Per-K-step adjustments may change kernel choices. Triggered by:
- Sustained MFU divergence from prediction (>5% for 100+ steps)
- Sustained imbalance (one Cog persistently slower than others)
- Workload phase change detected (e.g., entered grad checkpointing region)
- New KernelCache entry available (federation pull)

Optimizer:
1. Reads current per-Cog telemetry trajectories
2. Re-solves the SM allocation given current measured costs
3. Identifies kernels whose row metadata changed
4. Triggers Mimic recompilation for changed kernels (most are KernelCache hits)
5. Atomically swaps kernels at the next iteration boundary

Per-K-step optimization runs in O(seconds) on a background thread. Atomic swap uses the existing CrucibleContext mechanism.

## §5.5 The per-event loop

Per-event adjustments are major reorganizations. Triggered by:
- Cog quarantine (failure, wear, planned maintenance)
- Cog rejoin (recovery)
- Topology change (peer added, peer removed, link added, link removed)
- Workload spec change (user updates strategy, recipe, or mesh)
- Cipher cold-tier reconfiguration

Optimizer:
1. Triggers full topology re-discovery (LLDP, PCIe, calibration probes)
2. Updates the topology graph CRDT
3. Gossips changes to all Keepers via SWIM + scuttlebutt
4. Re-solves the entire 5D partition (TP × DP × PP × EP × CP)
5. Re-runs Forge end-to-end on the new partition
6. Re-emits affected kernels via Mimic
7. Atomically swaps at the next safe boundary (often requires draining in-flight collectives)

Per-event optimization runs in O(minutes) and is the heaviest. The system is designed so this is rare — once per major hardware change, not per training step.

## §5.6 Composite MFU as cost-model objective

The discrete-search partition optimizer (per Z3-removal cleanup, see CLAUDE.md and tasks #150, #810) minimizes:

```
cost(F, P, S) =  - composite_MFU(F, P, S)
              + barrier_overhead(F, P)
              + bubble(F)
              + estimated_recompile_cost(F, P, S)
```

Where `(F, P, S)` is the (factorization, placement, schedule) triple. The optimizer runs branch-and-bound with relative pruning (≥ 1.05× current best is pruned).

## §5.7 Out-of-scope: single-Cog kernel optimization

Single-Cog optimization (megakernel lowering, sub-graph batching, in-Cog SM replication, register pressure tuning, occupancy management) is **out of scope for this networking + distributed compute document**. It belongs to a separate single-machine optimization document.

The §5 multi-timescale loop discussed here adjusts **distributed scheduling decisions**: cross-Cog work placement, comm topology selection, multi-Cog mesh sizing, per-Cog DVFS within power-smoothing constraints, cross-Cog routing weights. Single-Cog policies are an orthogonal axis that composes with this loop but does not belong to it.

## §5.8 Crucible substrate vs Fixy DSL split for adaptive MFU

| Concern | Substrate | Fixy DSL |
|---|---|---|
| Telemetry harvesters | `cog/Telemetry.h`, `perf/Bpf.h` | (uses) |
| Augur per-step measurement | `augur/Step.h` | (uses) |
| Augur per-K-step trends | `augur/Trend.h` | (uses) |
| Atomic kernel swap mechanism | `cog/AtomicSwap.h` (existing) | (uses) |
| Discrete-search optimizer | `cog/Optimizer.h` | (uses) |
| Composite MFU computation | (none) | `fixy::Mfu` policy |
| Optimizer trigger conditions | (none) | `fixy::Trigger` predicates |
| Strategy replacement decisions | (none) | `fixy::strategy::*` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §6. Comm-Through-IR — Collectives in IR001 → IR002 → IR003*

## §6.1 The current state

Today's training stacks treat communication as a runtime library underneath the compiler:
- PyTorch + NCCL: `dist.all_reduce(tensor)` is a Python call that invokes NCCL via C++ bindings. Compiler doesn't see it.
- JAX + NCCL: `pjit` does some collective fusion at HLO level but ultimately lowers to NCCL calls.
- Megatron + NCCL: explicit collective calls in PyTorch wrapping NCCL.

The compiler sees `(GEMM, NCCL_call, GEMM)` as `(compiled_kernel, opaque_blob, compiled_kernel)`. No fusion across the boundary, no joint optimization, no cross-vendor verification.

## §6.2 The corrected architecture

Communication ops are **first-class IR001 ops**, identical in treatment to compute ops:

```
IR001 (frontend / vendor-neutral op level):
  - All compute ops: gemm, conv, attention, reduction, elementwise, ...
  - All comm ops: send, recv, all_reduce, all_gather, reduce_scatter,
                  broadcast, scatter, gather, all_to_all, all_to_all_v,
                  barrier, sparse_all_to_all, ...
  - All memory ops: copy, prefetch, evict, gather_via_tma
  - All coordination ops: atomic_add_remote, semaphore_wait, semaphore_post
  - All storage ops: load_nvme, store_nvme, checkpoint, restore
  - All telemetry ops: counter_read, sample_pmu, sample_thermal
  - All discovery ops: lldp_send, lldp_recv, swim_ping, swim_ack

IR002 (vendor-neutral kernel DAG with NumericalRecipe):
  - All_reduce becomes a KernelNode with:
      - kind = AllReduce
      - participants (Cog identities)
      - message_shape, message_dtype
      - recipe (BITEXACT_TC vs ORDERED vs UNORDERED)
      - row metadata (resource budget)
  - Same content-addressing as compute kernels

IR003* (per-vendor backend-specific):
  - mimic/nv/network/AllReduce.h emits CUDA-aware IB verbs sequence
  - mimic/am/network/AllReduce.h emits ROCm-aware RoCE verbs sequence
  - mimic/cpu/network/AllReduce.h emits sockets-based fallback
  - All produce bit-equivalent results under matching recipe
```

## §6.3 Why this works

Several gains accrue from comm-through-IR:

**(1) Content-addressable comm kernels.** `all_reduce(N=8, dtype=fp16, peers=[cog_a, cog_b, ..., cog_h], recipe=BITEXACT_TC, target=NV_HOPPER_RACK_LOCAL)` becomes a content_hash. Compiled variants for that exact configuration land in KernelCache. Cross-fleet federation shares them. Every operator's run enriches everyone else's cache.

**(2) MAP-Elites over comm variants.** Behavior axes for comm:
- chunk size (256B, 1KB, 4KB, 16KB, 64KB, 256KB, 1MB, 4MB)
- topology (ring, tree, recursive halving-doubling, direct, hierarchical)
- SM allocation (1, 2, 4, 8, 16, 32, 64)
- NIC parallelism (1, 2, 4, 8 NICs in parallel per direction)
- in-network aggregation (SHARP enable / disable)
- completion polling (busy-poll, event, hybrid)
- FEC overhead (none, 5%, 10%, 20%)
- compression (none, LZ4, zstd-fast, custom-quantize)

Same MAP-Elites search machinery as compute kernels. Best variant per (op, target_caps_class, peer_set, message_size) found empirically by simulating + measuring.

**(3) Cross-vendor CI extends to comm.** Same `all_reduce` on NV vs AM produces bit-equivalent result under BITEXACT_TC. NCCL doesn't promise this; OpenMPI doesn't either; RCCL is a fork of NCCL. Crucible's recipe-tier promise (MIMIC.md §41) generalizes: every IR002 comm kernel × NumericalRecipe × backend triple is compiled, executed, output-verified pairwise against CPU scalar-FMA oracle. A backend that violates tolerance fails the build.

**(4) Compute-comm fusion at Forge.** Phase D (FUSE) sees both compute and comm ops in the same graph. New fusion patterns become possible:

| Pattern | Today | Fused |
|---|---|---|
| Send-from-epilogue | GEMM writes to HBM, separate kernel reads from HBM and writes to NIC | GEMM's epilogue writes directly to NIC TX queue via Hopper TMA. No HBM round-trip on the comm payload. Saves 2× HBM bandwidth. |
| Reduce-during-recv | NIC writes to HBM, separate kernel reads + reduces | Incoming RDMA payload is summed into local accumulator on arrival. NCCL hand-codes this for ring all-reduce; in IR-aware compilers it's emitted automatically. |
| Compress-before-send | Gradient computed in HBM, copy to compression kernel, copy result to send buffer | Compression fuses into the SM kernel that produced the gradient. Single HBM write. |
| Decompress-after-recv | Receive into HBM, copy to decompression kernel, copy result to consumer | Decompression fuses with consumer kernel. |
| Scatter-from-attention | Attention output scattered across DP ranks via reduce-scatter | Attention's last operation writes directly into per-rank chunks via TMA scatter. |

**(5) Adaptive recalibration becomes possible.** Per the §5 multi-timescale loop, comm kernels are part of the optimization surface. The optimizer can choose ring vs tree per-collective per-step based on current congestion telemetry. NCCL picks tree-vs-ring once at init.

## §6.4 The IR001 comm op set

Canonical IR001 communication operations:

| Category | Op | Variants |
|---|---|---|
| Point-to-point | `send` | sync, async, with-completion, inline |
| | `recv` | sync, async, with-completion, into-existing-buffer |
| | `sendrecv` | (combined) |
| | `put` | one-sided RDMA write |
| | `get` | one-sided RDMA read |
| | `atomic` | cmp-and-swap, fetch-and-add |
| Collectives (sync) | `all_reduce` | sum, mean, max, min, custom-op |
| | `all_gather` | tensor concat, varying shapes (`_v` variant) |
| | `reduce_scatter` | sum, mean, custom-op |
| | `broadcast` | from root, multi-root |
| | `scatter` | from root |
| | `gather` | to root, varying shapes |
| | `all_to_all` | uniform, varying (`_v` variant) |
| | `barrier` | with timeout, without |
| | `sparse_all_to_all` | for MoE expert routing |
| Collectives (async) | `async_send_batch` | scatter without sync |
| | `async_recv_batch` | gather without sync |
| | `gossip_round` | random-neighbor exchange |
| | `eventual_aggregate` | DiLoCo outer sync style |
| Coordination | `barrier_with_quorum` | continue on N-of-M responses |
| | `lease_acquire` | distributed mutex |
| | `lease_release` | |
| Discovery | `lldp_send` | |
| | `swim_ping` | |
| | `swim_indirect_ping` | |
| | `scuttlebutt_delta` | |
| Telemetry | `wire_pcap_emit` | for distributed tracing |
| | `int_telemetry_emit` | in-band network telemetry |

## §6.5 Forge phases with comm awareness

Each Forge phase extends to handle comm:

| Phase | Compute treatment | Comm treatment |
|---|---|---|
| INGEST | Build IR001 from frontend | Same |
| ANALYZE | Identify reusable subgraphs, infer shapes | Identify collective patterns (broadcast-then-reduce = all_reduce, etc.) |
| REWRITE | Algebraic rewrites, fusion-prep | Comm rewrites: combine two consecutive all_reduces into one, unwrap unnecessary broadcasts |
| FUSE | Combine ops where beneficial | Fuse compute+comm boundary patterns (send-from-epilogue, reduce-on-recv, etc.) |
| LOWER_TO_KERNELS | IR001 → IR002 with NumericalRecipe pinning | Comm lowered to KernelNode with row metadata + participants |
| TILE | Pick tile shape | Pick chunk size |
| MEMPLAN | Static memory plan | Allocate send/recv staging buffers, MR registrations |
| COMPILE | Mimic per-vendor backend | Mimic per-vendor backend (network) emits verbs/eBPF/AF_XDP |
| SCHEDULE | Topo-sort with parallel streams | Topo-sort with comm streams + completion futures |
| EMIT | Write CompiledKernel + KernelCache entry | Same |
| DISTRIBUTE | (no-op for compute) | Distribute compiled kernels to participating Cogs |
| VALIDATE | Cross-vendor numerics CI | Cross-vendor + cross-recipe numerics CI for collectives |

## §6.6 Mimic per-vendor network backends

Each Mimic vendor backend gains a `network/` subdirectory:

```
mimic/nv/network/
  AllReduce.h      // CUDA-aware IB verbs (RoCEv2 or IB)
  AllGather.h
  ReduceScatter.h
  AllToAll.h
  Broadcast.h
  Send.h
  Recv.h
  PutGet.h         // one-sided
  Atomic.h
  GpuDirect.h      // GPU↔NIC bypass primitives
  Sharp.h          // in-network aggregation (Mellanox SHARP)

mimic/am/network/
  AllReduce.h      // ROCm-aware RoCE verbs
  AllGather.h
  ReduceScatter.h
  // ... mirror structure

mimic/cpu/network/
  AllReduce.h      // socket-based reference oracle
  AllGather.h
  // ...

mimic/intel/network/
  // ... GPU + IPU backends

mimic/mellanox/network/
  // ... pure NIC backend (BlueField DPU offload)

mimic/broadcom/network/
  // ... NIC-side primitives for Broadcom Tomahawk-class hardware
```

Each implementation:
- Takes IR002 KernelNode for the comm op
- Emits target-specific verb sequence / eBPF / AF_XDP code
- Stores compiled artifact in per-vendor KernelCache layer (L3)
- Reports back actual measured row metadata (refined budget) to Mimic for KernelCache update
- Is bit-equivalent to CPU oracle under matching NumericalRecipe

## §6.7 Compute-comm fusion examples

**Send-from-epilogue (Hopper TMA-aware):**

```cpp
// IR001 input:
auto y = gemm(x, w);          // compute
send(y, peer);                // comm

// After Forge Phase D FUSE (recognizes pattern):
auto y_at_peer = gemm_send_epilogue(x, w, peer);
// This becomes one Mimic kernel that:
// 1. Performs GEMM on SMs
// 2. In the epilogue, the per-CTA result tile is written
//    via TMA store directly into a NIC-registered MR
// 3. NIC TX is initiated by completion of the last CTA
// 4. No HBM round-trip on the y tensor
// MFU gain: 2× HBM bandwidth saved on the send payload
```

**Reduce-during-recv (NCCL-style, but compiler-emitted):**

```cpp
// IR001 input:
auto chunks = recv_n(peers);   // recv from N peers
auto sum = reduce(chunks);     // sum them

// After Forge Phase D FUSE:
auto sum = recv_and_reduce(peers);
// Mimic emits one kernel that:
// 1. Polls receive completions
// 2. As each recv completes, atomically adds the chunk to accumulator
// 3. No intermediate per-chunk storage in HBM
// MFU gain: avoids N HBM writes + N HBM reads of intermediate chunks
```

**Compress-before-send (gradient compression):**

```cpp
// IR001 input:
auto grad = compute_gradient(x, y);
auto compressed = topk(grad, k=1000);
send(compressed, peer);

// After Forge Phase D FUSE:
auto sent = compute_gradient_topk_send(x, y, k=1000, peer);
// Mimic emits one kernel that:
// 1. Computes gradient
// 2. Performs Top-K selection in registers
// 3. Writes only top-K values directly to NIC MR
// MFU gain: avoids storing the dense gradient anywhere
```

## §6.8 The Mimic-Cog model

Earlier I sketched "doppelganger Canopy of Mimics." More precisely:

- Each L0 compute Cog runs its own Mimic instance for compute kernels
- Each L0 NIC Cog runs its own Mimic instance for network kernels
- Cross-Cog kernels (e.g., a 16-Cog all_reduce) are emitted by all participating Cogs' Mimic instances coordinated by Forge
- The compiled artifacts are different on each Cog (each receives a different "side" of the protocol) but share content_hash
- KernelCache federation shares across Cogs of same target_caps_class

This means: compiling an `all_reduce(N=16)` is not one operation but 16 coordinated operations, one per participating Cog. Each Cog's Mimic emits its side. Forge's SCHEDULE phase ensures the sides compose correctly.

## §6.9 Crucible substrate vs Fixy DSL split for comm-through-IR

| Concern | Substrate | Fixy DSL |
|---|---|---|
| IR001 comm op definitions | `forge/Ir001/Comm.h` | (uses) |
| IR002 KernelNode comm variants | `forge/Ir002/CommKernel.h` | (uses) |
| Forge Phase D fusion patterns | `forge/Phases/Fuse.h` | (extension via user-defined patterns) |
| Mimic per-vendor network backends | `mimic/{nv,am,...}/network/` | (uses) |
| KernelCache for comm | existing KernelCache | (uses) |
| Cross-vendor CI for comm | `test/cross_vendor_comm_ci.cpp` | (uses) |
| Strategy choosing which collective to emit | (none) | `fixy::strategy::*` |
| User-defined custom collectives | (none) | `fixy::register_collective<>` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §7. The eBPF / NIC Programmability Stack

## §7.1 The 7-layer programmability stack

NIC programmability in 2026 spans seven layers, each with very different control granularity, latency, and access requirements:

```
Layer 7: Programmable switch ASICs (Tofino, Trident, Spectrum-4)
         P4 programs running on switch dataplane at terabits/sec.
         In-network aggregation, custom routing, INT telemetry.

Layer 6: DPU / SmartNIC (BlueField, Pensando, Marvell Octeon)
         Full Linux on NIC's onboard ARM cores.
         TCP offload, custom firmware, full subsystems offloaded.

Layer 5: RDMA Verbs (libibverbs)
         Userspace creates QPs/MRs/CQs, posts WRs directly to NIC.
         Zero kernel involvement on data path.
         700ns-2µs round-trip across rack with ConnectX-7.

Layer 4: AF_XDP (Linux 5.4+)
         Userspace zero-copy socket. UMEM mapped from NIC.
         500ns-2µs RX-to-userspace.

Layer 3: TC eBPF (Linux 4.1+)
         eBPF at egress and higher-level ingress.
         Rate limiting, priority queueing, action steering.

Layer 2: XDP (Linux 4.8+)
         eBPF at NIC driver RX path before SKB allocation.
         50-200ns per packet in NATIVE mode.
         OFFLOAD mode runs program on NIC firmware.

Layer 1: ethtool / sysctl / tc qdisc (every NIC)
         Configuration only — ring sizes, offloads, queues, RSS,
         qdisc, sysctl tunables.
         No programmability.
```

## §7.2 Layer 1 — ethtool / sysctl / tc qdisc

Configuration knobs available on every Linux NIC:

| Tool | Knob | What it controls |
|---|---|---|
| `ethtool -G` | TX/RX ring sizes | Descriptor counts (256-8192) |
| `ethtool -K` | Offloads | TSO, GSO, GRO, LRO, RX/TX checksum, scatter-gather |
| `ethtool -L` | Queue counts | Per-CPU steering capability |
| `ethtool -X` | RSS hash | Indirection table, hash function (Toeplitz, XOR, CRC32) |
| `ethtool -A` | Pause frames | Autonegotiation, RX/TX pause |
| `ethtool -s` | Speed/duplex | Link parameters |
| `ethtool --set-priv-flags` | Vendor-specific | Heterogeneous, NIC-specific |
| `tc qdisc` | Queueing discipline | fq, fq_codel, htb, mq, prio |
| `sysctl net.core.*` | Socket buffers | rmem_max, wmem_max, netdev_budget |
| `sysctl net.ipv4.tcp_*` | TCP behavior | congestion control, RTO_MIN, SACK, RACK-TLP |
| `sysctl net.core.busy_poll` | Busy polling | Low-latency receive |

This is **configuration only — no programmability**. Latency to apply: ms (one syscall). Latency to take effect: depends, often immediate.

Crucible substrate primitive (planned):

```cpp
// crucible/cog/NicConfig.h
namespace crucible::cog::nic {

struct EthtoolConfig {
    Refined<positive, uint16_t> tx_ring_size;
    Refined<positive, uint16_t> rx_ring_size;
    Refined<positive, uint16_t> tx_queues;
    Refined<positive, uint16_t> rx_queues;
    Bits<NicOffloads> offloads;
    RssConfig rss;
    QdiscConfig qdisc;
    SysctlConfig sysctl;
};

[[nodiscard]] auto apply_config(
    const CogIdentity& nic_cog,
    const EthtoolConfig& config
) noexcept -> std::expected<void, NicConfigError>;

[[nodiscard]] auto query_current(
    const CogIdentity& nic_cog
) noexcept -> std::expected<EthtoolConfig, NicConfigError>;

}
```

Fixy DSL surface:

```cpp
fixy::nic_config<my_nic_cog>{
    .tx_ring_size = 4096,
    .rx_ring_size = 4096,
    .tx_queues = 16,
    .rx_queues = 16,
    .offloads = fixy::NicOffloads::TSO
              | fixy::NicOffloads::GRO
              | fixy::NicOffloads::RX_CHECKSUM,
    .rss = fixy::Rss::Toeplitz<seed = 0xDEADBEEF>{},
    .qdisc = fixy::Qdisc::Fq{ .max_quantum = 8192 },
    .sysctl = {
        .tcp_congestion = "bbr3",
        .tcp_rto_min_ms = 10,
        .busy_poll_us = 50,
    }
};
```

## §7.3 Layer 2 — XDP (eXpress Data Path)

XDP runs eBPF programs at NIC driver RX path, before SKB allocation. Three modes:

| Mode | Latency | Where program runs | Hardware required |
|---|---|---|---|
| `XDP_NATIVE` | 50-200 ns/pkt | Driver code on host CPU | XDP-aware driver |
| `XDP_OFFLOAD` | line rate | NIC firmware | Netronome Agilio, some Mellanox |
| `XDP_GENERIC` | 5-10 µs/pkt | After SKB alloc, fallback | Any NIC |

XDP program return values:

| Return | Meaning |
|---|---|
| `XDP_PASS` | Continue to network stack |
| `XDP_DROP` | Discard immediately |
| `XDP_TX` | Bounce out the same NIC (TX-as-RX trick) |
| `XDP_REDIRECT` | To another NIC, AF_XDP socket, or CPU map |
| `XDP_ABORTED` | Error case, dropped + traced |

Programmability: full eBPF-restricted-C with verifier. Maps for state (per-CPU, hash, LRU, ringbuf). Helper functions for crypto, tunneling, FIB lookup. Tail calls. Limit: 1M instructions per program, no unbounded loops, verifier-proven safe.

Crucible substrate primitive (planned, GAPS-130):

```cpp
// crucible/perf/Xdp.h
namespace crucible::perf {

template <auto ProgramFnPtr>
class XdpProgram {
    // Compiled eBPF bytecode, generated from C++ at compile time
    // via Mimic-CPU-eBPF backend (similar discipline to Mimic for GPU)
public:
    auto attach(const CogIdentity& nic_cog, XdpMode mode) noexcept
        -> std::expected<XdpAttachment, XdpError>;
    auto detach(XdpAttachment attachment) noexcept -> void;
};

template <typename Key, typename Value>
class BpfMap {
public:
    auto lookup(const Key&) const noexcept -> std::optional<Value>;
    auto update(const Key&, Value) noexcept -> std::expected<void, BpfError>;
    auto delete_entry(const Key&) noexcept -> std::expected<void, BpfError>;
};

}
```

Fixy DSL surface:

```cpp
// User defines an eBPF program in regular C++
// (subset that compiles to eBPF bytecode via Mimic CPU backend)
auto my_filter = [](XdpContext& ctx) -> XdpAction {
    auto* eth = ctx.eth_header();
    if (eth->h_proto != htons(ETH_P_IP)) return XdpAction::Pass;

    auto* ip = ctx.ip_header();
    if (auto entry = blocklist.lookup(ip->saddr)) {
        return XdpAction::Drop;
    }

    return XdpAction::Pass;
};

fixy::xdp_attach<my_filter>(my_nic_cog, fixy::XdpMode::Native);
```

The program is compiled to eBPF bytecode at C++ compile time. Type system verifies the program meets eBPF verifier constraints (no unbounded loops, bounded stack, verified pointer arithmetic).

## §7.4 The TX-as-RX trick (XDP_TX)

A subtle but powerful XDP capability: when a program returns `XDP_TX`, the packet is bounced back out the same NIC port. This lets you do reflection, NAT-style rewriting, or packet replication entirely in kernel, with no userspace involvement.

Production uses:
- **Service load balancer (Cilium, Katran).** Receive packet, rewrite destination, XDP_TX. ~250 ns vs ~10 µs traditional.
- **DDoS mitigation reflection.** Receive bad packet, send rejection back via XDP_TX without ever entering userspace.
- **Custom protocol gateway.** Receive packet on management VLAN, rewrite, XDP_TX onto data VLAN.

Crucible application: **gossip multicast emulation.** Without IP multicast (broken in most modern datacenters), the standard pattern is N-1 unicast sends. XDP_TX can replicate one received packet to N destinations entirely in kernel:

```cpp
auto gossip_replicator = [](XdpContext& ctx) -> XdpAction {
    auto* hdr = ctx.gossip_header();
    if (hdr->magic != GOSSIP_MAGIC) return XdpAction::Pass;

    // Look up neighbors for this gossip topic
    auto neighbors = neighbor_map.lookup(hdr->topic);
    if (!neighbors) return XdpAction::Drop;

    // Replicate to each neighbor (cloning the packet)
    for (auto& neighbor : *neighbors) {
        ctx.clone_and_redirect(neighbor.ifindex, neighbor.dst_mac);
    }

    return XdpAction::Drop;  // original packet doesn't continue
};
```

Result: gossip multicast at line rate, no kernel context switch, no syscall, no userspace involvement. 100k peers can be reached from one origin in O(N) packets emitted by the NIC.

## §7.5 Layer 3 — TC eBPF

Same eBPF runtime as XDP but attached at egress (TX) and at higher-level ingress (after packet reassembly, before stack delivery). More flexible because runs on already-parsed packets, slightly slower than XDP.

Use cases: rate limiting per flow, priority queueing, action-driven steering, QoS marking.

Crucible substrate primitive (planned, partially subsumed by GAPS-130):

```cpp
// crucible/perf/TcEbpf.h
namespace crucible::perf {

template <auto ProgramFnPtr>
class TcProgram {
public:
    auto attach_egress(const CogIdentity& nic_cog) noexcept
        -> std::expected<TcAttachment, TcError>;
    auto attach_ingress(const CogIdentity& nic_cog) noexcept
        -> std::expected<TcAttachment, TcError>;
};

}
```

## §7.6 Layer 4 — AF_XDP

Userspace zero-copy socket. NIC RX queue is mapped directly into userspace via UMEM. Application reads packets from a ring buffer; writes to TX ring sends them out the NIC. No kernel copy, no driver SKB allocation.

Latency: ~500 ns to 2 µs RX-to-userspace (vs ~10-20 µs for traditional sockets).

Trade-off: userspace must implement protocol parsing. Good for custom transports (Crucible's networking primitives), bad for general-purpose stacks.

Combinable with XDP_REDIRECT to send specific flows to AF_XDP while normal traffic goes through the kernel stack.

Crucible substrate primitive (planned, GAPS-131):

```cpp
// crucible/cntp/AfXdp.h
namespace crucible::cntp {

template <uint32_t UmemSizeBytes>
class AfXdpSocket : Pinned {
    // UMEM is hugepage-backed memory shared with NIC
    safety::Linear<safety::AlignedBuffer> umem_;
    AfXdpRings rings_;

public:
    [[nodiscard]] auto enqueue_tx(safety::Borrowed<uint8_t, AfXdpSocket> packet) noexcept
        -> std::expected<void, AfXdpError>;

    [[nodiscard]] auto dequeue_rx() noexcept
        -> std::optional<safety::Borrowed<uint8_t, AfXdpSocket>>;

    [[nodiscard]] auto poll(Duration timeout) noexcept -> uint32_t;
};

[[nodiscard]] auto mint_af_xdp_socket(
    effects::Init init,
    const CogIdentity& nic_cog,
    AfXdpConfig config
) noexcept -> std::expected<AfXdpSocket<UmemSizeBytes>, AfXdpError>;

}
```

Fixy DSL surface:

```cpp
auto sock = fixy::af_xdp_socket<umem_size = 64_MB, queue = 0>(my_nic_cog);

// Send a packet
auto tx_buf = sock.alloc_tx_buffer(1500);
fill_packet(*tx_buf);
sock.enqueue_tx(std::move(*tx_buf));

// Receive packets
while (auto rx_buf = sock.dequeue_rx()) {
    process_packet(*rx_buf);
}
```

## §7.7 Layer 5 — RDMA Verbs

For RoCE and InfiniBand NICs (ConnectX-5/6/7, BlueField, Intel E810 with iWARP). Userspace creates Queue Pairs (QPs), Memory Regions (MRs), Completion Queues (CQs), posts Work Requests (WRs) directly to NIC hardware.

- Kernel involvement: MR registration (slow, ms), QP creation (slow, ms)
- After setup: zero kernel involvement on data path
- Latency: ~700 ns to 2 µs round-trip across rack with ConnectX-7

Verbs primitives:
- `SEND` — two-sided message, peer must POST_RECV
- `RECV` — receive into pre-posted buffer
- `RDMA_WRITE` — one-sided, write to peer's registered memory
- `RDMA_WRITE_WITH_IMM` — write + 4-byte immediate (notification)
- `RDMA_READ` — one-sided, read from peer's registered memory
- `ATOMIC_CMP_AND_SWAP` — one-sided atomic
- `ATOMIC_FETCH_AND_ADD` — one-sided atomic

Plus features:
- Selective signaling (only some WRs generate completions)
- Inline data (small WRs embed payload in WR itself)
- Solicited/unsolicited events
- Per-QP service level (priority class)
- Per-MR access flags

Crucible substrate primitive (planned, expanded from GAPS-125):

```cpp
// crucible/cntp/Rdma.h
namespace crucible::cntp::rdma {

class ProtectionDomain : Pinned { /* ... */ };

template <uint32_t MaxWr, uint32_t MaxSge, QpType Type>
class QueuePair : Pinned {
    Permission<QpTag> permission_;
public:
    [[nodiscard]] auto post_send(WorkRequest wr) noexcept
        -> std::expected<void, RdmaError>;
    [[nodiscard]] auto post_recv(WorkRequest wr) noexcept
        -> std::expected<void, RdmaError>;
    [[nodiscard]] auto poll_cq(uint32_t max_completions) noexcept
        -> std::expected<std::span<Completion>, RdmaError>;
};

class MemoryRegion : Pinned {
    Tagged<void*, source::Pinned> addr_;
    Refined<positive, uint64_t> length_;
    MrAccessFlags access_;
public:
    auto rkey() const noexcept -> RemoteKey;
    auto lkey() const noexcept -> LocalKey;
};

[[nodiscard]] auto mint_qp<...>(
    effects::Init init,
    const CogIdentity& nic_cog,
    const ProtectionDomain& pd,
    QpConfig config
) noexcept -> std::expected<QueuePair<...>, RdmaError>;

[[nodiscard]] auto mint_mr(
    effects::Init init,
    const ProtectionDomain& pd,
    void* addr,
    size_t length,
    MrAccessFlags access
) noexcept -> std::expected<MemoryRegion, RdmaError>;

}
```

## §7.8 Layer 6 — DPU / SmartNIC

NVIDIA BlueField-3 has 16 ARM Cortex-A78 cores at 2.75 GHz. AWS Nitro is similar (proprietary). Pensando DSC-200 has 8 ARM cores. You can run *arbitrary code* on the DPU's ARM cores, alongside the NIC packet path.

DOCA SDK exposes:
- Flow steering (millions of hardware ACL rules)
- DPI accelerators
- Compression/decompression in hardware (zstd, gzip, LZ4)
- Encryption in hardware (TLS, IPsec — line rate)
- Regex matching (hardware-accelerated)
- Storage emulation (NVMe-oF, virtio-blk endpoint)
- Custom firmware via DOCA Flow API

Crucible substrate primitive (planned, GAPS-144):

```cpp
// crucible/cntp/Doca.h
namespace crucible::cntp::doca {

template <auto OffloadProgramFnPtr>
class DocaOffload : Pinned {
public:
    [[nodiscard]] auto deploy(const CogIdentity& dpu_cog) noexcept
        -> std::expected<DocaOffloadHandle, DocaError>;
};

// User can offload entire subsystems:
// - SWIM gossip implementation runs on BlueField cores
// - TCP stack runs on BlueField (host CPU never sees most packets)
// - Inline LZ4 compression on egress
// - kTLS termination
}
```

This means: **Crucible could push GAPS-114 (SWIM gossip) entirely onto BlueField**, freeing host CPU for compute. The substrate primitive exposes the offload capability; Fixy DSL composes the offload decision into the deployment policy.

## §7.9 Layer 7 — Programmable switch ASICs

Not the NIC itself but the next hop. P4 programs run on switch ASICs (Tofino, Trident, Spectrum-4) at terabits/sec.

Capabilities:
- Custom routing (content-addressed, telemetry-aware)
- In-network aggregation (NVIDIA SHARP runs here)
- In-network telemetry (INT)
- In-network caching (limited)

P4 program runs at line rate, single-pass through the pipeline, with strict resource limits (TCAM entries, register width, stage count).

Crucible substrate primitive (planned, GAPS-145):

```cpp
// crucible/cntp/P4.h
namespace crucible::cntp::p4 {

template <auto P4ProgramFnPtr>
class P4Program {
public:
    [[nodiscard]] auto deploy(const CogIdentity& switch_cog) noexcept
        -> std::expected<P4Deployment, P4Error>;
};

}
```

This requires: switch SDK access (often under NDA), P4 compiler toolchain, vendor-specific deployment mechanism. Crucible's substrate primitive exposes the abstract capability; the actual deployment is per-vendor and per-deployment.

## §7.10 The integrated picture

Real Crucible deployment uses multiple layers simultaneously:

```
Workload                                    Layer
──────────────────────────────────────     ─────
Compute kernels (GEMM, attention)           (host CPU + GPU)
NCCL-equivalent collectives                 5 (RDMA verbs)
Custom transport for federation             4 (AF_XDP)
Per-flow filter (drop blocklist)            2 (XDP)
TLS offload                                 6 (DPU kTLS)
Standard config (offloads, queues, RSS)     1 (ethtool)
Adaptive routing                            7 (P4 switch, if available)
```

Each layer is a Fixy primitive. User declares which layers to use for which workloads. Type-system enforces composability:
- A session protocol declaring `Latency<2us>` can only be assigned to layer 4-5 (AF_XDP or RDMA)
- A session protocol declaring `Encryption<TlsRequired>` can be assigned to layer 5+kTLS, layer 6 (DPU), or upper-layer software TLS
- A session protocol declaring `Multicast` can be assigned to layer 2 (XDP_TX replication) or layer 7 (P4-based multicast)

Compile-time checks ensure the assignment is sound. Runtime configures the actual layer.

## §7.11 Crucible substrate vs Fixy DSL split for eBPF/NIC stack

| Layer | Crucible substrate | Fixy DSL |
|---|---|---|
| 1 ethtool/sysctl | `cog/NicConfig.h` | `fixy::nic_config<>` |
| 2 XDP | `perf/Xdp.h` | `fixy::xdp_attach<>` |
| 3 TC eBPF | `perf/TcEbpf.h` | `fixy::tc_attach<>` |
| 4 AF_XDP | `cntp/AfXdp.h` | `fixy::af_xdp_socket<>` |
| 5 RDMA verbs | `cntp/Rdma.h` | `fixy::rdma_qp<>` |
| 6 DPU/SmartNIC | `cntp/Doca.h`, `cntp/Nitro.h`, `cntp/Pensando.h` | `fixy::doca_offload<>` |
| 7 P4 switch | `cntp/P4.h` | `fixy::p4_program<>` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §8. The 17-Category SOTA Frontier — Index + Meta-Organization

## §8.1 The catalog

Frontier-SOTA distributed-computing networking spans roughly 200 distinct primitives across 17 categories. Each item has:

- A category and item number (e.g., 2.4 = Async DiLoCo)
- A brief description
- Status: ships / partial / planned / research-frontier
- Crucible substrate primitive (file path or GAPS task)
- Fixy DSL composition surface
- Key references / production users
- Dependencies on other items

Sections §10 through §26 each cover one category. The full enumeration:

| Cat | Topic | Items | First-half coverage |
|---|---|---|---|
| 1 | Synchronous collective algorithms | 16 | §10 |
| 2 | Asynchronous + eventually-consistent algorithms | 13 | §11 |
| 3 | Bandwidth reduction (gradient compression) | 14 | §12 |
| 4 | Pipeline parallelism | 10 | §13 |
| 5 | Tensor parallelism | 9 | §14 |
| 6 | FSDP / ZeRO families | 11 | §15 |
| 7 | Expert parallelism (MoE) | 11 | §16 (second half) |
| 8 | Fault tolerance + elasticity | 11 | §17 (second half) |
| 9 | Inference networking patterns (4 of 11; 7 single-machine items out-of-scope) | 4 | §18 (second half) |
| 10 | Pure networking primitives (frontier) | 18 | §19 (second half) |
| 11 | Coordination + consensus + membership | 14 | §20 (second half) |
| 12 | Observability + reliability | 13 | §21 (second half) |
| 13 | Power / thermal / reliability | 9 | §22 (second half) |
| 14 | Security + compliance | 12 | §23 (second half) |
| 15 | Operational primitives | 10 | §24 (second half) |
| 16 | Distributed perf frontiers (3 of 10; 7 single-Cog items out-of-scope) | 3 | §25 (second half) |
| 17 | Federation + cross-organization | 8 | §26 (second half) |

## §8.2 The status legend

For each item:

- **Ships** — implementation exists in `include/crucible/`, tests pass, verified. (Currently: 0 of ~200 items.)
- **Partial** — some substrate exists but the item-specific primitive doesn't. (Currently: ~5 of ~200, e.g. session-type machinery exists but specific protocol templates don't.)
- **Planned** — GAPS-* task exists, no implementation. (Currently: ~150 of ~200.)
- **Research-frontier** — no production system has it; design intent only. (Currently: ~45 of ~200.)

## §8.3 The Crucible/Fixy split per item

For each item, the doc specifies what lives where:

```
Item: Reed-Solomon FEC for unicast
Substrate (crucible/cntp/Fec.h, GAPS-117):
  - encoder: span<byte> input → span<byte> + parity output
  - decoder: span<byte> erasure-marked → span<byte> reconstructed
  - parameter: k (data shards), m (parity shards)
  - vectorized via AVX2 / NEON

Fixy DSL (composition):
  - fixy::transport_with_fec<k, m>(socket)
  - row tag: FecOverhead<percent>
  - cost-model input: bandwidth_overhead = m / (k+m)
  - cost-model input: cpu_overhead = O(k*m) per packet
  - composition rule: only use when packet_loss_rate > threshold
```

This format is repeated for every item in §10-§26.

## §8.4 The dependency graph

Items have implementation dependencies. Major ones:

- All collective algorithms (cat 1) depend on RDMA verbs primitive (cat 10.5)
- All async algorithms (cat 2) depend on completion futures + bounded-staleness machinery
- Fault tolerance (cat 8) depends on health scoring (cat 12) + quarantine state machine
- Inference patterns (cat 9) depend on continuous batching primitive
- Federation (cat 17) depends on encryption (cat 14) + KernelCache federation
- Operational primitives (cat 15) depend on most lower-layer machinery

Full dependency edges are catalogued in §36 of the second half.

## §8.5 Why this organization

The 17 categories are not arbitrary. They correspond to **separable axes of the SOTA frontier**:

- Cats 1-3: how data moves and gets reduced (synchronous, async, compressed)
- Cats 4-7: how computation is split (pipeline, tensor, FSDP, expert)
- Cat 8: what happens when something breaks
- Cat 9: serving patterns distinct from training
- Cats 10-12: pure infrastructure (network, consensus, observability)
- Cat 13: physical-system management
- Cat 14: security
- Cats 15-17: operations, perf, federation

Each axis evolves on its own timeline. Categorizing this way lets Fixy users select primitives along each axis independently — "I want DiLoCo async (cat 2.1) + ZeRO-3 (cat 6.5) + 1-bit Adam (cat 3.4) + crash-stop fault tolerance (cat 8.4)" — and the type system verifies the combination composes.

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §9. Substrate Audit — What Verifiably Ships Today

## §9.1 The verification

At write time (2026-05-03), `find include/crucible -name "*.h" | wc -l` returned 178 headers. Of those:

| Subsystem | Headers | LOC | Networking-relevant? |
|---|---|---|---|
| `safety/` | 89 | (varied) | Foundational (used by everything) |
| `sessions/` | 24 | 16,979 | Yes — protocols compose into network sessions |
| `concurrent/` | 26 | 11,568 | Yes — channel primitives, MPMC, SPSC, AtomicSnapshot |
| `effects/` | 12 | (varied) | Yes — row-typed effects propagate through network code |
| `bridges/` | 5 | 1,942 | Yes — RecordingSessionHandle, CrashTransport, MachineSessionBridge |
| `cog/` | 0 | 0 | NO (greenfield, planned) |
| `topology/` | 0 | 0 | NO (greenfield, planned) |
| `cntp/` | 0 | 0 | NO (greenfield, planned) |
| `network/` | 0 | 0 | NO (greenfield, planned) |
| `canopy/` | 0 | 0 | NO (greenfield, planned) |
| `perf/` | 0 | 0 | NO (greenfield, planned) |
| `mimic/*/network/` | 0 | 0 | NO (greenfield, planned) |
| Top-level | ~22 | (varied) | Existing: TraceGraph, MerkleDag, Vigil, Cipher, etc. |

**Networking-specific code in Crucible today: zero lines.** Every networking primitive is greenfield.

## §9.2 What the existing substrate provides

The substrate that everything else composes from:

### `safety/` (89 headers, foundational)

Provides the 8-axiom-enforcing wrappers:
- `Linear<T>` — move-only ownership
- `Refined<Pred, T>` — predicate-checked invariants
- `Tagged<T, Source>` — provenance markers
- `Secret<T>` — classified-by-default
- `Permission<Tag>` — CSL frame-rule tokens
- `Bits<EnumType>` — typed bit flags
- `Borrowed<T, Source>` — lifetime-bound borrow
- `Saturate.h`, `Checked.h` — arithmetic safety
- `Diagnostic.h` — routed diagnostic catalog
- `Reflectable.h`, `Fn.h`, `DimensionTraits.h` — Phase 0 substrate

Every networking primitive uses these. A `RdmaQueuePair` will be `Linear<>` + `Pinned`. A `PeerIdentity` will be `Tagged<UUID, source::Discovered>`. A `MemoryRegion` will hold a `Refined<positive, uint64_t>` length. An RDMA work request will use `Checked` arithmetic for byte counts.

### `sessions/` (24 headers, 16,979 LOC)

Provides the typed-protocol substrate:
- `Session.h` — base combinator (Send, Recv, Select, Offer, Loop, Continue, End, Stop)
- `SessionGlobal.h` — multiparty global types + projection (Honda 2008 MPST), with the GAPS-001 fix shipped
- `SessionCrash.h` — crash-stop extensions (BSYZ22, BHYZ23)
- `SessionPatterns.h` — protocol pattern library (RequestResponse, ScatterGather, etc.)
- `SessionContext.h`, `SessionAssoc.h`, `SessionQueue.h` — typing context, association, queue types
- `SessionDelegate.h` — higher-order session delegation
- `SessionDeclassify.h` — session-level Secret declassification
- `SessionDiagnostic.h` — session diagnostic tag taxonomy
- `SessionPayloadSubsort.h` — subsort axioms for typed payloads
- `SessionRowExtraction.h`, `SessionMint.h` — Met(X) row integration
- `SessionView.h` — read-only session views
- `PermissionedSession.h` — CSL × Session integration (PermissionedSessionHandle)
- `SpscSession.h` — typed-session façade over PermissionedSpscChannel
- `Sessions.h` — umbrella

This is substantial existing machinery. Every networking protocol (handshake, gossip, collective coordination, federation) becomes a typed session. Compile-time verification of protocol correctness is free.

### `concurrent/` (26 headers, 11,568 LOC)

Provides the channel + scheduling substrate:
- `PermissionedSpscChannel.h` — Single-Producer Single-Consumer with Permission tokens
- `PermissionedShardedGrid.h` — sharded MPMC
- `MpmcChannel.h` — multi-producer multi-consumer
- `ChaseLevDeque.h` — work-stealing deque
- `AtomicSnapshot.h` — seqlock-based SWMR snapshot
- `Substrate.h` — topology-to-channel-kind metafunction
- `Endpoint.h` — typed endpoints with mint factories
- `Stage.h`, `Pipeline.h` — stage composition, pipeline_chain concept
- `CostModel.h` — cache-tier rule encoding
- `Topology.h` — local-machine topology (cache sizes, NUMA, cores)

Networking will reuse: PermissionedSpscChannel for per-flow ordered streams, MpmcChannel for fan-in receive queues, AtomicSnapshot for telemetry broadcast, Stage/Pipeline for staged transport processing.

### `effects/` (12 headers)

Provides the Met(X) row substrate:
- `EffectRow.h` — Row<Es...>, Subrow concept, row union/diff/intersection
- `Computation.h` — Computation<Row, T> carrier
- `Capabilities.h` — Effect enum, cap::* tags, Bg/Init/Test contexts
- `ExecCtx.h` — execution context types
- `Capability.h` — linear capability proof tokens
- `EffectRowProjection.h` — bits_from_row<R>() runtime projection

Networking will extend with: `effects::Resources.h` (the 21+ resource axes from §4), `effects::Mfu.h` (composite MFU policy types).

### `bridges/` (5 headers, 1,942 LOC)

Provides session-bridge primitives:
- `RecordingSessionHandle.h` — recording bridge for session event log
- `CrashTransport.h` — CrashWatchedHandle for crash-stop runtime
- `MachineSessionBridge.h` — Machine + Session integration
- `SubstrateSessionBridge.h` — typed substrate bridges
- `Bridges.h` — umbrella

Networking will extend with new bridges:
- `bridges/RdmaTransport.h` — RDMA verb primitives ↔ session boundary
- `bridges/AfXdpTransport.h` — AF_XDP ↔ session boundary
- `bridges/QuicTransport.h` — QUIC ↔ session boundary
- `bridges/DocaTransport.h` — DPU offload ↔ session boundary

## §9.3 What the existing substrate does NOT provide

The greenfield. Every line below requires new code.

- No NIC discovery
- No NIC configuration API
- No telemetry harvesters (sysctl, ss, TCP_INFO, EDAC, thermal)
- No RDMA verb wrappers
- No XDP/AF_XDP/eBPF integration
- No QUIC integration
- No mTLS/WireGuard transport
- No SWIM gossip implementation
- No scuttlebutt CRDT
- No φ-accrual failure detector
- No Reed-Solomon / fountain code FEC
- No congestion control hooks (BBRv3, DCTCP)
- No pacing / qdisc verification
- No path-swap mechanism
- No PTP integration
- No GPUDirect RDMA primitives
- No SHARP primitives
- No P4 switch deployment
- No DOCA / BlueField / Pensando primitives
- No Cog hierarchy
- No topology graph
- No per-Cog Mimic instance
- No comm-through-IR (no IR001 comm op set, no Forge phases for comm, no Mimic backends for comm)
- No power/thermal/wear control
- No adaptive MFU control loop
- No discrete-search partition optimizer
- No collective algorithms (ring, tree, recursive halving-doubling, etc.)
- No async algorithms (DiLoCo, etc.)
- No gradient compression
- No pipeline parallelism primitives
- No tensor parallelism primitives
- No FSDP / ZeRO primitives
- No MoE primitives
- No fault tolerance primitives beyond crash-stop session types
- No inference-specific primitives
- No federation primitives beyond cross-organization session types
- No observability primitives beyond what concurrent/ provides
- No security primitives beyond Secret + declassify
- No operational primitives

This is the work the GAPS-110..150+ tasks own. The Crucible/Fixy split is what makes this work tractable: each item is a small substrate primitive plus a Fixy DSL composition layer, not a giant integrated subsystem.

## §9.4 Implementation status by category

| Cat | Total items | Ships | Partial | Planned | Research-frontier |
|---|---|---|---|---|---|
| 1 — Sync collective | 16 | 0 | 0 | 16 | 0 |
| 2 — Async/eventual | 13 | 0 | 0 | 8 | 5 |
| 3 — Bandwidth reduction | 14 | 0 | 0 | 14 | 0 |
| 4 — Pipeline | 10 | 0 | 0 | 10 | 0 |
| 5 — Tensor parallel | 9 | 0 | 0 | 9 | 0 |
| 6 — FSDP/ZeRO | 11 | 0 | 0 | 11 | 0 |
| 7 — Expert parallel (MoE) | 11 | 0 | 0 | 9 | 2 |
| 8 — Fault tolerance | 11 | 1 | 0 | 10 | 0 |
| 9 — Inference networking | 4 (was 11; 7 single-machine items out-of-scope) | 0 | 0 | 4 | 0 |
| 10 — Networking primitives | 18 | 0 | 0 | 13 | 5 |
| 11 — Consensus/membership | 14 | 0 | 0 | 13 | 1 |
| 12 — Observability | 13 | 0 | 0 | 13 | 0 |
| 13 — Power/thermal | 9 | 0 | 0 | 8 | 1 |
| 14 — Security | 12 | 1 | 0 | 11 | 0 |
| 15 — Operational | 10 | 0 | 0 | 10 | 0 |
| 16 — Distributed perf frontiers | 3 (was 10; 7 single-Cog items out-of-scope) | 0 | 0 | 3 | 0 |
| 17 — Federation | 8 | 0 | 0 | 6 | 2 |
| **Total** | **186** (was 200; 14 single-machine items moved out-of-scope) | **2** | **0** | **168** | **16** |

The 2 items that ship today: GAPS-001 crash-stop projection fix (cat 8.4 substrate), and the Secret-declassify discipline (cat 14, foundational).

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §10. Category 1 — Synchronous Collective Algorithms

## §10.1 Scope

Synchronous collectives are the backbone of conventional data-parallel and tensor-parallel training. Every step waits for the collective to complete before continuing. NCCL is the de-facto stack on NVIDIA hardware; RCCL on AMD; MPI on CPU clusters; UCX as a portable layer.

Crucible's contribution: **typed, content-addressed, MAP-Elites-tuned, cross-vendor-bit-equivalent** versions of all of these, composable in Fixy DSL.

## §10.2 The 16 items

### 1.1 Ring all-reduce

**Description.** N peers arranged in a ring. Tensor split into N chunks. In phase 1 (reduce-scatter), each peer sends chunk_i to next peer N-1 times, accumulating. In phase 2 (all-gather), each peer sends its complete chunk_i to next peer N-1 times. After 2(N-1) steps, every peer has the full reduced tensor. Bandwidth-optimal: each peer sends 2(N-1)/N × tensor_size bytes total.

**Status.** Planned (GAPS-149 will catalog the full collective set; this is item 1.1 within it).

**Substrate.** `crucible/cntp/collectives/RingAllReduce.h` (planned). Provides the IR001 op definition + the per-vendor IR003* lowering via `mimic/{nv,am,cpu}/network/RingAllReduce.h`.

```cpp
namespace crucible::cntp::collectives {

template <typename T, typename ReductionOp>
auto ring_all_reduce(
    const cog::CogIdentity ring[],
    size_t ring_size,
    safety::Borrowed<T, source::Local> input,
    safety::Linear<T*> output,
    NumericalRecipe recipe
) noexcept -> Computation<
    Row<effects::Bg,
        effects::SmBudget<8>,
        effects::NvlinkBandwidth<2 * (RingSize - 1) * MessageSize / RingSize>,
        effects::HbmBandwidth<2 * (RingSize - 1) * MessageSize / RingSize>>,
    std::expected<void, CollectiveError>>;

}
```

**Fixy DSL.** Composes via `fixy::all_reduce<topology = fixy::Topology::Ring>(tensor, mesh, op)`. Default for bandwidth-bound collectives on intra-rack peers. Topology choice is a Fixy policy decision driven by message size + measured bandwidth.

**MAP-Elites variants.** Chunk count (ring depth), SM count for reduction kernels, NIC parallelism, completion polling strategy.

**Cross-vendor CI.** Ring all-reduce on N=8 NV vs N=8 AM vs N=8 CPU oracle produces bit-equivalent result under BITEXACT_TC recipe.

### 1.2 Tree all-reduce

**Description.** Reduction tree (typically binary): leaves send to parents, parents reduce + forward upward, root broadcasts result downward. Latency-optimal: O(log N) hops vs ring's O(N).

**Status.** Planned.

**Substrate.** `crucible/cntp/collectives/TreeAllReduce.h`.

**Fixy DSL.** `fixy::all_reduce<topology = fixy::Topology::Tree>`. Default for latency-bound collectives (small messages) and for cross-rack collectives where ring's bandwidth advantage doesn't materialize.

**MAP-Elites variants.** Tree fan-out (binary, ternary, quaternary), tree shape (balanced, skewed for heterogeneous bandwidth), reduction location (parent vs child), in-network reduction enablement.

### 1.3 Recursive halving-doubling all-reduce

**Description.** Reduce-scatter via recursive halving (each step halves the data and the participant count); all-gather via recursive doubling (reverse). 2 log₂(N) steps total. Balanced bandwidth-latency tradeoff.

**Status.** Planned.

**Substrate.** `crucible/cntp/collectives/HalvingDoublingAllReduce.h`.

**Fixy DSL.** `fixy::all_reduce<topology = fixy::Topology::HalvingDoubling>`. Default for medium message sizes (1KB-1MB) on most fabric topologies. Strict requirement: N must be power of 2 (Rabenseifner extension handles non-powers but with worse performance).

### 1.4 Recursive doubling all-reduce

**Description.** Each peer exchanges with peer at distance 2^i in step i. After log₂(N) steps, every peer has the full reduced tensor. Latency-optimal, but bandwidth = log₂(N) × tensor_size (every peer sends the full tensor log₂(N) times).

**Status.** Planned.

**Fixy DSL.** Default for very small messages (< 1KB) on flat topologies.

### 1.5 Bidirectional ring

**Description.** Two rings running in opposite directions simultaneously. Doubles bandwidth utilization on bidirectional links (NVLink, IB).

**Status.** Planned.

**Fixy DSL.** Default on NVLink-rich topologies (DGX H100/H200 with NVSwitch).

### 1.6 2D ring (intra-node × inter-node)

**Description.** Inner ring within node (over NVLink), outer ring across nodes (over IB). Reduces inter-node traffic by N-fold.

**Status.** Planned.

**Fixy DSL.** Default for multi-node clusters with NVLink-internal topology. Composes with topology-aware Cog query.

### 1.7 Hierarchical all-reduce (multi-level)

**Description.** Generalization of 2D ring to N levels: GPU-within-NUMA-within-socket-within-node-within-rack-within-row. Each level uses the topology-appropriate algorithm.

**Status.** Planned.

**Fixy DSL.** Default for multi-DC or large multi-rack clusters. Driven by Cog hierarchy traversal.

### 1.8 Direct all-to-all

**Description.** Every peer sends a unique chunk to every other peer. N(N-1) total messages. Used for MoE expert routing where each token goes to a specific expert.

**Status.** Planned.

**Substrate.** `crucible/cntp/collectives/AllToAll.h`.

**Fixy DSL.** `fixy::all_to_all(tensor, mesh)`. Default for small N (< 32). For larger N, replaced by hierarchical all-to-all variants.

### 1.9 Variable-size all-to-all (`all_to_all_v`)

**Description.** Each pair (sender, receiver) has potentially different message size. Used for sparse MoE where each token's destination expert determines size. Pre-exchange of sizes followed by data exchange.

**Status.** Planned.

**Fixy DSL.** `fixy::all_to_all_v(tensors, sizes, mesh)`.

### 1.10 Sparse all-to-all (only nonzero entries)

**Description.** Optimization of all_to_all_v when many size entries are zero. Skip transmission for empty pairs. Reduces total messages from N(N-1) to nnz pairs.

**Status.** Planned.

**Fixy DSL.** `fixy::sparse_all_to_all(tensors, sparsity_mask, mesh)`. Composes with MoE token routing via expert-choice sparsity.

### 1.11 Reduce-scatter + all-gather decomposition

**Description.** All-reduce = reduce-scatter + all-gather. Sometimes more efficient to do them separately (e.g., when only the local shard is needed before further computation, then a separate all-gather later).

**Status.** Planned.

**Fixy DSL.** Both `fixy::reduce_scatter` and `fixy::all_gather` are first-class primitives. User strategy composes them.

### 1.12 Pipelined collectives (chunked + overlapped)

**Description.** Split tensor into K chunks, pipeline the collective across chunks. Each chunk's collective overlaps with the next chunk's compute. Reduces critical-path time.

**Status.** Planned.

**Fixy DSL.** `fixy::all_reduce<schedule = fixy::Schedule::Overlapped, chunks = K>`. K is a tunable; MAP-Elites finds optimal K per (op, target_caps_class, message_size).

### 1.13 SHARP in-network reduction

**Description.** Switch ASIC performs reduction operation as packets transit through the switch fabric. Requires Mellanox SHARP-capable switches. Eliminates the bandwidth cost of ring/tree reduction; only the result needs to traverse switches.

**Status.** Planned (GAPS-133, in §10 Pure networking primitives in second half).

**Substrate.** `crucible/cntp/collectives/SharpReduction.h`. Calls SHARP API on supported switches.

**Fixy DSL.** `fixy::all_reduce<topology = fixy::Topology::Sharp>`. Eligibility check: only `Associative<true>` + `Commutative<true>` recipes. BITEXACT_STRICT recipes that require ordered reduction are ineligible.

### 1.14 NCCL-style protocols (LL, LL128, Simple)

**Description.** NCCL has three internal protocols:
- **Simple:** standard ring with compute+comm overlap
- **LL (Low Latency):** uses 8-byte flag for synchronization, lower latency, higher bandwidth cost
- **LL128:** 128-byte flag-based sync, balance between LL and Simple

NCCL picks per-collective based on message size + cluster topology.

**Status.** Planned. Crucible's equivalent is the MAP-Elites search over completion-polling + chunk-size axes.

**Fixy DSL.** `fixy::all_reduce<protocol = fixy::Protocol::Auto>` (default), with explicit overrides.

### 1.15 Topology-aware ring construction

**Description.** Ring order is not arbitrary — for NVLink-rich topology, the ring must respect the NVLink connectivity to achieve full bandwidth. NCCL has internal heuristics; Crucible explicitly walks the Cog topology graph.

**Status.** Planned. Composes with §3 Cog topology.

**Fixy DSL.** `fixy::ring_for(cogs)` returns the topology-optimal ring order.

### 1.16 Fused collective + compute

**Description.** Fusion patterns from §6.7. Send-from-epilogue, reduce-on-recv, compress-before-send. Crucible's distinguishing feature.

**Status.** Planned. Forge Phase D extension.

**Fixy DSL.** Automatic — Forge applies fusion when patterns match. User can disable per-collective via `fixy::all_reduce<fuse_with_producer = false>`.

## §10.3 Crucible substrate vs Fixy DSL split — Category 1 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| Per-algorithm IR001 op | `crucible/cntp/collectives/*.h` | (uses) |
| Per-vendor lowering | `mimic/{nv,am,cpu,...}/network/*.h` | (uses) |
| Cross-vendor CI tests | `test/cross_vendor_collective_*.cpp` | (uses) |
| MAP-Elites search infrastructure | existing Mimic search | (uses) |
| Topology-aware ring construction | `cntp/collectives/RingTopo.h` | (uses) |
| Algorithm choice | (none) | `fixy::all_reduce<topology = ...>` |
| Schedule choice | (none) | `fixy::Schedule::*` |
| Protocol choice | (none) | `fixy::Protocol::*` |
| Eligibility checks | concept gates | `requires Associative<recipe> ∧ Commutative<recipe>` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §11. Category 2 — Asynchronous + Eventually-Consistent Algorithms

## §11.1 Scope

The shift from synchronous SGD to asynchronous and eventually-consistent algorithms is the most significant active research direction in distributed training. Key motivations:

- **Cross-DC training.** WAN bandwidth is too low for sync SGD; latency too high. DiLoCo (Google 2023) demonstrated training across DCs with 500-step inner loops and outer sync every 500 steps.
- **Decentralized training.** No central coordinator; peers gossip. Tolerates Byzantine peers, allows volunteer compute. Hivemind, Petals, OpenDiLoCo.
- **Bandwidth scarcity.** WAN or shared-tenant cloud limits gradient communication. Compression + asynchrony together reduce communication volume by 1000-3000× (DisTrO).
- **Fault tolerance.** Async tolerates partial failures naturally — late peers' updates simply arrive late or get dropped.

Crucible's contribution: each algorithm becomes a Fixy `DistributedStrategy` satisfying a concept gate. Composable, replaceable, type-checked.

## §11.2 The 13 items

### 2.1 DiLoCo (Distributed Low-Communication)

**Description.** Workers run K inner SGD steps locally without communication. After K steps, workers compute pseudo-gradient (difference between current weights and start-of-period weights) and outer optimizer (typically Nesterov momentum) aggregates pseudo-gradients globally. K is large (500-5000 steps).

**Reference.** Douillard et al. 2023 (Google), "DiLoCo: Distributed Low-Communication Training of Language Models."

**Production users.** Google internal, OpenDiLoCo (Prime Intellect open-source).

**Status.** Planned.

**Substrate.** `crucible/cntp/strategies/DiLoCo.h` (planned). Provides:
- Type for inner-loop state (saved start weights)
- Type for outer-loop state (Nesterov momentum buffer)
- Type for pseudo-gradient computation
- Concept gate `DiLoCoStrategy` that user implementations satisfy

**Fixy DSL.**

```cpp
struct DiLoCo : fixy::DistributedStrategy {
    static constexpr uint32_t inner_steps = 500;
    using outer_optimizer = fixy::optim::Nesterov<momentum = 0.9>;

    // satisfies fixy::DistributedStrategy via:
    template <fixy::ModelLike M>
    auto inner_step(M& model, fixy::Batch batch) const noexcept -> ...;

    template <fixy::ModelLike M>
    auto outer_sync(M& model, M const& period_start) const noexcept -> ...;
};

fixy::train<DiLoCo>(model, dataset, mesh);
```

**Composition.** Inner steps are pure local SGD — no Crucible collective needed beyond per-rank optimizer. Outer sync is one all_reduce of pseudo-gradients (cat 1.1 substrate) per K steps. The bandwidth saving: factor of K compared to standard DDP.

### 2.2 Streaming DiLoCo

**Description.** DiLoCo + overlapped pseudo-gradient transfer. While workers run inner step k+1, the previous outer sync's communication completes in background. Hides outer sync latency.

**Reference.** Douillard et al. 2024, "Streaming DiLoCo."

**Status.** Planned.

**Fixy DSL.**

```cpp
struct StreamingDiLoCo : fixy::DistributedStrategy {
    static constexpr uint32_t inner_steps = 500;
    static constexpr bool overlap_outer_sync = true;
    using outer_optimizer = fixy::optim::Nesterov<momentum = 0.9>;
    // ...
};
```

The overlap is a Fixy scheduling decision; substrate provides async send + completion future primitives.

### 2.3 Async DiLoCo with staleness bounds

**Description.** Workers don't synchronize their outer-sync rounds. Each worker independently completes its inner loops; outer aggregator combines pseudo-gradients with staleness-aware weighting.

**Reference.** Prime Intellect 2024, INTELLECT-1 training run.

**Status.** Planned.

**Substrate.** Requires `BoundedDelay<τ>` row tag. Staleness-weighted aggregation primitive in `cntp/StaleAggregator.h`.

**Fixy DSL.**

```cpp
struct AsyncDiLoCo : fixy::DistributedStrategy {
    static constexpr uint32_t inner_steps_min = 300;
    static constexpr uint32_t inner_steps_max = 700;  // workers vary
    static constexpr uint32_t max_staleness_rounds = 5;
    using outer_optimizer = fixy::optim::Nesterov<momentum = 0.9>;
    using staleness_weight = fixy::weight::ExponentialDecay<half_life = 2>;
};
```

**Composition with crash-stop.** A worker crash during inner loop is invisible to others (no synchronization). The worker's pseudo-gradient simply doesn't arrive that round; staleness weighting handles the absence.

### 2.4 Local SGD (DiLoCo's predecessor)

**Description.** K local steps + sync. K is small (10-100). Older formulation; DiLoCo is the modern variant with outer optimizer.

**Reference.** Stich 2018, "Local SGD Converges Fast and Communicates Little."

**Status.** Planned. Subsumed by DiLoCo with `outer_optimizer = fixy::optim::Sgd`.

### 2.5 EASGD (Elastic Averaging SGD)

**Description.** Workers maintain local parameters and periodically pull toward a "elastic" center variable with momentum. Center is updated by averaging worker parameters.

**Reference.** Zhang et al. 2014.

**Status.** Planned.

**Fixy DSL.** `fixy::strategy::Easgd<elasticity = 0.1, sync_period = 100>`.

### 2.6 Gossip-SGD

**Description.** Workers exchange gradients with random subset of peers each round (gossip). Eventually all workers converge to similar gradients without centralized aggregation.

**Reference.** Boyd et al. 2006 (gossip), Lian et al. 2017 (D-PSGD), Lu et al. 2020 (gossip-SGD).

**Status.** Planned.

**Substrate.** Requires gossip primitive (cat 11.3 SWIM-style + scuttlebutt). `crucible/cntp/Gossip.h`.

**Fixy DSL.**

```cpp
struct GossipSgd : fixy::DistributedStrategy {
    static constexpr uint32_t fanout = 3;        // peers per round
    static constexpr uint32_t rounds_per_step = 1;
    using mixing_matrix = fixy::mixing::Metropolis;  // ensures convergence
};
```

### 2.7 AD-PSGD (Asynchronous Decentralized Parallel SGD)

**Description.** Combines async + gossip. Workers proceed asynchronously, exchanging gradients with random neighbors. No global synchronization point.

**Reference.** Lian et al. 2018.

**Status.** Planned.

**Fixy DSL.** Composes from gossip + async primitives.

### 2.8 CocktailSGD

**Description.** Combines random sparsification + quantization + low-rank approximation. Tunable mix per layer. Achieves 10-100× bandwidth reduction.

**Reference.** Wang et al. 2023.

**Status.** Planned.

**Substrate.** Requires sparsification, quantization, low-rank primitives (cat 3 Bandwidth reduction).

**Fixy DSL.**

```cpp
struct CocktailSgd : fixy::DistributedStrategy {
    using compression = fixy::compress::Cocktail<
        sparsify = fixy::compress::TopK<sparsity = 0.01>,
        quantize = fixy::compress::Int4<>,
        low_rank = fixy::compress::PowerSgd<rank = 4>
    >;
};
```

### 2.9 Bounded-staleness async SGD (classical)

**Description.** Workers update with stale gradients, bounded by τ. Convergence proven for bounded staleness.

**Reference.** Recht et al. 2011 (Hogwild!), Ho et al. 2013 (SSP).

**Status.** Planned.

**Fixy DSL.** `fixy::strategy::BoundedStalenessSgd<tau = 5>`.

### 2.10 Federated Averaging (FedAvg)

**Description.** Clients perform local training; central server aggregates. Privacy-preserving (raw gradients don't cross client boundary). Standard for cross-device federated learning.

**Reference.** McMahan et al. 2016.

**Status.** Planned.

**Fixy DSL.**

```cpp
struct FedAvg : fixy::DistributedStrategy {
    static constexpr uint32_t local_epochs = 5;
    using aggregation = fixy::agg::WeightedAverage<by = fixy::weight::SampleCount>;
    using privacy = fixy::privacy::DifferentialPrivacy<epsilon = 1.0>;
};
```

### 2.11 SCAFFOLD (variance-reduced FedAvg)

**Description.** FedAvg + control variates to reduce client drift. Each client maintains local + global control variate; corrected gradient = local_grad - local_cv + global_cv.

**Reference.** Karimireddy et al. 2020.

**Status.** Planned.

**Fixy DSL.** `fixy::strategy::Scaffold<...>`.

### 2.12 SWARM Parallelism

**Description.** Pipeline parallel with redundancy. Each pipeline stage has multiple replicas; if one fails, traffic re-routes to another. Designed for cross-DC training over heterogeneous fabric.

**Reference.** Ryabinin et al. 2023.

**Status.** Planned.

**Substrate.** Requires pipeline primitives (cat 4) + redundant routing (cat 8.6 hot peer replacement) + gossip-based scheduler.

**Fixy DSL.**

```cpp
struct SwarmParallel : fixy::DistributedStrategy {
    static constexpr uint32_t replicas_per_stage = 3;
    using stage_assignment = fixy::assign::DynamicReroute;
    using fault_tolerance = fixy::ft::HotReplacement;
};
```

### 2.13 Hivemind-style Byzantine-tolerant averaging

**Description.** Aggregation protocol that tolerates malicious peers reporting false gradients. Uses Krum / Bulyan / median-based aggregation rules.

**Reference.** Hivemind library, Volunteer compute systems.

**Status.** Planned.

**Substrate.** Requires Byzantine-tolerant aggregation primitives in `cntp/Byzantine.h`.

**Fixy DSL.**

```cpp
struct HivemindAvg : fixy::DistributedStrategy {
    using aggregation = fixy::agg::Krum<f = 1>;  // tolerates 1 Byzantine per round
};
```

## §11.3 Composition with crash-stop session types

The crash-stop machinery (GAPS-001, BHYZ23) composes naturally with async strategies. A worker crash:

- In sync DDP: every other worker blocks on the missing peer's contribution → cascade failure
- In Async DiLoCo: the crashed worker's pseudo-gradient simply doesn't arrive that round → outer sync proceeds with N-1 contributions, no cascade
- In Gossip-SGD: gossip continues, the crashed peer is removed from neighbor lists → no global impact

The session-type encoding makes this explicit. Each strategy declares its crash-tolerance in a row tag:

```cpp
template <typename Strategy>
concept CrashTolerantStrategy = requires {
    typename Strategy::tolerated_failures_per_round;
    requires Strategy::tolerated_failures_per_round::value > 0;
};

// Sync DDP: NOT CrashTolerantStrategy (any peer crash kills the round)
// Async DiLoCo: IS CrashTolerantStrategy (tolerates up to N-1 crashes per round)
// Gossip-SGD: IS CrashTolerantStrategy (tolerates fanout-1 crashes per round)
```

User can choose strategy based on operational requirements:

```cpp
// "I need sub-quorum tolerance"
auto strategy = fixy::pick_strategy<
    fixy::CrashTolerantStrategy,
    fixy::TolerableFailures<fixy::FractionOf<3>>  // tolerate up to N/3 failures
>();
```

## §11.4 Compositions across categories

Async + bandwidth reduction + crash-stop: an `AsyncDiLoCo + CocktailSgd` strategy is type-correct because:
- `AsyncDiLoCo` row: `BoundedDelay<5> + AggregationPolicy::WeightedDecay`
- `CocktailSgd` row: `Compression::Combined<TopK + Int4 + PowerSgd>`
- Combined row: union of above
- Crash-stop tolerance: AsyncDiLoCo's `tolerated_failures` propagates through the combination

The user composes:

```cpp
struct MyStrategy : fixy::DistributedStrategy {
    using base = fixy::strategy::AsyncDiLoCo<inner_steps = 500>;
    using compression = fixy::compress::Cocktail<...>;
    using crash_policy = fixy::crash::IgnoreLatecomers<timeout_seconds = 30>;
};
```

The Fixy DSL composes these into a single strategy whose row metadata is the union, whose concept gates require the union to satisfy all participating concepts, and whose runtime behavior is the layered composition.

## §11.5 Crucible substrate vs Fixy DSL split — Category 2 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| Async send/recv with completion futures | `cntp/AsyncIo.h` | (uses) |
| Bounded-staleness machinery | `cntp/Staleness.h` | (uses) |
| Gossip primitive | `cntp/Gossip.h` | (uses) |
| Byzantine-tolerant aggregation | `cntp/Byzantine.h` | (uses) |
| `DistributedStrategy` concept | `fixy/Strategy.h` | (uses) |
| Strategy implementations | (none) | `fixy::strategy::*` |
| Strategy composition | (none) | `fixy::compose_strategy<>` |
| Concept-based strategy selection | (none) | `fixy::pick_strategy<>` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §12. Category 3 — Bandwidth Reduction (Gradient Compression)

## §12.1 Scope

Bandwidth reduction techniques compress the data transferred during gradient synchronization. Critical for:
- Cross-DC training (WAN bandwidth scarcity)
- Large-scale clusters (aggregate fabric utilization)
- Bandwidth-bound regimes (training where comm > compute time)
- Federated learning (client uplink limits)

Compression introduces error; the techniques bound or recover the error via various mechanisms (residual accumulation, error feedback, control variates).

## §12.2 The 14 items

### 3.1 Top-K sparsification

**Description.** Send only the K largest-magnitude gradient elements per layer. Locally accumulate the residuals and add them to the next round's gradient (error feedback). Convergence proven equivalent to dense SGD with bounded slowdown.

**Reference.** Aji & Heafield 2017, Stich et al. 2018.

**Status.** Planned.

**Substrate.** `crucible/cntp/compress/TopK.h`.

```cpp
namespace crucible::cntp::compress {

template <typename T>
struct TopKResult {
    safety::Linear<safety::FixedArray<uint32_t, MaxK>> indices;
    safety::Linear<safety::FixedArray<T, MaxK>> values;
};

template <uint32_t K, typename T>
auto top_k(safety::Borrowed<T, source::Local> tensor) noexcept
    -> Computation<Row<effects::Bg, effects::SmBudget<2>>, TopKResult<T>>;

template <typename T>
auto reconstruct_dense(
    TopKResult<T> const& sparse,
    safety::Linear<T*> dense_output
) noexcept -> Computation<Row<effects::Bg, effects::SmBudget<1>>, void>;

}
```

**Fixy DSL.**

```cpp
fixy::all_reduce<
    compression = fixy::compress::TopK<sparsity = 0.01>
>(grad_tensor, mesh, fixy::ReductionOp::Sum);
```

**MAP-Elites variants.** K value, residual accumulator dtype (FP32 vs FP16), index encoding (raw vs delta-compressed).

### 3.2 Random-K sparsification

**Description.** Send K random gradient elements per round. Lower compute cost than Top-K (no sorting). Slightly slower convergence but adequate for many workloads.

**Status.** Planned.

**Fixy DSL.** `fixy::compress::RandomK<sparsity = 0.01>`.

### 3.3 Threshold sparsification

**Description.** Send all gradient elements with magnitude > threshold. Variable count per round. Threshold is dynamically tuned to maintain target compression ratio.

**Status.** Planned.

**Fixy DSL.** `fixy::compress::Threshold<target_density = 0.01, adaptive = true>`.

### 3.4 PowerSGD low-rank approximation

**Description.** Approximate gradient matrix as U × V^T via power iteration. Send only U and V, much smaller than full matrix. Used in DiLoCo for outer pseudo-gradients.

**Reference.** Vogels et al. 2019.

**Status.** Planned.

**Substrate.** `crucible/cntp/compress/PowerSgd.h`. Power iteration kernel.

**Fixy DSL.** `fixy::compress::PowerSgd<rank = 4, iterations = 1>`.

### 3.5 1-bit Adam / 1-bit LAMB

**Description.** Quantize gradients to single bit (sign). Combined with error feedback for convergence. Designed for Adam optimizer with variance compensation. 32× bandwidth reduction.

**Reference.** Tang et al. 2020 (1-bit Adam), Microsoft DeepSpeed.

**Status.** Planned.

**Fixy DSL.** `fixy::compress::OneBitAdam<warmup_steps = 1000>`.

### 3.6 Signum / signSGD

**Description.** Send only sign of gradient. Convergence proven for stochastic regime. Variant of 1-bit Adam without optimizer-specific compensation.

**Reference.** Bernstein et al. 2018.

**Status.** Planned.

**Fixy DSL.** `fixy::compress::SignSgd<>`.

### 3.7 QSGD (Quantized SGD)

**Description.** Stochastic quantization to k-bit representation with unbiased estimation. Tunable bit-width; tradeoff between communication cost and convergence rate.

**Reference.** Alistarh et al. 2017.

**Status.** Planned.

**Fixy DSL.** `fixy::compress::Qsgd<bits = 8, stochastic = true>`.

### 3.8 TernGrad

**Description.** Quantize to {-1, 0, +1}. ~16× compression vs FP32. Convergence proven for unbiased TernGrad.

**Reference.** Wen et al. 2017.

**Status.** Planned.

**Fixy DSL.** `fixy::compress::TernGrad<>`.

### 3.9 Deep gradient compression (residual accumulation)

**Description.** Combines Top-K + momentum-correction + warm-up + gradient clipping. Industrial-strength sparsification for production training.

**Reference.** Lin et al. 2017 (DGC).

**Status.** Planned.

**Fixy DSL.** `fixy::compress::Dgc<sparsity = 0.999, momentum_correction = true>`.

### 3.10 ATOMO (atomic decomposition)

**Description.** Decompose gradient into a sparse atomic basis. Send only nonzero coefficients. Generalization of Top-K to arbitrary basis (not just standard basis).

**Reference.** Wang et al. 2018.

**Status.** Planned.

**Fixy DSL.** `fixy::compress::Atomo<basis = ...>`.

### 3.11 DRIVE (deep gradient compression variants)

**Description.** Family of DGC follow-ups with various tweaks for specific architectures. CNN-specific, transformer-specific variants.

**Status.** Planned. Crucible's substrate covers via parameterized DGC primitives.

### 3.12 THC (Thresholded Hadamard Compression)

**Description.** Apply Hadamard transform, threshold, transmit nonzero coefficients. Reduces correlation in gradients before sparsification.

**Status.** Research-frontier.

**Fixy DSL.** `fixy::compress::Thc<threshold = ...>`.

### 3.13 Inline compression (LZ4 / zstd) on gradient stream

**Description.** General-purpose lossless compression on the gradient byte stream. Lower compression ratio (~2-4×) but no convergence impact. Fast (LZ4 ~5 GB/s).

**Status.** Planned. Substrate: `crucible/cntp/compress/Generic.h`.

**Fixy DSL.** `fixy::compress::Lz4<level = fast>` or `fixy::compress::Zstd<level = 3>`.

**Composition.** Compose with sparsification: sparsify first (lossy compression), then LZ4 (lossless on the compressed representation).

### 3.14 Sketched gradient communication (Count-Sketch)

**Description.** Hash gradient elements into count-min sketch. Send sketch. Receiver reconstructs approximate gradient. Constant memory regardless of model size.

**Reference.** Ivkin et al. 2019 (SKETCHED-SGD).

**Status.** Research-frontier.

**Fixy DSL.** `fixy::compress::CountSketch<width = 1024, depth = 4>`.

## §12.3 The error-feedback discipline

All lossy compression techniques (3.1, 3.2, 3.3, 3.5, 3.6, 3.7, 3.8, 3.9, 3.10, 3.12, 3.14) require **error feedback** (also called residual accumulation) for convergence:

```
e_t = compressed_grad_t - true_grad_t  // error of round t
true_grad_{t+1} += e_t                   // accumulate into next round
```

Without error feedback, lossy compression introduces unbounded bias and breaks convergence. The substrate primitive enforces this via type contract:

```cpp
template <typename Compressor>
concept LossyCompressor = requires {
    typename Compressor::error_state;  // must declare an error state type
    requires sizeof(typename Compressor::error_state) > 0;  // non-trivial
};
```

User-defined compressors that fail this concept are rejected at compile time with a routed diagnostic naming the missing requirement.

## §12.4 Compression composition

Compressions compose. The pattern from CocktailSGD is general:

```cpp
fixy::compress::Composed<
    fixy::compress::TopK<sparsity = 0.01>,   // first: sparsify
    fixy::compress::Int4<>,                   // second: quantize the values
    fixy::compress::Lz4<>                     // third: lossless on the bits
>
```

Order matters. Sparsify first (most data eliminated), then quantize remaining values (precision reduction), then lossless compress the bit stream. Reverse order would not gain from later passes since earlier passes wouldn't see compressed data.

The Fixy DSL composer ensures the composition is sound:
- Each component must satisfy `Compressor` concept
- Component output types must chain (TopK output → Int4 input → Lz4 input)
- Combined error feedback state must be tracked correctly

## §12.5 Crucible substrate vs Fixy DSL split — Category 3 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| Per-compressor primitive | `cntp/compress/*.h` | (uses) |
| Error-feedback state machine | `cntp/compress/ErrorFeedback.h` | (uses) |
| `Compressor` / `LossyCompressor` concepts | `cntp/compress/Concept.h` | (uses) |
| Vectorized implementations | per-compressor SIMD code | (uses) |
| Compression choice | (none) | `fixy::compress::*` |
| Compression composition | (none) | `fixy::compress::Composed<>` |
| Eligibility check | concept gate | `requires LossyCompressor<C> ⇒ HasErrorState<C>` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §13. Category 4 — Pipeline Parallelism

## §13.1 Scope

Pipeline parallelism partitions the model along the depth axis. Each pipeline stage runs on a different group of devices. Microbatches flow through the pipeline; multiple microbatches are in flight simultaneously to keep all stages busy.

Trade-offs:
- **Bubble overhead.** First and last microbatches face idle stages. Bubble fraction = (P-1)/N where P = stages, N = microbatches.
- **Memory savings.** Each device holds only its stage's parameters and activations.
- **Composability.** Combines with TP, DP, FSDP for higher-dimensional parallelism.

## §13.2 The 10 items

### 4.1 GPipe (synchronous pipeline)

**Description.** All forward passes complete before any backward pass starts. Simple but high bubble.

**Reference.** Huang et al. 2019.

**Status.** Planned.

**Fixy DSL.** `fixy::pipeline::Gpipe<stages = 4, microbatches = 16>`.

### 4.2 PipeDream (1F1B asynchronous pipeline)

**Description.** 1-Forward-1-Backward schedule. After warm-up, every step does one forward and one backward. Reduces bubble vs GPipe but requires weight stashing for correctness.

**Reference.** Narayanan et al. 2019.

**Status.** Planned.

**Fixy DSL.** `fixy::pipeline::PipeDream<stages = 4>`.

### 4.3 PipeDream-2BW (bounded weight staleness)

**Description.** PipeDream variant that uses 2 weight versions, bounded weight staleness. Eliminates weight stashing memory overhead.

**Reference.** Narayanan et al. 2021.

**Status.** Planned.

### 4.4 Chimera (bidirectional pipeline)

**Description.** Two pipelines running in opposite directions simultaneously. Reduces bubble. Doubles weight memory but halves bubble fraction.

**Reference.** Li et al. 2021.

**Status.** Planned.

**Fixy DSL.** `fixy::pipeline::Chimera<stages = 4>`.

### 4.5 ZeRO-Bubble (zero pipeline bubble)

**Description.** Splits backward into B (compute gradient w.r.t. inputs) and W (compute gradient w.r.t. weights). Schedules to fully eliminate bubble.

**Reference.** Qi et al. 2024.

**Status.** Planned.

**Fixy DSL.** `fixy::pipeline::ZeroBubble<stages = 4>`.

### 4.6 Interleaved 1F1B (Megatron)

**Description.** Each stage holds multiple non-contiguous layer chunks. Reduces bubble at cost of more cross-device communication.

**Reference.** Narayanan et al. 2021 (Megatron-LM).

**Status.** Planned.

**Fixy DSL.** `fixy::pipeline::Interleaved1F1B<stages = 4, chunks_per_stage = 2>`.

### 4.7 Looped pipeline schedules (Megatron-LM v3)

**Description.** Generalization of Interleaved 1F1B with arbitrary loop structure. Optimizer chooses loop pattern.

**Reference.** Megatron-LM v3.

**Status.** Planned.

### 4.8 Variable-size microbatches

**Description.** Microbatches have different sizes (e.g., variable sequence length in inference). Pipeline schedule accounts for variable per-microbatch latency.

**Status.** Planned. Substrate: variable-size scheduling primitive.

### 4.9 Activation recomputation policies

**Description.** Trade compute for memory: re-execute forward pass during backward instead of storing activations. Per-layer policy.

**Reference.** Chen et al. 2016 (gradient checkpointing).

**Status.** Planned.

**Fixy DSL.** `fixy::pipeline::Recompute<policy = fixy::recompute::Selective>`.

### 4.10 Dynamic pipeline rebalancing

**Description.** When measured per-stage time imbalance exceeds threshold, reassign layers to stages. Rare event but needed for heterogeneous fleets.

**Status.** Planned.

**Composition.** Triggered by §5.5 per-event optimizer.

## §13.3 Crucible substrate vs Fixy DSL split — Category 4 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| Pipeline scheduler primitive | `cntp/pipeline/Scheduler.h` | (uses) |
| Microbatch dispatch | `cntp/pipeline/Microbatch.h` | (uses) |
| Activation recomputation | `cntp/pipeline/Recompute.h` | (uses) |
| Weight stashing | `cntp/pipeline/WeightStash.h` | (uses) |
| Bubble computation | `cntp/pipeline/Bubble.h` | (uses) |
| Schedule choice | (none) | `fixy::pipeline::*` |
| Stage assignment | (none) | `fixy::pipeline::assign<>` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §14. Category 5 — Tensor Parallelism

## §14.1 Scope

Tensor parallelism partitions individual operations across devices. The most common pattern: GEMM split along output dimension (column parallel) on one layer, along input dimension (row parallel) on the next, with all-reduce between to maintain correctness.

## §14.2 The 9 items

### 5.1 Megatron TP (column + row split)

**Description.** Standard TP from Megatron-LM. Column parallel on attention QKV projections + MLP up-projection; row parallel on attention output + MLP down-projection. All-reduce between row-parallel and next column-parallel.

**Reference.** Shoeybi et al. 2019, Korthikanti et al. 2022.

**Status.** Planned.

**Fixy DSL.** `fixy::tp::Megatron<degree = 8>`.

### 5.2 Sequence parallelism (Megatron-SP)

**Description.** Adds parallelism along the sequence dimension on top of TP. Reduces activation memory.

**Reference.** Korthikanti et al. 2022.

**Status.** Planned.

**Fixy DSL.** `fixy::tp::MegatronSp<tp = 8, sp = 8>`.

### 5.3 Context parallelism (Megatron-CP, ring attention)

**Description.** Distributes attention computation across devices via ring-based KV exchange. Enables training on very long sequences (millions of tokens).

**Reference.** Liu et al. 2023 (Ring Attention), Megatron-CP.

**Status.** Planned.

**Fixy DSL.** `fixy::tp::ContextParallel<degree = 8>`.

### 5.4 Ulysses sequence parallelism

**Description.** Alternative SP using all-to-all to redistribute sequence chunks across devices. Different communication pattern than Megatron-SP.

**Reference.** Jacobs et al. 2023.

**Status.** Planned.

**Fixy DSL.** `fixy::tp::Ulysses<degree = 8>`.

### 5.5 DeepSpeed-Ulysses

**Description.** DeepSpeed's productionization of Ulysses with framework integrations.

**Status.** Planned. Substrate-equivalent to 5.4.

### 5.6 Async-TP with comm overlap

**Description.** Overlaps TP all-reduce with subsequent computation. Standard optimization in production stacks.

**Status.** Planned.

**Fixy DSL.** `fixy::tp::Megatron<degree = 8, overlap = fixy::Schedule::Async>`.

### 5.7 2.5D tensor parallelism

**Description.** Halfway between 2D and 3D TP. Reduces communication cost vs 2D for very large degrees.

**Reference.** Wang et al. 2021.

**Status.** Planned.

**Fixy DSL.** `fixy::tp::Tensor2_5D<degree = 16>`.

### 5.8 Tesseract 3D tensor parallelism

**Description.** 3D mesh of devices, each holding a 3D shard of the weight matrix. Bandwidth-optimal for very large TP degrees.

**Reference.** Bian et al. 2021.

**Status.** Planned.

**Fixy DSL.** `fixy::tp::Tesseract<x = 4, y = 4, z = 4>`.

### 5.9 Custom mesh shapes

**Description.** TP degree need not be power of 2 or square. User specifies arbitrary mesh shape.

**Status.** Planned.

**Fixy DSL.** `fixy::tp::Custom<mesh = fixy::Mesh<...>>`.

## §14.3 Crucible substrate vs Fixy DSL split — Category 5 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| TP partitioning primitive | `cntp/tp/Partition.h` | (uses) |
| Column-parallel GEMM | IR002 KernelNode variant | (uses) |
| Row-parallel GEMM | IR002 KernelNode variant | (uses) |
| Ring attention KV exchange | `cntp/tp/RingAttention.h` | (uses) |
| All-to-all sequence redistribute | (cat 1.8 substrate) | (uses) |
| TP scheme choice | (none) | `fixy::tp::*` |
| Mesh shape | (none) | `fixy::Mesh<>` template |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §15. Category 6 — FSDP / ZeRO Families

## §15.1 Scope

FSDP (Fully Sharded Data Parallel) and ZeRO (Zero Redundancy Optimizer) shard model state across data-parallel ranks to reduce per-rank memory. Three levels of sharding: optimizer state (ZeRO-1), gradients (ZeRO-2), parameters (ZeRO-3 / FSDP). Plus offload variants moving state to CPU/NVMe.

## §15.2 The 11 items

### 6.1 ZeRO-1 (optimizer state sharding)

**Description.** Optimizer state (Adam first/second moments) sharded across DP ranks. Reduces optimizer memory by N. Each rank's optimizer step covers only its shard; all-gather to combine.

**Reference.** Rajbhandari et al. 2020 (ZeRO).

**Status.** Planned.

**Fixy DSL.** `fixy::fsdp::Zero<level = 1>`.

### 6.2 ZeRO-2 (gradient sharding)

**Description.** ZeRO-1 + gradient sharding. Reduce-scatter instead of all-reduce. Each rank holds only its gradient shard.

**Status.** Planned.

**Fixy DSL.** `fixy::fsdp::Zero<level = 2>`.

### 6.3 ZeRO-3 (parameter sharding) / FSDP

**Description.** ZeRO-2 + parameter sharding. Each rank holds only its parameter shard. All-gather before each forward layer; all-gather + reduce-scatter for backward.

**Reference.** Rajbhandari et al. 2020, PyTorch FSDP (Zhao et al. 2023).

**Status.** Planned.

**Fixy DSL.** `fixy::fsdp::Zero<level = 3>`.

### 6.4 ZeRO-Infinity (CPU/NVMe offload)

**Description.** Parameters + optimizer state offloaded to CPU memory or NVMe. Reduces GPU memory at cost of CPU/NVMe bandwidth.

**Reference.** Rajbhandari et al. 2021.

**Status.** Planned.

**Fixy DSL.** `fixy::fsdp::Zero<level = 3, offload = fixy::Offload::Nvme>`.

### 6.5 FSDP (PyTorch)

**Description.** PyTorch's productionization of ZeRO-3. Various optimizations and APIs.

**Status.** Planned. Substrate-equivalent to 6.3 with PyTorch-specific shim.

### 6.6 FSDPv2 (per-parameter sharding)

**Description.** FSDP with finer-grained sharding (per-parameter rather than per-module). Better memory efficiency, more flexible mixed precision.

**Status.** Planned.

**Fixy DSL.** `fixy::fsdp::FsdpV2<>`.

### 6.7 HSDP (Hybrid Sharded Data Parallel)

**Description.** TP × FSDP composition. Within TP group: FSDP-style sharding. Across TP groups: standard data parallel. Standard for hyperscale training.

**Status.** Planned.

**Fixy DSL.** `fixy::fsdp::Hybrid<tp_degree = 8, dp_degree = 64>`.

### 6.8 ZeRO-Offload variants

**Description.** Various offload strategies (CPU only, NVMe only, hybrid). Per-tensor offload decisions.

**Status.** Planned.

### 6.9 Activation offload (host / NVMe)

**Description.** Activations stored to CPU memory or NVMe between forward and backward. Trades bandwidth for memory.

**Status.** Planned.

**Fixy DSL.** `fixy::offload::Activations<target = fixy::Offload::Host>`.

### 6.10 Optimizer state offload

**Description.** Optimizer state (Adam moments) offloaded to host. Combined with overlap, hides offload latency.

**Status.** Planned.

### 6.11 Gradient accumulation with FSDP

**Description.** Multi-step gradient accumulation under FSDP sharding. Requires careful orchestration to avoid early all-gather.

**Status.** Planned.

**Fixy DSL.** `fixy::fsdp::Zero<level = 3, gradient_accumulation_steps = 4>`.

## §15.3 The "FSDPv3" hypothetical

The user's earlier FSDPv3 concern motivates this section. Today, writing FSDPv3 in PyTorch requires forking the framework and modifying ~5000 LOC across multiple subsystems. In Fixy:

```cpp
struct FSDPv3 : fixy::DistributedStrategy {
    // User's novel design choices:

    // (1) Per-parameter sharding with custom strategy per layer kind
    template <fixy::ModelLike M, fixy::MeshLike Mesh>
    constexpr auto shard(M&& model, Mesh mesh) const noexcept {
        return fixy::for_each_parameter(std::forward<M>(model),
            [mesh](auto& param, auto layer_kind) {
                if constexpr (layer_kind == fixy::LayerKind::Attention) {
                    // 2D shard for attention: TP × DP
                    return fixy::shard_2d<Mesh::tp_axis, Mesh::dp_axis>(param);
                } else if constexpr (layer_kind == fixy::LayerKind::Mlp) {
                    // 1D shard for MLP: TP only, replicate over DP
                    return fixy::shard_along<Mesh::tp_axis>(param);
                } else {
                    // 1D shard for everything else: DP only
                    return fixy::shard_along<Mesh::dp_axis>(param);
                }
            });
    }

    // (2) Per-shard forward with overlap
    template <fixy::ShardLike S, fixy::TensorLike T>
    constexpr auto forward_shard(S&& shard, T&& input) const noexcept {
        // Overlap all-gather with computation of previous layer
        co_await fixy::all_gather<
            schedule = fixy::Schedule::Overlapped,
            chunks = 4,
            recipe = fixy::recipe::BITEXACT_TC
        >(std::forward<S>(shard));

        co_return shard(std::forward<T>(input));
    }

    // (3) Custom backward with reduce-scatter and FP8 gradient compression
    template <fixy::ShardLike S, fixy::TensorLike T>
    constexpr auto backward_shard(S&& shard, T&& grad) const noexcept {
        auto grad_quantized = fixy::quantize<fixy::dtype::Fp8>(std::forward<T>(grad));

        co_await fixy::reduce_scatter<
            schedule = fixy::Schedule::Overlapped,
            chunks = 4
        >(grad_quantized);

        co_return apply_gradient_to_shard(shard, grad_quantized);
    }

    // (4) Optimizer state offload to NVMe with prefetch
    using optimizer_state_storage = fixy::Offload::Nvme<
        prefetch_depth = 2,
        compression = fixy::compress::Lz4<level = fast>
    >;
};

// Use it:
fixy::train<FSDPv3>(model, dataset, mesh, recipe);
```

The user wrote ~70 lines of C++ DSL. No Crucible/Fixy modification. The strategy:
- Composes with the existing all_gather, reduce_scatter primitives (cat 1)
- Uses the FP8 quantization primitive (cat 3)
- Uses the NVMe offload primitive (cat 6.4)
- Uses overlap scheduling (cat 1.12)
- Inherits crash-stop tolerance from the underlying session-type machinery

The Fixy compiler verifies:
- Concept satisfaction: `FSDPv3 satisfies DistributedStrategy`
- Row consistency: combined row metadata across all strategy methods
- Type consistency: shard / forward / backward signatures chain correctly
- Resource budget: combined resource consumption fits Cog capacity
- Recipe consistency: BITEXACT_TC is achievable through the composition

If any verification fails, compile error with routed diagnostic. If all pass, the strategy is type-safe by construction. This is the load-bearing capability that made the user say "you can't write FSDPv3 in PyTorch" — and that Fixy makes possible.

## §15.4 Crucible substrate vs Fixy DSL split — Category 6 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| Sharding primitive | `cntp/fsdp/Shard.h` | (uses) |
| Per-parameter shard tracking | `cntp/fsdp/ShardRegistry.h` | (uses) |
| All-gather + reduce-scatter | (cat 1 substrate) | (uses) |
| Offload primitive | `cntp/fsdp/Offload.h` | (uses) |
| `DistributedStrategy` concept | `fixy/Strategy.h` | (uses) |
| Per-parameter shard policy | (none) | `fixy::shard_along<>`, `fixy::shard_2d<>` |
| Strategy composition | (none) | user code in DSL |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §16. Category 7 — Expert Parallelism (MoE)

## §16.1 Scope

Mixture-of-Experts (MoE) models route each token through a small subset of experts (typically top-k of N experts). At training scale, experts are partitioned across devices (Expert Parallelism, EP). The routing produces sparse, irregular all-to-all communication that dominates step time in well-tuned MoE training.

DeepSeek-V3, Mixtral, Switch Transformer, GShard — every modern MoE production system has its own routing variant. Each variant interacts non-trivially with EP, TP, and DP composition.

## §16.2 The 11 items

### 7.1 EP (Expert Parallelism — different experts on different devices)

**Description.** Each device holds a subset of the N experts. Tokens are routed to their assigned expert via all-to-all communication. After expert computation, results are routed back via reverse all-to-all.

**Reference.** Lepikhin et al. 2020 (GShard), Fedus et al. 2021 (Switch Transformer).

**Status.** Planned.

**Substrate.** `crucible/cntp/moe/Ep.h`. Provides:
- Expert assignment table per Cog
- Per-token routing computation
- Sparse all-to-all integration (cat 1.10)
- Reverse all-to-all for result return

```cpp
namespace crucible::cntp::moe {

template <uint32_t NumExperts, uint32_t TopK>
struct ExpertAssignment {
    Refined<bounded_above<NumExperts>, uint32_t> num_experts;
    Refined<bounded_above<TopK>, uint8_t> top_k;
    safety::Borrowed<const cog::CogIdentity, source::ExpertMap> expert_to_cog;
};

template <typename Token, uint32_t TopK>
auto route_tokens(
    safety::Borrowed<Token, source::Local> tokens,
    safety::Borrowed<float, source::Gating> gating_logits,
    ExpertAssignment<...> const& assignment
) noexcept -> Computation<
    Row<effects::Bg,
        effects::SmBudget<4>,
        effects::HbmBandwidth<NumTokens * TopK * sizeof(uint32_t)>>,
    RoutingPlan<TopK>>;

}
```

**Fixy DSL.**

```cpp
fixy::moe::ep<num_experts = 64, top_k = 2>(model, mesh);
```

**MAP-Elites variants.** Routing kernel SM count, all-to-all chunk size, capacity factor pre-allocation strategy.

### 7.2 EP + TP composition

**Description.** Within an expert, TP partitions the expert's GEMM. Across experts, EP partitions the expert population. 2D mesh: TP_axis × EP_axis. Standard for very large MoE models (DeepSeek-V3 uses TP=4, EP=64).

**Status.** Planned.

**Fixy DSL.**

```cpp
fixy::moe::ep_tp<
    num_experts = 256,
    top_k = 8,
    tp_degree = 4,
    ep_degree = 64
>(model, mesh);
```

**Composition.** Each expert is sharded across `tp_degree` Cogs; each (expert × TP-shard) pair occupies one Cog. Token routing all-to-all happens across `ep_degree`; intra-expert TP all-reduce happens across `tp_degree`. The two collectives are independent.

### 7.3 Capacity-factor tuning

**Description.** Each expert has a capacity (max tokens per step). Capacity factor = capacity / (tokens_per_device × top_k / num_experts). Higher factor = less token dropping but more padding; lower factor = more dropping but better efficiency.

**Reference.** GShard, Switch Transformer.

**Status.** Planned.

**Substrate.** `crucible/cntp/moe/Capacity.h` provides:
- Per-step capacity computation
- Token-drop policy enforcement
- Capacity-overflow detection + overflow handling

**Fixy DSL.**

```cpp
fixy::moe::ep<
    num_experts = 64,
    top_k = 2,
    capacity_factor = 1.25,
    drop_policy = fixy::moe::Drop::DropExcess  // alternatives: Pad, Reroute, Replay
>;
```

### 7.4 Dropless MoE (token shuffling)

**Description.** Avoid token dropping entirely. Tokens beyond capacity are shuffled to under-capacity experts. Requires variable-size all-to-all (cat 1.9).

**Reference.** Megablocks (Gale et al. 2023).

**Status.** Planned.

**Substrate.** Composes EP (7.1) + variable-size all-to-all (cat 1.9 substrate).

**Fixy DSL.**

```cpp
fixy::moe::ep<
    num_experts = 64,
    top_k = 2,
    drop_policy = fixy::moe::Drop::Reroute
>;
```

### 7.5 Megablocks-style block-sparse MoE

**Description.** Represent expert computation as block-sparse matmul. Tokens are not duplicated; each token contributes to only its assigned expert's GEMM rows. Eliminates the "dummy capacity padding" of standard MoE.

**Reference.** Gale et al. 2023.

**Status.** Planned.

**Substrate.** `crucible/cntp/moe/BlockSparse.h`. Block-sparse matmul kernel via Mimic.

**Fixy DSL.** `fixy::moe::block_sparse<num_experts = 64, top_k = 2>`.

### 7.6 Token-choice vs expert-choice routing

**Description.** Two routing paradigms:
- **Token-choice:** each token picks its top-K experts. Standard pattern.
- **Expert-choice:** each expert picks its top-K tokens. Symmetric inverse. Better load balancing.

**Reference.** Zhou et al. 2022 (Expert Choice Routing).

**Status.** Planned.

**Fixy DSL.**

```cpp
fixy::moe::ep<
    num_experts = 64,
    top_k_per_token = 2,
    routing = fixy::moe::Routing::TokenChoice
>;

// vs

fixy::moe::ep<
    num_experts = 64,
    top_k_per_expert = 32,  // each expert picks 32 tokens
    routing = fixy::moe::Routing::ExpertChoice
>;
```

### 7.7 Hash routing (deterministic)

**Description.** Route token to expert via hash(token_id). Deterministic, no learned gating. Used as baseline; rarely production-grade alone.

**Status.** Planned.

**Fixy DSL.** `fixy::moe::ep<routing = fixy::moe::Routing::Hash<seed = 0xDEADBEEF>>`.

### 7.8 Noisy top-K gating

**Description.** Add Gaussian noise to gating logits before top-K selection. Encourages exploration during training, helps load balancing.

**Reference.** Shazeer et al. 2017 (sparsely-gated MoE).

**Status.** Planned.

**Fixy DSL.** `fixy::moe::gating::NoisyTopK<noise_scale = 0.01>`.

### 7.9 Switch Transformer routing

**Description.** Top-1 routing (k=1). Simpler than top-K. Per-expert capacity factor + auxiliary load balancing loss.

**Reference.** Fedus et al. 2021.

**Status.** Planned.

**Fixy DSL.** `fixy::moe::ep<top_k = 1, aux_loss = fixy::moe::AuxLoss::LoadBalance>`.

### 7.10 Auxiliary loss-free routing (DeepSeek-V3 style)

**Description.** Load balancing without an auxiliary loss term. Uses bias terms updated based on expert utilization. Cleaner training dynamics, better quality at convergence.

**Reference.** DeepSeek-V3 (2024).

**Status.** Planned.

**Substrate.** `crucible/cntp/moe/AuxFreeRouter.h`. Bias update mechanism.

**Fixy DSL.** `fixy::moe::ep<routing = fixy::moe::Routing::AuxFree<bias_update_rate = 0.001>>`.

### 7.11 Expert-replica with backup (resilience)

**Description.** Each expert has K replicas across different Cogs. If one replica fails or becomes overloaded, traffic re-routes to a sibling replica. Improves resilience and dynamic load balancing.

**Status.** Research-frontier (no production system has this).

**Substrate.** Composes EP + cat 8.6 hot peer replacement + cat 12.9 health scoring.

**Fixy DSL.**

```cpp
fixy::moe::ep<
    num_experts = 64,
    top_k = 2,
    replicas_per_expert = 3,
    failover = fixy::moe::Failover::ReplicaBased
>;
```

## §16.3 The MoE optimization surface

MoE introduces a high-dimensional optimization problem. The discrete-search optimizer (per Z3-removal cleanup) faces:

- **Routing kernel placement.** Which Cogs run the routing kernel. Should it be SM-resident on the same Cog as the producing layer?
- **Expert placement.** Which experts on which Cogs. Static (round-robin) vs dynamic (utilization-aware).
- **Capacity factor.** Per-deployment static vs per-step adaptive.
- **All-to-all topology.** Direct vs hierarchical vs sparse.
- **Replica count.** Tradeoff: more replicas = more memory, less drop rate.
- **Routing strategy.** Token-choice vs expert-choice vs aux-free.
- **TP × EP partition.** Combined with cat 5 TP.

The Fixy DSL exposes each as a tunable. The optimizer picks defaults; user overrides as needed.

## §16.4 Crucible substrate vs Fixy DSL split — Category 7 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| EP routing primitive | `cntp/moe/Ep.h` | (uses) |
| Token routing kernels | `mimic/{nv,am}/moe/Routing.h` | (uses) |
| Block-sparse matmul | `mimic/{nv,am}/BlockSparse.h` | (uses) |
| Capacity computation | `cntp/moe/Capacity.h` | (uses) |
| Aux-free bias updates | `cntp/moe/AuxFreeRouter.h` | (uses) |
| Routing strategy choice | (none) | `fixy::moe::Routing::*` |
| Expert placement strategy | (none) | `fixy::moe::Place::*` |
| Replica failover | (none) | `fixy::moe::Failover::*` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §17. Category 8 — Fault Tolerance + Elasticity

## §17.1 Scope

Production training at scale must tolerate hardware failures. Modern hyperscale clusters see ~1-5 GPU failures per day per 10K GPUs (Meta, Google operational data). Without fault tolerance, mean time to job failure approaches mean time between failures — every 12-hour run completes with probability < 50%.

The four failure-tolerance discipline tiers:

1. **Checkpoint + restart.** The minimum. Every job loses progress to last checkpoint.
2. **Mid-iteration recovery.** Job continues from current step after peer failure.
3. **Elastic membership.** Job adapts to changing fleet size during execution.
4. **Hot peer replacement.** New peer joins running job without restart.

Frontier production stacks reach tier 3-4 (Microsoft Azure, Google Pathways). Open-source typically reaches tier 1 with manual ops glue for tier 2-3.

## §17.2 The 11 items

### 8.1 Checkpoint + restart

**Description.** Periodic save of model + optimizer state to durable storage. On any failure, restart from last checkpoint. Lose work since checkpoint.

**Status.** Substrate exists in `Cipher.h` (855 LOC). Strategy not yet wired.

**Substrate.** Existing `crucible/Cipher.h` provides three-tier persistence (hot/warm/cold). Checkpoint serialization composes existing Cipher API.

**Fixy DSL.**

```cpp
fixy::ft::Checkpoint<
    interval = fixy::Steps<100>,
    storage = fixy::cipher::Cold<bucket = "s3://my-job/ckpts">
>;
```

### 8.2 Async checkpoint (write to NVMe in background)

**Description.** Checkpoint write does not block training. Background thread serializes weights to NVMe; main thread continues training.

**Status.** Planned.

**Substrate.** `crucible/cipher/AsyncCheckpoint.h`. Composes existing Cipher hot tier with new async-write primitive.

**Fixy DSL.** `fixy::ft::AsyncCheckpoint<interval = fixy::Steps<100>>`.

### 8.3 Sharded checkpoint

**Description.** Each rank writes only its parameter shard. Per-rank file naming with content-addressing for deduplication. Restore reconstructs from N shards.

**Status.** Planned.

**Substrate.** `crucible/cipher/ShardedCheckpoint.h`.

**Fixy DSL.** `fixy::ft::ShardedCheckpoint<>`.

### 8.4 Mid-iteration recovery from peer crash

**Description.** When a peer crashes mid-iteration, surviving peers complete the iteration with the available subset, contribute their results to the next round's collective, and the system reshards on the new fleet size.

**Status.** **PARTIAL — substrate ships.** GAPS-001 fix (SessionGlobal::project<StopG>) shipped 2026-05-03 enables crash-safety projection through the session-type tree. Composition into a working FT strategy is planned.

**Substrate.** Existing `bridges/CrashTransport.h:273` (CrashWatchedHandle), `sessions/SessionCrash.h:540` (every_offer_has_crash_branch_for_peer_v walker), `sessions/SessionGlobal.h:1100+` (the GAPS-001 projection fix).

**Fixy DSL.**

```cpp
fixy::ft::MidIterationRecovery<
    crash_detection = fixy::detect::SwimAccrual,
    timeout = fixy::Milliseconds<500>,
    recovery_action = fixy::ft::Recovery::ContinueWithSubset
>;
```

### 8.5 Elastic membership

**Description.** Training continues with N-1 nodes when one is lost; can also accept N+1 if a new node joins. Mesh re-shapes; collectives reconfigure.

**Status.** Planned.

**Substrate.** Composes:
- §3 Cog topology graph (gains/loses nodes via §5 per-event optimizer)
- Cat 11.1 Raft for membership consensus
- Cat 11.4 SWIM for failure detection

**Fixy DSL.**

```cpp
fixy::ft::Elastic<
    min_workers = 4,
    max_workers = 64,
    rejoin_policy = fixy::ft::Rejoin::CanaryThenFull
>;
```

### 8.6 Hot peer replacement

**Description.** New peer joins a running job. Receives weights via background sync, then catches up to current step, then begins contributing to collectives.

**Status.** Planned.

**Substrate.** Composes 8.5 + canary deployment (cat 15.3).

**Fixy DSL.** `fixy::ft::HotReplace<canary_steps = 10, sync_method = fixy::Sync::Streaming>`.

### 8.7 Spot-instance preemption tolerance

**Description.** Spot-VM preemption gives 30-second warning. Job uses warning to: (a) finish current step, (b) write incremental checkpoint, (c) drain in-flight collectives, (d) gracefully exit. Other peers continue without the lost worker.

**Status.** Planned.

**Substrate.** Cloud-specific preemption signal handlers in `crucible/runtime/Preemption.h`.

**Fixy DSL.** `fixy::ft::Preemption<warning_seconds = 30, action = fixy::ft::Drain>`.

### 8.8 Cross-region failover

**Description.** Job can fail over from one DC to another. Active-active (split job across DCs) or active-passive (full replica in standby DC). Triggered by region-wide outage.

**Status.** Planned.

**Substrate.** Composes Cipher cross-region replication + topology graph re-discovery.

**Fixy DSL.** `fixy::ft::CrossRegion<mode = fixy::ft::ActiveActive, regions = ["us-east-1", "eu-west-1"]>`.

### 8.9 Byzantine-tolerant aggregation

**Description.** Aggregation that tolerates malicious peers reporting false gradients. Rules:
- **Krum.** Pick the gradient closest to its k nearest neighbors. Tolerates f Byzantine in N ≥ 2f+3 peers.
- **Bulyan.** Aggregate over Krum-selected subset. Tighter tolerance.
- **Trimmed mean.** Discard top + bottom α fraction; mean the rest.
- **Median.** Per-coordinate median.

**Reference.** Blanchard et al. 2017 (Krum), El Mhamdi et al. 2018 (Bulyan), Yin et al. 2018 (trimmed mean).

**Status.** Planned.

**Substrate.** `crucible/cntp/aggregation/Byzantine.h`.

**Fixy DSL.** `fixy::aggregate::Krum<f = 1>` or `fixy::aggregate::Bulyan<f = 1>` or `fixy::aggregate::TrimmedMean<alpha = 0.1>`.

### 8.10 Detection of corrupted gradients

**Description.** Detect gradients with NaN, Inf, or extreme magnitudes. Discard or replay the step. Different from Byzantine tolerance — detects accidental corruption, not malicious peers.

**Status.** Planned.

**Substrate.** `crucible/cntp/aggregation/CorruptionDetect.h`.

**Fixy DSL.** `fixy::aggregate::DetectCorruption<action = fixy::aggregate::Replay>`.

### 8.11 Optimistic vs pessimistic recovery

**Description.** Two recovery strategies:
- **Optimistic.** Continue training; reconcile on next checkpoint. Fast in common case.
- **Pessimistic.** Pause training; verify state; resume. Slow but safe.

**Status.** Planned.

**Fixy DSL.** `fixy::ft::Recovery::Optimistic` vs `fixy::ft::Recovery::Pessimistic`.

## §17.3 Composition with crash-stop session types

The cat 8 fault tolerance strategies all compose with the GAPS-001-shipped crash-stop machinery. The pattern:

1. **Detection.** SWIM (cat 11.4) + φ-accrual (cat 12.9) detect peer failure.
2. **Type-system propagation.** The session protocol's StopG<Peer> projection (per GAPS-001 fix) routes Stop signals to surviving roles' protocol continuations.
3. **Recovery dispatch.** The protocol's Recv<Crash<Peer>, RecoveryK> branch fires.
4. **Strategy action.** Fixy DSL strategy's recovery callback executes (continue with subset / quarantine peer / await replacement).

The composition is sound because:
- Detection is observable
- Type-system propagation is structurally correct (verified by GAPS-001 multiparty fixtures including 5-7 role scenarios)
- Strategy action is concept-gated (strategy must satisfy CrashTolerantStrategy concept)

## §17.4 The mean-time-between-failure budget

For a job training for D days on N GPUs with per-GPU mean-time-between-failure F (typical: ~10000 hours for H100 / B200), the expected number of GPU failures during the job is approximately D × N × 24 / F.

| D | N | F (hours) | Expected failures |
|---|---|---|---|
| 1 | 1000 | 10000 | 2.4 |
| 7 | 10000 | 10000 | 168 |
| 30 | 10000 | 10000 | 720 |
| 90 | 100000 | 10000 | 21,600 |

For multi-month large-scale runs, failures are routine. Tier-3 (elastic membership) and tier-4 (hot replacement) are not optional. The Fixy DSL forces this by making fault-tolerance strategy a required field in `fixy::TrainConfig` — no default, user must declare.

## §17.5 Crucible substrate vs Fixy DSL split — Category 8 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| Cipher persistence (existing) | `Cipher.h` | (uses) |
| Async checkpoint | `cipher/AsyncCheckpoint.h` | (uses) |
| Sharded checkpoint | `cipher/ShardedCheckpoint.h` | (uses) |
| Crash-stop projection (shipped) | `sessions/SessionGlobal.h` | (uses) |
| CrashWatchedHandle (shipped) | `bridges/CrashTransport.h` | (uses) |
| Byzantine aggregation primitives | `cntp/aggregation/Byzantine.h` | (uses) |
| Corruption detection | `cntp/aggregation/CorruptionDetect.h` | (uses) |
| Preemption signal handler | `runtime/Preemption.h` | (uses) |
| FT strategy choice | (none) | `fixy::ft::*` |
| Recovery policy | (none) | `fixy::ft::Recovery::*` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §18. Category 9 — Inference Networking Patterns

## §18.1 Scope and what's stripped

Most inference-specific patterns are **single-machine compute optimizations** and are out of scope for this networking + distributed compute document. The following items belong to a separate inference architecture doc and are removed here:

- 9.* Continuous batching (single-machine request scheduler)
- 9.* PagedAttention (single-GPU KV layout)
- 9.* Speculative decoding draft+verify (single-machine compute)
- 9.* Tree-of-drafts / Medusa / EAGLE (single-machine compute)
- 9.* KV cache offload to CPU/NVMe (single-machine memory)
- 9.* Multi-LoRA serving (single-machine batching)
- 9.* Request preemption (single-machine scheduler)
- 9.* Cross-request KV reuse / RadixAttention (single-machine prefix cache)

The inference patterns that **are networking + distributed**:

- Prefill / decode disaggregation (cross-Cog KV transfer)
- Streaming token generation (network-side delivery)
- Multi-model routing (distributed request routing)
- Cross-region inference failover (distributed FT)

These are covered below.

## §18.2 The 4 networking-relevant items

### 9.1 Prefill / decode disaggregation

**Description.** Prefill is compute-bound (GEMM-heavy); decode is memory-bound (sequential, low arithmetic intensity). Run them on different Cog sets — prefill on high-compute Cogs, decode on high-memory-bandwidth Cogs. KV cache transfers between them via RDMA. The networking aspect: cross-Cog KV transfer protocol with completion synchronization.

**Reference.** Zhong et al. 2024 (DistServe), Hu et al. 2024 (Splitwise).

**Status.** Planned.

**Substrate.** Composes Cog query (§3) + RDMA KV transfer (cat 1.5 + cat 10.9 GPUDirect).

**Fixy DSL.**

```cpp
fixy::infer::Disaggregate<
    prefill_cogs = fixy::cog_query<>{}.where(...).resolve(),
    decode_cogs = fixy::cog_query<>{}.where(...).resolve(),
    kv_transport = fixy::infer::Transport::RdmaWithGpuDirect
>;
```

### 9.2 Streaming token generation

**Description.** Each generated token is streamed to the client immediately rather than buffering the full response. Network-side: per-token push protocol over SSE / WebSocket / QUIC stream. Reduces client-perceived latency.

**Status.** Planned.

**Substrate.** `crucible/cntp/inference/Stream.h`. Composes existing session-type machinery for streaming protocols.

**Fixy DSL.** `fixy::infer::Stream<transport = fixy::infer::Stream::ServerSentEvents>`.

### 9.3 Multi-model routing

**Description.** Different requests routed to different models based on quality / cost / latency tradeoffs. Router decides per-request which model (and therefore which Cog set) to invoke. Distributed routing across the inference fleet.

**Status.** Planned.

**Fixy DSL.** `fixy::infer::Router<models = [m1, m2, m3], policy = my_routing_policy>`.

### 9.4 Cross-region inference failover

**Description.** When a region fails, in-flight inference requests fail over to another region. Active-active or active-passive. Composes cat 8.8 cross-region failover applied to inference.

**Status.** Planned.

**Fixy DSL.** `fixy::infer::FailoverRegions<regions = [...], mode = fixy::ft::ActiveActive>`.

## §18.3 The inference networking surface

The networking primitives needed for distributed inference:

- **Cross-Cog KV cache transfer** (prefill ↔ decode disaggregation). RDMA write with completion notification. Composes cat 10.9.
- **Streaming response delivery.** Composes existing session machinery with TCP / QUIC streaming.
- **Cross-replica request affinity coordination.** Composes cat 11 consensus primitives lightly.
- **Cross-region failover orchestration.** Composes cat 8.8 + cat 17 federation.

None require new transport primitives beyond what cat 1, 8, 10, 11, 17 provide. The inference category mostly **composes** existing networking primitives with inference-specific request semantics.

## §18.4 Crucible substrate vs Fixy DSL split — Category 9 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| Cross-Cog KV transport (RDMA) | (cat 1, 10 substrate) | (uses) |
| Streaming session protocol | (existing session machinery) | (uses) |
| Cross-region failover | (cat 8.8 substrate) | (uses) |
| Routing policy | (none) | `fixy::infer::Router<>` |
| Disaggregation policy | (none) | `fixy::infer::Disaggregate<>` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §19. Category 10 — Pure Networking Primitives (Frontier)

## §19.1 Scope

This category enumerates the underlying transport, fabric, and offload technologies that everything else composes from. Mostly external (Linux kernel, NIC firmware, switch ASICs); Crucible's role is to expose them through typed primitives with proper concept gates.

## §19.2 The 18 items

### 10.1 RoCEv2 (RDMA over Converged Ethernet)

**Description.** RDMA over standard Ethernet with UDP/IP encapsulation. Requires PFC + ECN for lossless behavior (or DCQCN for end-to-end CC). Standard for hyperscale GPU fabrics.

**Status.** Planned (GAPS-125 covers RoCE config; verb-level primitives in GAPS-125+).

**Substrate.** `crucible/cntp/Rdma.h` (verb wrappers), `crucible/cntp/RoceConfig.h` (PFC/ECN/DCQCN setup).

**Fixy DSL.** `fixy::cntp::transport = fixy::Transport::Rdma<protocol = fixy::Roce::V2>`.

### 10.2 iWARP (TCP-based RDMA)

**Description.** RDMA over TCP. Less efficient than RoCE but works on lossy networks without PFC. Used in Intel E810 NICs.

**Status.** Planned.

**Fixy DSL.** `fixy::Transport::Rdma<protocol = fixy::Iwarp>`.

### 10.3 UCX (Unified Communication X)

**Description.** Vendor-neutral communication library. Abstracts over RDMA, TCP, shared memory, GPU-aware transports. Used by OpenMPI and other frameworks.

**Status.** Planned. Crucible's substrate primitives provide UCX-equivalent abstractions natively (no UCX dependency).

### 10.4 libfabric (OpenFabrics)

**Description.** Vendor-neutral fabric API similar to UCX. Different abstraction approach.

**Status.** Planned. Same approach as UCX — Crucible provides equivalent abstractions natively.

### 10.5 NVSHMEM (one-sided device-initiated comm)

**Description.** SHMEM-style one-sided communication initiated from CUDA kernels. SM-resident put/get. Eliminates host involvement on data path.

**Reference.** NVIDIA NVSHMEM.

**Status.** Planned.

**Substrate.** `crucible/cntp/Nvshmem.h`. Wraps NVSHMEM library where available; provides equivalent primitive (device-initiated RDMA verb posting via doorbell from SM) where not.

**Fixy DSL.** `fixy::cntp::shmem<>`.

### 10.6 ROCm-aware OpenMPI

**Description.** OpenMPI variant with ROCm/HIP integration for AMD GPUs. AMD's analog of CUDA-aware MPI.

**Status.** Planned. Crucible's per-vendor backends use direct verbs, no MPI dependency.

### 10.7 NCCL replacement (vendor-neutral)

**Description.** The big one. Replace NCCL/RCCL with vendor-neutral collectives (cat 1 substrate) that bit-exact-equivalent across NV/AM/CPU.

**Status.** Planned. This is essentially cat 1 + per-vendor Mimic backends.

### 10.8 CXL.cache + CXL.mem (coherent fabric)

**Description.** CXL provides cache-coherent memory access across PCIe. CXL.mem allows GPUs to access pooled memory; CXL.cache allows coherent caching across devices. Game-changer for memory-bound workloads.

**Status.** Planned.

**Substrate.** `crucible/cntp/Cxl.h`. CXL device discovery + memory region exposure.

**Fixy DSL.** `fixy::cntp::cxl_pool<>` for pool addressing.

### 10.9 GPUDirect RDMA (GPU↔NIC bypass)

**Description.** NIC reads/writes directly from/to GPU memory without CPU involvement. Required for high-throughput GPU collectives. ConnectX-5+ on NVIDIA, Intel/AMD analogs.

**Status.** Planned (GAPS-132).

**Substrate.** `crucible/cntp/GpuDirect.h`. Pin GPU memory as RDMA MR; verb posting targets GPU pages.

**Fixy DSL.** `fixy::cntp::gpu_direct<>` (eligibility query + automatic use when present).

### 10.10 GPUDirect Storage (GPU↔NVMe bypass)

**Description.** GPU reads/writes directly from/to NVMe. Used for KV cache offload, dataset streaming.

**Status.** Planned.

**Substrate.** `crucible/cntp/GpuDirectStorage.h`.

**Fixy DSL.** `fixy::cipher::gpu_direct_storage<>`.

### 10.11 NVMe-oF (NVMe over Fabrics)

**Description.** NVMe protocol over RDMA / TCP / FibreChannel. Remote NVMe drives appear local.

**Status.** Planned.

**Substrate.** `crucible/cntp/NvmeOf.h`.

### 10.12 SmartNIC offload (TCP, IPsec, kTLS)

**Description.** NIC firmware handles transport-layer protocols. ConnectX-6/7, Intel E810, AWS Nitro, BlueField.

**Status.** Planned (covered by cat 14.4, 14.5 + DPU primitives).

### 10.13 P4-programmable switches (Tofino)

**Description.** Custom switch dataplane via P4. SHARP runs on this layer. INT (in-band network telemetry) runs on this layer.

**Status.** Planned (GAPS-145).

**Substrate.** `crucible/cntp/P4.h`.

**Fixy DSL.** `fixy::cntp::p4_program<my_program>(switch_cog)`.

### 10.14 Optical Circuit Switching (Google's Jupiter / Aquila)

**Description.** Circuit-switched optical paths. Reconfigured per-collective for bandwidth optimization. Google's hyperscale fabric.

**Status.** Research-frontier. No open hardware available; Crucible can support via P4 abstraction once vendors expose it.

### 10.15 Topology-aware routing (CLOS, dragonfly, fat-tree, hypercube)

**Description.** Different fabric topologies imply different optimal routing patterns. Crucible's topology graph (§3) supports any topology; routing primitive picks per-fabric.

**Status.** Planned. Composes §3 + cat 1.15 topology-aware ring.

### 10.16 ECMP (Equal Cost Multi-Path)

**Description.** Standard L3 multi-path routing. Routers hash flow tuple to pick path. Crucible exposes per-flow path selection.

**Status.** Planned.

**Substrate.** `crucible/cntp/Ecmp.h`. Hash function + path selection helper.

### 10.17 Per-flow vs per-packet load balancing

**Description.** ECMP is per-flow (consistent path per 5-tuple). Per-packet spreading (used in some fabrics) gives better balancing but requires reordering tolerance at receiver.

**Status.** Planned.

**Fixy DSL.** `fixy::cntp::lb_policy = fixy::Lb::PerFlow` or `fixy::Lb::PerPacket<reorder_tolerance = ...>`.

### 10.18 Adaptive routing (UltraEthernet Consortium)

**Description.** Switches dynamically reroute around congestion. UEC spec defines this for next-gen Ethernet fabrics.

**Status.** Research-frontier (UEC spec emerging).

**Substrate.** Will compose with §5 telemetry feedback loop.

## §19.3 The transport eligibility matrix

For each Fixy session protocol, the type system computes which transports are eligible:

| Protocol requirement | Eligible transports |
|---|---|
| `Latency<2us>` | RDMA (10.1, 10.2), NVSHMEM (10.5), GPUDirect (10.9) |
| `Latency<10us>` | + AF_XDP (cat 7), kernel TCP+XDP |
| `Latency<100us>` | + standard TCP, QUIC |
| `Bandwidth<100Gbps>` | RDMA, GPUDirect, AF_XDP |
| `Encryption<TlsRequired>` | + kTLS (10.12), TLS via QUIC, software TLS |
| `Multicast<true>` | XDP_TX replication (cat 7.4), P4 multicast (10.13), overlay multicast (cat 11.7) |
| `BiDirectional<true>` | All except one-sided RDMA-WRITE-only |
| `Atomics<true>` | RDMA atomic verbs only |

Compile-time check that the user's session protocol can be assigned to an available transport. Compile fail otherwise with diagnostic naming the unmet requirement.

## §19.4 Crucible substrate vs Fixy DSL split — Category 10 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| RDMA verb wrappers | `cntp/Rdma.h` | (uses) |
| RoCE configuration | `cntp/RoceConfig.h` | (uses) |
| GPUDirect primitives | `cntp/GpuDirect.h`, `cntp/GpuDirectStorage.h` | (uses) |
| NVSHMEM-equivalent | `cntp/Nvshmem.h` | (uses) |
| CXL primitives | `cntp/Cxl.h` | (uses) |
| P4 program loader | `cntp/P4.h` | (uses) |
| ECMP helper | `cntp/Ecmp.h` | (uses) |
| Transport eligibility check | concept gates | `fixy::TransportEligible<>` |
| Transport choice | (none) | `fixy::Transport::*` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §20. Category 11 — Coordination + Consensus + Membership

## §20.1 Scope

Distributed coordination primitives. Used internally by Crucible (for Canopy mesh, Cipher coordination, fleet membership) and exposed to Fixy users for application-level coordination needs (distributed locks, shared counters, leader election).

The core insight: each consensus / membership protocol has different latency/reliability/scalability tradeoffs. Crucible exposes the primitives; Fixy users pick per use case.

## §20.2 The 14 items

### 11.1 Raft (single-leader consensus)

**Description.** Single-leader replicated state machine. Strong consistency with bounded latency. Standard for systems needing linearizable operations.

**Reference.** Ongaro & Ousterhout 2014.

**Status.** Planned.

**Substrate.** `crucible/canopy/Raft.h`. Implementation with:
- Leader election
- Log replication
- Membership changes
- Snapshot installation

```cpp
namespace crucible::canopy {

template <typename StateMachine>
class RaftCluster : Pinned {
public:
    [[nodiscard]] auto propose(StateMachine::Command cmd) noexcept
        -> Computation<Row<effects::Bg, effects::IO, effects::Block>,
                       std::expected<typename StateMachine::Response, RaftError>>;

    [[nodiscard]] auto leader() const noexcept
        -> std::optional<cog::CogIdentity>;
};

[[nodiscard]] auto mint_raft<StateMachine>(
    effects::Init init,
    std::span<const cog::CogIdentity> members,
    RaftConfig config
) noexcept -> std::expected<RaftCluster<StateMachine>, RaftError>;

}
```

**Fixy DSL.** `fixy::raft<MyStateMachine>(members, config)`.

### 11.2 Multi-Paxos / EPaxos

**Description.** Alternative consensus protocols. Multi-Paxos is leader-based like Raft. EPaxos is leaderless (any replica can propose). EPaxos lower latency in WAN settings; harder to implement.

**Reference.** Lamport 1998 (Paxos), Moraru et al. 2013 (EPaxos).

**Status.** Planned. Substrate API mirror of Raft.

### 11.3 Viewstamped Replication (Revisited)

**Description.** Earlier consensus protocol that's equivalent to Raft. Some literature uses VR terminology.

**Reference.** Liskov & Cowling 2012.

**Status.** Planned. Substrate same as Raft.

### 11.4 SWIM (membership)

**Description.** Scalable Weakly-consistent Infection-style process Membership. Each node periodically pings random peer; if no ack, marks suspect; if multiple peers confirm dead, marks dead. Detection time bounded; false-positive rate low.

**Reference.** Das et al. 2002.

**Status.** Planned (GAPS-114).

**Substrate.** `crucible/canopy/Swim.h`.

```cpp
namespace crucible::canopy {

class SwimMembership : Pinned {
public:
    [[nodiscard]] auto add_peer(cog::CogIdentity peer) noexcept
        -> std::expected<void, SwimError>;

    [[nodiscard]] auto health(const cog::CogIdentity& peer) const noexcept
        -> Stale<PeerHealth>;

    [[nodiscard]] auto live_peers() const noexcept
        -> safety::Borrowed<const cog::CogIdentity, source::SwimMember>;
};

}
```

**Fixy DSL.** `fixy::canopy::swim_membership<>(initial_peers, config)`.

### 11.5 Lifeguard (improved SWIM)

**Description.** SWIM with better congestion behavior + asymmetric failure handling. Reduces false positives in WAN settings.

**Reference.** Dadgar et al. 2018 (HashiCorp).

**Status.** Planned. Substrate extension of SWIM.

### 11.6 HyParView (gossip overlay)

**Description.** Two-layer membership: small "active view" (heartbeats + data) + large "passive view" (backup peers). Survives high churn.

**Reference.** Leitão et al. 2007.

**Status.** Planned.

**Substrate.** `crucible/canopy/HyParView.h`.

### 11.7 Plumtree (gossip + spanning tree)

**Description.** Builds optimal spanning tree from gossip. Latency-optimal for stable topology.

**Reference.** Leitão et al. 2007.

**Status.** Planned.

**Substrate.** `crucible/canopy/Plumtree.h`.

### 11.8 Scuttlebutt (anti-entropy)

**Description.** Periodic delta exchange for eventual consistency. Each peer maintains version vectors; pulls deltas from peers periodically. Used in Cassandra, Riak.

**Reference.** Van Renesse et al. 2008.

**Status.** Planned (GAPS-115).

**Substrate.** `crucible/canopy/Scuttlebutt.h`. Composes with topology graph CRDT.

### 11.9 CRDT-based state

**Description.** Conflict-free Replicated Data Types. Eventually-consistent state where any merge order produces the same result. G-Set, OR-Set, LWW-Register, RGA, JSON-CRDT.

**Reference.** Shapiro et al. 2011.

**Status.** Planned.

**Substrate.** `crucible/canopy/Crdt.h`.

```cpp
template <typename Element>
class GSet { /* grow-only set */ };

template <typename Element>
class OrSet { /* observed-remove set */ };

template <typename Value, typename Clock>
class LwwRegister { /* last-writer-wins register */ };

template <typename Element>
class RgaList { /* replicated growable array */ };
```

### 11.10 Hybrid Logical Clocks (HLC)

**Description.** Sub-microsecond ordering across nodes. Combines physical clock + logical counter. Bounded clock drift; doesn't require GPS like Spanner's TrueTime.

**Reference.** Kulkarni et al. 2014.

**Status.** Planned (GAPS-138).

**Substrate.** `crucible/canopy/Hlc.h`.

### 11.11 TrueTime-style (Spanner)

**Description.** Bounded clock uncertainty via GPS + atomic clocks. Enables externally-consistent transactions. Google-internal infrastructure.

**Status.** Research-frontier. Out of scope for open-source Crucible.

### 11.12 CAS-based atomic broadcast

**Description.** RDMA atomic operations enable distributed CAS. Build atomic broadcast on top.

**Status.** Planned. Composes RDMA atomic verbs.

### 11.13 Vector clocks

**Description.** Per-node logical timestamp vector. Tracks happens-before relationships across nodes. Foundational for many CRDTs.

**Status.** Planned.

**Substrate.** `crucible/canopy/VectorClock.h`.

### 11.14 Lattice-based agreement (CALM theorem)

**Description.** Computations expressible as monotonic functions over lattices can be coordinated without consensus. Massive performance win for problems that fit.

**Reference.** Hellerstein & Alvaro 2020.

**Status.** Research-frontier.

**Composition.** Crucible's existing lattice machinery (algebra/lattices/) provides the substrate. CALM-aware Fixy strategies can recognize monotonic operations and skip consensus.

## §20.3 The consensus selection matrix

| Use case | Recommended primitive |
|---|---|
| Cipher hot-tier metadata | Raft (linearizable, low N) |
| Cipher cold-tier metadata | Raft + S3 backing |
| Cog membership tracking | SWIM + Scuttlebutt |
| Topology graph distribution | Scuttlebutt CRDT (LWW-Register per edge) |
| Distributed lock | Raft-backed lease |
| Counter (eventual) | G-Counter CRDT |
| Counter (linearizable) | Raft-backed counter |
| Set membership (eventual) | OR-Set CRDT |
| Per-step training coordination | Lattice-based (CALM) where possible, else Raft |

## §20.4 Crucible substrate vs Fixy DSL split — Category 11 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| Raft implementation | `canopy/Raft.h` | (uses) |
| SWIM implementation | `canopy/Swim.h` | (uses) |
| Scuttlebutt CRDT | `canopy/Scuttlebutt.h` | (uses) |
| HLC primitive | `canopy/Hlc.h` | (uses) |
| CRDT types | `canopy/Crdt.h` | (uses) |
| Vector clocks | `canopy/VectorClock.h` | (uses) |
| Consensus protocol choice | (none) | `fixy::canopy::*` |
| Per-use-case selection | (none) | composed at strategy level |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §21. Category 12 — Observability + Reliability

## §21.1 Scope

Observability for distributed systems is hard. Crucible's contribution: every observability primitive is a Fixy DSL element, type-system-integrated, no separate "monitoring stack" to maintain.

## §21.2 The 13 items

### 12.1 Pingmesh (all-pairs latency)

**Description.** Every host pings every other host every K seconds. Latency distribution per (source, destination) pair tracked over time. Anomalies (single rack dark, one host slow, asymmetric latency) jump out as heatmap discontinuities.

**Reference.** Guo et al. 2015 (Microsoft).

**Status.** Planned (GAPS-134).

**Substrate.** `crucible/topology/Pingmesh.h`. Composes φ-accrual (12.9) + HDR-histogram (12.8 below).

**Fixy DSL.** `fixy::observe::pingmesh<period_ms = 1000>`.

### 12.2 End-to-end checksumming

**Description.** xxHash64 over message payload at sender; verify at receiver. Catches silent data corruption that hardware CRC misses (corruption inside switches, between RX-CRC-check and TX-CRC-recompute).

**Reference.** Stone & Partridge 2000.

**Status.** Planned (GAPS-116).

**Substrate.** `crucible/cntp/Integrity.h`. xxHash64 implementation, vectorized via AVX2.

**Fixy DSL.** `fixy::cntp::integrity<algorithm = fixy::hash::XxHash64>`.

### 12.3 Reed-Solomon FEC for unicast

**Description.** Erasure coding (k+m): k data shards, m parity shards. Receiver can reconstruct from any k shards. Tolerates m losses.

**Reference.** Reed & Solomon 1960. AVX2-optimized implementations available (Intel ISA-L, Plank's jerasure).

**Status.** Planned (GAPS-117).

**Substrate.** `crucible/cntp/Fec.h`. RS encoder/decoder, GF-256 arithmetic, AVX2 vectorized.

```cpp
namespace crucible::cntp::fec {

template <uint8_t K, uint8_t M>
class ReedSolomon {
public:
    [[nodiscard]] auto encode(
        std::span<const std::byte> input,
        std::span<std::byte> output_with_parity
    ) const noexcept -> std::expected<void, FecError>;

    [[nodiscard]] auto decode(
        std::span<const std::byte> received,
        std::span<const bool> erasure_mask,
        std::span<std::byte> output
    ) const noexcept -> std::expected<void, FecError>;
};

}
```

**Fixy DSL.** `fixy::cntp::fec<k = 10, m = 2>` (10 data + 2 parity, 20% overhead, survives 2 losses).

### 12.4 LT codes / Raptor codes for multicast

**Description.** Fountain codes for multicast / gossip. Sender produces unbounded stream of parity packets; any receiver that gets "enough" packets can decode regardless of which packets were lost.

**Reference.** Luby 2002 (LT codes), Shokrollahi 2006 (Raptor codes).

**Status.** Planned (GAPS-119).

**Substrate.** `crucible/cntp/Fountain.h`.

**Fixy DSL.** `fixy::cntp::fountain<algorithm = fixy::fountain::Raptor10>`.

### 12.5 Network telemetry (gNMI, IPFIX, sFlow)

**Description.** Standard switch/router telemetry protocols. Push-based (gNMI streaming, IPFIX) or pull-based (sFlow sampling). Crucible ingests these into the topology graph + cost model.

**Status.** Planned.

**Substrate.** `crucible/topology/SwitchTelemetry.h`.

### 12.6 In-band Network Telemetry (INT)

**Description.** Switches embed telemetry data into packet headers as packets transit. Receiver extracts per-hop latency, queue depth, drop counts. Provides per-packet visibility instead of per-flow sampling.

**Reference.** P4 INT spec.

**Status.** Planned.

**Substrate.** Composes P4 program (10.13) with header-extraction primitive.

**Fixy DSL.** `fixy::observe::int_telemetry<>`.

### 12.7 Distributed tracing

**Description.** Trace IDs propagate across hops; each component records spans; backend correlates. OpenTelemetry-compatible.

**Status.** Planned.

**Substrate.** `crucible/observe/Trace.h`. Trace context type as row tag; span recording macro.

**Fixy DSL.** `fixy::observe::trace<exporter = fixy::trace::Otlp>`.

### 12.8 Continuous profiling

**Description.** Always-on `perf record` style profiling, sub-1% overhead with frame pointers. Production teams keep this on permanently.

**Status.** Planned.

**Substrate.** `crucible/observe/ContinuousProfile.h`. eBPF-based profiler integration.

### 12.9 φ-accrual failure detection

**Description.** Computes suspicion level φ = -log10(P(no_heartbeat | history)). Continuously monotonic; application picks action threshold. Adapts to observed RTT distribution per peer.

**Reference.** Hayashibara et al. 2004.

**Status.** Planned (GAPS-113).

**Substrate.** `crucible/topology/Health.h`.

```cpp
namespace crucible::topology {

class PhiAccrualDetector {
public:
    auto record_heartbeat(const cog::CogIdentity& peer) noexcept -> void;
    auto suspicion_phi(const cog::CogIdentity& peer) const noexcept -> double;
};

}
```

**Fixy DSL.** `fixy::observe::phi_accrual<threshold = 8.0>`.

### 12.10 Asymmetric failure detection

**Description.** Bidirectional probing from multiple vantage points. Detects "A can reach B but B cannot reach A" failures.

**Status.** Planned (GAPS-127).

**Substrate.** `crucible/topology/AsymmetricDetect.h`.

### 12.11 Gray failure detection

**Description.** Degraded but not down. 10% packet loss looks like noise but indicates degraded transceiver. Synthetic-transaction probes per-transport per-peer; trend analysis on per-transport health.

**Reference.** Huang et al. 2017 (Microsoft, "Gray Failure: The Achilles' Heel of Cloud Systems").

**Status.** Planned.

**Substrate.** Composes synthetic probes (cat 15.5) + φ-accrual + per-transport health tracking.

### 12.12 Silent data corruption detection

**Description.** Compute redundantly on multiple Cogs; compare results. Detects SDCs from cosmic rays, marginal hardware, firmware bugs. Microsoft, Meta have published on the prevalence of this.

**Reference.** Hochschild et al. 2021 (Google), Dixit et al. 2021 (Meta), Bacon 2022 (Microsoft).

**Status.** Planned.

**Substrate.** `crucible/observe/SdcDetect.h`. Composes redundant compute + result comparison.

### 12.13 Per-flow microburst detection

**Description.** Sub-millisecond traffic spikes that destroy tail latency. Invisible to per-second sampling. Hardware-timestamped per-packet capture + sliding-window analysis.

**Status.** Planned.

**Substrate.** Composes PTP timestamps (cat 13.7 below) + INT (12.6).

## §21.3 HDR-histogram for everything

Mean and median lie in datacenter networks because latency is multi-modal (NUMA-local, NUMA-remote, switch-traversed, congested-path). HDR-histogram (Tene 2014) captures the full distribution at log-scale precision in O(log N) memory.

Crucible substrate ships `crucible/observe/HdrHistogram.h` (GAPS-135). Used everywhere a latency or duration is measured. Default observability primitive.

## §21.4 The observability composition

A typical Fixy observability stack:

```cpp
fixy::observe::stack{
    .pingmesh = fixy::observe::pingmesh<period_ms = 5000>{},
    .health = fixy::observe::phi_accrual<threshold = 8.0>{},
    .integrity = fixy::cntp::integrity<algorithm = fixy::hash::XxHash64>{},
    .fec = fixy::cntp::fec<k = 10, m = 2>{},
    .tracing = fixy::observe::trace<exporter = fixy::trace::Otlp>{},
    .histograms = fixy::observe::hdr<>{},
    .profile = fixy::observe::continuous_profile<rate_hz = 99>{},
    .sdc = fixy::observe::sdc_detect<redundancy = 2>{}
};
```

Each is independently composable. Substrate primitives compose into the user's deployment without modification of Crucible.

## §21.5 Crucible substrate vs Fixy DSL split — Category 12 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| xxHash64 | `cntp/Integrity.h` | (uses) |
| Reed-Solomon FEC | `cntp/Fec.h` | (uses) |
| Fountain codes | `cntp/Fountain.h` | (uses) |
| Pingmesh | `topology/Pingmesh.h` | (uses) |
| φ-accrual | `topology/Health.h` | (uses) |
| HDR-histogram | `observe/HdrHistogram.h` | (uses) |
| Trace context | `observe/Trace.h` | (uses) |
| INT integration | `observe/Int.h` | (uses) |
| Asymmetric/gray detection | `topology/AsymmetricDetect.h` | (uses) |
| SDC detection | `observe/SdcDetect.h` | (uses) |
| Stack composition | (none) | `fixy::observe::stack{}` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §22. Category 13 — Power / Thermal / Reliability

## §22.1 Scope

Physical-system management: GPU power capping, clock locking, datacenter-scale power smoothing, thermal forecasting, carbon-aware scheduling, hardware wear tracking. The §3 Cog substrate makes these per-Cog primitives; §5 adaptive MFU integrates them into the optimization loop.

## §22.2 The 9 items

### 13.1 NVML power capping

**Description.** Per-GPU hard power limit via NVML. `nvmlDeviceSetPowerManagementLimit()`. Enforced by GPU firmware. Operator-set, can be lowered by optimizer within bounds.

**Status.** Planned.

**Substrate.** `crucible/cog/Nvml.h`. Wraps NVML; provides per-Cog power control.

```cpp
namespace crucible::cog::nvml {

[[nodiscard]] auto set_power_limit(
    const cog::CogIdentity& gpu_cog,
    Refined<positive, uint32_t> watts
) noexcept -> std::expected<void, NvmlError>;

[[nodiscard]] auto current_power_draw(
    const cog::CogIdentity& gpu_cog
) noexcept -> std::expected<Refined<positive, uint32_t>, NvmlError>;

}
```

**Fixy DSL.** `fixy::power::cap<watts = 700>(gpu_cog)`.

### 13.2 NVML clock locking

**Description.** Lock GPU graphics clock via `nvmlDeviceSetGpuLockedClocks()`. Prevents DVFS jitter; required for replay determinism. Cost: ~3-5% throughput on average; benefit: deterministic timing.

**Status.** Planned.

**Substrate.** `crucible/cog/Nvml.h` (same header as 13.1).

**Fixy DSL.** `fixy::power::lock_clock<mhz = 1500>(gpu_cog)`.

### 13.3 Per-Cog DVFS policy

**Description.** Dynamic Voltage / Frequency Scaling per Cog. Composite policy: operator outer bounds + optimizer inner adjustment + per-step telemetry feedback.

**Status.** Planned.

**Substrate.** `crucible/cog/Dvfs.h`. Encodes the policy state machine.

**Fixy DSL.** `fixy::power::dvfs_policy{...}`. See §31 for full layout.

### 13.4 Per-Cog wear estimation

**Description.** Heuristic wear model accumulating from operational counters: thermal cycles, power transients, ECC corrections, XID errors. Per §3.8.

**Status.** Planned (GAPS-170).

**Substrate.** `crucible/cog/WearEstimate.h`. Updated periodically by Augur background thread.

**Fixy DSL.** `fixy::power::wear_aware<scheduling = fixy::power::WearAware::Conservative>`.

### 13.5 Datacenter-scale power smoothing

**Description.** Multiple techniques to reduce datacenter-wide power oscillation:
- **Filler GEMMs** during all-reduce phases (~5-10% throughput cost)
- **Frequency floor** to raise idle power, reducing peak-to-trough ratio (~5-15% cost)
- **Stagger jitter** across peers to decorrelate phases (~1% cost, full smoothing)
- **Phase-aware DVFS** to lower clock during expected comm phases (~2% cost, near-perfect smoothing)

**Status.** Planned.

**Substrate.** `crucible/cog/PowerSmooth.h`. Provides each smoothing primitive.

**Fixy DSL.**

```cpp
fixy::power::smoothing{
    .technique = fixy::power::Smooth::PhaseAwareDvfs,
    .max_di_dt_per_ms = fixy::WattsPerMs<50>{},
    .stagger_jitter = fixy::Range<Milliseconds<0>, Milliseconds<5>>{}
};
```

### 13.6 Phase-aware DVFS

**Description.** Detect compute vs comm phases. Lower clock during comm; raise during compute. Eliminates the synchronized-power-droop pattern at datacenter scale.

**Status.** Planned (special case of 13.5).

**Composition.** Composes 13.3 DVFS + per-step phase classification (Augur).

### 13.7 Carbon-aware scheduling

**Description.** Each datacenter's grid mix has a carbon intensity (gCO2/kWh) that varies by hour. Schedule heavy workloads to low-carbon hours / regions. Long-duration training jobs can defer steps to grid-favorable times.

**Status.** Planned (GAPS-171).

**Substrate.** `crucible/cog/Carbon.h`. Wraps grid carbon-intensity feeds (WattTime, ElectricityMaps APIs).

**Fixy DSL.**

```cpp
fixy::power::carbon_aware{
    .max_carbon_intensity_g_per_kwh = 200,
    .defer_policy = fixy::power::Defer::DeferIfHeavy
};
```

### 13.8 Thermal forecasting

**Description.** Predict GPU thermal throttle before it happens. Linear forecast based on temp trajectory + workload intensity. Throttle preemptively (lower clock) to avoid hard throttle that drops MFU 30-50%.

**Status.** Research-frontier.

**Substrate.** `crucible/cog/ThermalForecast.h`. Linear regression on temp history.

**Fixy DSL.** `fixy::power::thermal_forecast<window_ms = 1000, threshold_celsius_below_throttle = 5>`.

### 13.9 Coordinated power capping across rack

**Description.** Rack-level PDU has a power budget. Sum of GPU + CPU + NIC + storage power across rack must fit. Coordinated cap respects this constraint.

**Status.** Planned.

**Substrate.** `crucible/cog/RackPower.h`. Composes 13.1 with rack-level aggregation via Canopy gossip.

**Fixy DSL.** `fixy::power::rack_cap<kw = 30>(rack_cog)`.

## §22.3 The B200 capacitor wear case

Per §0 (TL;DR) and the conversation context: B200 GPUs reportedly have power-smoothing capacitors near the package that wear under high di/dt workloads. The wear correlates with:
- Sustained spiky workload (synchronized all-reduce)
- Small-model-on-big-GPU workloads (microsecond-scale work cycles)
- Frequent compute↔comm phase transitions

Crucible's response:
1. **Track transient frequency per Cog** (cat 13.4 wear counter).
2. **Throttle workloads with high transient rate** via 13.5 power smoothing (fillers, jitter, phase-aware DVFS).
3. **Wear-based scheduling** that prefers worn Cogs for steady workloads, fresh Cogs for spiky ones.
4. **Operator policy** that caps acceptable wear-rate per Cog.

The full disclosure: without vendor-published capacitor wear models, our wear estimates are heuristic. The mitigations are sound (reducing transient rate is unambiguously good for any capacitor); the wear estimate accuracy is best-effort.

## §22.4 Crucible substrate vs Fixy DSL split — Category 13 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| NVML power/clock control | `cog/Nvml.h` | (uses) |
| AMD-PM analog | `cog/AmdPm.h` | (uses) |
| Per-Cog DVFS state machine | `cog/Dvfs.h` | (uses) |
| Wear estimation | `cog/WearEstimate.h` | (uses) |
| Power smoothing techniques | `cog/PowerSmooth.h` | (uses) |
| Carbon intensity ingestion | `cog/Carbon.h` | (uses) |
| Thermal forecast | `cog/ThermalForecast.h` | (uses) |
| Rack-level coordination | `cog/RackPower.h` | (uses) |
| Power policy choice | (none) | `fixy::power::*` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §23. Category 14 — Security + Compliance

## §23.1 Scope

Security primitives for cross-organization federation, multi-tenant clusters, regulated workloads. The discipline: each primitive is a typed Fixy DSL element; the type system enforces composition correctness (e.g., a session declaring `Encryption<TlsRequired>` cannot be assigned to an unencrypted transport).

## §23.2 The 12 items

### 14.1 mTLS for federation

**Description.** Mutual TLS authentication for cross-organization federation flows. Each peer holds a certificate; both ends authenticate.

**Status.** Planned (GAPS-126).

**Substrate.** `crucible/cntp/MtlsTransport.h`. Wraps OpenSSL or BoringSSL.

**Fixy DSL.** `fixy::cntp::mtls<ca = my_ca, cert = my_cert, key = my_key>`.

### 14.2 WireGuard for site-to-site

**Description.** Modern VPN protocol. Noise framework cryptography. Linux 5.6+ kernel module. ~4000 LOC of cryptographic code (vs OpenSSL's millions). Simpler, faster, more auditable.

**Status.** Planned (GAPS-160).

**Substrate.** `crucible/cntp/WireguardTransport.h`. Wraps Linux WireGuard kernel module.

**Fixy DSL.** `fixy::cntp::wireguard<peer_pubkey = ..., my_privkey = ...>`.

### 14.3 QUIC + TLS 1.3

**Description.** QUIC bundles TLS 1.3, stream multiplexing, connection migration, 0-RTT resume. Linux 6.7+ kernel-mode (msquic) or userspace (quiche, ngtcp2).

**Status.** Planned (GAPS-128).

**Substrate.** `crucible/cntp/QuicTransport.h`.

**Fixy DSL.** `fixy::cntp::quic<>`.

### 14.4 kTLS hardware offload

**Description.** TLS encryption/decryption in NIC hardware. Set `setsockopt(SOL_TLS, TLS_TX, ...)` and NIC handles record framing + AES. ConnectX-6+, Intel E810, BlueField.

**Status.** Planned (GAPS-146).

**Substrate.** `crucible/cntp/KtlsOffload.h`.

**Fixy DSL.** `fixy::cntp::tls<offload = fixy::tls::Offload::Hardware>` (auto-fallback to software if hardware not present).

### 14.5 IPsec (transport / tunnel mode)

**Description.** Network-layer encryption. Standard for VPN deployments. Hardware-offloaded on modern NICs.

**Status.** Planned.

**Fixy DSL.** `fixy::cntp::ipsec<mode = fixy::ipsec::Tunnel>`.

### 14.6 Macsec (L2 encryption)

**Description.** Link-layer encryption. Used between switches in datacenter fabrics.

**Status.** Planned.

### 14.7 End-to-end encryption with PSK rotation

**Description.** Pre-shared key encryption with periodic rotation. For workloads where PKI overhead is unacceptable.

**Status.** Planned.

### 14.8 Differential privacy in federated aggregation

**Description.** Add calibrated noise to gradient contributions before aggregation. Provides ε-differential privacy guarantee. Used in federated learning.

**Reference.** Dwork et al. 2006 (DP), Abadi et al. 2016 (DP-SGD).

**Status.** Planned.

**Substrate.** `crucible/cntp/aggregation/DifferentialPrivacy.h`. Gaussian noise sampler with sensitivity bound enforcement.

**Fixy DSL.** `fixy::aggregate::DifferentialPrivacy<epsilon = 1.0, delta = 1e-5>`.

### 14.9 Secure aggregation

**Description.** Cryptographic protocol where aggregator learns sum of inputs but not individual inputs. Uses additive secret sharing or homomorphic encryption.

**Reference.** Bonawitz et al. 2017.

**Status.** Planned.

**Substrate.** `crucible/cntp/aggregation/SecureAgg.h`.

### 14.10 Trusted Execution Environment (TEE)

**Description.** Hardware-isolated execution: Intel TDX, AMD SEV-SNP, NVIDIA confidential compute. Code + data inside TEE encrypted in memory; attestation provides proof of execution.

**Status.** Planned (GAPS-161).

**Substrate.** `crucible/cntp/Tee.h`. Wraps per-vendor TEE APIs.

**Fixy DSL.** `fixy::sec::tee<vendor = fixy::tee::IntelTdx>`.

### 14.11 Audit log of every cross-org data movement

**Description.** Cryptographically signed log of every payload that crosses an org boundary. Composes the existing Cipher event-sourced audit pattern with cross-org authentication.

**Status.** Planned.

**Substrate.** Composes Cipher event log + mTLS identity.

**Fixy DSL.** `fixy::sec::audit_log<storage = fixy::cipher::Cold<>, signing = fixy::sec::Ed25519>`.

### 14.12 RBAC for federation membership

**Description.** Role-based access control. Each peer has roles; each role has permissions; permissions gate operations.

**Status.** Planned.

**Substrate.** `crucible/canopy/Rbac.h`.

**Fixy DSL.** `fixy::canopy::rbac{...}`.

## §23.3 The Secret + Tagged composition

The existing `Secret<T>` (`safety/Secret.h`) and `Tagged<T, Source>` (`safety/Tagged.h`) provide the foundational classification:

- `Secret<Gradient>` — gradient is classified; cannot escape without `declassify<Policy>`
- `Tagged<Bytes, source::CrossOrg>` — bytes from external org; subject to additional checks
- `Tagged<Bytes, source::Tee>` — bytes attested by TEE; can be trusted with elevated privileges

Cross-vendor / cross-org flows compose these:

```cpp
auto encrypted_grad = fixy::sec::encrypt<fixy::cipher::Aes256Gcm>(
    Secret<Gradient>{ my_gradient }
);

auto signed_audit = fixy::sec::sign<fixy::sec::Ed25519>(
    Tagged<Bytes, source::Internal>{ encrypted_grad.bytes() }
);

co_await fixy::cntp::mtls_send(peer_org, signed_audit);
```

The type system verifies the composition is sound.

## §23.4 Crucible substrate vs Fixy DSL split — Category 14 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| mTLS transport | `cntp/MtlsTransport.h` | (uses) |
| WireGuard transport | `cntp/WireguardTransport.h` | (uses) |
| QUIC transport | `cntp/QuicTransport.h` | (uses) |
| kTLS offload | `cntp/KtlsOffload.h` | (uses) |
| TEE integration | `cntp/Tee.h` | (uses) |
| DP noise sampler | `cntp/aggregation/DifferentialPrivacy.h` | (uses) |
| Secure aggregation | `cntp/aggregation/SecureAgg.h` | (uses) |
| RBAC | `canopy/Rbac.h` | (uses) |
| Existing Secret + Tagged | `safety/Secret.h`, `safety/Tagged.h` | (uses) |
| Encryption choice | (none) | `fixy::sec::encrypt<>` |
| Transport-encryption pairing | concept gate | type-system enforced |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §24. Category 15 — Operational Primitives

## §24.1 Scope

Day-2 operations: rolling upgrades, lame-duck mode, canary deployments, chaos engineering, hot-swap of Cogs, live migration. The discipline: every operational action is a Fixy DSL primitive; the type system verifies it composes safely.

## §24.2 The 10 items

### 15.1 Rolling upgrade (in-place version swap)

**Description.** Replace running binary with new version without dropping connections. Existing flows continue on old code; new flows route to new code; gradual cutover.

**Status.** Planned (GAPS-142).

**Substrate.** `crucible/runtime/RollingUpgrade.h`. Composes the existing atomic-swap mechanism with binary version tracking.

**Fixy DSL.**

```cpp
fixy::ops::rolling_upgrade{
    .new_binary = "/path/to/new",
    .canary_fraction = 0.05,
    .full_cutover_after = fixy::Hours<2>{},
    .rollback_on_error_rate = 0.001
};
```

### 15.2 Lame-duck mode (drain before shutdown)

**Description.** Cog announces "draining" state. New work is not assigned. Existing work completes. After drain timeout, Cog shuts down. Standard graceful shutdown pattern.

**Status.** Planned.

**Substrate.** `crucible/runtime/LameDuck.h`.

**Fixy DSL.** `fixy::ops::lame_duck<drain_seconds = 300>(cog)`.

### 15.3 Canary deployment (1% → 10% → 100%)

**Description.** Roll out change to small fraction, observe metrics, expand if green. Standard production deployment pattern.

**Status.** Planned.

**Substrate.** `crucible/runtime/Canary.h`.

**Fixy DSL.**

```cpp
fixy::ops::canary{
    .stages = {0.01, 0.10, 1.00},
    .stage_duration = fixy::Hours<1>{},
    .gates = { fixy::ops::Gate::ErrorRate<0.001>, fixy::ops::Gate::P99Latency<fixy::Ms<100>> }
};
```

### 15.4 Feature flags per-Cog

**Description.** Per-Cog configuration flags. Enable a feature on a subset of Cogs for testing. Composes with canary deployment.

**Status.** Planned.

**Fixy DSL.** `fixy::ops::feature_flag<my_feature>{ .cogs = ... }`.

### 15.5 Chaos engineering hooks (fault injection)

**Description.** Deliberately inject faults to test fault-tolerance: drop packets, slow links, kill processes, corrupt memory. Run in production at low rate to continuously verify resilience.

**Status.** Planned (GAPS-143).

**Substrate.** `crucible/runtime/Chaos.h`. Composes with topology graph + transport primitives.

**Fixy DSL.**

```cpp
fixy::ops::chaos{
    .fault = fixy::chaos::DropPackets<rate = 0.001>{},
    .target = fixy::cog_query<>{}.where(...).resolve(),
    .duration = fixy::Minutes<5>{},
    .frequency = fixy::PerHour<1>{}
};
```

### 15.6 Maintenance mode (planned downtime)

**Description.** Mark Cog for planned maintenance. Optimizer routes around. Cog can be shut down or rebooted without operational impact.

**Status.** Planned.

**Fixy DSL.** `fixy::ops::maintenance<reason = "firmware_upgrade">(cog, schedule)`.

### 15.7 Quarantine state machine

**Description.** Per-Cog state machine: Healthy → Suspect → Quarantined → (Recovered → Healthy or Permanent → removed). State transitions driven by health scores + operator overrides.

**Status.** Planned (GAPS-118).

**Substrate.** `crucible/cog/Quarantine.h`. Composes φ-accrual + thermal + ECC + wear + operator inputs.

**Fixy DSL.** `fixy::ops::quarantine_policy{...}`.

### 15.8 Recovery probing

**Description.** Quarantined Cog passes health probes; gradually re-add to fleet. Canary at 1% load, expand on success.

**Status.** Planned.

**Substrate.** Composes 15.3 canary + 15.7 quarantine.

### 15.9 Hot-swap of Cogs without dropping connections

**Description.** Replace Cog while running collectives. Connections re-routed to replacement Cog. No restart.

**Status.** Planned.

**Substrate.** Composes 15.1 rolling upgrade + 8.6 hot peer replacement.

### 15.10 Live migration of in-flight collectives

**Description.** Migrate a collective from one Cog set to another while it's executing. Used for evacuating Cogs in maintenance window.

**Status.** Research-frontier.

## §24.3 The operational composition

A typical production Crucible deployment composes:

```cpp
fixy::ops::stack{
    .upgrade_policy = fixy::ops::rolling_upgrade<...>,
    .quarantine = fixy::ops::quarantine_policy<...>,
    .canary = fixy::ops::canary<...>,
    .chaos = fixy::ops::chaos<...>,
    .maintenance = fixy::ops::maintenance_calendar<...>,
    .feature_flags = my_feature_flag_set
};
```

The optimizer respects all operational policies: it doesn't schedule heavy work on quarantined Cogs, doesn't violate canary rollout fractions, respects maintenance windows.

## §24.4 Crucible substrate vs Fixy DSL split — Category 15 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| Rolling upgrade mechanism | `runtime/RollingUpgrade.h` | (uses) |
| Lame-duck state | `runtime/LameDuck.h` | (uses) |
| Canary state machine | `runtime/Canary.h` | (uses) |
| Chaos fault injection | `runtime/Chaos.h` | (uses) |
| Quarantine state machine | `cog/Quarantine.h` | (uses) |
| Operational policy | (none) | `fixy::ops::*` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §25. Category 16 — Distributed Performance Frontiers

## §25.1 Scope and what's stripped

Single-Cog kernel optimization patterns are **out of scope for this networking + distributed compute document** and belong to a separate single-machine optimization doc. The following items are removed here:

- 16.* Persistent kernels / megakernel (single-Cog)
- 16.* Stream-K / Wave-aware scheduling (single-Cog SM)
- 16.* Producer-consumer kernels via Hopper TMA (single-Cog)
- 16.* Async copy with completion futures (single-Cog)
- 16.* Warp-specialized kernels (single-Cog warp scheduling)
- 16.* Cluster launch / Hopper+ multi-CTA (single-Cog)
- 16.* SM-aware scheduling (single-Cog SM)
- 16.* Prefetch + pipelining at IR level (mostly single-Cog)

The performance-frontier items that **are networking + distributed**:

- Compute-comm overlap by IR scheduling (cross-Cog)
- NUMA-aware data placement (cross-NUMA, distributed-relevant for ingress paths)
- Cross-Cog activation prefetch (overlap distributed comm with compute)

These are covered below.

## §25.2 The 3 networking-relevant items

### 16.1 Compute-comm overlap by IR scheduling

**Description.** Per §6 comm-through-IR. Forge Phase H schedules collectives concurrently with compute, accounting for SM budget contention. The optimizer reasons about which compute kernels can run while which collective is in flight, and emits the schedule that maximizes overlap. The unified row-typed budget (§4) is what makes this principled rather than heuristic.

**Status.** Planned.

**Substrate.** `crucible/forge/Phases/Schedule.h`. Composes IR001 comm op set (§6) + concurrent-row union (§4) + per-Cog SM budget.

**Fixy DSL.** Automatic. User can override per-collective via `fixy::all_reduce<schedule = fixy::Schedule::Sequential>` to disable overlap.

### 16.2 NUMA-aware data placement

**Description.** Allocate data on NUMA-local memory. Bind compute thread to NUMA-local CPU. Bind ingress NIC RX queues to NUMA-local cores. Avoids cross-socket memory traffic. The networking-relevant aspect: NIC ↔ NUMA pinning so packets arrive at the CPU socket that will process them.

**Status.** Planned.

**Substrate.** Composes existing `concurrent/Topology.h` (NUMA-aware) + new NIC ↔ NUMA pinning primitive in `cog/NumaNic.h`.

**Fixy DSL.** `fixy::cog::numa_nic_affinity<nic_cog, numa_node>`. Default automatic per Cog topology graph; explicit override available.

### 16.3 Cross-Cog activation prefetch

**Description.** When a downstream Cog will need an activation that an upstream Cog produced, prefetch it via async RDMA read while the downstream Cog is still computing on prior data. Hides comm latency.

**Status.** Planned.

**Substrate.** Composes RDMA async read (cat 10.9) + Forge Phase H scheduling.

**Fixy DSL.** Automatic when downstream-dependency pattern is recognized; explicit override via `fixy::compile<cross_cog_prefetch = false>`.

## §25.3 Crucible substrate vs Fixy DSL split — Category 16 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| Forge Phase H scheduler | `forge/Phases/Schedule.h` | (uses) |
| Compute-comm overlap pattern | `forge/Phases/CommOverlap.h` | (uses) |
| NUMA-NIC affinity | `cog/NumaNic.h` | (uses) |
| Cross-Cog prefetch | (composes existing primitives) | (uses) |
| Per-pattern enable/disable | (none) | `fixy::compile<>` flags |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §26. Category 17 — Federation + Cross-Organization

## §26.1 Scope

Federation = training / inference across organization boundaries. Each organization has its own trust model, security policy, compliance requirements. Crucible's contribution: type-system encoding of trust + audit trail + KernelCache federation enabling cross-org collaboration without data leakage.

## §26.2 The 8 items

### 17.1 Gradient sharing across organizations with DP

**Description.** Organizations contribute to a shared model by sending gradients. Differential privacy noise applied; raw gradients never cross org boundary. Standard federated learning.

**Status.** Planned.

**Substrate.** Composes cat 14.8 DP + cat 14.1 mTLS.

**Fixy DSL.**

```cpp
fixy::fed::gradient_sharing<
    organizations = my_consortium,
    privacy = fixy::sec::DifferentialPrivacy<epsilon = 1.0>,
    transport = fixy::cntp::mtls<...>
>;
```

### 17.2 Model weight sharing with provenance

**Description.** Share trained weights across organizations with cryptographic provenance. Recipient can verify weights came from declared source and weren't tampered with.

**Status.** Planned.

**Substrate.** Composes Cipher (existing) with cryptographic signing.

### 17.3 KernelCache federation

**Description.** Kernels compiled on one org's hardware can be reused by other orgs with same hardware. Content-addressed; verified via signature; cross-org sharing requires federation membership.

**Status.** Planned (GAPS-163).

**Substrate.** `crucible/cipher/KernelCacheFederation.h`. Wraps existing KernelCache with federation transport.

**Fixy DSL.** `fixy::fed::kernel_cache<peers = my_consortium>`.

### 17.4 Trust model negotiation per peer

**Description.** Each peer declares trust requirements: minimum encryption, minimum DP noise, minimum audit retention. Negotiation finds common subset; if no overlap, peering rejected.

**Status.** Planned.

**Fixy DSL.** `fixy::fed::trust_negotiation<my_requirements>(peer)`.

### 17.5 Per-org quarantine policies

**Description.** Each org has its own quarantine policy; composing federations requires negotiating compatible policies. "I'll accept your peer as healthy iff they pass YOUR policy AND mine."

**Status.** Planned.

**Fixy DSL.** `fixy::fed::quarantine_compose<policy_a, policy_b>`.

### 17.6 Cross-org topology graph (sharded by trust boundary)

**Description.** Shared topology graph CRDT across federation. Each org sees public Cogs of other orgs; private Cogs hidden. Cross-org links visible to both endpoints.

**Status.** Planned.

**Substrate.** Sharded scuttlebutt CRDT with per-shard ACL.

### 17.7 Federated MAP-Elites archive sharing

**Description.** MAP-Elites kernel archives shared across federation. Each org's calibration enriches everyone's archive. Discoveries propagate.

**Status.** Research-frontier.

**Substrate.** Composes KernelCache federation + MAP-Elites archive serialization.

### 17.8 Reputation tracking per peer

**Description.** Per-peer reputation score based on: gradient validity, latency consistency, uptime, byzantine-detection events. Low-reputation peers get reduced weight in aggregations.

**Status.** Research-frontier.

## §26.3 Crucible substrate vs Fixy DSL split — Category 17 summary

| Aspect | Substrate | Fixy DSL |
|---|---|---|
| Federation transport | (cat 14 substrate) | (uses) |
| KernelCache federation | `cipher/KernelCacheFederation.h` | (uses) |
| Cross-org CRDT | `canopy/FederatedScuttlebutt.h` | (uses) |
| Trust model | (none) | `fixy::fed::trust_*` |
| Federation strategies | (none) | `fixy::fed::*` |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §27. GAPS-110..184 Catalog

## §27.1 The 75 networking-related GAPS tasks

Full catalog of new tasks for the networking + Cog + comm-through-IR effort. Each task has: ID, subject, file, dependencies, gates, estimated LOC, estimated effort.

| ID | Subject | File | Deps | Gates | LOC | Effort |
|---|---|---|---|---|---|---|
| 110 | TopologyGraph.h — typed property graph for fleet | `topology/TopologyGraph.h` | — | 111-115 | 400 | 3d |
| 111 | Discovery.h — LLDP/PCIe/lspci/ethtool | `topology/Discovery.h` | 110 | 112-115 | 600 | 4d |
| 112 | Telemetry.h — per-NIC effective-capacity | `topology/Telemetry.h` | 111 | 113, 134 | 400 | 3d |
| 113 | Health.h — φ-accrual + thermal + ECC + drop-rate | `topology/Health.h` | 112 | 118, 127 | 300 | 2d |
| 114 | SwimGossip.h — SWIM membership | `canopy/SwimGossip.h` | 110 | 115, 118 | 500 | 4d |
| 115 | Scuttlebutt.h — anti-entropy CRDT | `canopy/Scuttlebutt.h` | 114 | 118, 162 | 400 | 3d |
| 116 | Integrity.h — E2E xxHash64 | `cntp/Integrity.h` | — | 117 | 150 | 1d |
| 117 | ReedSolomon.h — k+m FEC, AVX2 | `cntp/Fec.h` | 116 | 119 | 600 | 5d |
| 118 | Quarantine.h — auto-evict + recovery | `cog/Quarantine.h` | 113-115 | 142, 143 | 300 | 3d |
| 119 | Fountain.h — LT/Raptor codes for multicast | `cntp/Fountain.h` | 117 | — | 400 | 4d |
| 120 | CongestionControl.h — BBRv3, CUBIC, DCTCP | `cntp/CongestionControl.h` | — | 121, 123 | 150 | 1d |
| 121 | Pacing.h — fq verification + auto-config | `cntp/Pacing.h` | 120 | — | 200 | 2d |
| 122 | PathSwap.h — app-level path swap protocol | `cntp/PathSwap.h` | 110, 115 | 109 (sweep) | 400 | 3d |
| 123 | CongestionTelemetry.h — TCP_INFO/BBR_INFO | `topology/CongestionTelemetry.h` | 120 | 134 | 250 | 2d |
| 124 | IncastControl.h — DCTCP, RTO_MIN | `cntp/IncastControl.h` | 120 | — | 300 | 3d |
| 125 | RoceConfig.h — PFC/ECN/DCQCN | `cntp/RoceConfig.h` | — | 132, 133 | 250 | 2d |
| 126 | MtlsTransport.h — mTLS for federation | `cntp/MtlsTransport.h` | — | 162 | 500 | 4d |
| 127 | AsymmetricFailure.h — multi-vantage detection | `topology/AsymmetricDetect.h` | 113 | — | 400 | 3d |
| 128 | QuicTransport.h — QUIC + TLS 1.3 | `cntp/QuicTransport.h` | — | 162 | 600 | 5d |
| 129 | Ptp.h — PTP daemon + hw timestamping | `topology/Ptp.h` | 110 | 134 | 300 | 2d |
| 130 | XdpFilter.h — eBPF/XDP packet filtering | `perf/Xdp.h` | — | 131, 172 | 400 | 4d |
| 131 | AfXdpTransport.h — AF_XDP zero-copy | `cntp/AfXdp.h` | 130 | — | 500 | 4d |
| 132 | GpuDirect.h — GPUDirect RDMA + Storage | `cntp/GpuDirect.h` | 125 | 167 | 400 | 3d |
| 133 | InNetworkAgg.h — SHARP eligibility + dispatch | `cntp/Sharp.h` | 125, 145 | — | 350 | 3d |
| 134 | Pingmesh.h — all-pairs latency + HDR | `topology/Pingmesh.h` | 110, 113, 135 | — | 400 | 3d |
| 135 | RttHistogram.h — HDR-histogram primitive | `observe/HdrHistogram.h` | — | 134 | 200 | 1d |
| 136 | ConnectionPool.h — RDMA QP + TLS reuse | `cntp/ConnectionPool.h` | 125, 126 | — | 300 | 2d |
| 137 | Backpressure.h — credit-based + admission | `cntp/Backpressure.h` | — | — | 400 | 3d |
| 138 | Hlc.h — hybrid logical clock | `canopy/Hlc.h` | — | 115 | 150 | 1d |
| 139 | OverlayMulticast.h — Splitstream-style | `cntp/OverlayMulticast.h` | 130 | — | 500 | 4d |
| 140 | NicOffloadAudit.h — TSO/GSO/GRO/RSS verify | `cog/NicOffloadAudit.h` | 110 | — | 200 | 1d |
| 141 | SyntheticProbe.h — per-transport probes | `observe/SyntheticProbe.h` | 113 | — | 250 | 2d |
| 142 | RollingUpgrade.h — in-place version swap | `runtime/RollingUpgrade.h` | 118 | — | 400 | 4d |
| 143 | ChaosHooks.h — fault injection | `runtime/Chaos.h` | 110, 118 | — | 250 | 2d |
| 144 | DocaTransport.h — DPU offload | `cntp/Doca.h` | — | — | 600 | 6d |
| 145 | P4Program.h — programmable switch | `cntp/P4.h` | — | 133 | 500 | 5d |
| 146 | KtlsOffload.h — TLS hw offload | `cntp/KtlsOffload.h` | 126 | — | 200 | 2d |
| 147 | SrIovManager.h — SR-IOV / VF management | `cog/SrIov.h` | 110 | — | 400 | 3d |
| 148 | TcamFlowRules.h — hardware ACLs | `cntp/Tcam.h` | 130 | — | 350 | 3d |
| 149 | CollectiveCatalog.h — 16 sync algorithms | `cntp/collectives/Catalog.h` | 132, 133 | 150-159 | 800 | 7d |
| 150 | StrategyCatalog.h — 13 async strategies | `cntp/strategies/Catalog.h` | 149 | 154-159 | 1000 | 9d |
| 151 | CompressorCatalog.h — 14 compressors | `cntp/compress/Catalog.h` | — | 150, 154 | 700 | 6d |
| 152 | PipelineCatalog.h — 10 pipeline schedules | `cntp/pipeline/Catalog.h` | — | 154 | 600 | 5d |
| 153 | TpCatalog.h — 9 TP schemes | `cntp/tp/Catalog.h` | 149 | 154 | 500 | 4d |
| 154 | FsdpCatalog.h — 11 FSDP variants | `cntp/fsdp/Catalog.h` | 149-153 | 159 | 800 | 7d |
| 155 | MoeCatalog.h — 11 MoE variants | `cntp/moe/Catalog.h` | 149, 153 | — | 700 | 6d |
| 156 | ByzantineCatalog.h — Krum, Bulyan, trim | `cntp/aggregation/Byzantine.h` | — | — | 400 | 3d |
| 157 | InferenceCatalog.h — 11 inference patterns | `cntp/inference/Catalog.h` | 132, 149 | — | 800 | 7d |
| 158 | ConsensusCatalog.h — Raft, Paxos, VR | `canopy/ConsensusCatalog.h` | 138 | 159 | 1000 | 9d |
| 159 | PowerCatalog.h — DVFS, smoothing, capping | `cog/PowerCatalog.h` | — | — | 400 | 4d |
| 160 | WireguardTransport.h — WireGuard | `cntp/Wireguard.h` | — | 162 | 300 | 3d |
| 161 | TeeIntegration.h — TDX/SEV-SNP/CC | `cntp/Tee.h` | — | — | 500 | 5d |
| 162 | FederationProtocol.h — federation orchestration | `canopy/FederationProtocol.h` | 115, 126, 128 | 163-165 | 600 | 5d |
| 163 | KernelCacheFederation.h | `cipher/KernelCacheFederation.h` | 162 | — | 400 | 4d |
| 164 | Mfu.h — composite MFU types | `effects/Mfu.h` | — | 165 | 200 | 1d |
| 165 | AdaptiveOptimizer.h — multi-timescale | `cog/AdaptiveOptimizer.h` | 113, 123, 164 | — | 500 | 5d |
| 166 | CogQueryDsl.h — fixy::cog_query<> | `fixy/cog/Query.h` | 110 | — | 300 | 2d |
| 167 | NetworkIr001.h — comm op set | `forge/Ir001/Comm.h` | — | 168, 169 | 400 | 4d |
| 168 | NetworkForge.h — Forge phases for comm | `forge/Phases/Comm.h` | 167 | 169 | 600 | 6d |
| 169 | NetworkMimic.h — per-vendor network backends | `mimic/{nv,am}/network/` | 167, 168 | — | 1500 | 12d |
| 170 | WearTracking.h — per-Cog wear estimation | `cog/WearEstimate.h` | 113 | — | 250 | 2d |
| 171 | CarbonAware.h — carbon intensity ingestion | `cog/Carbon.h` | — | — | 200 | 2d |
| 172 | GossipMulticast.h — XDP_TX based replication | `cntp/GossipMulticast.h` | 130 | — | 350 | 3d |
| 173 | ~~MegakernelLowering.h~~ — **out-of-scope: single-machine optimization, deferred to separate doc** | — | — | — | — | — |
| 174 | NetworkRecipeRegistry.h | `forge/recipes/Network.h` | 167 | — | 200 | 2d |
| 175 | NetworkCrossVendorCi.h | `test/cross_vendor_network/` | 169 | — | 600 | 5d |
| 176 | UltrEthernet.h — UEC adaptive routing | `cntp/UltraEthernet.h` | — | — | 400 | 4d |
| 177 | CxlIntegration.h — CXL.cache + CXL.mem | `cntp/Cxl.h` | — | — | 400 | 4d |
| 178 | NvmeOf.h — NVMe over Fabrics | `cntp/NvmeOf.h` | 132 | — | 300 | 3d |
| 179 | DistributedTracing.h — OpenTelemetry | `observe/Trace.h` | — | — | 350 | 3d |
| 180 | SdcDetection.h — silent corruption | `observe/SdcDetect.h` | — | — | 400 | 4d |
| 181 | Differential​Privacy.h — DP-SGD primitives | `cntp/aggregation/Dp.h` | — | — | 300 | 3d |
| 182 | SecureAggregation.h — cryptographic aggreg | `cntp/aggregation/SecureAgg.h` | — | — | 500 | 5d |
| 183 | RbacFederation.h — role-based access | `canopy/Rbac.h` | 162 | — | 300 | 3d |
| 184 | NetworkBenchSuite.h — bench harness | `bench/network/` | 110-183 | — | 800 | 7d |

**Total:** 75 tasks, ~30,500 LOC, ~280 person-days (~14 weeks per engineer, ~3 months for a 5-engineer team).

## §27.2 Dependency graph

Critical path (dependencies that block downstream work):

```
110 (TopologyGraph) ──→ 111, 114, 134, 140, 143, 166
111 (Discovery)     ──→ 112
112 (Telemetry)     ──→ 113, 134
113 (Health)        ──→ 118, 127, 141, 165, 170
114 (SwimGossip)    ──→ 115, 118
115 (Scuttlebutt)   ──→ 118, 122, 162
116 (Integrity)     ──→ 117
117 (RS-FEC)        ──→ 119
118 (Quarantine)    ──→ 142, 143
120 (CC)            ──→ 121, 123, 124
125 (RoCE)          ──→ 132, 133, 136
126 (mTLS)          ──→ 136, 146, 162
130 (XDP)           ──→ 131, 139, 172, 148
132 (GPUDirect)     ──→ 149, 157, 167
145 (P4)            ──→ 133
149 (Collectives)   ──→ 150, 153, 154, 155, 157
150 (Strategies)    ──→ 154
151 (Compressors)   ──→ 150, 154
152 (Pipeline)      ──→ 154
153 (TP)            ──→ 154, 155
154 (FSDP)          ──→ (terminal in this chain)
158 (Consensus)     ──→ 159
162 (Federation)    ──→ 163-165, 183
164 (Mfu)           ──→ 165
167 (Network IR001) ──→ 168, 169, 174
168 (Forge phases)  ──→ 169
169 (Mimic backends)──→ 175 (CI)
```

The critical paths suggest sequencing:
- Months 1-2: foundational substrate (110-118, 130, 138)
- Months 2-3: transports (120-128, 131, 132, 144)
- Months 3-4: comm-through-IR (167-169)
- Months 4-5: collectives + strategies (149-155)
- Months 5-6: federation + security (126, 160-163, 183)
- Months 6-7: inference + perf (157, 173, 184)
- Months 7-8: fault tolerance + ops (118, 142, 143)
- Months 8-9: integration + CI (175)
- Months 9-12: hardening + benchmarking + production rollout

## §27.3 Effort allocation

Total ~280 person-days across 75 tasks. Breakdown by category:

| Category | Tasks | LOC | Person-days |
|---|---|---|---|
| Topology + Discovery + Telemetry | 110-115, 134, 140 | 2,750 | 22 |
| Transports (RDMA, AF_XDP, QUIC, mTLS) | 120-122, 126, 128, 131, 132, 136, 137, 144, 160 | 4,400 | 38 |
| Collectives + Strategies | 149-155 | 5,100 | 44 |
| Health + Quarantine + FT | 113, 118, 127, 142, 143 | 1,650 | 14 |
| Power + Reliability + Cog policies | 159, 170, 171, 165 | 1,150 | 12 |
| Comm-through-IR | 167-169, 173, 174 | 3,200 | 29 |
| Federation + Security | 126, 161-163, 181-183 | 2,500 | 24 |
| Observability | 116, 117, 119, 134, 135, 141, 179, 180 | 3,400 | 26 |
| eBPF + NIC programmability | 130, 144-148 | 2,150 | 18 |
| Inference patterns | 157 | 800 | 7 |
| Power smoothing | 159 | 400 | 4 |
| Operational + chaos + canary | 142, 143 + composition | 650 | 6 |
| Cross-vendor CI | 175 | 600 | 5 |
| Bench harness | 184 | 800 | 7 |

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §28. Out-of-scope: Megakernel Lowering Case Study

The 100M-model-on-B200 megakernel lowering case is **single-machine compute optimization** and is out of scope for this networking + distributed compute document. It belongs to a separate single-machine optimization document.

## §28.1 The networking-relevant tail

The networking-relevant aspect of small-model regimes is **what to do when a job has heterogeneous Cog sizes** — some Cogs big enough for the model, some not, some with different in-Cog optimization behavior. That decision belongs to:

- §3 Cog substrate — Cog query DSL filters Cogs by capability
- §3.7 Per-Cog Mimic instance — each Cog compiles its own optimal variant
- §5 Adaptive MFU multi-timescale loop — chooses cross-Cog placement

The cross-Cog placement decision (which Cog runs which work item, given heterogeneous Cog effective performance) IS distributed and IS in scope. The single-Cog megakernel choice (does this one Cog use a megakernel) is NOT in scope here.

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §29. Datacenter-Scale Power Dynamics

## §29.1 The synchronized-all-reduce phenomenon

100,000 GPUs in synchronized data-parallel training. Every step:
- t=0: all GPUs begin GEMM. Power: 100 MW (100k × 1 kW)
- t=200ms: all GPUs finish, begin all-reduce. NICs active, SMs idle. Power: 30 MW.
- t=300ms: all-reduce done, GEMM begins again. Power: 100 MW.

**70 MW oscillation at 5 Hz**, perfectly synchronized across the entire datacenter.

## §29.2 What breaks

| Scale | What breaks |
|---|---|
| Per-GPU | Capacitor wear (B200, see §30) |
| Per-rack | PDU transient response. Modern PDUs handle steady load fine; sub-second transients exceed response curve |
| Per-row | Switch power supplies oscillate, increasing risk of trip |
| Per-DC | UPS goes into bypass mode or trips. Modern UPS handles 100 ms transient response; 70 MW droop in <100 ms exceeds spec |
| Per-substation | Grid frequency stability. 70 MW is a notable fraction of even a substation's capacity. Sub-second oscillations show up as flicker on the regional grid |
| Per-utility | Some utilities have refused new datacenter contracts because of this |

## §29.3 The mitigations

| Technique | Mechanism | Cost |
|---|---|---|
| Filler GEMMs | Run small dummy GEMM during all-reduce to keep SMs ~50% loaded | ~5-10% throughput |
| Frequency floor | Lock GPU clock to baseline (50% of max) to raise floor power | ~5-15% throughput |
| Power cap + smoothing | Hard-cap each GPU at 600W (vs 700W TDP); firmware smooths transients above this | ~3-5% throughput |
| Stagger jitter | Each GPU adds small random delay (~ms) to all-reduce start | ~1% throughput, full smoothing |
| Phase-aware DVFS | Lower clock during expected comm phases, raise during compute | ~2% throughput, near-perfect smoothing |

## §29.4 Fixy DSL composition

Each technique is a Fixy DSL primitive. The training job declares its power profile:

```cpp
fixy::TrainConfig{
    .power_smoothing = fixy::PowerSmoothing::PhaseAwareDvfs,
    .max_di_dt_per_ms = fixy::WattsPerMs<50>{},
    .stagger_jitter_ms = fixy::Range<0, 5>{},
    .filler_workload_target_util = 0.5
};
```

The compiler emits the corresponding scheduling — filler ops, frequency-lock instructions, jitter delays — into the IR. The optimizer budgets for the throughput cost in its decision-making.

## §29.5 The deeper architectural point

**Datacenter-scale physical effects must be first-class in the IR.** Today they're not; framework treats power and frequency as runtime knobs invisible to the optimizer. Crucible's row system already has the shape to encode them. Power becomes a row axis, just like NumericalTier and Vendor.

The optimizer (per §5 multi-timescale loop) sees:

```
Step N optimizer state:
    measured_power_oscillation_rate = 5.2 Hz
    measured_max_di_dt_per_ms = 78 W/ms
    declared_max_di_dt_per_ms = 50 W/ms
    → VIOLATION: di/dt budget exceeded
    → ACTION: increase stagger jitter from 2ms to 4ms
    → PREDICTED IMPACT: oscillation rate drops to <2 Hz, throughput cost <0.5%
```

The optimizer learns the cost-benefit empirically and adjusts. No human tuning required after initial policy declaration.

## §29.6 Cross-DC implications

For multi-DC training (e.g., DiLoCo across regions), the power oscillation problem inverts: each DC oscillates independently, but if many DCs run simultaneously, regional grid impact compounds. Coordination becomes inter-DC.

The Fixy DSL exposes coordinated stagger:

```cpp
fixy::fed::power_coordination{
    .organizations = my_consortium,
    .stagger_per_org_ms = fixy::Range<0, 100>{},
    .max_aggregate_di_dt_mw_per_ms = 1000  // protect grid
};
```

Cross-org gossip propagates per-org stagger offsets. Combined oscillation is bounded.

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §30. Hardware Reliability Axis

## §30.1 The B200 capacitor wear case

Per §22.3 and conversation context: B200 GPUs reportedly have power-smoothing components near the package that wear under high di/dt workloads. The component might be:
- VRM capacitors (multi-layer ceramic, dielectric work-hardening)
- Bulk decoupling capacitors
- Inductor saturation thermal cycling

The exact failure mode investigation is ongoing publicly. What's reported is correlation with workloads having **high transient frequency** — exactly the synchronized-all-reduce pattern from §29 and the small-model-on-big-GPU pattern from §28 (microsecond-scale work cycles producing constant transitions).

## §30.2 The wear estimation model

Per §3.8, each Cog tracks operational counters and derives wear estimates:

```cpp
namespace crucible::cog {

struct WearEstimate {
    Monotonic<uint64_t> total_uptime_seconds;
    Monotonic<uint64_t> total_compute_cycles;
    Monotonic<uint64_t> total_thermal_cycles;
    Monotonic<uint64_t> total_power_transients;
    Monotonic<uint64_t> total_ecc_corrected;
    Monotonic<uint64_t> total_xid_errors;

    // Derived heuristic estimates
    Tagged<double, source::Heuristic> capacitor_wear_fraction;
    Tagged<double, source::Heuristic> die_thermal_wear_fraction;
    Tagged<double, source::Heuristic> nand_program_erase_wear_fraction;
    Tagged<double, source::Heuristic> optical_decay_fraction;

    Stale<Duration> estimated_rul;
};

}
```

## §30.3 Honest accuracy

Without vendor-published wear models, our estimates are heuristic. The doc commits to:

- **Tracking the operational counters precisely** — they're observable from telemetry (NVML, EDAC, hardware counters)
- **Building empirical wear models over time** as field failures correlate with counter trajectories
- **Exposing wear-based policies in Fixy DSL** even when underlying estimate is best-effort
- **Honest Tagged<source::Heuristic>** marker so consumers know the limitation

When vendors eventually publish wear models (or empirical correlation accumulates from field deployments), the Tagged source upgrades to Tagged<source::Calibrated>. The DSL surface is unchanged.

## §30.4 Wear-based scheduling

The §5 optimizer uses wear estimates as input to the composite MFU objective:

```
composite_MFU =
      (compute_MFU)^α
    × (1 - wear_acceleration)^γ
    × ...
```

For γ > 0, the optimizer prefers Cog assignments that don't accelerate wear. Concretely:
- Worn Cogs get lower-transient workloads (steady GEMM, not synchronized collectives)
- Fresh Cogs get higher-transient workloads
- Workloads with high di/dt get stagger jitter applied
- Power smoothing techniques (§29) reduce average wear rate across the fleet

## §30.5 The wear-leveling analogy

NAND flash has limited program-erase cycles per cell. Filesystems wear-level by spreading writes across cells, prolonging device life. The same principle applies to GPU wear:

| NAND | GPU |
|---|---|
| P/E cycles per cell | Power transients per Cog |
| Wear-leveling at filesystem | Wear-leveling at scheduler |
| Bad-block remap | Cog quarantine on excess wear |
| TBW (Total Bytes Written) spec | Total transients spec (heuristic) |

Crucible's substrate provides the counters; Fixy DSL provides the wear-leveling policy:

```cpp
fixy::power::wear_aware{
    .scheduling = fixy::power::WearAware::Conservative,
    .max_acceptable_wear_rate_per_year = 0.20,  // 5-year lifetime target
    .quarantine_threshold = 0.70                 // pull Cog at 70% wear
};
```

## §30.6 The reliability rollup

Per-Cog wear → per-Cog health score → per-rack rollup → fleet-wide health snapshot. Operators see:

```
Fleet wear summary (2026-05-03 12:00:00 UTC)
─────────────────────────────────────────────
Total Cogs:               10,000
  Healthy (<30% wear):       9,200
  Watch (30-50% wear):         600
  Suspect (50-70% wear):       180
  Quarantined (>70% wear):      20

Wear acceleration (last 30 days):
  Average:                    +0.4% / month
  Worst Cog:                  +2.1% / month
  At-risk for replacement:     35 Cogs

Predicted exhaustion (next 6 months):
  Rack-12-shelf-3:             3 Cogs at >90% wear by 2026-09-15
  Rack-7-shelf-1:              2 Cogs at >90% wear by 2026-10-22
```

This is operationally useful even with heuristic estimates.

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §31. Per-Cog Clock/Power Manager

## §31.1 The empirical evidence

Per the conversation: capping GPUs at 80-90% TDP delivers 95-97% of peak performance with substantial reliability gains. Numbers:

| GPU | Default TDP | Cap @ 80% | Performance retained | Thermal headroom | Wear improvement |
|---|---|---|---|---|---|
| H100 | 700W | 560W | ~94-96% | +12°C | ~1.5-2× lifetime |
| H200 | 700W | 560W | ~95-97% | +14°C | ~1.5-2× lifetime |
| B200 | 1000W | 800W | ~93-95% | +18°C | ~2-3× lifetime |
| MI300X | 750W | 600W | ~92-95% | +10°C | ~1.5-2× lifetime |

For memory-bound workloads (the 100M-on-B200 case is extreme example), the gain from full TDP is nearly zero. The card spends extra power on idle SMs spinning while waiting for HBM.

## §31.2 The full design

```cpp
namespace crucible::cog {

struct CogPower {
    // Operator-set policies (long-lived, hard outer bounds)
    Refined<positive, uint32_t> power_cap_watts;
    Optional<Refined<positive, uint32_t>> clock_lock_mhz;
    Refined<positive, uint32_t> frequency_floor_mhz;
    Refined<positive, uint32_t> frequency_ceiling_mhz;

    // Optimizer-set policies (per-iteration adjustable within bounds)
    Stale<uint32_t> current_target_power_watts;
    Stale<uint32_t> current_target_clock_mhz;

    // Telemetry inputs (refreshed every step)
    Stale<uint8_t> current_temp_celsius;
    Stale<uint32_t> current_draw_watts;
    Stale<uint32_t> current_actual_clock_mhz;
    Stale<WearEstimate> wear;
};

}
```

## §31.3 The control hierarchy

Three layers, in order of authority:

### Layer 1: Operator policy (hardest)

Operator declares hard bounds at deployment:

```cpp
fixy::power::operator_policy{
    .max_power_per_gpu_w = 800,         // never exceed
    .min_clock_mhz = 1500,              // power smoothing floor
    .max_clock_mhz = 1980,              // wear management ceiling
    .wear_quarantine_threshold = 0.70   // pull Cog at this wear
};
```

The optimizer cannot violate these. They're security/reliability invariants.

### Layer 2: Optimizer policy (within operator bounds)

Optimizer adjusts target power and clock at iteration boundaries:

```
Step N optimizer decision:
    measured_compute_util = 0.92
    measured_thermal = 75°C (throttle at 90°C)
    measured_wear_rate = 0.15% / month (within budget)
    measured_di_dt = 38 W/ms (under 50 W/ms budget)

    → No adjustment needed; current state optimal.

Step N+1000 optimizer decision:
    measured_compute_util = 0.34 (suddenly memory-bound)
    measured_thermal = 81°C (rising)
    measured_wear_rate = 0.21% / month (rising)

    → ADJUST: lower target power from 800W to 700W
    → PREDICTED: util drops <2%, thermal drops 5°C, wear rate drops to 0.16%
    → APPLY at next iteration boundary
```

### Layer 3: Per-step routing (no power changes, just placement)

Within current power profile, route work to maximize composite MFU. No DVFS adjustments at this layer.

## §31.4 Replay-determinism implications

DVFS jitter breaks replay-determinism: same kernel on same data produces same result, but takes different wall-clock time, affecting scheduling and downstream behavior. Crucible's BITEXACT_STRICT recipe pins the result to bit-equivalence; replay-determinism additionally requires bit-equivalent timing.

For workloads requiring replay-determinism (Cipher's deterministic-replay invariant, debugging workflows), clock locking is mandatory:

```cpp
fixy::power::deterministic_replay{
    .enabled = true,
    .clock_lock_mhz = 1500,
    .accept_throughput_cost_percent = 5
};
```

When this is enabled, the optimizer cannot adjust DVFS; only filler / smoothing techniques that don't affect arithmetic are available.

## §31.5 The composite MFU exponents

For per-Cog clock/power decisions, composite MFU exponents control the tradeoff:

| Workload | α (compute) | β (power) | γ (wear) | δ (thermal) | ε (di/dt) | ζ (SLA) |
|---|---|---|---|---|---|---|
| Research (burn-it) | 5 | 0.1 | 0.1 | 1 | 1 | 0 |
| Production (balanced) | 2 | 1 | 2 | 2 | 2 | 1 |
| Long-duration (preserve hardware) | 1 | 1 | 4 | 2 | 2 | 1 |
| Reliability-critical | 1 | 1 | 2 | 4 | 4 | 5 |
| Inference latency-bound | 3 | 0.5 | 1 | 1 | 1 | 5 |

Operators pick exponents in `fixy::TrainConfig`. The optimizer optimizes the resulting composite.

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §32. Cross-Vendor Numerics CI Extended to Network Kernels

## §32.1 The existing pattern

MIMIC.md §41 specifies: every (KernelKind × NumericalRecipe × Backend) triple is compiled, executed, output-verified pairwise against CPU scalar-FMA oracle. A backend that violates tolerance fails the build.

For compute kernels:
- KernelKind: GEMM, ATTENTION, REDUCTION, ELEMENTWISE, ...
- NumericalRecipe: BITEXACT_STRICT, BITEXACT_TC, ORDERED, UNORDERED
- Backend: NV-Hopper, NV-Blackwell, AM-CDNA3, AM-RDNA3, CPU, TPU, Trainium

Example:
```
GEMM × BITEXACT_TC × NV-Hopper: Verified ≤ 1 ULP vs CPU oracle ✓
GEMM × BITEXACT_TC × AM-CDNA3:  Verified ≤ 1 ULP vs CPU oracle ✓
```

## §32.2 Extension to network kernels

For network kernels, add Cog set as a verification axis:

| Axis | Values |
|---|---|
| CollectiveKind | RING_ALLREDUCE, TREE_ALLREDUCE, RECURSIVE_HALVING_DOUBLING, DIRECT_ALLTOALL, SHARP_REDUCTION, ... |
| NumericalRecipe | BITEXACT_STRICT, BITEXACT_TC, ORDERED, UNORDERED |
| Backend | NV+ConnectX-7+RoCEv2, NV+NVLink, AM+ConnectX-7, AM+Slingshot, CPU+TCP, ... |
| PeerSet | (N=2, intra-rack), (N=8, intra-rack), (N=8, inter-rack), (N=64, mixed) |

Total verification matrix (upper bound): 10 × 4 × 16 × 8 = 5,120 cells. In practice many cells are trivially satisfied by composition (e.g., all PeerSets share verification within a CollectiveKind).

## §32.3 Verification methodology

For each cell in the matrix:

1. **Synthesize input.** Generate reproducible random inputs (Philox-seeded, deterministic).
2. **Run reference.** CPU oracle with scalar FMA.
3. **Run target.** Backend with the specified collective + recipe.
4. **Compare.** Pairwise per-element comparison.
5. **Verify tolerance.** Per recipe:
   - BITEXACT_STRICT: 0 bytes diff
   - BITEXACT_TC: ≤1 ULP per element
   - ORDERED: per-recipe tolerance
   - UNORDERED: bounded relative error

A cell that violates tolerance fails the build. CI runs nightly across the full matrix on physical hardware.

## §32.4 The bit-equivalence property

For BITEXACT_STRICT and BITEXACT_TC, the property is: **the same ring all-reduce of the same N peers' inputs produces bit-equivalent results regardless of which vendor backends the peers use**.

This is unprecedented. NCCL doesn't promise this; OpenMPI doesn't either. The reason no one promises it: NCCL's reduction order depends on ring topology, which depends on cluster layout, which varies. Crucible's recipe-tier discipline pins the reduction order in the recipe itself, making cross-vendor bit-equivalence achievable.

## §32.5 The CI infrastructure

GAPS-175 (NetworkCrossVendorCi.h) ships:
- Per-backend test fixtures
- Reference oracle (`mimic/cpu/network/`)
- Pairwise comparison harness
- Failure attribution (which cell failed, what the diff was)
- Tolerance enforcement per recipe

CI runtime budget: nightly, ~2 hours on a 64-Cog test cluster. Per-PR CI subset (10-20 most-affected cells): ~5 minutes.

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §33. Worked Example — FSDPv3 in Fixy DSL (Full Version)

## §33.1 Context

§15.3 sketched FSDPv3 in ~70 lines. The full version, integrating all the substrate primitives this doc commits to, is ~250 lines. It demonstrates: per-parameter sharding, custom forward/backward, FP8 gradient compression, NVMe optimizer state offload, custom CC for cross-DC sync, crash-stop tolerance, audit logging.

## §33.2 The strategy

```cpp
// User code, no Crucible/Fixy modification needed.
// File: my_org/strategies/FSDPv3.h

#include <fixy/strategy.h>
#include <fixy/cntp/all.h>
#include <fixy/sec/all.h>

namespace my_org {

// ─────────────────────────────────────────────────────────────────────
// Shard layout policy (per-parameter, layer-kind aware)
// ─────────────────────────────────────────────────────────────────────

template <fixy::ParameterLike P>
struct shard_policy {
    static constexpr fixy::ShardStrategy choose() noexcept {
        if constexpr (fixy::is_attention_qkv_v<P>) {
            // QKV is wide enough to benefit from 2D shard (TP × DP)
            return fixy::ShardStrategy::TwoD;
        } else if constexpr (fixy::is_attention_output_v<P>) {
            // Attention output: 1D shard along TP axis (row-parallel)
            return fixy::ShardStrategy::TpRowParallel;
        } else if constexpr (fixy::is_mlp_up_v<P>) {
            // MLP up-projection: column-parallel along TP axis
            return fixy::ShardStrategy::TpColParallel;
        } else if constexpr (fixy::is_mlp_down_v<P>) {
            // MLP down-projection: row-parallel along TP axis
            return fixy::ShardStrategy::TpRowParallel;
        } else if constexpr (fixy::is_layer_norm_v<P>) {
            // LayerNorm: replicate across TP (small, no benefit to shard)
            // Shard across DP for memory savings
            return fixy::ShardStrategy::DpReplicated;
        } else if constexpr (fixy::is_embedding_v<P>) {
            // Embedding: vocab-parallel along TP axis
            return fixy::ShardStrategy::VocabParallel;
        } else {
            // Default: 1D shard along DP axis (FSDP-style)
            return fixy::ShardStrategy::DpSharded;
        }
    }
};

// ─────────────────────────────────────────────────────────────────────
// FSDPv3 strategy (composes all the above)
// ─────────────────────────────────────────────────────────────────────

struct FSDPv3 : fixy::DistributedStrategy {

    // ────── Configuration ────────────────────────────────────────────

    static constexpr fixy::recipe::Recipe forward_recipe = fixy::recipe::BITEXACT_TC;
    static constexpr fixy::recipe::Recipe backward_recipe = fixy::recipe::ORDERED;

    // FP8 gradient compression for cross-DP-shard reduction
    using gradient_compression = fixy::compress::Fp8<
        scaling = fixy::compress::Fp8::Tensor,
        accumulation_dtype = fixy::dtype::Fp32
    >;

    // Optimizer state offload to NVMe with prefetch
    using optimizer_state_storage = fixy::offload::Nvme<
        prefetch_depth = 2,
        compression = fixy::compress::Lz4<level = fixy::compress::Lz4::Fast>,
        write_async = true
    >;

    // Activation recomputation for memory-bound layers
    using activation_recompute_policy = fixy::recompute::Selective<
        recompute = [](auto layer_kind) constexpr {
            return layer_kind == fixy::LayerKind::Attention
                || layer_kind == fixy::LayerKind::MlpUp;
        }
    >;

    // Crash-stop tolerance
    static constexpr uint32_t tolerated_failures_per_round = 2;
    using crash_recovery = fixy::ft::ContinueWithSubset<
        min_quorum_fraction = 0.75,
        timeout = fixy::Seconds<60>
    >;

    // Custom CC for cross-DC links (slow paths)
    using cross_dc_cc = fixy::cntp::cc::CustomBbr<
        rtt_inflation_threshold_ms = 100,
        bandwidth_smoothing_window_s = 5
    >;

    // ────── Sharding ─────────────────────────────────────────────────

    template <fixy::ModelLike M, fixy::MeshLike Mesh>
    constexpr auto shard(M&& model, Mesh mesh) const noexcept
        -> fixy::Computation<
               fixy::Row<fixy::Effect::Bg, fixy::Effect::Init>,
               fixy::ShardedModel<M, Mesh>>
    {
        return fixy::for_each_parameter(
            std::forward<M>(model),
            [mesh]<fixy::ParameterLike P>(P&& param) {
                constexpr auto strategy = shard_policy<P>::choose();
                return fixy::shard<strategy>(std::forward<P>(param), mesh);
            }
        );
    }

    // ────── Forward ──────────────────────────────────────────────────

    template <fixy::ShardLike S, fixy::TensorLike T>
    constexpr auto forward_shard(S&& shard, T&& input) const noexcept
        -> fixy::Computation<
               fixy::Row<fixy::Effect::Bg, fixy::Effect::IO>,
               typename S::output_type>
    {
        // Overlap all-gather with computation of previous layer
        // (existing layer's outputs feed into all-gather queue)
        co_await fixy::all_gather<
            schedule = fixy::Schedule::Overlapped,
            chunks = 4,
            sm_budget = fixy::SmBudget<8>{},
            recipe = forward_recipe,
            cc = cross_dc_cc{}
        >(std::forward<S>(shard));

        co_return shard.compute(std::forward<T>(input));
    }

    // ────── Backward ─────────────────────────────────────────────────

    template <fixy::ShardLike S, fixy::TensorLike T>
    constexpr auto backward_shard(S&& shard, T&& grad_output) const noexcept
        -> fixy::Computation<
               fixy::Row<fixy::Effect::Bg, fixy::Effect::IO>,
               typename S::gradient_type>
    {
        // Compute gradient w.r.t. weights (from grad_output)
        auto grad_weights = shard.backward_weights(std::forward<T>(grad_output));

        // FP8 quantization of gradients before reduce-scatter
        auto grad_quantized = fixy::compress::quantize<gradient_compression>(
            std::move(grad_weights)
        );

        // Reduce-scatter with overlap (each rank receives its shard's gradient)
        co_await fixy::reduce_scatter<
            schedule = fixy::Schedule::Overlapped,
            chunks = 4,
            sm_budget = fixy::SmBudget<8>{},
            recipe = backward_recipe,
            cc = cross_dc_cc{}
        >(grad_quantized);

        // Compute gradient w.r.t. inputs (returns to upstream layer)
        co_return shard.backward_inputs(std::forward<T>(grad_output));
    }

    // ────── Optimizer Step ───────────────────────────────────────────

    template <fixy::ShardLike S, fixy::ShardLike Grad>
    constexpr auto optimizer_step(
        S&& shard,
        Grad&& gradient,
        fixy::OptimizerState& opt_state
    ) const noexcept
        -> fixy::Computation<
               fixy::Row<fixy::Effect::Bg, fixy::Effect::IO>,
               void>
    {
        // Prefetch optimizer state from NVMe
        auto fetched_state = co_await opt_state.fetch_prefetched(
            shard.parameter_id()
        );

        // Apply optimizer update (Adam, SGD, etc. - configurable)
        auto updated_param = fetched_state.apply(
            std::forward<S>(shard),
            std::forward<Grad>(gradient)
        );

        // Async write back to NVMe
        co_await opt_state.write_async(
            shard.parameter_id(),
            updated_param
        );

        co_return;
    }

    // ────── Crash recovery callback ──────────────────────────────────

    template <fixy::PeerLike Peer>
    constexpr auto on_peer_crash(Peer&& crashed_peer) const noexcept
        -> fixy::Computation<
               fixy::Row<fixy::Effect::Bg>,
               fixy::ft::RecoveryAction>
    {
        // Log to audit trail
        fixy::sec::audit_log::record(
            "peer_crash",
            fixy::sec::Severity::Warning,
            crashed_peer.identity()
        );

        // Continue with subset if quorum maintained
        if (fixy::current_quorum_fraction() >= 0.75) {
            co_return fixy::ft::RecoveryAction::ContinueWithSubset;
        } else {
            // Below quorum: pause + alert
            co_return fixy::ft::RecoveryAction::PauseAndAlert;
        }
    }
};

}  // namespace my_org
```

## §33.3 Use it

```cpp
// User's training script:
fixy::train<my_org::FSDPv3>(
    my_model,
    my_dataset,
    my_mesh,
    fixy::TrainConfig{
        .recipe = fixy::recipe::BITEXACT_TC,
        .composite_mfu_exponents = {
            .compute = 2.0,
            .power = 1.0,
            .wear = 2.0,
            .thermal = 2.0,
            .di_dt = 2.0,
            .sla = 1.0
        },
        .observability = fixy::observe::stack{
            .pingmesh = fixy::observe::pingmesh<period_ms = 5000>{},
            .health = fixy::observe::phi_accrual<threshold = 8.0>{},
            .integrity = fixy::cntp::integrity<algorithm = fixy::hash::XxHash64>{},
            .tracing = fixy::observe::trace<exporter = fixy::trace::Otlp>{}
        }
    }
);
```

## §33.4 What the Fixy compiler verifies

At compile time:
- `FSDPv3` satisfies `fixy::DistributedStrategy` concept
- All template type chains: shard return → forward_shard input → backward_shard input → optimizer_step input
- Row consistency: combined effect rows across all methods are well-formed
- Resource budget: combined resource consumption fits target Cog capacity for given mesh
- Recipe consistency: BITEXACT_TC achievable through the composition (forward path, backward path, optimizer)
- Crash-tolerance contract: tolerated_failures_per_round > 0 satisfies CrashTolerantStrategy
- Audit log integration: requires sec::AuditLog; verified
- Cross-DC CC: requires Cog topology with cross-DC links; verified at deployment time

If any check fails, compile error with routed diagnostic naming the violation.

If all checks pass, the strategy is type-safe by construction. **This is the load-bearing capability that makes "you can't write FSDPv3 in PyTorch" no longer true.**

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §34. Worked Example — Custom CC for Satellite Link

## §34.1 Why standard CC fails for satellites

LEO satellite links (Starlink, OneWeb) have:
- High bandwidth-delay product (RTT ~30-50ms, BW ~100 Mbps → BDP ~400 KB)
- Variable RTT (handover between satellites every 4-5 min causes 10-20ms swings)
- Non-congestive losses (atmospheric scintillation, weather, antenna pointing)
- Asymmetric capacity (downlink typically 5-10× uplink)

CUBIC interprets non-congestive losses as congestion → unnecessary back-off → severe under-utilization.
BBR's BtlBw estimate confused by RTT swings → also under-utilizes.
Custom CC needed.

## §34.2 The custom CC

```cpp
// User code, file: my_org/cc/SatelliteCc.h
#include <fixy/cntp/cc.h>

namespace my_org {

// Custom CC for satellite links: BBR variant with:
// - Loss-aware (distinguishes congestion loss from random loss)
// - RTT-swing tolerant (handles handover-induced spikes)
// - Capacity-asymmetric (downlink/uplink independent)

struct SatelliteCc : fixy::cntp::cc::CongestionControl {

    static constexpr fixy::Milliseconds rtt_smoothing_window{500};
    static constexpr double random_loss_threshold = 0.001;  // 0.1% baseline

    struct flow_state {
        fixy::Bytes btl_bw_estimate{0};
        fixy::Milliseconds rt_prop{0};
        fixy::Bytes inflight{0};

        // Random-loss tracking
        uint32_t recent_losses = 0;
        uint32_t recent_packets = 0;

        // RTT swing detection
        fixy::Milliseconds rtt_min_recent{0};
        fixy::Milliseconds rtt_max_recent{0};
        fixy::Microseconds last_handover_detect_us{0};
    };

    // Called on each acknowledged packet
    constexpr auto on_ack(flow_state& s, AckEvent const& ack) const noexcept -> void {
        // Update RTT estimate with smoothing
        s.rt_prop = fixy::ewma(s.rt_prop, ack.rtt, 0.1);

        // Detect RTT swing (handover indicator)
        if (ack.rtt > s.rtt_min_recent * 1.5) {
            // Likely handover; don't react aggressively to RTT increase
            s.last_handover_detect_us = fixy::now_us();
            return;
        }

        // Update bandwidth estimate
        auto delivery_rate = ack.acked_bytes / ack.elapsed_time;
        s.btl_bw_estimate = std::max(s.btl_bw_estimate, delivery_rate);
    }

    // Called on each loss event
    constexpr auto on_loss(flow_state& s, LossEvent const& loss) const noexcept -> void {
        s.recent_losses++;

        // Compute loss rate over recent window
        auto loss_rate = double(s.recent_losses) / double(s.recent_packets);

        if (loss_rate < random_loss_threshold) {
            // Below threshold: assume random loss; don't back off
            return;
        }

        if (fixy::now_us() - s.last_handover_detect_us
            < std::chrono::seconds(2)) {
            // Recent handover: don't back off (loss is transient)
            return;
        }

        // Above random-loss threshold and not in handover: real congestion
        s.btl_bw_estimate *= 0.7;  // 30% back-off (less aggressive than CUBIC's 50%)
    }

    // Compute target pacing rate
    constexpr auto target_pacing_rate(flow_state const& s) const noexcept
        -> fixy::BytesPerSec
    {
        // BDP-based target with conservative gain
        return s.btl_bw_estimate * 1.05;
    }

    // Compute target congestion window
    constexpr auto target_cwnd(flow_state const& s) const noexcept
        -> fixy::Bytes
    {
        // BDP + small headroom
        return s.btl_bw_estimate * s.rt_prop * 1.2;
    }
};

}  // namespace my_org
```

## §34.3 Use it

```cpp
// User configures their federation transport with custom CC for satellite peers
auto satellite_transport = fixy::cntp::quic<
    cc = my_org::SatelliteCc{},
    encryption = fixy::sec::tls13{},
    streams = 8
>;

auto datacenter_transport = fixy::cntp::rdma<
    cc = fixy::cntp::cc::Bbr3{},
    protocol = fixy::Roce::V2
>;

// Per-peer transport selection
fixy::fed::transport_per_peer{
    .when([](peer p) { return p.fabric_type == fixy::Fabric::Satellite; })
        .use(satellite_transport),
    .when([](peer p) { return p.fabric_type == fixy::Fabric::DatacenterEthernet; })
        .use(datacenter_transport),
    .default_use(fixy::cntp::tcp<cc = fixy::cntp::cc::Bbr3{}>)
};
```

## §34.4 What the Fixy compiler verifies

- `SatelliteCc` satisfies `fixy::cntp::cc::CongestionControl` concept
- Required methods present: `on_ack`, `on_loss`, `target_pacing_rate`, `target_cwnd`
- Method signatures correctly typed
- Composition with QUIC transport: type-safe
- Per-peer transport selection: exhaustive, no ambiguity

User wrote ~80 lines of custom CC. Crucible/Fixy unmodified.

## §34.5 What this enables

A federation deployment can include peers across:
- Datacenter Ethernet (use BBRv3 + RDMA)
- LEO satellite links (use SatelliteCc + QUIC)
- Cellular backhaul (use a different custom CC)
- WAN ISP links (use BBRv3 + standard TCP)

All within one training job. Each peer gets the right CC for its link characteristics. Crucible's substrate exposes the primitives; Fixy's composition makes the per-peer policy cleanly expressible.

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §35. Worked Example — Federated Train Across DCs

## §35.1 The scenario

Three organizations (A, B, C) want to collaboratively train a model on combined data without any organization sharing raw data. Each org has its own DC, its own GPU fleet, its own security policy, its own quarantine rules.

Requirements:
- Differential privacy on shared gradients (ε ≤ 1.0)
- mTLS between orgs
- Audit log of every cross-org transfer
- Each org's quarantine policy respected (a peer healthy by Org A's policy may be suspect by Org B's)
- DiLoCo-style async updates (cross-DC bandwidth too low for sync)
- Per-org KernelCache federation
- Reputation tracking per peer

## §35.2 The composition

```cpp
// User code, file: my_org/federated_train.cpp

namespace my_org {

// ─────────────────────────────────────────────────────────────────────
// Strategy: Async DiLoCo with DP and federation
// ─────────────────────────────────────────────────────────────────────

struct FederatedDiLoCo : fixy::DistributedStrategy {

    static constexpr uint32_t inner_steps = 1000;
    static constexpr uint32_t max_staleness_rounds = 3;

    using outer_optimizer = fixy::optim::Nesterov<momentum = 0.9>;

    using gradient_compression = fixy::compress::Composed<
        fixy::compress::TopK<sparsity = 0.01>,
        fixy::compress::PowerSgd<rank = 4>,
        fixy::compress::Lz4<level = fixy::compress::Lz4::Fast>
    >;

    using privacy = fixy::sec::DifferentialPrivacy<
        epsilon = 1.0,
        delta = 1e-5,
        clipping = fixy::sec::dp::PerSample<bound = 1.0>
    >;

    using cross_org_transport = fixy::cntp::quic<
        cc = fixy::cntp::cc::Bbr3{},
        encryption = fixy::sec::tls13<
            client_cert = my_org::cert,
            ca = my_org::federation_ca,
            verify_peer = true
        >,
        streams = 4
    >;

    using audit = fixy::sec::audit_log<
        storage = fixy::cipher::Cold<bucket = "s3://my-org/audit/">,
        signing = fixy::sec::Ed25519{},
        retention = fixy::Years<7>{}  // compliance requirement
    >;

    using crash_policy = fixy::ft::TolerateLatecomers<
        timeout = fixy::Seconds<60>{},
        min_participating_orgs = 2  // need ≥ 2 of 3 orgs
    >;

    template <fixy::ModelLike M>
    auto inner_step(M& model, fixy::Batch batch) const noexcept {
        // Inner step: standard local SGD (no cross-org communication)
        return fixy::sgd_step(model, batch);
    }

    template <fixy::ModelLike M>
    auto outer_sync(M& model, M const& period_start) const noexcept {
        // Compute pseudo-gradient
        auto pseudo_grad = model.parameters() - period_start.parameters();

        // Compress (top-K + PowerSGD + LZ4)
        auto compressed = fixy::compress::apply<gradient_compression>(pseudo_grad);

        // Add DP noise (BEFORE leaving local enclave)
        auto noisy = fixy::sec::dp::apply_noise<privacy>(compressed);

        // Audit log
        audit::record(
            "outer_sync_send",
            fixy::sec::Severity::Info,
            {.bytes = noisy.size_bytes(), .epsilon_consumed = privacy::epsilon}
        );

        // Send to all federation peers via mTLS
        auto peer_responses = co_await fixy::all_reduce<
            transport = cross_org_transport{},
            recipe = fixy::recipe::ORDERED  // not bit-exact across orgs (DP noise differs)
        >(noisy, federation_peers);

        // Apply outer optimizer to averaged result
        outer_optimizer::apply(model.parameters(), peer_responses.averaged);

        // Audit log receive
        audit::record(
            "outer_sync_receive",
            fixy::sec::Severity::Info,
            {.bytes = peer_responses.total_bytes_received,
             .peers_responded = peer_responses.peer_count}
        );
    }
};

// ─────────────────────────────────────────────────────────────────────
// Federation membership + per-org quarantine policy composition
// ─────────────────────────────────────────────────────────────────────

const auto my_org_quarantine = fixy::ops::quarantine_policy{
    .health_threshold = 0.7,
    .grace_period = fixy::Minutes<5>{}
};

const auto federation = fixy::fed::federation{
    .members = {
        fixy::fed::peer{
            .org = "OrgA",
            .endpoint = "fed.orga.example.com:8443",
            .public_key = orga_pubkey,
            .trust_level = fixy::sec::TrustLevel::Verified
        },
        fixy::fed::peer{
            .org = "OrgB",
            .endpoint = "fed.orgb.example.com:8443",
            .public_key = orgb_pubkey,
            .trust_level = fixy::sec::TrustLevel::Verified
        },
        fixy::fed::peer{
            .org = "OrgC",
            .endpoint = "fed.orgc.example.com:8443",
            .public_key = orgc_pubkey,
            .trust_level = fixy::sec::TrustLevel::Verified
        }
    },
    .quarantine_compose = fixy::fed::quarantine::Intersection{
        // Peer must pass MY quarantine AND THEIR quarantine
        .my_policy = my_org_quarantine
    },
    .reputation = fixy::fed::reputation_tracking{
        .grace_score = 1.0,
        .penalty_per_byzantine_event = 0.1,
        .min_score_to_aggregate = 0.5
    },
    .kernel_cache_federation = fixy::fed::kernel_cache{
        .accept_from_trust_level = fixy::sec::TrustLevel::Verified
    }
};

// ─────────────────────────────────────────────────────────────────────
// Run it
// ─────────────────────────────────────────────────────────────────────

int main() {
    auto model = my_org::load_model("100M-base");
    auto dataset = my_org::load_local_dataset();
    auto mesh = fixy::cog_query<fixy::CogKind::Gpu>{}.where(...).resolve();

    fixy::train<FederatedDiLoCo>(
        model,
        dataset,
        mesh,
        fixy::TrainConfig{
            .federation = federation,
            .recipe = fixy::recipe::ORDERED,
            .observability = fixy::observe::stack{
                .pingmesh = fixy::observe::pingmesh<period_ms = 5000>{},
                .health = fixy::observe::phi_accrual<threshold = 8.0>{},
                .integrity = fixy::cntp::integrity<algorithm = fixy::hash::XxHash64>{},
                .tracing = fixy::observe::trace<exporter = fixy::trace::Otlp>{}
            }
        }
    );
    return 0;
}

}  // namespace my_org
```

## §35.3 What the Fixy compiler verifies

- `FederatedDiLoCo` satisfies `fixy::DistributedStrategy` concept
- Privacy budget composes correctly (per-step ε accounting)
- Audit log integration: every cross-org transfer is logged
- mTLS configuration: peer pubkeys verified at compile time
- Quarantine intersection: well-formed
- Recipe + DP: ORDERED recipe correct (BITEXACT impossible with per-org DP noise)
- Crash policy: min_participating_orgs ≤ federation size
- Cross-org transport: QUIC + TLS 1.3 satisfies cross-org transport requirements
- Federation member trust levels: only Verified peers participate
- Reputation tracking: composable with aggregation rules

## §35.4 What this enables

Three orgs collaboratively train on combined data:
- Each org's data stays local (DP guarantees)
- Cross-org coordination is async (DiLoCo, no sync barrier)
- Failure of one org doesn't kill the training (continue with 2 of 3)
- Audit trail for compliance (Ed25519-signed log of every transfer)
- Per-org quarantine policies respected
- Reputation tracks peer behavior; misbehaving peers reduced in weight
- Kernel cache shared (compiled kernels travel between orgs)

This is **frontier production federated learning**. No open-source framework today provides all of this composably. Crucible/Fixy makes it ~250 lines of user code with full type-system verification.

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §36. Sequencing + Risk Register

## §36.1 12-month implementation timeline

The full GAPS-110..184 chain delivers in ~12 months with a 5-engineer team. Sequencing:

### Months 1-2: Foundation

Foundational substrate that everything else depends on.

| Task | Eng | Effort |
|---|---|---|
| GAPS-110 TopologyGraph.h | 1 | 3d |
| GAPS-111 Discovery.h | 1 | 4d |
| GAPS-112 Telemetry.h | 1 | 3d |
| GAPS-113 Health.h | 1 | 2d |
| GAPS-114 SwimGossip.h | 1 | 4d |
| GAPS-115 Scuttlebutt.h | 1 | 3d |
| GAPS-116 Integrity.h | 1 | 1d |
| GAPS-117 ReedSolomon.h | 1 | 5d |
| GAPS-118 Quarantine.h | 1 | 3d |
| GAPS-119 Fountain.h | 1 | 4d |
| GAPS-130 XdpFilter.h | 1 | 4d |
| GAPS-138 Hlc.h | 1 | 1d |
| GAPS-140 NicOffloadAudit.h | 1 | 1d |
| GAPS-166 CogQueryDsl.h | 1 | 2d |
| GAPS-167 NetworkIr001.h | 1 | 4d |

**Total: 44 person-days = ~9 person-weeks. Achievable in 2 months at 5 engineers.**

### Months 2-3: Transports

Per-layer transport primitives.

| Task | Eng | Effort |
|---|---|---|
| GAPS-120 CongestionControl.h | 1 | 1d |
| GAPS-121 Pacing.h | 1 | 2d |
| GAPS-122 PathSwap.h | 1 | 3d |
| GAPS-123 CongestionTelemetry.h | 1 | 2d |
| GAPS-124 IncastControl.h | 1 | 3d |
| GAPS-125 RoceConfig.h | 1 | 2d |
| GAPS-126 MtlsTransport.h | 1 | 4d |
| GAPS-127 AsymmetricFailure.h | 1 | 3d |
| GAPS-128 QuicTransport.h | 1 | 5d |
| GAPS-129 Ptp.h | 1 | 2d |
| GAPS-131 AfXdpTransport.h | 1 | 4d |
| GAPS-132 GpuDirect.h | 1 | 3d |
| GAPS-136 ConnectionPool.h | 1 | 2d |
| GAPS-137 Backpressure.h | 1 | 3d |
| GAPS-144 DocaTransport.h | 1 | 6d |
| GAPS-145 P4Program.h | 1 | 5d |
| GAPS-146 KtlsOffload.h | 1 | 2d |
| GAPS-147 SrIovManager.h | 1 | 3d |
| GAPS-148 TcamFlowRules.h | 1 | 3d |
| GAPS-160 WireguardTransport.h | 1 | 3d |

**Total: 61 person-days. Achievable in 2.5 months at 5 engineers.**

### Months 3-5: Comm-through-IR + Collectives

The biggest chunk of work. Comm-through-IR enables MAP-Elites for collectives.

| Task | Eng | Effort |
|---|---|---|
| GAPS-168 NetworkForge.h | 2 | 6d |
| GAPS-169 NetworkMimic.h (per-vendor) | 2 | 12d |
| GAPS-149 CollectiveCatalog.h (16 algos) | 2 | 7d |
| GAPS-150 StrategyCatalog.h (13 async) | 2 | 9d |
| GAPS-151 CompressorCatalog.h (14 compressors) | 1 | 6d |
| GAPS-152 PipelineCatalog.h (10 pipeline) | 1 | 5d |
| GAPS-153 TpCatalog.h (9 TP) | 1 | 4d |
| GAPS-154 FsdpCatalog.h (11 FSDP) | 1 | 7d |
| GAPS-155 MoeCatalog.h (11 MoE) | 1 | 6d |
| GAPS-173 MegakernelLowering.h | 1 | 5d |
| GAPS-174 NetworkRecipeRegistry.h | 1 | 2d |

**Total: 69 person-days. ~3 months at 5 engineers.**

### Months 5-7: Health + Power + Quarantine + FT

Operational and reliability primitives.

| Task | Eng | Effort |
|---|---|---|
| GAPS-156 ByzantineCatalog.h | 1 | 3d |
| GAPS-159 PowerCatalog.h | 1 | 4d |
| GAPS-164 Mfu.h | 1 | 1d |
| GAPS-165 AdaptiveOptimizer.h | 1 | 5d |
| GAPS-170 WearTracking.h | 1 | 2d |
| GAPS-171 CarbonAware.h | 1 | 2d |
| GAPS-141 SyntheticProbe.h | 1 | 2d |
| GAPS-142 RollingUpgrade.h | 1 | 4d |
| GAPS-143 ChaosHooks.h | 1 | 2d |

**Total: 25 person-days. ~1 month at 5 engineers.**

### Months 7-9: Federation + Security + Inference

| Task | Eng | Effort |
|---|---|---|
| GAPS-161 TeeIntegration.h | 1 | 5d |
| GAPS-162 FederationProtocol.h | 1 | 5d |
| GAPS-163 KernelCacheFederation.h | 1 | 4d |
| GAPS-181 DifferentialPrivacy.h | 1 | 3d |
| GAPS-182 SecureAggregation.h | 1 | 5d |
| GAPS-183 RbacFederation.h | 1 | 3d |
| GAPS-157 InferenceCatalog.h | 1 | 7d |
| GAPS-158 ConsensusCatalog.h | 2 | 9d |
| GAPS-176 UltraEthernet.h | 1 | 4d |
| GAPS-177 CxlIntegration.h | 1 | 4d |
| GAPS-178 NvmeOf.h | 1 | 3d |
| GAPS-179 DistributedTracing.h | 1 | 3d |
| GAPS-180 SdcDetection.h | 1 | 4d |

**Total: 59 person-days. ~2.5 months at 5 engineers.**

### Months 9-11: Observability + Operations

| Task | Eng | Effort |
|---|---|---|
| GAPS-133 InNetworkAgg.h (SHARP) | 1 | 3d |
| GAPS-134 Pingmesh.h | 1 | 3d |
| GAPS-135 RttHistogram.h | 1 | 1d |
| GAPS-139 OverlayMulticast.h | 1 | 4d |
| GAPS-172 GossipMulticast.h | 1 | 3d |

**Total: 14 person-days. ~3 weeks at 5 engineers (low-priority infrastructure).**

### Months 11-12: Cross-vendor CI + Bench harness + production rollout

| Task | Eng | Effort |
|---|---|---|
| GAPS-175 NetworkCrossVendorCi.h | 2 | 5d |
| GAPS-184 NetworkBenchSuite.h | 2 | 7d |
| Production hardening | All | ongoing |
| Documentation | All | ongoing |
| Worked examples | All | ongoing |

**Total: 12 person-days plus integration work.**

## §36.2 Milestones

| Month | Milestone |
|---|---|
| M2 | Topology graph + SWIM + φ-accrual operational; can detect peer health |
| M3 | RDMA + AF_XDP + QUIC transports working; can move data between Cogs |
| M5 | First all-reduce running through Forge/Mimic; cross-vendor CI bit-equivalent |
| M7 | First training run with FSDP-style strategy; MFU comparable to NCCL+PyTorch |
| M9 | Federation working across orgs with mTLS + DP |
| M11 | Inference patterns (continuous batching, paged attention) operational |
| M12 | Production deployment-ready; bench numbers competitive with NCCL+UCX |

## §36.3 Risk register

| # | Risk | Probability | Impact | Mitigation |
|---|---|---|---|---|
| R1 | Per-vendor backend complexity exceeds estimate | High | Schedule slip 1-2 months | Start with 2 backends (NV+CPU), add others in v2 |
| R2 | RDMA verb wrapper has hidden behavioral differences across NIC vendors | Medium | Compatibility issues | Comprehensive cross-vendor CI from M3 |
| R3 | DOCA / BlueField requires NVIDIA-specific tooling, slows portability | Medium | Vendor lock-in for DPU features | Make DPU features opt-in; not on critical path |
| R4 | P4 switch requires switch SDK access (often under NDA) | High | Limited deployment options | Make P4 features opt-in; standard Ethernet path always works |
| R5 | Megakernel lowering has correctness pitfalls in edge cases | Medium | Fallback to per-layer | Aggressive cross-vendor CI; clear opt-out flag |
| R6 | Kernel firmware changes invalidate compiled artifacts | Low | KernelCache invalidation | Cog identity includes firmware revision; stale cache entries auto-purged |
| R7 | Federation across DCs has unpredictable WAN behavior | Medium | Performance unpredictability | Custom CC per peer (§34); telemetry-driven adjustment |
| R8 | Wear estimation accuracy depends on vendor data we don't have | High | Sub-optimal wear policies | Tagged<source::Heuristic>; expose as best-effort |
| R9 | Cross-vendor BITEXACT compliance hard to verify exhaustively | High | Some workloads fail tolerance | Accept partial coverage; document non-bit-exact paths clearly |
| R10 | 12-month timeline is aggressive | High | Slip to 18 months | Sequence prioritizes cri tical path; parallelize where possible |

## §36.4 Resourcing assumptions

- 5 engineers, full-time
- Each engineer contributes ~60 productive person-days per quarter (after meetings, reviews, on-call, etc.)
- No major architectural redesigns mid-stream
- Hardware available for testing: at least one NV cluster (8+ GPUs), one AM cluster, one CPU-only test cluster

If team is smaller, timeline scales linearly. If hardware is limited, cross-vendor CI cycle time grows.

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

# §37. Cross-Reference Index + Closing

## §37.1 Section index

### Part 1 (`misc/03_05_2026_networking.md`)

- §0 TL;DR
- §1 Methodology + verification
- §2 Architectural reframing (no sealed CNTP)
- §3 Cog substrate (L0..L7 hierarchy)
- §4 Resource budgeting on shared hardware
- §5 Adaptive MFU multi-timescale loop
- §6 Comm-through-IR
- §7 eBPF / NIC programmability stack
- §8 17-category index
- §9 Substrate audit
- §10 Cat 1 — Synchronous collective algorithms
- §11 Cat 2 — Async + eventually-consistent
- §12 Cat 3 — Bandwidth reduction
- §13 Cat 4 — Pipeline parallelism
- §14 Cat 5 — Tensor parallelism
- §15 Cat 6 — FSDP / ZeRO families

### Part 2 (this file)

- §16 Cat 7 — Expert parallelism (MoE)
- §17 Cat 8 — Fault tolerance + elasticity
- §18 Cat 9 — Inference-specific patterns
- §19 Cat 10 — Pure networking primitives
- §20 Cat 11 — Coordination + consensus + membership
- §21 Cat 12 — Observability + reliability
- §22 Cat 13 — Power / thermal / reliability
- §23 Cat 14 — Security + compliance
- §24 Cat 15 — Operational primitives
- §25 Cat 16 — Performance optimization frontiers
- §26 Cat 17 — Federation + cross-organization
- §27 GAPS-110..184 catalog
- §28 Megakernel lowering case study
- §29 Datacenter-scale power dynamics
- §30 Hardware reliability axis
- §31 Per-Cog clock/power manager
- §32 Cross-vendor numerics CI for network kernels
- §33 Worked example: FSDPv3 in Fixy DSL
- §34 Worked example: custom CC for satellite link
- §35 Worked example: federated train across DCs
- §36 Sequencing + risk register
- §37 Cross-reference index + closing (this section)

## §37.2 GAPS task index

By task ID:

| GAPS | Subject | Section |
|---|---|---|
| 110 | TopologyGraph.h | §3, §27 |
| 111 | Discovery.h | §3, §27 |
| 112 | Telemetry.h | §27 |
| 113 | Health.h | §21.2 (12.9), §27 |
| 114 | SwimGossip.h | §20.2 (11.4), §27 |
| 115 | Scuttlebutt.h | §20.2 (11.8), §27 |
| 116 | Integrity.h | §21.2 (12.2), §27 |
| 117 | ReedSolomon.h | §21.2 (12.3), §27 |
| 118 | Quarantine.h | §24.2 (15.7), §27 |
| 119 | Fountain.h | §21.2 (12.4), §27 |
| 120 | CongestionControl.h | §27 |
| 121 | Pacing.h | §27 |
| 122 | PathSwap.h | §27 |
| 123 | CongestionTelemetry.h | §27 |
| 124 | IncastControl.h | §27 |
| 125 | RoceConfig.h | §19.2 (10.1), §27 |
| 126 | MtlsTransport.h | §23.2 (14.1), §27 |
| 127 | AsymmetricFailure.h | §21.2 (12.10), §27 |
| 128 | QuicTransport.h | §23.2 (14.3), §27 |
| 129 | Ptp.h | §27 |
| 130 | XdpFilter.h | §7.3, §27 |
| 131 | AfXdpTransport.h | §7.6, §27 |
| 132 | GpuDirect.h | §19.2 (10.9), §27 |
| 133 | InNetworkAgg.h | §10.2 (1.13), §27 |
| 134 | Pingmesh.h | §21.2 (12.1), §27 |
| 135 | RttHistogram.h | §21.3, §27 |
| 136 | ConnectionPool.h | §27 |
| 137 | Backpressure.h | §27 |
| 138 | Hlc.h | §20.2 (11.10), §27 |
| 139 | OverlayMulticast.h | §27 |
| 140 | NicOffloadAudit.h | §27 |
| 141 | SyntheticProbe.h | §27 |
| 142 | RollingUpgrade.h | §24.2 (15.1), §27 |
| 143 | ChaosHooks.h | §24.2 (15.5), §27 |
| 144 | DocaTransport.h | §7.8, §27 |
| 145 | P4Program.h | §7.9, §27 |
| 146 | KtlsOffload.h | §23.2 (14.4), §27 |
| 147 | SrIovManager.h | §27 |
| 148 | TcamFlowRules.h | §27 |
| 149 | CollectiveCatalog.h | §10, §27 |
| 150 | StrategyCatalog.h | §11, §27 |
| 151 | CompressorCatalog.h | §12, §27 |
| 152 | PipelineCatalog.h | §13, §27 |
| 153 | TpCatalog.h | §14, §27 |
| 154 | FsdpCatalog.h | §15, §27 |
| 155 | MoeCatalog.h | §16, §27 |
| 156 | ByzantineCatalog.h | §17.2 (8.9), §27 |
| 157 | InferenceCatalog.h | §18, §27 |
| 158 | ConsensusCatalog.h | §20, §27 |
| 159 | PowerCatalog.h | §22, §27 |
| 160 | WireguardTransport.h | §23.2 (14.2), §27 |
| 161 | TeeIntegration.h | §23.2 (14.10), §27 |
| 162 | FederationProtocol.h | §26, §27 |
| 163 | KernelCacheFederation.h | §26.2 (17.3), §27 |
| 164 | Mfu.h | §5, §27 |
| 165 | AdaptiveOptimizer.h | §5, §27 |
| 166 | CogQueryDsl.h | §3.9, §27 |
| 167 | NetworkIr001.h | §6.4, §27 |
| 168 | NetworkForge.h | §6.5, §27 |
| 169 | NetworkMimic.h | §6.6, §27 |
| 170 | WearTracking.h | §22.2 (13.4), §27 |
| 171 | CarbonAware.h | §22.2 (13.7), §27 |
| 172 | GossipMulticast.h | §7.4, §27 |
| 173 | ~~MegakernelLowering.h~~ — out-of-scope (single-machine) | §28 (out-of-scope note) |
| 174 | NetworkRecipeRegistry.h | §27 |
| 175 | NetworkCrossVendorCi.h | §32, §27 |
| 176 | UltraEthernet.h | §19.2 (10.18), §27 |
| 177 | CxlIntegration.h | §19.2 (10.8), §27 |
| 178 | NvmeOf.h | §19.2 (10.11), §27 |
| 179 | DistributedTracing.h | §21.2 (12.7), §27 |
| 180 | SdcDetection.h | §21.2 (12.12), §27 |
| 181 | DifferentialPrivacy.h | §23.2 (14.8), §27 |
| 182 | SecureAggregation.h | §23.2 (14.9), §27 |
| 183 | RbacFederation.h | §23.2 (14.12), §27 |
| 184 | NetworkBenchSuite.h | §27 |

## §37.3 Category item index

By category and item:

| Cat | Item | Description | Section | GAPS |
|---|---|---|---|---|
| 1 | 1.1 | Ring all-reduce | §10.2 | 149 |
| 1 | 1.2 | Tree all-reduce | §10.2 | 149 |
| 1 | 1.3 | Recursive halving-doubling | §10.2 | 149 |
| 1 | 1.4 | Recursive doubling | §10.2 | 149 |
| 1 | 1.5 | Bidirectional ring | §10.2 | 149 |
| 1 | 1.6 | 2D ring | §10.2 | 149 |
| 1 | 1.7 | Hierarchical all-reduce | §10.2 | 149 |
| 1 | 1.8 | Direct all-to-all | §10.2 | 149 |
| 1 | 1.9 | Variable-size all-to-all | §10.2 | 149 |
| 1 | 1.10 | Sparse all-to-all | §10.2 | 149 |
| 1 | 1.11 | Reduce-scatter + all-gather | §10.2 | 149 |
| 1 | 1.12 | Pipelined collectives | §10.2 | 149 |
| 1 | 1.13 | SHARP | §10.2 | 133 |
| 1 | 1.14 | NCCL-style protocols | §10.2 | 149 |
| 1 | 1.15 | Topology-aware ring | §10.2 | 149 |
| 1 | 1.16 | Fused collective + compute | §10.2, §6.7 | 149, 168 |
| 2 | 2.1 | DiLoCo | §11.2 | 150 |
| 2 | 2.2 | Streaming DiLoCo | §11.2 | 150 |
| 2 | 2.3 | Async DiLoCo | §11.2 | 150 |
| 2 | 2.4 | Local SGD | §11.2 | 150 |
| 2 | 2.5 | EASGD | §11.2 | 150 |
| 2 | 2.6 | Gossip-SGD | §11.2 | 150 |
| 2 | 2.7 | AD-PSGD | §11.2 | 150 |
| 2 | 2.8 | CocktailSGD | §11.2 | 150 |
| 2 | 2.9 | Bounded-staleness async | §11.2 | 150 |
| 2 | 2.10 | FedAvg | §11.2 | 150 |
| 2 | 2.11 | SCAFFOLD | §11.2 | 150 |
| 2 | 2.12 | SWARM Parallelism | §11.2 | 150 |
| 2 | 2.13 | Hivemind Byzantine | §11.2 | 150 |
| 3 | 3.1 | Top-K sparsification | §12.2 | 151 |
| 3 | 3.2 | Random-K sparsification | §12.2 | 151 |
| 3 | 3.3 | Threshold sparsification | §12.2 | 151 |
| 3 | 3.4 | PowerSGD | §12.2 | 151 |
| 3 | 3.5 | 1-bit Adam / LAMB | §12.2 | 151 |
| 3 | 3.6 | Signum / signSGD | §12.2 | 151 |
| 3 | 3.7 | QSGD | §12.2 | 151 |
| 3 | 3.8 | TernGrad | §12.2 | 151 |
| 3 | 3.9 | Deep gradient compression | §12.2 | 151 |
| 3 | 3.10 | ATOMO | §12.2 | 151 |
| 3 | 3.11 | DRIVE variants | §12.2 | 151 |
| 3 | 3.12 | THC | §12.2 | 151 |
| 3 | 3.13 | Inline LZ4 / zstd | §12.2 | 151 |
| 3 | 3.14 | Sketched gradient | §12.2 | 151 |
| 4 | 4.1-4.10 | Pipeline schedules | §13.2 | 152 |
| 5 | 5.1-5.9 | Tensor parallelism | §14.2 | 153 |
| 6 | 6.1-6.11 | FSDP / ZeRO | §15.2, §33 | 154 |
| 7 | 7.1-7.11 | MoE | §16.2 | 155 |
| 8 | 8.1-8.11 | Fault tolerance | §17.2 | various |
| 9 | 9.1-9.4 | Inference (networking-relevant only; rest out-of-scope) | §18.2 | 157 |
| 10 | 10.1-10.18 | Networking primitives | §19.2 | various |
| 11 | 11.1-11.14 | Consensus / membership | §20.2 | 158 |
| 12 | 12.1-12.13 | Observability | §21.2 | various |
| 13 | 13.1-13.9 | Power / thermal | §22.2, §29-§31 | 159 |
| 14 | 14.1-14.12 | Security | §23.2 | various |
| 15 | 15.1-15.10 | Operational | §24.2 | 142, 143 |
| 16 | 16.1-16.3 | Distributed perf frontiers (rest out-of-scope) | §25.2 | (compose existing) |
| 17 | 17.1-17.8 | Federation | §26.2, §35 | 162-163 |

## §37.4 The Crucible vs Fixy split rule

Repeated for emphasis (this is the load-bearing architectural discipline):

> **Crucible** owns mechanism — driver wrappers, syscalls, verbs, kernel emitters, telemetry harvesters, hardware control surfaces, type-theory machinery, concept gates, mint factories. Regular C++26.
>
> **Fixy** owns policy — strategies, schedulers, optimizers, composition templates. C++26 DSL with Met(X) row notation and FX-like syntax.

Every primitive in this doc follows this rule. When in doubt:
- Does it touch hardware / drivers / syscalls? → Substrate.
- Does it choose between alternatives based on user intent? → DSL.
- Does it provide a typed wrapper around a system facility? → Substrate.
- Does it compose multiple primitives into a strategy? → DSL.

## §37.5 Honest closing assessment

This document specifies ~200 primitives across 17 categories, ~75 GAPS-* tasks, ~30,500 LOC of new code, ~280 person-days of focused engineering effort. The architectural shape is decided; the verification methodology is committed; the type-system discipline is established.

**What ships today:** 1 of ~200 items (GAPS-001 crash-stop projection fix, 2026-05-03). The substrate that everything composes from (sessions, concurrent, safety, effects, bridges) is substantial — ~30,000 LOC across 156 headers — but the networking subsystem itself is greenfield (0 LOC).

**Calendar timeline to defensible "most advanced shipped" claim:** 12 months focused (5 engineers), realistic 18 months. The competitors:
- Microsoft Azure RDMA fabric (proprietary, deployed at scale, years ahead in measured operational data)
- Google Slingshot/Aquila (proprietary, deployed)
- NCCL+UCX+libfabric stack (open-source, mature, untyped)
- Various PyTorch / JAX distribution layers (open-source, vendor-coupled)

**Where Crucible/Fixy would lead the open-source space:**
- Type-system end-to-end semantic guarantees from session protocol through wire-level FEC (no other system attempts this)
- Cross-vendor bit-equivalence under recipe-tier discipline (no other system promises this)
- Federation kernel cache (computation genome) tied to topology-aware peer pairing (no other system has this)
- User-extensibility of every primitive without forking (PyTorch/JAX both require forks for FSDPv3-class changes)
- Cog substrate with hierarchical wear/health/power tracking (no other system has this)
- Adaptive MFU as control loop with multi-timescale optimization (no other system has this)
- Composable async + bandwidth reduction + crash-stop strategies (each component exists somewhere; the composition discipline doesn't)

**Where production stacks remain ahead:**
- Operational data at scale (10+ years of running 100K-GPU clusters)
- Hardware-specific tuning depth (NCCL has years of NVIDIA-specific optimizations)
- Battle-tested security in production federation
- Vendor relationship with hardware roadmaps

**The honest pitch:**

> "Most advanced *design intent* in the open-source distributed-training space, decomposing the entire problem into 75 atomic shippable tasks, all using primitives that exist in Linux 6.7+ or as small in-tree implementations, integrated under a type-system substrate that no other system attempts. Implementation: 1/200 items shipped today. Calendar timeline to defensible 'most advanced shipped': 12-18 months focused engineering. Closest production analogues are proprietary; closest open-source analogues are vendor-coupled and not user-extensible at the strategy level. Crucible's distinguishing claim — *type-system end-to-end semantic guarantees from session protocol through wire-level FEC, with full user-extensibility via Fixy DSL composition* — has no equivalent anywhere in open source."

That's defensible. Doesn't oversell. Maps to a concrete 12-month plan.


═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

**END OF DOCUMENT (§0–§37).**

Companion documents and existing infrastructure references:
- `CLAUDE.md` — substrate disciplines (8 axioms, Universal Mint Pattern, Cog framing)
- `misc/03_05_2026.md` — earlier integration audit (40 sections, 200 items)
- `misc/02_05_2026.md` — Phase 0 task tracking
- `misc/fixy.md` — Fixy DSL specification
- `MIMIC.md` — per-vendor backend specification (referenced for cross-vendor CI pattern)
- `FORGE.md` — vendor-neutral optimizer specification (referenced for IR pipeline)
- Existing GAPS-001..109 task catalog (from earlier work)
- New GAPS-110..184 task catalog (from this document; GAPS-173 marked out-of-scope)

═══════════════════════════════════════════════════════════════════════
═══════════════════════════════════════════════════════════════════════

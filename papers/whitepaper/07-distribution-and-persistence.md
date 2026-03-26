# 7. Distribution and Persistence

These components are designs.

## 7.1 The Canopy: Masterless Mesh

One Keeper per Relay. Gossip for discovery, Raft for critical state consensus, CRDTs for eventually-consistent metrics. No master node.

Any Keeper can propose changes (topology updates, optimization activations, Relay eviction). Changes take effect at the same iteration boundary across all participating Relays, using the same atomic swap mechanism as local DAG updates (Section 3.4).

**Spot-aware.** On 30-second eviction signal, the Keeper notifies the Canopy. Reshard to N-1 Relays --- redundant copies already exist on other Relays (Section 7.3), so no data migration is needed. New instance: Keeper discovers Canopy, loads Cipher, compiles device-specific kernels, joins.

## 7.2 Heterogeneous Compute

A single Vigil executes across Relays with different GPUs. The MemoryPlan carries device context (`DeviceType`, `device_idx`, `device_capability`) so each plan is self-describing. Each Relay compiles kernels for its local device via `(ContentHash, device_capability)`. The DAG is shared; only compiled kernels and memory plans differ.

**Load balancing.** Micro-batches distributed proportionally to measured throughput (LOR). An H100 receives more micro-batches than a 3090; both fully utilized. Gradients weighted by actual batch size.

**Multi-backend transport.** UCX: GPUDirect RDMA (NVIDIA), ROCm-aware RDMA (AMD), host-staged (TPU). Transport selected per link from Longitude measurements. Not NCCL-locked: cross-vendor GPU-to-GPU transfers (AMD to NVIDIA) route through the InfiniBand fabric with zero CPU staging.

**Adaptive topology.** The Canopy probes the actual network state continuously (N*N latency and bandwidth matrices). Per-collective, per-message-size algorithm selection: ring for bandwidth-bound (gradient all-reduce, 128MB), tree for latency-bound (parameter broadcast, 2MB), recursive halving-doubling for balanced (activation all-gather, 32MB), direct for sparse (expert routing all-to-all). Topology swaps at iteration boundaries via the same atomic mechanism as DAG branches. The Canopy routes around degraded links.

**DiLoCo enhancement.** DiLoCo (Distributed Local Optimization) runs DDP within each island, with periodic outer sync across islands. Crucible enhances every axis: (1) adaptive H --- measure parameter drift between islands, sync sooner on high drift, less on low drift. (2) Heterogeneous islands --- each island runs at full speed with different inner step counts, pseudo-gradients weighted by actual work. (3) Selective sync --- skip parameters with small deltas across H steps, saving 60%+ WAN bandwidth. (4) Compressed pseudo-gradients --- top-K sparsification with error feedback plus int8 quantization yields 50--100x bandwidth reduction. (5) Async outer sync --- no barriers, staleness-aware weighting: `weight = 1/(1 + staleness/H)`. (6) Hierarchical --- NVLink every step, InfiniBand every 5, WAN every 50, H auto-tuned per level from measured latencies, expressed as nested LoopNodes.

**5D parallelism auto-tuning.** Crucible measures actual per-dimension costs at runtime: TP all-gather latency, PP pipeline bubble, DP reduce-scatter time, EP all-to-all time, CP chunk transfer time. Given measurements, simulate alternative configurations; if predicted improvement exceeds a threshold, try for a few iterations, commit or rollback. The parallelism configuration evolves during training.

## 7.3 Redundancy

When a Relay fails in current distributed training systems, work since the last checkpoint is lost (typically 3-8 minutes). The Canopy implements configurable redundancy through an overlapping factor α:

- α = 0: pure FSDP. Each Relay stores only its own parameter shard. Any failure is catastrophic.
- α = 0.125: each Relay stores its shard plus 1/8 of its neighbor's. Survives 1 Relay failure. Memory overhead: 12.5%.
- α = 1.0: pure DDP. Every Relay stores everything. Survives N-1 failures. Maximum memory overhead.

Redundancy updates are pipelined into communication dead time (the network is idle during forward pass, backward pass, and optimizer step; redundancy copies transfer during these windows). On Relay failure: surviving Relays already hold the failed Relay's data. Reshard to N-1, recompute memory plans. Resume from exactly where the failed Relay left off.

Dynamic α: the Keeper on each Relay monitors hardware health (ECC errors, thermal events). A Relay accumulating errors gets higher α for its neighbors. Healthy Relays reduce α to save memory. Topology-aware placement ensures redundant copies span failure domains (different racks, different power supplies).

## 7.4 The Cipher: Event-Sourced Persistence

Event-sourced, not snapshot-based. Each iteration: the DAG chain is persisted (a few KB per step). Weight snapshots are periodic. Recovery to step T+500 from snapshot at T: load snapshot, replay 500 steps deterministically (DAG fixes execution order, memory plan fixes addresses, Philox4x32 fixes RNG state --- counter-based, platform-independent).

**Three tiers:**

- **Hot:** other Relays' RAM from redundancy (Section 7.3). Single Relay failure: immediate recovery, zero cost.
- **Warm:** local NVMe per Relay. Relay reboot recovery: seconds.
- **Cold:** durable object storage (S3, GCS). Total Canopy failure recovery: minutes.

The Cipher also stores the KernelCache, FX proof certificates, Longitude calibration data, and Augur's history. On migration: universal proofs (hash, protocol) are inherited. Hardware-specific state (kernels, topology) is regenerated.

## 7.5 Deterministic Replay

Four invariants produce bit-identical execution on the same hardware: the Merkle DAG fixes execution order, the MemoryPlan fixes tensor addresses, the KernelCache fixes kernel selection, and Philox4x32 (counter-based, platform-independent, seeded from a master counter in the Cipher) fixes random state. Each op derives its key from `hash(master_counter, op_index, content_hash)`; each thread generates `philox(thread_idx, op_key)` --- ~10 integer instructions in registers.

Cross-hardware replay preserves semantics but not bits (floating-point non-associativity in different kernel implementations).

Applications: exact reproducibility for research, regression testing between DAG versions, time-travel debugging (replay to any step, extract any activation, trace anomalies backward through the dataflow graph to root cause).

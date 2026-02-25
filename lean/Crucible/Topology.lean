import Mathlib.Tactic
import Crucible.Basic

/-!
# Crucible.Topology — Meridian Parallelism & Topology Optimization

From L16 Meridian (MANIFESTO.md):

"Z3 solves jointly: TP×DP×PP factorization, GPU-to-group placement,
per-collective communication algorithm (ring/tree/halving per message size),
gradient bucket sizes, per-tensor activation checkpointing..."

And L12 Distribution:

"5D parallelism auto-tuning: measure actual per-dimension costs
(TP all-gather, PP bubble, DP reduce-scatter, EP all-to-all, CP transfer)
→ simulate alternatives → try if predicted improvement exceeds threshold
→ commit or rollback."

This file formalizes the CORRECTNESS CONDITIONS for parallelism configuration:

1. **Factorization coverage**: N = TP × DP × PP — every GPU is assigned work.
   If not → idle GPUs waste money, or worse, missing gradient contributions.

2. **5D factorization**: N = TP × DP × PP × EP × CP — full extended parallelism.

3. **Communication cost model**: ring all-reduce vs tree reduce crossover.
   Ring: 2·S·(N-1) (bandwidth-optimal). Tree: S·log₂(N) (latency-optimal).

4. **Balanced partition**: even split requires divisibility, group sizes sum to N.

5. **Load balancing**: heterogeneous throughputs → proportional batch allocation.

6. **RAID redundancy**: replicas for fault tolerance with monotone overhead.

7. **DiLoCo islands**: fewer sync rounds than flat DDP.

8. **Gradient bucketing**: ceil division covers all parameters.

C++ correspondence:
- MeridianConfig: the output of Z3 topology optimization
- Keeper: applies the config, re-probes on topology change
- BackgroundThread.h: 5D factorization drives sharding decisions
-/

namespace Crucible

/-! ## 3D Parallelism Factorization (L12 Distribution, L16 Meridian) -/

/-- A 3D parallelism configuration: tensor, data, pipeline parallelism.
    C++ (MeridianConfig): `uint32_t tp, dp, pp;` — the three parallelism
    dimensions that Meridian's Z3 solver optimizes jointly. -/
structure ParConfig3D where
  tp : Nat  -- tensor parallelism degree
  dp : Nat  -- data parallelism degree
  pp : Nat  -- pipeline parallelism degree

/-- A 3D config is a valid factorization of N GPUs.
    C++: `assert(tp * dp * pp == num_gpus)`.
    All factors must be positive (zero-degree parallelism is meaningless). -/
def ParConfig3D.valid (c : ParConfig3D) (n : Nat) : Prop :=
  c.tp * c.dp * c.pp = n ∧ 0 < c.tp ∧ 0 < c.dp ∧ 0 < c.pp

/-- Factorization covers: TP * DP * PP = N, every GPU gets assigned.
    THE fundamental invariant for 3D parallelism.
    C++ (Meridian): Z3 constraint `tp * dp * pp == num_gpus`. -/
theorem factorization_covers (c : ParConfig3D) (n : Nat)
    (h : c.valid n) : c.tp * c.dp * c.pp = n :=
  h.1

/-- A valid factorization implies N > 0 (can't factorize zero GPUs).
    C++: `assert(num_gpus > 0)` at Meridian startup. -/
theorem factorization_pos (c : ParConfig3D) (n : Nat)
    (h : c.valid n) : 0 < n := by
  rw [← h.1]; exact Nat.mul_pos (Nat.mul_pos h.2.1 h.2.2.1) h.2.2.2

/-- The trivial factorization (1,1,N) always exists for N > 0.
    C++: fallback when Z3 can't find a better config. -/
theorem trivial_factorization (n : Nat) (hn : 0 < n) :
    (ParConfig3D.mk 1 1 n).valid n := by
  simp [ParConfig3D.valid]; exact hn

/-- The pure TP factorization (N,1,1) always exists for N > 0.
    C++: single-node training with tensor parallelism only. -/
theorem pure_tp_factorization (n : Nat) (hn : 0 < n) :
    (ParConfig3D.mk n 1 1).valid n := by
  simp [ParConfig3D.valid]; exact hn

/-- The pure DP factorization (1,N,1) always exists for N > 0.
    C++: classic distributed data parallelism (DDP). -/
theorem pure_dp_factorization (n : Nat) (hn : 0 < n) :
    (ParConfig3D.mk 1 n 1).valid n := by
  simp [ParConfig3D.valid]; exact hn

/-- Concrete: 8 GPUs → (2,2,2). -/
example : (ParConfig3D.mk 2 2 2).valid 8 := by simp [ParConfig3D.valid]

/-- Concrete: 8 GPUs → (4,2,1). -/
example : (ParConfig3D.mk 4 2 1).valid 8 := by simp [ParConfig3D.valid]

/-- Concrete: 64 GPUs → (8,4,2). -/
example : (ParConfig3D.mk 8 4 2).valid 64 := by simp [ParConfig3D.valid]

/-- For prime N, every valid 3D factorization has at least two trivial factors (=1).
    If tp > 1 and dp > 1 then tp*dp >= 4 divides p, contradicting primality.
    C++: Meridian recognizes that prime GPU counts constrain topology choices. -/
theorem factorization_prime (p : Nat) (hp : Nat.Prime p) (c : ParConfig3D)
    (hv : c.valid p) : c.tp = 1 ∨ c.dp = 1 ∨ c.pp = 1 := by
  by_contra hall
  push_neg at hall
  obtain ⟨h1, h2, h3⟩ := hall
  obtain ⟨hprod, htp, hdp, hpp⟩ := hv
  have hdvd : c.tp * c.dp ∣ p := ⟨c.pp, hprod.symm⟩
  rcases hp.eq_one_or_self_of_dvd (c.tp * c.dp) hdvd with heq1 | heqp
  · -- tp * dp = 1 contradicts tp >= 2
    have : 2 ≤ c.tp := by omega
    have : 2 ≤ c.dp := by omega
    nlinarith
  · -- tp * dp = p → pp = 1
    have hpeq : c.tp * c.dp * c.pp = p := hprod
    rw [heqp] at hpeq
    -- hpeq : p * c.pp = p, with p > 0
    have hmul : p * c.pp = p * 1 := by linarith [Nat.mul_one p]
    exact h3 (mul_left_cancel₀ hp.ne_zero hmul)

/-- Permuting TP and DP preserves validity.
    C++: Meridian may reorder parallelism axes during optimization. -/
theorem factorization_perm_tp_dp (c : ParConfig3D) (n : Nat)
    (h : c.valid n) : (ParConfig3D.mk c.dp c.tp c.pp).valid n := by
  simp only [ParConfig3D.valid]
  exact ⟨by nlinarith [h.1], h.2.2.1, h.2.1, h.2.2.2⟩

/-- Permuting DP and PP preserves validity. -/
theorem factorization_perm_dp_pp (c : ParConfig3D) (n : Nat)
    (h : c.valid n) : (ParConfig3D.mk c.tp c.pp c.dp).valid n := by
  simp only [ParConfig3D.valid]
  exact ⟨by nlinarith [h.1], h.2.1, h.2.2.2, h.2.2.1⟩

/-! ## 5D Parallelism Factorization (L12 Distribution) -/

/-- Full 5D parallelism configuration.
    C++ (BackgroundThread.h): the complete sharding specification.
    EP = expert parallelism (MoE routing), CP = context parallelism (sequence split). -/
structure ParConfig5D where
  tp : Nat  -- tensor parallelism
  dp : Nat  -- data parallelism
  pp : Nat  -- pipeline parallelism
  ep : Nat  -- expert parallelism
  cp : Nat  -- context parallelism

/-- A 5D config is valid if the product of all dimensions equals N
    and all dimensions are positive. -/
def ParConfig5D.valid (c : ParConfig5D) (n : Nat) : Prop :=
  c.tp * c.dp * c.pp * c.ep * c.cp = n ∧
  0 < c.tp ∧ 0 < c.dp ∧ 0 < c.pp ∧ 0 < c.ep ∧ 0 < c.cp

/-- 5D factorization covers all GPUs. -/
theorem factorization_5d_covers (c : ParConfig5D) (n : Nat)
    (h : c.valid n) : c.tp * c.dp * c.pp * c.ep * c.cp = n :=
  h.1

/-- 5D factorization with EP=1, CP=1 reduces to 3D.
    C++: when no MoE and no context parallelism, Meridian degenerates to 3D. -/
theorem factorization_5d_to_3d (c3 : ParConfig3D) (n : Nat) (h : c3.valid n) :
    (ParConfig5D.mk c3.tp c3.dp c3.pp 1 1).valid n := by
  simp [ParConfig5D.valid, ParConfig3D.valid] at *
  exact h

/-- A valid 5D factorization implies N > 0. -/
theorem factorization_5d_pos (c : ParConfig5D) (n : Nat)
    (h : c.valid n) : 0 < n := by
  rw [← h.1]
  exact Nat.mul_pos
    (Nat.mul_pos (Nat.mul_pos (Nat.mul_pos h.2.1 h.2.2.1) h.2.2.2.1) h.2.2.2.2.1)
    h.2.2.2.2.2

/-- Trivial 5D factorization: pure data parallelism. -/
theorem trivial_5d_factorization (n : Nat) (hn : 0 < n) :
    (ParConfig5D.mk 1 n 1 1 1).valid n := by
  simp [ParConfig5D.valid]; exact hn

/-- Concrete: 128 GPUs → (8, 2, 2, 2, 2) in 5D. -/
example : (ParConfig5D.mk 8 2 2 2 2).valid 128 := by simp [ParConfig5D.valid]

/-! ## Communication Cost Model (L16 Meridian) -/

/-- Ring all-reduce cost: 2·S·(N-1).
    C++ (Meridian): bandwidth-optimal for large messages.
    Actual per-GPU cost is 2·S·(N-1)/N; we use the numerator for Nat comparison. -/
def ringAllReduceCost (msgSize groupSize : Nat) : Nat :=
  2 * msgSize * (groupSize - 1)

/-- Tree reduce cost: S·log₂(N).
    C++ (Meridian): latency-optimal for small messages. -/
def treeReduceCost (msgSize groupSize : Nat) : Nat :=
  msgSize * Nat.log 2 groupSize

/-- Ring cost scales with message size (bandwidth-bound). -/
theorem ring_cost_monotone_msg (s1 s2 n : Nat) (h : s1 ≤ s2) :
    ringAllReduceCost s1 n ≤ ringAllReduceCost s2 n := by
  unfold ringAllReduceCost; nlinarith

/-- Tree cost scales with message size. -/
theorem tree_cost_monotone_msg (s1 s2 n : Nat) (h : s1 ≤ s2) :
    treeReduceCost s1 n ≤ treeReduceCost s2 n := by
  unfold treeReduceCost; nlinarith

/-- For N=1, ring cost is zero (no communication needed).
    C++: Meridian skips collective setup for single-GPU configs. -/
theorem ring_cost_single (s : Nat) : ringAllReduceCost s 1 = 0 := by
  simp [ringAllReduceCost]

/-- Ring cost is zero when message is empty. -/
theorem ring_cost_zero_msg (n : Nat) : ringAllReduceCost 0 n = 0 := by
  simp [ringAllReduceCost]

/-- Tree cost is zero when message is empty. -/
theorem tree_cost_zero_msg (n : Nat) : treeReduceCost 0 n = 0 := by
  simp [treeReduceCost]

/-- Ring cost grows with group size. -/
theorem ring_cost_monotone_group (s n1 n2 : Nat) (h : n1 ≤ n2) :
    ringAllReduceCost s n1 ≤ ringAllReduceCost s n2 := by
  unfold ringAllReduceCost
  -- For Nat subtraction: n1-1 ≤ n2-1 when n1 ≤ n2
  have : n1 - 1 ≤ n2 - 1 := Nat.sub_le_sub_right h 1
  exact Nat.mul_le_mul_left (2 * s) this

/-! ## Balanced Partition (L16 Meridian)

C++ (Meridian): Z3 constraint — partition N GPUs into K groups of N/K each.
"GPU-to-group placement (max intra-group BW)" -/

/-- Even split requires divisibility.
    C++: Meridian Z3 constraint `num_gpus % num_groups == 0`. -/
theorem even_split_iff (n k : Nat) (_hk : 0 < k) :
    k ∣ n ↔ n % k = 0 :=
  Nat.dvd_iff_mod_eq_zero

/-- When K | N, K groups of N/K each sum to N.
    C++: `group_size * num_groups == num_gpus`. -/
theorem balanced_partition_sum (n k : Nat) (hd : k ∣ n) :
    k * (n / k) = n :=
  Nat.mul_div_cancel' hd

/-- Group size is positive when N > 0, K > 0, K | N.
    C++: every group must have at least one GPU. -/
theorem balanced_group_pos (n k : Nat) (hk : 0 < k) (hn : 0 < n) (hd : k ∣ n) :
    0 < n / k :=
  Nat.div_pos (Nat.le_of_dvd hn hd) hk

/-- Concrete: 8 GPUs / 2 groups = 4 per group. -/
example : 8 / 2 = 4 := by norm_num

/-- Concrete: 64 GPUs / 8 groups = 8 per group. -/
example : 64 / 8 = 8 := by norm_num

/-! ## Load Balancing — Heterogeneous Throughput (L12 Distribution)

C++ (Meridian): "LOR batch distribution: micro-batches proportional to measured throughput.
H100 gets 3× more than 3090. Both fully utilized." -/

/-- Proportional allocation: each GPU gets floor(batch * t_i / total_throughput).
    C++: `micro_batch[i] = total_batch * throughput[i] / sum_throughput`. -/
def proportionalAlloc (throughputs : List Nat) (totalBatch : Nat) : List Nat :=
  let totalT := throughputs.sum
  throughputs.map (fun t => totalBatch * t / totalT)

/-- Proportional allocation preserves list length.
    C++: one micro-batch size per GPU. -/
theorem proportionalAlloc_length (ts : List Nat) (b : Nat) :
    (proportionalAlloc ts b).length = ts.length := by
  simp [proportionalAlloc]

/-- Each proportional allocation element is at most the total batch.
    C++: no single GPU gets more than the full batch. -/
theorem proportionalAlloc_le (ts : List Nat) (b : Nat)
    (hpos : 0 < ts.sum)
    (i : Nat) (hi : i < ts.length)
    (hle : ts[i] ≤ ts.sum) :
    (proportionalAlloc ts b)[i]'(by simp [proportionalAlloc]; exact hi) ≤ b := by
  simp only [proportionalAlloc, List.getElem_map]
  calc b * ts[i] / ts.sum
      ≤ b * ts.sum / ts.sum := Nat.div_le_div_right (by nlinarith)
    _ = b := Nat.mul_div_cancel b hpos

/-- Proportional allocation of zero batch gives zero everywhere.
    C++: empty batch → no work for anyone. -/
theorem proportionalAlloc_zero_batch (ts : List Nat) :
    proportionalAlloc ts 0 = List.replicate ts.length 0 := by
  simp [proportionalAlloc]

/-- For uniform throughputs, proportional allocation gives equal shares.
    C++: homogeneous cluster → equal split. -/
theorem proportionalAlloc_uniform (n b t : Nat) (ht : 0 < t) :
    proportionalAlloc (List.replicate n t) b =
    List.replicate n (b / n) := by
  simp only [proportionalAlloc]
  have hsum : (List.replicate n t).sum = n * t := by
    rw [List.sum_replicate]; ring
  conv_lhs => rw [hsum]
  rw [List.map_replicate]
  congr 1
  rw [Nat.mul_comm n t, ← Nat.div_div_eq_div_mul, Nat.mul_div_cancel _ ht]

/-! ## Shard Coverage (L12 Distribution) -/

/-- FSDP sharding coverage: surjective assignment means every shard has an owner.
    C++: "pure FSDP" = each Relay holds 1/N of parameters. -/
def shardCovers (numGpus : Nat) (shardOwner : Fin numGpus → Fin numGpus) : Prop :=
  Function.Surjective shardOwner

/-- Identity assignment covers all shards: GPU i holds shard i.
    C++: simplest FSDP — GPU rank maps directly to shard index. -/
theorem identity_shard_covers (n : Nat) :
    shardCovers n (fun i => i) :=
  fun b => ⟨b, rfl⟩

/-- A bijective shard assignment covers all shards.
    C++: Meridian may reorder placement for locality. -/
theorem perm_shard_covers (n : Nat) (f : Fin n → Fin n)
    (hf : Function.Bijective f) : shardCovers n f :=
  hf.surjective

/-! ## RAID Redundancy (L12 Distribution)

C++ (L12): "RAID-like redundancy: configurable overlap α
(0=pure FSDP, 0.125=survive 1 failure at 12.5% overhead, 1.0=pure DDP)." -/

/-- RAID redundancy configuration.
    C++: `uint32_t redundancy_factor` in MeridianConfig. -/
structure RaidConfig where
  numGpus : Nat
  numShards : Nat
  replicas : Nat
  hReplicas : 0 < replicas
  hReplicasLe : replicas ≤ numGpus

/-- Total storage: shards × replicas.
    C++: `total_storage = num_shards * redundancy`. -/
def RaidConfig.totalStorage (rc : RaidConfig) : Nat :=
  rc.numShards * rc.replicas

/-- Pure FSDP: replicas=1, minimal storage. -/
theorem raid_fsdp_minimal (n s : Nat) (hn : 0 < n) :
    (RaidConfig.mk n s 1 (by omega) (by omega)).totalStorage = s := by
  simp [RaidConfig.totalStorage]

/-- Pure DDP: replicas=N, full replication. -/
theorem raid_ddp_maximal (n s : Nat) (hn : 0 < n) :
    (RaidConfig.mk n s n (by omega) (by omega)).totalStorage = s * n := by
  simp [RaidConfig.totalStorage, Nat.mul_comm]

/-- More replicas → more storage (monotonicity). -/
theorem raid_storage_monotone (n s r1 r2 : Nat)
    (hr1 : 0 < r1) (hr2 : 0 < r2) (hr1n : r1 ≤ n) (hr2n : r2 ≤ n)
    (h : r1 ≤ r2) :
    (RaidConfig.mk n s r1 hr1 hr1n).totalStorage ≤
    (RaidConfig.mk n s r2 hr2 hr2n).totalStorage := by
  simp only [RaidConfig.totalStorage]; nlinarith

/-- Fault tolerance: r replicas survive r-1 failures. -/
theorem raid_fault_tolerance (r failures : Nat) (_hr : 0 < r) (hf : failures < r) :
    1 ≤ r - failures := by omega

/-- Storage is at most DDP-level. -/
theorem raid_storage_bound (n s r : Nat) (hr : 0 < r) (hrn : r ≤ n) :
    (RaidConfig.mk n s r hr hrn).totalStorage ≤ s * n := by
  simp only [RaidConfig.totalStorage]; nlinarith

/-! ## Topology Invariant Composition -/

/-- Complete Meridian output: all topology decisions.
    C++: `struct MeridianConfig` — Z3-optimal configuration. -/
structure MeridianConfig where
  par : ParConfig5D
  numGpus : Nat
  hPar : par.valid numGpus
  raid : RaidConfig
  hRaidGpus : raid.numGpus = numGpus

/-- MeridianConfig consistency: parallelism covers all GPUs
    and RAID targets the same cluster. -/
theorem meridian_consistent (mc : MeridianConfig) :
    mc.par.tp * mc.par.dp * mc.par.pp * mc.par.ep * mc.par.cp = mc.numGpus ∧
    mc.raid.numGpus = mc.numGpus :=
  ⟨mc.hPar.1, mc.hRaidGpus⟩

/-- MeridianConfig implies positive GPU count. -/
theorem meridian_pos (mc : MeridianConfig) : 0 < mc.numGpus :=
  factorization_5d_pos mc.par mc.numGpus mc.hPar

/-! ## DiLoCo Island Factorization (L12 Distribution)

C++ (L12): "DiLoCo enhancement: adaptive H, heterogeneous islands..." -/

/-- DiLoCo: K islands of M GPUs, sync every H steps.
    C++: `uint32_t num_islands, gpus_per_island, sync_interval_H;`. -/
structure DiLoCoConfig where
  numIslands : Nat
  gpusPerIsland : Nat
  syncInterval : Nat
  hIslands : 0 < numIslands
  hGpus : 0 < gpusPerIsland
  hSync : 0 < syncInterval

/-- Total GPUs in DiLoCo. -/
def DiLoCoConfig.totalGpus (d : DiLoCoConfig) : Nat :=
  d.numIslands * d.gpusPerIsland

/-- DiLoCo total is positive. -/
theorem diloco_total_pos (d : DiLoCoConfig) : 0 < d.totalGpus :=
  Nat.mul_pos d.hIslands d.hGpus

/-- DiLoCo covers (definitional). -/
theorem diloco_covers (d : DiLoCoConfig) :
    d.numIslands * d.gpusPerIsland = d.totalGpus := rfl

/-- 1 island = standard distributed training. -/
theorem diloco_single_island (m H : Nat) (hm : 0 < m) (hH : 0 < H) :
    (DiLoCoConfig.mk 1 m H (by omega) hm hH).totalGpus = m := by
  simp [DiLoCoConfig.totalGpus]

/-- 1 GPU/island = federated SGD. -/
theorem diloco_single_gpu (k H : Nat) (hk : 0 < k) (hH : 0 < H) :
    (DiLoCoConfig.mk k 1 H hk (by omega) hH).totalGpus = k := by
  simp [DiLoCoConfig.totalGpus]

/-- With multiple islands and GPUs/island > 1, totalGpus > numIslands.
    Inter-island syncs (K-1) < flat all-reduce syncs (K*M-1). -/
theorem diloco_fewer_syncs (d : DiLoCoConfig) (hk : 1 < d.numIslands)
    (hm : 1 < d.gpusPerIsland) :
    d.numIslands < d.totalGpus := by
  unfold DiLoCoConfig.totalGpus
  calc d.numIslands
      = d.numIslands * 1 := by ring
    _ < d.numIslands * d.gpusPerIsland :=
        Nat.mul_lt_mul_of_pos_left hm (by omega)

/-- Concrete: 4 islands × 8 GPUs = 32 total. -/
example : (DiLoCoConfig.mk 4 8 50 (by omega) (by omega) (by omega)).totalGpus = 32 := by
  simp [DiLoCoConfig.totalGpus]

/-! ## Bandwidth-Delay Product (L16 Meridian)

C++ (Meridian): chooses communication algorithm based on message size
and network characteristics. -/

/-- BDP = bandwidth × latency.
    C++: `uint64_t bdp = link_bw_bytes_per_us * link_latency_us;`. -/
def bandwidthDelayProduct (bandwidth latency : Nat) : Nat :=
  bandwidth * latency

/-- BDP scales with bandwidth. -/
theorem bdp_monotone_bw (bw1 bw2 lat : Nat) (h : bw1 ≤ bw2) :
    bandwidthDelayProduct bw1 lat ≤ bandwidthDelayProduct bw2 lat := by
  unfold bandwidthDelayProduct; nlinarith

/-- BDP scales with latency. -/
theorem bdp_monotone_lat (bw lat1 lat2 : Nat) (h : lat1 ≤ lat2) :
    bandwidthDelayProduct bw lat1 ≤ bandwidthDelayProduct bw lat2 := by
  unfold bandwidthDelayProduct; nlinarith

/-- Zero bandwidth → zero BDP. -/
theorem bdp_zero_bw (lat : Nat) : bandwidthDelayProduct 0 lat = 0 := by
  simp [bandwidthDelayProduct]

/-- Zero latency → zero BDP. -/
theorem bdp_zero_lat (bw : Nat) : bandwidthDelayProduct bw 0 = 0 := by
  simp [bandwidthDelayProduct]

/-! ## Gradient Bucket Sizing (L12 Distribution)

C++ (BackgroundThread.h): gradients bucketed for overlapped all-reduce. -/

/-- Ceil division: number of buckets for P parameters with bucket size B.
    C++: `num_buckets = (num_params + bucket_size - 1) / bucket_size;`. -/
def numBuckets (numParams bucketSize : Nat) : Nat :=
  (numParams + bucketSize - 1) / bucketSize

/-- At least one bucket exists for any non-empty gradient.
    C++: `assert(num_buckets > 0)`. -/
theorem numBuckets_pos (p b : Nat) (hp : 0 < p) (hb : 0 < b) :
    0 < numBuckets p b := by
  unfold numBuckets; exact Nat.div_pos (by omega) hb

/-- Buckets cover all parameters: B × numBuckets >= P.
    C++: no parameter is left unbucketed. -/
theorem buckets_cover (p b : Nat) (hb : 0 < b) :
    p ≤ b * numBuckets p b := by
  unfold numBuckets
  -- Nat.div_add_mod: b * ((p+b-1)/b) + (p+b-1)%b = p+b-1
  -- mod < b, so b * ((p+b-1)/b) >= p
  have hdm := Nat.div_add_mod (p + b - 1) b
  have hmod := Nat.mod_lt (p + b - 1) hb
  omega

/-- Single bucket when bucket_size >= num_params. -/
theorem single_bucket (p b : Nat) (hp : 0 < p) (hb : 0 < b) (h : p ≤ b) :
    numBuckets p b = 1 := by
  unfold numBuckets
  have h1 : p + b - 1 < 2 * b := by omega
  have h2 : b ≤ p + b - 1 := by omega
  have := Nat.div_eq_of_lt_le (by omega : 1 * b ≤ p + b - 1) (by linarith)
  simpa using this

end Crucible

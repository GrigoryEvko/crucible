import Mathlib.Tactic
import Crucible.Basic

/-!
# Crucible.Fusion — L1/L6 Kernel Fusion Legality & Optimization

From L1 Kernels (MANIFESTO.md):

  "Kernel fusion: adjacent ops with single producer-consumer chain fuse into
   one kernel keeping intermediates in registers/shared memory, eliminating
   HBM round trips. Decision from DFG topology at compile time."

From L6 Merkle DAG:

  "RegionNodes: compilable op sequences. content_hash = hash(schema_hashes,
   input shapes/strides/dtypes/devices, scalar values). Identical computation
   → identical hash, even across models."

This file formalizes:

1. **DFG structure**: dataflow graph as adjacency list (nodes + directed edges)
2. **Fusion legality**: edge exists ∧ single producer ∧ no external consumers
3. **Memory savings**: fused ops eliminate HBM round-trips for intermediates
4. **Cost model**: unfused vs fused execution cost with bandwidth term
5. **Fusion chains**: multi-op fusion, associative savings
6. **Register pressure**: chain length vs register file capacity constraint
7. **Elementwise fusion**: always-legal special case with zero intermediate
8. **Shared memory staging**: intermediate tier between registers and HBM

All quantities use Nat (cycles, bytes). Zero sorry.

C++ correspondence:
- `TraceGraph::build()` — discovers producer-consumer DFG edges
- `BackgroundThread::fuse_region()` — fusion legality check
- `KernelCache` — fused kernels cached by content_hash
- `PoolAllocator` — memory plan eliminates fused intermediates
- `RegionNode::compiled` — compiled fused kernel pointer
-/

namespace Crucible

/-! ## 1. Dataflow Graph Structure

C++ (TraceGraph.h): bidirectional CSR property graph. Nodes = ops, edges = DATA_FLOW
relationships discovered via PtrMap (open-addressing, 8192 slots). Output data_ptr
matches input data_ptr → producer-consumer edge. -/

/-- A node in the dataflow graph: one recorded op.
    C++: `TraceEntry` in TraceRing — each op has a compute cost and output size. -/
structure DFGNode where
  id : Nat
  op_cost : Nat       -- compute cost in cycles (kernel execution time)
  mem_bytes : Nat      -- output tensor size in bytes (intermediate if fused away)

/-- Dataflow graph: nodes + directed edges (producer → consumer).
    C++: `TraceGraph` — built from one iteration's recorded ops.
    Edges represent data dependencies: edge (a, b) means node a produces
    a tensor consumed by node b. -/
structure DFG where
  nodes : List DFGNode
  edges : List (Nat × Nat)  -- (producer_id, consumer_id)

/-- Check whether a directed edge exists between two node IDs.
    C++: `TraceGraph::has_edge(src, dst)` — CSR adjacency lookup. -/
def DFG.hasEdge (g : DFG) (src dst : Nat) : Prop :=
  (src, dst) ∈ g.edges

instance (g : DFG) (src dst : Nat) : Decidable (g.hasEdge src dst) := by
  unfold DFG.hasEdge; infer_instance

/-- Check whether a node ID exists in the graph. -/
def DFG.hasNode (g : DFG) (nid : Nat) : Prop :=
  ∃ n ∈ g.nodes, n.id = nid

/-- Count the number of producers feeding into a given consumer.
    C++: in-degree from CSR backward adjacency. -/
def DFG.producerCount (g : DFG) (consumer : Nat) : Nat :=
  g.edges.countP (fun e => e.2 == consumer)

/-- Count the number of consumers of a given producer's output.
    C++: out-degree from CSR forward adjacency — determines if intermediate
    has external observers. -/
def DFG.consumerCount (g : DFG) (producer : Nat) : Nat :=
  g.edges.countP (fun e => e.1 == producer)

/-! ## 2. Fusion Legality

Two ops can be fused if and only if:
1. Direct data-flow edge exists (producer → consumer)
2. Consumer has exactly one producer for that input (no fan-in)
3. Intermediate tensor has no other consumers (no external observers)

C++: `BackgroundThread::fuse_region()` checks these three conditions
before creating a fused RegionNode in the Merkle DAG. -/

/-- A fusion candidate: a pair (producer, consumer) to potentially merge.
    C++: candidate pairs discovered during `build_trace()`. -/
abbrev FusionCandidate := Nat × Nat

/-- Fusion legality predicate. All three conditions must hold.
    C++: `BackgroundThread::is_legal_fusion(producer_id, consumer_id)`. -/
def isLegalFusion (g : DFG) (f : FusionCandidate) : Prop :=
  g.hasEdge f.1 f.2 ∧                    -- direct edge exists
  g.producerCount f.2 = 1 ∧              -- consumer has exactly one producer
  g.consumerCount f.1 = 1                 -- intermediate has no other consumers

instance (g : DFG) (f : FusionCandidate) : Decidable (isLegalFusion g f) :=
  inferInstanceAs (Decidable (_ ∧ _ ∧ _))

/-- Legal fusion requires an edge between producer and consumer.
    C++: can't fuse ops with no data dependency. -/
theorem legal_fusion_has_edge (g : DFG) (f : FusionCandidate)
    (h : isLegalFusion g f) : g.hasEdge f.1 f.2 :=
  h.1

/-- Legal fusion requires single producer.
    C++: fan-in fusion needs special reduction handling — not supported here. -/
theorem legal_fusion_single_producer (g : DFG) (f : FusionCandidate)
    (h : isLegalFusion g f) : g.producerCount f.2 = 1 :=
  h.2.1

/-- Legal fusion requires no external consumers.
    C++: if intermediate is observed by another op, it must be materialized to HBM. -/
theorem legal_fusion_no_external (g : DFG) (f : FusionCandidate)
    (h : isLegalFusion g f) : g.consumerCount f.1 = 1 :=
  h.2.2

/-- Illegal fusion example: when the intermediate has multiple consumers,
    fusion is illegal because materializing the intermediate is required.
    C++: `fuse_region()` rejects when `consumer_count > 1`. -/
theorem illegal_when_multiple_consumers (g : DFG) (f : FusionCandidate)
    (h : 1 < g.consumerCount f.1) : ¬isLegalFusion g f := by
  intro ⟨_, _, hc⟩
  omega

/-- Illegal fusion example: when consumer has multiple producers,
    fusion is illegal (fan-in not supported without reduction).
    C++: `fuse_region()` rejects fan-in candidates. -/
theorem illegal_when_fan_in (g : DFG) (f : FusionCandidate)
    (h : 1 < g.producerCount f.2) : ¬isLegalFusion g f := by
  intro ⟨_, hp, _⟩
  omega

/-- No edge → no fusion (contrapositive of legality).
    C++: only data-flow edges create fusion opportunities. -/
theorem no_edge_no_fusion (g : DFG) (f : FusionCandidate)
    (h : ¬g.hasEdge f.1 f.2) : ¬isLegalFusion g f := by
  intro ⟨he, _, _⟩
  exact h he

/-! ## 3. Memory Savings from Fusion

Without fusion: write intermediate to HBM (1 × mem_bytes), read it back
(1 × mem_bytes) → total 2 × mem_bytes HBM traffic for the intermediate.
With fusion: keep in registers → 0 HBM traffic for intermediate.

C++ (L1): "adjacent ops with single producer-consumer chain fuse into one
kernel keeping intermediates in registers/shared memory, eliminating HBM
round trips." -/

/-- HBM bytes saved by fusing: the intermediate's write + read eliminated.
    C++: each eliminated intermediate saves 2× its size in HBM traffic. -/
def hbmSaved (intermediate_bytes : Nat) : Nat :=
  2 * intermediate_bytes

/-- Fusion always saves non-negative bytes (trivially, savings ≥ 0).
    C++: fusing never increases HBM traffic. -/
theorem fusion_saves_nonneg (intermediate_bytes : Nat) :
    0 ≤ hbmSaved intermediate_bytes :=
  Nat.zero_le _

/-- Fusion saves exactly 2× the intermediate size.
    C++: write (producer→HBM) + read (HBM→consumer) both eliminated. -/
theorem fusion_saves_double (b : Nat) : hbmSaved b = 2 * b := rfl

/-- Larger intermediates → more savings (proportional).
    C++: Augur prioritizes fusing ops with large intermediate tensors. -/
theorem fusion_savings_monotone (b₁ b₂ : Nat) (h : b₁ ≤ b₂) :
    hbmSaved b₁ ≤ hbmSaved b₂ := by
  simp only [hbmSaved]; omega

/-- Positive intermediate → positive savings.
    C++: every non-zero intermediate produces real HBM traffic reduction. -/
theorem fusion_saves_pos (b : Nat) (hb : 0 < b) : 0 < hbmSaved b := by
  simp only [hbmSaved]; omega

/-! ## 4. Fused Kernel Cost Model

Unfused: cost₁ + cost₂ + memory_overhead (2 × intermediate_bytes / bandwidth).
Fused:   cost₁ + cost₂ (memory traffic eliminated for intermediate).
Savings: the memory overhead term.

We model the memory overhead as a separate Nat parameter to avoid Nat division
complications. The caller computes `mem_overhead = 2 * ib / bw` externally.

C++ (L1): "CUPTI-informed autotuning: each CUPTI diagnosis → one targeted
variant → one benchmark → converge in 3-5 iterations." -/

/-- Cost of executing two ops unfused (sequential with HBM round-trip).
    mem_overhead = 2 * intermediate_bytes / bandwidth (precomputed by caller).
    C++: `kernel_launch(op1); cudaMemcpy; kernel_launch(op2);` -/
def unfusedCost (cost₁ cost₂ mem_overhead : Nat) : Nat :=
  cost₁ + cost₂ + mem_overhead

/-- Cost of executing two ops fused (single kernel, no HBM round-trip).
    C++: `kernel_launch(fused_op1_op2);` — intermediate stays in registers. -/
def fusedCost (cost₁ cost₂ : Nat) : Nat :=
  cost₁ + cost₂

/-- Fused cost ≤ unfused cost: fusion is always beneficial when legal.
    THE fundamental fusion theorem — fusing never makes things worse.
    C++: `assert(fused_time <= unfused_time)` in benchmark validation. -/
theorem fused_le_unfused (c₁ c₂ overhead : Nat) :
    fusedCost c₁ c₂ ≤ unfusedCost c₁ c₂ overhead := by
  simp only [fusedCost, unfusedCost]; omega

/-- The savings from fusion equal the memory overhead term.
    C++: savings measured as eliminated HBM traffic time. -/
theorem fusion_savings_eq (c₁ c₂ overhead : Nat) :
    unfusedCost c₁ c₂ overhead - fusedCost c₁ c₂ = overhead := by
  simp only [unfusedCost, fusedCost]; omega

/-- Strictly beneficial when memory overhead is positive.
    C++: positive savings whenever there's a real intermediate tensor. -/
theorem fusion_strictly_beneficial (c₁ c₂ overhead : Nat)
    (h : 0 < overhead) :
    fusedCost c₁ c₂ < unfusedCost c₁ c₂ overhead := by
  simp only [fusedCost, unfusedCost]; omega

/-- Larger memory overhead → more savings from fusion (monotone).
    C++: Augur prioritizes fusing ops with large intermediate tensors. -/
theorem fusion_savings_monotone_overhead (c₁ c₂ o₁ o₂ : Nat) (h : o₁ ≤ o₂) :
    unfusedCost c₁ c₂ o₁ ≤ unfusedCost c₁ c₂ o₂ := by
  simp only [unfusedCost]; omega

/-! ## 5. Fusion Chains (Multi-op Fusion)

A chain [op₁, op₂, ..., opₖ] where each opᵢ → opᵢ₊₁ can be fused into
one kernel. Total savings = sum of all intermediate memory eliminated.

C++ (L6): fused chains become single RegionNodes in the Merkle DAG.
content_hash covers the entire chain, enabling cross-model kernel reuse. -/

/-- Total compute cost of a chain of ops (sum of individual costs).
    C++: `RegionNode::compute_cost` for the fused region. -/
def chainComputeCost (ops : List DFGNode) : Nat :=
  ops.foldl (fun acc n => acc + n.op_cost) 0

/-- Total intermediate bytes in a chain: sum of all output sizes except the last.
    The last op's output goes to HBM (it's the final result, not an intermediate).
    C++: `RegionNode::intermediate_bytes` — bytes eliminated by fusion. -/
def chainIntermediateBytes : List DFGNode → Nat
  | [] => 0
  | [_] => 0
  | n :: rest => n.mem_bytes + chainIntermediateBytes rest

/-- Total HBM saved by fusing an entire chain: 2× each intermediate.
    C++: total traffic reduction from fusing a RegionNode. -/
def chainHbmSaved (ops : List DFGNode) : Nat :=
  2 * chainIntermediateBytes ops

/-- Empty chain has zero intermediate bytes. -/
theorem chain_empty_zero : chainIntermediateBytes ([] : List DFGNode) = 0 := rfl

/-- Single op has zero intermediate bytes (nothing to fuse).
    C++: a RegionNode with one op has no internal intermediates. -/
theorem chain_single_zero (n : DFGNode) : chainIntermediateBytes [n] = 0 := rfl

/-- Two-op chain: intermediate = first op's output.
    C++: fusing two adjacent ops eliminates one intermediate. -/
theorem chain_two_intermediate (a b : DFGNode) :
    chainIntermediateBytes [a, b] = a.mem_bytes := by
  simp [chainIntermediateBytes]

/-- Chain savings are non-negative.
    C++: fusion never increases HBM traffic. -/
theorem chain_savings_nonneg (ops : List DFGNode) :
    0 ≤ chainHbmSaved ops :=
  Nat.zero_le _

/-- Fusing a two-op chain saves 2 × first op's output.
    C++: the simplest fusion case — one intermediate eliminated. -/
theorem chain_two_savings (a b : DFGNode) :
    chainHbmSaved [a, b] = 2 * a.mem_bytes := by
  simp [chainHbmSaved, chainIntermediateBytes]

/-- Extending a chain by prepending a node increases intermediate bytes.
    C++: longer fused regions eliminate more intermediates. -/
theorem chain_extend_intermediates (n : DFGNode) (a : DFGNode)
    (rest : List DFGNode) :
    chainIntermediateBytes (a :: rest) ≤
    chainIntermediateBytes (n :: a :: rest) := by
  show chainIntermediateBytes (a :: rest) ≤ n.mem_bytes + chainIntermediateBytes (a :: rest)
  omega

/-- Chain of k ops (k ≥ 2) with positive mem_bytes eliminates at least one intermediate.
    C++: any non-trivial fusion provides measurable HBM savings. -/
theorem chain_nontrivial_savings (a b : DFGNode) (rest : List DFGNode)
    (hpos : 0 < a.mem_bytes) :
    0 < chainIntermediateBytes (a :: b :: rest) := by
  simp [chainIntermediateBytes]; omega

/-- Fusion chain cost: foldl with shifted accumulator.
    C++: RegionNode compute cost is a simple sum — order doesn't matter. -/
private theorem foldl_op_cost_shift (base extra : Nat) (ops : List DFGNode) :
    List.foldl (fun a m => a + m.op_cost) (base + extra) ops =
    List.foldl (fun a m => a + m.op_cost) base ops + extra := by
  induction ops generalizing base with
  | nil => rfl
  | cons hd tl ih =>
    simp only [List.foldl_cons]
    rw [show base + extra + hd.op_cost = (base + hd.op_cost) + extra from by omega]
    exact ih (base + hd.op_cost)

theorem chain_compute_cons (nd : DFGNode) (ops : List DFGNode) :
    chainComputeCost (nd :: ops) = chainComputeCost ops + nd.op_cost := by
  simp only [chainComputeCost, List.foldl_cons]
  exact foldl_op_cost_shift 0 nd.op_cost ops

/-! ## 6. Register Pressure Constraint

Fusion is limited by the GPU register file. Each fused op may keep its
intermediate output in registers. If total register usage exceeds
max_registers, the kernel must spill to shared memory (slower, but still
better than HBM round-trip).

C++ (L1): "CUPTI: Register spill > 0 AND occupancy < 50% → register
pressure → use shared memory staging." -/

/-- Register pressure model: simplified as chain length (each fused op
    uses one set of registers for its intermediate).
    C++: actual pressure depends on tensor sizes and kernel code, but
    chain length is a conservative proxy. -/
def registerPressure (chain : List DFGNode) : Nat :=
  chain.length

/-- Predicate: chain fits in register file.
    C++: CUPTI counter `register_spill == 0` means chain fits. -/
def canFuseInRegisters (chain : List DFGNode) (max_regs : Nat) : Prop :=
  registerPressure chain ≤ max_regs

instance (chain : List DFGNode) (max_regs : Nat) :
    Decidable (canFuseInRegisters chain max_regs) :=
  inferInstanceAs (Decidable (_ ≤ _))

/-- Empty chain trivially fits in any register file.
    C++: no ops → no registers needed. -/
theorem empty_chain_fits (max_regs : Nat) :
    canFuseInRegisters [] max_regs := by
  simp [canFuseInRegisters, registerPressure]

/-- Single op always fits when max_regs ≥ 1.
    C++: a single kernel never has fusion-related register pressure. -/
theorem single_op_fits (n : DFGNode) (max_regs : Nat) (h : 1 ≤ max_regs) :
    canFuseInRegisters [n] max_regs := by
  simp [canFuseInRegisters, registerPressure]; exact h

/-- Two-op chain fits when max_regs ≥ 2.
    C++: the simplest fusion case always fits in practice (GPUs have ≥64K regs). -/
theorem two_op_fits (a b : DFGNode) (max_regs : Nat) (h : 2 ≤ max_regs) :
    canFuseInRegisters [a, b] max_regs := by
  simp [canFuseInRegisters, registerPressure]; exact h

/-- Register pressure is monotone: longer chains → more pressure.
    C++: CUPTI register_spill increases monotonically with fusion depth. -/
theorem register_pressure_monotone (chain : List DFGNode) (n : DFGNode) :
    registerPressure chain ≤ registerPressure (n :: chain) := by
  simp [registerPressure]

/-- Sub-chain fits if full chain fits (monotonicity).
    C++: if a fused region passes register check, any prefix also passes. -/
theorem subchain_fits (chain : List DFGNode) (n : DFGNode)
    (max_regs : Nat) (h : canFuseInRegisters (n :: chain) max_regs) :
    canFuseInRegisters chain max_regs := by
  simp only [canFuseInRegisters, registerPressure] at *
  simp only [List.length_cons] at h
  omega

/-- Exceeding register limit: chain too long for register file.
    C++: CUPTI detects `register_spill > 0` → fall back to shared memory staging. -/
theorem exceeds_registers (chain : List DFGNode) (max_regs : Nat)
    (h : max_regs < registerPressure chain) :
    ¬canFuseInRegisters chain max_regs := by
  simp only [canFuseInRegisters]; omega

/-! ## 7. Elementwise Fusion (Special Case)

Elementwise ops (ADD, MUL, RELU, etc.) can ALWAYS fuse — they share the same
iteration pattern (one thread per element), produce no real intermediate
(each element is consumed immediately by the next op), and have minimal
register pressure per element.

C++ (L1): elementwise kernels have identical launch configurations (same grid
dimensions), so fusion is a simple loop body concatenation. -/

/-- An elementwise op produces zero intermediate bytes per element:
    the output of op₁ is immediately consumed by op₂ in the same thread.
    C++: elementwise fusion keeps values in a single register per thread. -/
structure ElementwiseOp where
  id : Nat
  op_cost : Nat
  -- mem_bytes = 0 for intermediates: no HBM materialization needed

/-- Convert elementwise op to DFGNode with zero intermediate bytes.
    C++: elementwise ops produce output tensors of known size, but when
    fused, the intermediate is never materialized. -/
def ElementwiseOp.toDFGNode (e : ElementwiseOp) : DFGNode :=
  { id := e.id, op_cost := e.op_cost, mem_bytes := 0 }

/-- Helper: mapping elementwise ops to DFGNodes preserves list structure. -/
private theorem ew_map_cons (x : ElementwiseOp) (xs : List ElementwiseOp) :
    (x :: xs).map ElementwiseOp.toDFGNode =
    x.toDFGNode :: xs.map ElementwiseOp.toDFGNode := rfl

/-- Any list of DFGNodes with all mem_bytes = 0 has zero intermediate bytes. -/
private theorem chainIntermediateBytes_all_zero :
    ∀ (ns : List DFGNode), (∀ n ∈ ns, n.mem_bytes = 0) →
    chainIntermediateBytes ns = 0 := by
  intro ns
  match ns with
  | [] => simp [chainIntermediateBytes]
  | [_] => simp [chainIntermediateBytes]
  | a :: b :: rest =>
    intro hall
    show a.mem_bytes + chainIntermediateBytes (b :: rest) = 0
    have ha : a.mem_bytes = 0 := hall a (.head _)
    have htail : ∀ n ∈ b :: rest, n.mem_bytes = 0 :=
      fun n hn => hall n (.tail a hn)
    rw [ha, chainIntermediateBytes_all_zero (b :: rest) htail]

/-- Elementwise chain has zero intermediate bytes: nothing written to HBM.
    THE elementwise fusion theorem — these ops always keep data in registers.
    C++: `fuse_elementwise()` skips the intermediate materialization check. -/
theorem elementwise_zero_intermediate (ops : List ElementwiseOp) :
    chainIntermediateBytes (ops.map ElementwiseOp.toDFGNode) = 0 := by
  apply chainIntermediateBytes_all_zero
  intro n hn
  rw [List.mem_map] at hn
  obtain ⟨e, _, rfl⟩ := hn
  rfl

/-- Elementwise fusion saves zero HBM (intermediates are already zero-cost).
    The benefit is launch overhead elimination, not memory savings.
    C++: elementwise fusion benefit is kernel launch reduction (~5μs/launch). -/
theorem elementwise_zero_hbm_saved (ops : List ElementwiseOp) :
    chainHbmSaved (ops.map ElementwiseOp.toDFGNode) = 0 := by
  simp [chainHbmSaved, elementwise_zero_intermediate]

/-- Elementwise fusion is always legal in terms of register pressure:
    each element uses one register, independent of chain length.
    (This models the per-element view; total registers = chain_length × 1.) -/
theorem elementwise_low_pressure (ops : List ElementwiseOp) (max_regs : Nat)
    (h : ops.length ≤ max_regs) :
    canFuseInRegisters (ops.map ElementwiseOp.toDFGNode) max_regs := by
  simp [canFuseInRegisters, registerPressure, List.length_map]; exact h

/-! ## 8. Shared Memory Staging

When register fusion isn't possible (too much data, complex access patterns),
use shared memory (48KB L1d on modern GPUs) as an intermediate tier:

- Without fusion: HBM write + HBM read per intermediate (slow, ~200ns)
- Shared memory: HBM read once → shared memory → multiple reads (~5ns each)
- Register fusion: everything in registers (~0ns for intermediates)

Three-tier cost model: register < shared_mem < HBM.

C++ (L1): "CUPTI: memory bandwidth >80% AND SM <60% → memory-bound →
increase tile size" — shared memory tiling is the standard fix. -/

/-- Memory tier for intermediate storage.
    C++: determined by CUPTI register_spill and shared_memory_usage counters. -/
inductive MemTier where
  | Register     -- fastest: ~0 extra cycles for intermediate access
  | SharedMem    -- medium: ~5ns per access, 48KB capacity
  | HBM          -- slowest: ~200ns per access, multi-GB capacity
  deriving DecidableEq, Repr

/-- Access cost per byte for each memory tier (in abstract units).
    C++: approximate latencies from NVIDIA Hopper architecture. -/
def tierAccessCost (t : MemTier) : Nat :=
  match t with
  | .Register  => 0    -- free (same thread, same register)
  | .SharedMem => 5    -- ~5ns per 128B cacheline
  | .HBM       => 200  -- ~200ns per 128B cacheline

/-- Cost of staging an intermediate through a given memory tier.
    Two accesses: one write (producer) + one read (consumer).
    C++: `intermediate_cost = 2 * bytes * tier_latency / cacheline_size`. -/
def stagingCost (tier : MemTier) (bytes : Nat) : Nat :=
  2 * bytes * tierAccessCost tier

/-- Register staging is free: zero cost for intermediates.
    C++: fused kernels with register intermediates have no staging overhead. -/
theorem register_staging_free (bytes : Nat) :
    stagingCost .Register bytes = 0 := by
  simp [stagingCost, tierAccessCost]

/-- Shared memory staging is cheaper than HBM.
    THE shared memory tiling theorem — always better than no fusion.
    C++: "shared memory fusion < no fusion" in Augur's recommendation. -/
theorem shared_mem_cheaper_than_hbm (bytes : Nat) :
    stagingCost .SharedMem bytes ≤ stagingCost .HBM bytes := by
  simp [stagingCost, tierAccessCost]; omega

/-- Register staging is cheaper than shared memory.
    THE register fusion superiority — registers beat everything.
    C++: "register > shared memory > no fusion" in optimization ranking. -/
theorem register_cheaper_than_shared (bytes : Nat) :
    stagingCost .Register bytes ≤ stagingCost .SharedMem bytes := by
  simp [stagingCost, tierAccessCost]

/-- Full ordering: register ≤ shared memory ≤ HBM.
    C++: Augur's optimization preference order for intermediate placement. -/
theorem tier_ordering (bytes : Nat) :
    stagingCost .Register bytes ≤ stagingCost .SharedMem bytes ∧
    stagingCost .SharedMem bytes ≤ stagingCost .HBM bytes := by
  exact ⟨register_cheaper_than_shared bytes, shared_mem_cheaper_than_hbm bytes⟩

/-- Staging cost is monotone in bytes: larger intermediates cost more.
    C++: Augur prioritizes fusing ops with large intermediates. -/
theorem staging_cost_monotone (tier : MemTier) (b₁ b₂ : Nat) (h : b₁ ≤ b₂) :
    stagingCost tier b₁ ≤ stagingCost tier b₂ := by
  simp only [stagingCost]
  exact Nat.mul_le_mul_right _ (Nat.mul_le_mul_left _ h)

/-- Staging cost with positive bytes is strictly ordered across tiers.
    C++: for any non-zero intermediate, the tier hierarchy is strict. -/
theorem shared_strictly_cheaper_than_hbm (bytes : Nat) (h : 0 < bytes) :
    stagingCost .SharedMem bytes < stagingCost .HBM bytes := by
  simp [stagingCost, tierAccessCost]; omega

/-- Shared memory capacity constraint.
    C++: SM L1d = 48KB on Hopper, 128KB on Blackwell. -/
def fitsInSharedMem (bytes shared_mem_size : Nat) : Prop :=
  bytes ≤ shared_mem_size

instance (bytes sm : Nat) : Decidable (fitsInSharedMem bytes sm) :=
  inferInstanceAs (Decidable (_ ≤ _))

/-- Zero bytes always fit in shared memory.
    C++: elementwise ops have zero intermediate → always fit. -/
theorem zero_fits_shared (sm : Nat) : fitsInSharedMem 0 sm := Nat.zero_le _

/-- If intermediate fits in shared memory, we can use shared memory staging.
    If it also fits in registers, we use register staging.
    C++: `BackgroundThread::select_staging_tier()`. -/
def selectTier (bytes max_regs shared_mem_size : Nat) : MemTier :=
  if bytes ≤ max_regs then .Register
  else if bytes ≤ shared_mem_size then .SharedMem
  else .HBM

/-- Tier selection always returns a valid tier.
    C++: `select_staging_tier()` is total — always picks one of three tiers. -/
theorem select_tier_total (bytes max_regs sm : Nat) :
    ∃ t : MemTier, selectTier bytes max_regs sm = t :=
  ⟨selectTier bytes max_regs sm, rfl⟩

/-- When bytes fit in registers, register tier is selected (cheapest).
    C++: register fusion is always preferred when possible. -/
theorem select_tier_register (bytes max_regs sm : Nat) (h : bytes ≤ max_regs) :
    selectTier bytes max_regs sm = .Register := by
  simp [selectTier, h]

/-- When bytes don't fit in registers but fit in shared memory.
    C++: fall back to shared memory tiling. -/
theorem select_tier_shared (bytes max_regs sm : Nat)
    (hr : max_regs < bytes) (hs : bytes ≤ sm) :
    selectTier bytes max_regs sm = .SharedMem := by
  simp [selectTier, show ¬(bytes ≤ max_regs) from by omega, hs]

/-- When bytes exceed shared memory, fall back to HBM (no fusion benefit).
    C++: intermediate too large for on-chip memory → materialize to HBM. -/
theorem select_tier_hbm (bytes max_regs sm : Nat)
    (hr : max_regs < bytes) (hs : sm < bytes) :
    selectTier bytes max_regs sm = .HBM := by
  simp [selectTier, show ¬(bytes ≤ max_regs) from by omega,
    show ¬(bytes ≤ sm) from by omega]

/-! ## 9. Fusion Benefit — Combined Cost Analysis

Putting it all together: given a fusion candidate, compute the total
benefit considering legality, memory tier, and cost savings.

C++: `Augur::predict_fusion_benefit()` — ranked recommendation for
which ops to fuse, considering register pressure and shared memory capacity. -/

/-- Total cost of two ops with explicit memory tier for intermediate.
    C++: `predict_cost(op1, op2, tier)` in Augur's digital twin. -/
def totalCostWithTier (cost₁ cost₂ intermediate_bytes : Nat)
    (tier : MemTier) : Nat :=
  cost₁ + cost₂ + stagingCost tier intermediate_bytes

/-- Register fusion eliminates all staging overhead.
    C++: fused kernel time ≈ sum of op compute times. -/
theorem register_fusion_cost (c₁ c₂ ib : Nat) :
    totalCostWithTier c₁ c₂ ib .Register = c₁ + c₂ := by
  simp [totalCostWithTier, stagingCost, tierAccessCost]

/-- Shared memory fusion is strictly better than HBM (when bytes > 0).
    C++: tiled kernel always beats unfused for non-zero intermediates. -/
theorem shared_better_than_hbm (c₁ c₂ ib : Nat) :
    totalCostWithTier c₁ c₂ ib .SharedMem ≤
    totalCostWithTier c₁ c₂ ib .HBM := by
  simp only [totalCostWithTier]
  exact Nat.add_le_add_left (shared_mem_cheaper_than_hbm ib) _

/-- Register fusion is best overall: minimizes total cost.
    C++: Augur always prefers register fusion when feasible. -/
theorem register_best (c₁ c₂ ib : Nat) :
    totalCostWithTier c₁ c₂ ib .Register ≤
    totalCostWithTier c₁ c₂ ib .SharedMem := by
  simp only [totalCostWithTier]
  exact Nat.add_le_add_left (register_cheaper_than_shared ib) _

/-- Full cost ordering: register ≤ shared memory ≤ HBM.
    C++: `Augur::rank_fusion_strategies()` returns this ordering. -/
theorem cost_ordering (c₁ c₂ ib : Nat) :
    totalCostWithTier c₁ c₂ ib .Register ≤
    totalCostWithTier c₁ c₂ ib .SharedMem ∧
    totalCostWithTier c₁ c₂ ib .SharedMem ≤
    totalCostWithTier c₁ c₂ ib .HBM :=
  ⟨register_best c₁ c₂ ib, shared_better_than_hbm c₁ c₂ ib⟩

/-! ## 10. Launch Overhead Model

Fusing N kernels into one eliminates N-1 kernel launches.
Each launch costs ~5μs on modern GPUs (driver overhead + grid setup).
For 1000-op models with heavy elementwise chains, this dominates.

C++ (L1): "Stream parallelism: DFG reveals independent ops → launch on
different CUDA streams. Zero scheduling overhead at runtime." -/

/-- Kernel launches saved by fusing num_fused kernels into one.
    C++: each fused region saves (region_size - 1) launches. -/
def launchOverheadSaved (num_fused : Nat) (launch_cost : Nat) : Nat :=
  if num_fused = 0 then 0
  else (num_fused - 1) * launch_cost

/-- Single kernel: no launches saved (nothing to fuse with).
    C++: a RegionNode with one op has no fusion benefit. -/
theorem launch_overhead_single (lc : Nat) :
    launchOverheadSaved 1 lc = 0 := by
  simp [launchOverheadSaved]

/-- Fusing ≥ 2 kernels saves positive launch overhead when launch_cost > 0.
    C++: every multi-op fusion saves at least one launch. -/
theorem launch_overhead_positive (n lc : Nat) (hn : 2 ≤ n) (hlc : 0 < lc) :
    0 < launchOverheadSaved n lc := by
  simp only [launchOverheadSaved, show n ≠ 0 from by omega, ↓reduceIte]
  exact Nat.mul_pos (by omega) hlc

/-- More fused kernels → more launch overhead saved (monotone).
    C++: larger RegionNodes save proportionally more launch time. -/
theorem launch_overhead_monotone (n₁ n₂ lc : Nat) (h : n₁ ≤ n₂)
    (hn₁ : 0 < n₁) :
    launchOverheadSaved n₁ lc ≤ launchOverheadSaved n₂ lc := by
  simp only [launchOverheadSaved, show n₁ ≠ 0 from by omega,
    show n₂ ≠ 0 from by omega, ↓reduceIte]
  exact Nat.mul_le_mul_right lc (by omega)

/-! ## 11. Concrete Examples

Verify formalization against realistic fusion scenarios. -/

/-- Two-op elementwise fusion: ReLU after MatMul.
    MatMul: 1000 cycles, 4KB output. ReLU: 10 cycles, 4KB output.
    Unfused: 1010 + 8192 = 9202 cycles. Fused: 1010 cycles. -/
example : fusedCost 1000 10 = 1010 := rfl
example : unfusedCost 1000 10 8192 = 9202 := by native_decide

/-- Chain of 3 elementwise ops: ADD → MUL → RELU.
    All intermediates are zero (elementwise). -/
private def add_op : ElementwiseOp := ⟨0, 5⟩
private def mul_op : ElementwiseOp := ⟨1, 5⟩
private def relu_op : ElementwiseOp := ⟨2, 3⟩

example : chainIntermediateBytes
    [add_op.toDFGNode, mul_op.toDFGNode, relu_op.toDFGNode] = 0 := by
  simp [ElementwiseOp.toDFGNode, chainIntermediateBytes]

/-- Register vs shared memory vs HBM for 1KB intermediate.
    Register: 0. Shared: 10240. HBM: 409600. -/
example : stagingCost .Register 1024 = 0 := by native_decide
example : stagingCost .SharedMem 1024 = 10240 := by native_decide
example : stagingCost .HBM 1024 = 409600 := by native_decide

/-- Tier selection with 256 registers, 49152B shared memory (48KB).
    128B intermediate → register. 512B → shared. 49152B → shared. 65536B → HBM. -/
example : selectTier 128 256 49152 = .Register := by native_decide
example : selectTier 512 256 49152 = .SharedMem := by native_decide
example : selectTier 49152 256 49152 = .SharedMem := by native_decide
example : selectTier 65536 256 49152 = .HBM := by native_decide

/-- Simple graph fusion legality check. -/
private def simple_graph : DFG :=
  { nodes := [⟨0, 100, 1024⟩, ⟨1, 50, 512⟩, ⟨2, 30, 256⟩],
    edges := [(0, 1), (1, 2)] }

example : isLegalFusion simple_graph (0, 1) := by
  simp [isLegalFusion, DFG.hasEdge, DFG.producerCount, DFG.consumerCount]
  native_decide

example : isLegalFusion simple_graph (1, 2) := by
  simp [isLegalFusion, DFG.hasEdge, DFG.producerCount, DFG.consumerCount]
  native_decide

/-- Graph with fan-out: node 0 feeds both node 1 and node 2.
    Fusion (0,1) is illegal because node 0 has two consumers. -/
private def fanout_graph : DFG :=
  { nodes := [⟨0, 100, 1024⟩, ⟨1, 50, 512⟩, ⟨2, 30, 256⟩],
    edges := [(0, 1), (0, 2)] }

example : ¬isLegalFusion fanout_graph (0, 1) := by
  simp [isLegalFusion, DFG.hasEdge, DFG.producerCount, DFG.consumerCount]
  native_decide

/-- Launch overhead: fusing 3 kernels at 5μs/launch → save 10μs. -/
example : launchOverheadSaved 3 5 = 10 := by native_decide

/-- Two-op chain intermediate bytes: first node's output. -/
example : chainIntermediateBytes [⟨0, 100, 4096⟩, ⟨1, 50, 2048⟩] = 4096 := by
  native_decide

/-- Three-op chain: first two nodes contribute intermediates. -/
example : chainIntermediateBytes [⟨0, 100, 4096⟩, ⟨1, 50, 2048⟩, ⟨2, 30, 1024⟩] = 6144 := by
  native_decide

/-- HBM saved for three-op chain: 2 × 6144 = 12288. -/
example : chainHbmSaved [⟨0, 100, 4096⟩, ⟨1, 50, 2048⟩, ⟨2, 30, 1024⟩] = 12288 := by
  native_decide

/-! ## Summary

Key results:
- `fused_le_unfused`: fused cost ≤ unfused cost (fusion never hurts)
- `fusion_strictly_beneficial`: strict improvement with positive overhead
- `fusion_savings_eq`: savings = memory overhead term
- `illegal_when_multiple_consumers`: fan-out blocks fusion
- `illegal_when_fan_in`: fan-in blocks fusion
- `chain_two_savings`: two-op chain saves 2 × first output
- `chain_nontrivial_savings`: k ≥ 2 ops → positive savings
- `chain_extend_intermediates`: prepending a node increases intermediates
- `register_pressure_monotone`: longer chains → more register pressure
- `exceeds_registers`: chain too long → must spill
- `elementwise_zero_intermediate`: elementwise fusion has zero HBM intermediate
- `shared_mem_cheaper_than_hbm`: shared memory tiling beats no fusion
- `register_cheaper_than_shared`: register fusion beats shared memory
- `tier_ordering`: register ≤ shared_mem ≤ HBM (full cost hierarchy)
- `cost_ordering`: combined cost respects tier ordering
- `register_fusion_cost`: register fusion cost = pure compute
- `launch_overhead_positive`: fusing ≥ 2 kernels saves launch overhead
- `launch_overhead_monotone`: more fused kernels → more savings
-/

end Crucible

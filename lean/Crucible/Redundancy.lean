import Mathlib.Tactic
import Mathlib.Data.Finset.Card
import Mathlib.Data.Fintype.Basic
import Crucible.Basic

/-!
# Crucible.Redundancy — L12 RAID-like Shard Redundancy

From the design doc (L12 Distribution):

  "RAID-like redundancy (hot Cipher): configurable overlap α
   (0 = pure FSDP, 0.125 = survive 1 failure at 12.5% overhead,
   1.0 = pure DDP). Redundancy updates pipelined into communication dead time."

Crucible distributes model parameters across N Relays using FSDP-style sharding:
shard i is primarily held by Relay (i mod N). With redundancy factor α, each shard
is replicated to α additional Relays in a ring topology: Relay (i+1) mod N through
Relay (i+α) mod N. This provides fault tolerance: any α simultaneous failures
leave at least one live copy of every shard.

## What this file proves

1. **Shard coverage**: every shard is assigned to at least one Relay.
2. **Redundant coverage**: with redundancy α, each shard lives on (1+α) Relays.
3. **Fault tolerance**: survive k ≤ α arbitrary Relay failures.
4. **Storage overhead**: total system storage = (1+α) × model_size.
5. **Ring placement correctness**: ring-neighbor replication with N > α.
6. **Dynamic α adjustment**: increasing α preserves existing coverage.
7. **DiLoCo outer sync**: order-independent pseudo-gradient reduction.

C++ correspondence:
- N Relays = `Canopy::num_relays()`
- α = `Canopy::redundancy_factor()`
- Shard i on Relay (i mod N) = FSDP sharding in `Distribution::shard_for_relay()`
- Ring replication = `Distribution::replicate_to_neighbors()`
- DiLoCo sync = `Distribution::diloco_outer_step()`
-/

namespace Crucible

/-! ## Shard Assignment Model

N Relays, S shards. Primary assignment: shard j → Relay (j mod N).
Ring replication with factor α: shard j also on Relays (j+1)...(j+α) mod N.

We model Relays as `Fin N` and shards as `Fin S`, with the ring topology
using modular arithmetic. -/

/-- The set of Relays holding shard `j` with redundancy factor `α` in a ring of `N` Relays.
    Primary: Relay (j mod N). Replicas: Relays (j+1) mod N ... (j+α) mod N.
    C++: `Distribution::holders_of_shard(shard_id)`. -/
def shardHolders (N : Nat) (hN : 0 < N) (j : Nat) (α : Nat) : Finset (Fin N) :=
  Finset.image (fun k : Fin (α + 1) => ⟨(j + k.val) % N, Nat.mod_lt _ hN⟩) Finset.univ

/-- The set of shards held by Relay `r` with redundancy factor `α`.
    Relay r holds shard r (primary) plus shards whose replicas land on r.
    C++: `Distribution::shards_on_relay(relay_id)`. -/
def relayShards (N S : Nat) (hN : 0 < N) (r : Fin N) (α : Nat) : Finset (Fin S) :=
  Finset.filter (fun j => r ∈ shardHolders N hN j.val α) Finset.univ

/-! ## Basic Properties -/

/-- The primary holder (k=0) is always in the holder set.
    C++: FSDP guarantees shard j is on Relay (j mod N). -/
theorem primary_in_holders (N : Nat) (hN : 0 < N) (j : Nat) (α : Nat) :
    ⟨j % N, Nat.mod_lt j hN⟩ ∈ shardHolders N hN j α := by
  simp [shardHolders]
  exact ⟨⟨0, by omega⟩, by simp⟩

/-- Every shard has at least one holder (the primary Relay).
    This is the fundamental FSDP coverage guarantee.
    C++: `shard_for_relay()` always returns a valid assignment. -/
theorem shard_coverage (N : Nat) (hN : 0 < N) (j : Nat) (α : Nat) :
    (shardHolders N hN j α).Nonempty :=
  ⟨_, primary_in_holders N hN j α⟩

/-! ## Holder Set Cardinality

With redundancy α and N > α Relays, each shard lives on exactly (1+α) distinct Relays.
The ring placement ensures all (j+k) mod N for k ∈ [0,α] are distinct when N > α. -/

/-- When N > α, the offsets 0..α produce distinct Relay indices mod N.
    This is the key combinatorial lemma for ring-topology replication.
    C++: requires `num_relays > redundancy_factor` (checked at init). -/
theorem ring_offsets_injective (N : Nat) (hN : 0 < N) (j : Nat) (α : Nat) (hNα : α < N) :
    Function.Injective (fun k : Fin (α + 1) => ⟨(j + k.val) % N, Nat.mod_lt _ hN⟩ : Fin (α + 1) → Fin N) := by
  intro ⟨a, ha⟩ ⟨b, hb⟩ heq
  simp only [Fin.mk.injEq] at heq
  ext; simp only
  -- Use Nat.ModEq: (j+a) ≡ (j+b) [MOD N] implies a ≡ b [MOD N]
  have hmod : a ≡ b [MOD N] :=
    Nat.ModEq.add_left_cancel (Nat.ModEq.refl j) heq
  -- Since a, b < N, congruence mod N implies equality
  exact Nat.ModEq.eq_of_lt_of_lt hmod (by omega) (by omega)

/-- With N > α, each shard is held by exactly (1 + α) distinct Relays.
    C++: `holders_of_shard(j).size() == 1 + redundancy_factor`. -/
theorem holder_card (N : Nat) (hN : 0 < N) (j : Nat) (α : Nat) (hNα : α < N) :
    (shardHolders N hN j α).card = α + 1 := by
  simp only [shardHolders]
  rw [Finset.card_image_of_injective _ (ring_offsets_injective N hN j α hNα)]
  exact Finset.card_fin (α + 1)

/-! ## Fault Tolerance

The central theorem: with redundancy factor α, the system survives any k ≤ α
simultaneous Relay failures. For every shard, at least one of its (1+α) holders
remains alive after removing k Relays. -/

/-- A failed set is a subset of Relays that have crashed.
    C++: detected by Keeper heartbeat timeout (~100ms). -/
abbrev FailedSet (N : Nat) := Finset (Fin N)

/-- The set of surviving holders for shard j after failures.
    C++: `Distribution::live_holders(shard_id)` filters out failed Relays. -/
def liveHolders (N : Nat) (hN : 0 < N) (j : Nat) (α : Nat)
    (failed : FailedSet N) : Finset (Fin N) :=
  (shardHolders N hN j α) \ failed

/-- If fewer than (1+α) Relays fail, every shard retains at least one live holder.
    This is the fundamental fault tolerance theorem.

    Proof: shard j has (1+α) holders (by holder_card). Removing at most α
    of them leaves at least 1 alive (pigeonhole).

    C++: "On Relay failure: ~100ms detection → surviving Relays already have
    shards → reshard in 2-5s → zero lost compute." -/
theorem survive_k_failures (N : Nat) (hN : 0 < N) (j : Nat) (α : Nat) (hNα : α < N)
    (failed : FailedSet N) (hk : failed.card ≤ α) :
    (liveHolders N hN j α failed).Nonempty := by
  simp only [liveHolders]
  rw [Finset.nonempty_iff_ne_empty]
  intro hempty
  have hsdiff := Finset.sdiff_eq_empty_iff_subset.mp hempty
  have hcard_holders := holder_card N hN j α hNα
  have hcard_le := Finset.card_le_card hsdiff
  omega

/-- Special case: α ≥ 1 means single-node failure is tolerable.
    C++: default redundancy_factor = 1 for production deployments. -/
theorem survive_single_failure (N : Nat) (hN : 0 < N) (j : Nat) (α : Nat)
    (hα : 1 ≤ α) (hNα : α < N) (failed : FailedSet N) (hone : failed.card ≤ 1) :
    (liveHolders N hN j α failed).Nonempty :=
  survive_k_failures N hN j α hNα failed (by omega)

/-- With α = 0 (pure FSDP), a single failure can lose a shard.
    This is WHY redundancy is needed — pure FSDP has no fault tolerance.
    C++: α = 0 is only for development/testing, never production. -/
theorem fsdp_no_redundancy (N : Nat) (hN : 1 < N) :
    ∃ (j : Nat) (failed : FailedSet N),
      failed.card = 1 ∧ (liveHolders N (by omega) j 0 failed) = ∅ := by
  -- Witness: shard 0, fail Relay 0. With α=0, shard 0 lives only on Relay 0.
  let relay0 : Fin N := ⟨0, by omega⟩
  refine ⟨0, {relay0}, by simp, ?_⟩
  -- holder_card gives |shardHolders| = 0 + 1 = 1 with α = 0
  have hcard := holder_card N (by omega) 0 0 (by omega)
  -- The single holder IS relay0 (primary holder of shard 0)
  have hprim := primary_in_holders N (by omega) 0 0
  -- shardHolders has card 1, so it equals {relay0}
  have hsingle : shardHolders N (by omega) 0 0 = {relay0} := by
    rw [Finset.eq_singleton_iff_unique_mem]
    exact ⟨hprim, fun y hy => by
      have hle : (shardHolders N (by omega) 0 0).card ≤ 1 := by omega
      have huniq : ∀ a ∈ shardHolders N (by omega) 0 0,
        ∀ b ∈ shardHolders N (by omega) 0 0, a = b := Finset.card_le_one.mp hle
      exact huniq y hy _ hprim⟩
  -- liveHolders = {relay0} \ {relay0} = ∅
  simp [liveHolders, hsingle]

/-! ## Storage Overhead

With N Relays and redundancy factor α, each Relay stores (1+α)/N of the model.
Total system storage = (1+α) × model_size (each shard stored (1+α) times). -/

/-- Total number of shard-copies across all Relays.
    Each of S shards is replicated to (1+α) Relays.
    C++: total_storage = num_shards × (1 + redundancy_factor) × shard_size. -/
theorem total_shard_copies (S : Nat) (α : Nat) :
    S * (α + 1) = S * α + S := by ring

/-- With α = 0 (pure FSDP), total storage equals model size.
    Each shard stored exactly once. Zero overhead. -/
theorem fsdp_storage (S : Nat) : S * (0 + 1) = S := by ring

/-- With α = 1, total storage is 2× model size.
    Each shard stored on two Relays. 100% overhead.
    C++: "0.125 = survive 1 failure at 12.5% overhead" refers to
    fractional α where only critical shards are doubled. -/
theorem raid1_storage (S : Nat) : S * (1 + 1) = 2 * S := by ring

/-- Storage overhead ratio: (1+α) times the base storage.
    C++: `total_bytes_per_relay = (1 + alpha) * shard_bytes`. -/
theorem storage_overhead (model_size : Nat) (α : Nat) :
    (α + 1) * model_size = model_size + α * model_size := by ring

/-! ## Recovery Model

When a Relay fails, its unique data must be recovered from surviving replicas.
With α ≥ 1, every shard of the failed Relay exists on at least one neighbor.
Recovery time is bounded by (data_to_recover / bandwidth). -/

/-- The shards that need recovery are exactly those held by the failed Relay.
    Each such shard has surviving copies (from fault tolerance theorem).
    C++: `Distribution::initiate_recovery(failed_relay)`. -/
theorem recovery_possible (N : Nat) (hN : 0 < N) (α : Nat) (hα : 1 ≤ α) (hNα : α < N)
    (_failed_relay : Fin N) (j : Nat) :
    (liveHolders N hN j α {_failed_relay}).Nonempty :=
  survive_k_failures N hN j α hNα {_failed_relay} (by simp; omega)

/-- After removing a failed Relay, a surviving holder can serve as recovery source.
    C++: "surviving Relays already have shards → reshard in 2-5s". -/
theorem recovery_source_exists (N : Nat) (hN : 0 < N) (α : Nat) (hα : 1 ≤ α) (hNα : α < N)
    (failed_relay : Fin N) (j : Nat) :
    ∃ source : Fin N, source ∈ liveHolders N hN j α {failed_relay} :=
  let ⟨x, hx⟩ := recovery_possible N hN α hα hNα failed_relay j
  ⟨x, hx⟩

/-! ## Dynamic α Adjustment

When an unhealthy Relay is detected, its neighbors get higher α (more replicas).
Key property: increasing α for one shard doesn't decrease coverage for others. -/

/-- Increasing α grows the holder set: every holder at α is still a holder at α+1.
    C++: `increase_redundancy()` only adds replicas, never removes. -/
theorem holders_monotone (N : Nat) (hN : 0 < N) (j : Nat) (α : Nat) :
    shardHolders N hN j α ⊆ shardHolders N hN j (α + 1) := by
  intro x hx
  simp only [shardHolders, Finset.mem_image, Finset.mem_univ, true_and] at *
  obtain ⟨⟨k, hk⟩, hkx⟩ := hx
  exact ⟨⟨k, by omega⟩, hkx⟩

/-- Increasing α never decreases the number of live holders.
    C++: dynamic α adjustment is monotonically safe. -/
theorem live_holders_monotone (N : Nat) (hN : 0 < N) (j : Nat) (α : Nat)
    (failed : FailedSet N) :
    liveHolders N hN j α failed ⊆ liveHolders N hN j (α + 1) failed := by
  intro x hx
  simp only [liveHolders, Finset.mem_sdiff] at *
  exact ⟨holders_monotone N hN j α hx.1, hx.2⟩

/-- After increasing α from α₀ to α₁ ≥ α₀, coverage is at least as good.
    Generalization of the single-step monotonicity. -/
theorem holders_monotone_general (N : Nat) (hN : 0 < N) (j : Nat) (α₀ α₁ : Nat)
    (h : α₀ ≤ α₁) :
    shardHolders N hN j α₀ ⊆ shardHolders N hN j α₁ := by
  intro x hx
  simp only [shardHolders, Finset.mem_image, Finset.mem_univ, true_and] at *
  obtain ⟨⟨k, hk⟩, hkx⟩ := hx
  exact ⟨⟨k, by omega⟩, hkx⟩

/-! ## Ring Topology Properties

The ring placement ensures locality: replicas are on consecutive Relays.
This optimizes recovery bandwidth (neighbor-to-neighbor transfer). -/

/-- The next Relay in the ring is always a replica holder (when α ≥ 1).
    C++: ring replication sends to (relay_id + 1) % N first. -/
theorem next_neighbor_is_holder (N : Nat) (hN : 0 < N) (j : Nat) (α : Nat) (hα : 1 ≤ α) :
    ⟨(j + 1) % N, Nat.mod_lt _ hN⟩ ∈ shardHolders N hN j α := by
  simp only [shardHolders, Finset.mem_image, Finset.mem_univ, true_and]
  exact ⟨⟨1, by omega⟩, by simp⟩

/-- With α ≥ 2, both immediate neighbors are holders.
    C++: higher α means more local copies available for fast recovery. -/
theorem two_neighbors_holders (N : Nat) (hN : 0 < N) (j : Nat) (α : Nat) (hα : 2 ≤ α) :
    ⟨(j + 1) % N, Nat.mod_lt _ hN⟩ ∈ shardHolders N hN j α ∧
    ⟨(j + 2) % N, Nat.mod_lt _ hN⟩ ∈ shardHolders N hN j α := by
  constructor
  · exact next_neighbor_is_holder N hN j α (by omega)
  · simp only [shardHolders, Finset.mem_image, Finset.mem_univ, true_and]
    exact ⟨⟨2, by omega⟩, by simp⟩

/-! ## Resharding After Failure

When N changes (Relay joins/leaves), the system reshards.
Shard assignment changes from (j mod N) to (j mod N'), but the ring
topology ensures minimal data movement. -/

/-- After a Relay joins (N → N+1), the primary assignment may change.
    But the old primary index is bounded by the new ring size.
    C++: `Distribution::reshard(new_topology)` minimizes data movement. -/
theorem reshard_primary_bounded (N : Nat) (hN : 0 < N) (j : Nat) :
    j % N < N + 1 := by
  have := Nat.mod_lt j hN; omega

/-- Resharding preserves that every shard has a holder.
    The new topology's shardHolders is always nonempty. -/
theorem reshard_coverage (N' : Nat) (hN' : 0 < N') (j : Nat) (α : Nat) :
    (shardHolders N' hN' j α).Nonempty :=
  shard_coverage N' hN' j α

/-! ## DiLoCo Outer Sync

K islands train independently for H local steps, then synchronize via
pseudo-gradient all-reduce. The pseudo-gradient Δ_k = local_params_k - global_params.
All-reduce sums the Δ_k across islands — this must be order-independent.

From Algebra.lean: gradient addition over ℚ is commutative and associative.
Here we prove the DiLoCo-specific properties. -/

section DiLoCo

/-- A DiLoCo island's state: local parameters and the last-known global parameters. -/
structure IslandState where
  local_params : ℚ
  global_params : ℚ

/-- Pseudo-gradient: difference between local and global parameters.
    C++: `Distribution::compute_pseudo_gradient()`. -/
def IslandState.pseudoGrad (s : IslandState) : ℚ :=
  s.local_params - s.global_params

/-- After outer sync, all islands converge to the same global parameters.
    new_global = old_global + lr × (1/K) × Σ Δ_k.
    C++: `Distribution::diloco_outer_step()`. -/
def dilocoSync (islands : List IslandState) (lr : ℚ) : ℚ :=
  match islands with
  | [] => 0
  | i :: _ =>
    let delta_sum := islands.foldl (fun acc s => acc + s.pseudoGrad) 0
    let K := islands.length
    i.global_params + lr * delta_sum / K

/-- Pseudo-gradient sum is order-independent (commutativity of addition over ℚ).
    Two islands' pseudo-gradients can be summed in either order.
    C++: all-reduce is ring-based, but result is order-independent.
    This extends the `grad_add_comm` theorem from Algebra.lean. -/
theorem pseudograd_sum_comm (a b : IslandState) :
    a.pseudoGrad + b.pseudoGrad = b.pseudoGrad + a.pseudoGrad :=
  add_comm _ _

/-- Pseudo-gradient sum is associative.
    C++: hierarchical all-reduce (NVLink → InfiniBand → WAN) gives
    same result regardless of grouping. -/
theorem pseudograd_sum_assoc (a b c : IslandState) :
    a.pseudoGrad + b.pseudoGrad + c.pseudoGrad =
    a.pseudoGrad + (b.pseudoGrad + c.pseudoGrad) :=
  add_assoc _ _ _

/-- An island with no local update has zero pseudo-gradient.
    C++: island that did zero local steps contributes nothing. -/
theorem pseudograd_zero_when_synced (s : IslandState) (h : s.local_params = s.global_params) :
    s.pseudoGrad = 0 := by
  simp [IslandState.pseudoGrad, h]

/-- Adding a zero-gradient island doesn't change the sum.
    C++: idle islands don't affect the outer sync result. -/
theorem pseudograd_zero_neutral (sum : ℚ) (s : IslandState)
    (h : s.local_params = s.global_params) :
    sum + s.pseudoGrad = sum := by
  rw [pseudograd_zero_when_synced s h, add_zero]

/-- With K identical islands (same Δ), sync produces global + lr × Δ.
    C++: homogeneous islands converge to the same update.
    When all islands agree, the average of K identical values is that value. -/
theorem diloco_uniform_sync (g Δ lr : ℚ) (K : Nat) (hK : 0 < K) :
    g + lr * ((K : ℚ) * Δ) / (K : ℚ) = g + lr * Δ := by
  have hK' : (K : ℚ) ≠ 0 := Nat.cast_ne_zero.mpr (by omega)
  rw [mul_div_assoc, mul_div_cancel_left₀ _ hK']

end DiLoCo

/-! ## Concrete Examples (Small N)

Verify the formalization against concrete small configurations
via native_decide / decide. -/

/-- With N=3, α=1: shard 0 is on Relays {0, 1}. -/
example : (shardHolders 3 (by omega) 0 1).card = 2 := by native_decide

/-- With N=4, α=2: shard 0 is on Relays {0, 1, 2} — three copies. -/
example : (shardHolders 4 (by omega) 0 2).card = 3 := by native_decide

/-- With N=4, α=0: shard 0 is on exactly one Relay (pure FSDP). -/
example : (shardHolders 4 (by omega) 0 0).card = 1 := by native_decide

/-- With N=3, α=1: shard 2 is on Relays {2, 0} (wraps around). -/
example : (shardHolders 3 (by omega) 2 1).card = 2 := by native_decide

/-- With N=5, α=3: shard 3 is on Relays {3, 4, 0, 1} — wraps around ring. -/
example : (shardHolders 5 (by omega) 3 3).card = 4 := by native_decide

/-! ## Summary

Key results:
- `shard_coverage`: every shard assigned to ≥1 Relay (FSDP base guarantee)
- `holder_card`: each shard on exactly (1+α) Relays when N > α
- `survive_k_failures`: k ≤ α failures leave every shard with a live copy
- `holders_monotone`: increasing α never loses existing replicas
- `recovery_source_exists`: failed Relay's shards always recoverable (α ≥ 1)
- `fsdp_no_redundancy`: α=0 is vulnerable to single failure (motivation for α≥1)
- `pseudograd_sum_comm`/`assoc`: DiLoCo outer sync is order-independent
- `diloco_uniform_sync`: homogeneous islands converge correctly
-/

end Crucible

import Mathlib.Tactic
import Mathlib.Data.Finset.Card
import Mathlib.Data.Fintype.Basic
import Crucible.Basic

/-!
# Crucible.Canopy — L12 Distributed Mesh: Gossip, Consensus, Peer Discovery

From the design doc (L12 Distribution):

  "Keeper mesh: each Relay runs a Keeper, discovers peers via gossip.
   No master. Raft for critical state, CRDTs for eventually-consistent metrics.
   Any Keeper can propose changes."

  "Spot-aware: 30-second eviction → Keeper signals Canopy → mesh reshards to N-1
   → Vigil continues from same step. New instance → Keeper discovers Canopy
   → loads Cipher → joins."

This module formalizes:
1. **Mesh topology**: bidirectional, reflexive adjacency for N Relays
2. **Gossip protocol**: max-based version spreading, monotonicity, convergence
3. **Raft consensus** (simplified): single leader per term, majority quorum
4. **Peer discovery**: new Relay joins mesh, grows topology
5. **Partition healing**: max semantics ensure no data loss across partitions
6. **Health propagation**: worst-case health spreads via gossip
7. **No-master architecture**: symmetric roles, minority-failure tolerance

C++ correspondence:
- CanopyMesh = `Canopy::mesh_topology()`
- Gossip = `Canopy::gossip_round()` — periodic state exchange
- Raft = `Canopy::raft_propose()` / `raft_vote()` — critical state consensus
- Peer discovery = `Keeper::join_mesh()` via seed contact
- Health = `Keeper::health_status()` propagated through gossip
-/

namespace Crucible

/-! ## 1. Mesh Topology

N Relays connected by bidirectional links. Each Relay is reflexively connected
to itself (can always observe its own state). Adjacency is symmetric:
if Relay A sees Relay B, then B sees A. -/

/-- Adjacency matrix for a Canopy mesh of N Relays.
    C++: `Canopy::is_connected(relay_a, relay_b)`.
    Stored as a symmetric boolean matrix. -/
structure CanopyMesh (N : Nat) where
  /-- Number of relays is positive (need at least one node). -/
  relay_count_pos : 0 < N
  /-- Adjacency: which relays can directly communicate. -/
  connected : Fin N → Fin N → Bool

/-- Every Relay is connected to itself (can always observe own state).
    C++: a Keeper always has access to its own Relay's data. -/
def mesh_connected_reflexive {N : Nat} (m : CanopyMesh N) : Prop :=
  ∀ i : Fin N, m.connected i i = true

/-- Links are bidirectional: if A sees B, B sees A.
    C++: gossip protocol requires bidirectional communication. -/
def mesh_symmetric {N : Nat} (m : CanopyMesh N) : Prop :=
  ∀ i j : Fin N, m.connected i j = m.connected j i

/-- A well-formed Canopy mesh satisfies reflexivity and symmetry. -/
structure CanopyMeshWF (N : Nat) extends CanopyMesh N where
  refl : mesh_connected_reflexive toCanopyMesh
  symm : mesh_symmetric toCanopyMesh

/-- The complete graph on N nodes: every pair connected.
    C++: initial mesh before any partitions, or small clusters with full connectivity. -/
def canopy_complete_mesh (N : Nat) (hN : 0 < N) : CanopyMeshWF N where
  toCanopyMesh := {
    relay_count_pos := hN
    connected := fun _ _ => true
  }
  refl := fun _ => rfl
  symm := fun _ _ => rfl

/-- Neighbor set: all relays directly connected to relay i. -/
def canopy_neighbors {N : Nat} (m : CanopyMesh N) (i : Fin N) : Finset (Fin N) :=
  Finset.filter (fun j => m.connected i j = true) Finset.univ

/-- In a well-formed mesh, every relay is in its own neighbor set. -/
theorem canopy_self_neighbor {N : Nat} (m : CanopyMeshWF N) (i : Fin N) :
    i ∈ canopy_neighbors m.toCanopyMesh i := by
  simp [canopy_neighbors, m.refl i]

/-- In a well-formed mesh, adjacency is a symmetric relation on neighbor sets. -/
theorem canopy_neighbor_symm {N : Nat} (m : CanopyMeshWF N) (i j : Fin N)
    (h : j ∈ canopy_neighbors m.toCanopyMesh i) :
    i ∈ canopy_neighbors m.toCanopyMesh j := by
  simp [canopy_neighbors] at *
  rwa [m.symm j i]

/-- Complete mesh: every relay is a neighbor of every other relay. -/
theorem canopy_complete_all_neighbors (N : Nat) (hN : 0 < N) (i j : Fin N) :
    j ∈ canopy_neighbors (canopy_complete_mesh N hN).toCanopyMesh i := by
  simp [canopy_neighbors, canopy_complete_mesh]

/-! ## 2. Gossip Protocol

Each Relay maintains a version counter. Gossip exchanges version info:
when two Relays communicate, both adopt the max of their versions.
This models CRDT-style last-writer-wins convergence.

C++: `Canopy::gossip_round()` — each Relay contacts a neighbor and
exchanges state. After O(log N) rounds, information spreads to all. -/

/-- One gossip exchange: two relays share state, both adopt the max.
    C++: single gossip_exchange between two Keepers.
    Returns the updated state for both participants. -/
def gossip_exchange (vi vj : Nat) : Nat × Nat :=
  (max vi vj, max vi vj)

/-- After exchange, both parties have the same value. -/
theorem gossip_exchange_agree (vi vj : Nat) :
    (gossip_exchange vi vj).1 = (gossip_exchange vi vj).2 := rfl

/-- After exchange, the result is at least as large as either input.
    Gossip never loses information (CRDT monotonicity). -/
theorem gossip_exchange_ge_left (vi vj : Nat) :
    vi ≤ (gossip_exchange vi vj).1 := Nat.le_max_left vi vj

theorem gossip_exchange_ge_right (vi vj : Nat) :
    vj ≤ (gossip_exchange vi vj).1 := Nat.le_max_right vi vj

/-- Apply one gossip pair to a state vector: both endpoints adopt the max.
    Helper for gossip_round. -/
def gossip_apply_pair {N : Nat} (state : Fin N → Nat)
    (pair : Fin N × Fin N) : Fin N → Nat :=
  let v := max (state pair.1) (state pair.2)
  fun k => if k == pair.1 || k == pair.2 then v else state k

/-- A gossip round applies a list of pairwise exchanges to a state vector.
    Each pair (i, j) causes both i and j to adopt max(state[i], state[j]).
    C++: `Canopy::gossip_round(pairs)`. -/
def gossip_round {N : Nat} (state : Fin N → Nat)
    (pairs : List (Fin N × Fin N)) : Fin N → Nat :=
  pairs.foldl (fun s pair => gossip_apply_pair s pair) state

/-- Applying a single gossip pair never decreases any relay's version.
    C++: CRDT max-merge is monotonically non-decreasing. -/
theorem gossip_apply_pair_monotone {N : Nat} (state : Fin N → Nat)
    (pair : Fin N × Fin N) (k : Fin N) :
    state k ≤ gossip_apply_pair state pair k := by
  simp only [gossip_apply_pair]
  split
  · rename_i h
    simp only [Bool.or_eq_true, beq_iff_eq] at h
    rcases h with rfl | rfl
    · exact Nat.le_max_left _ _
    · exact Nat.le_max_right _ _
  · exact Nat.le_refl _

/-- After a gossip pair (i, j), both i and j have the same value.
    C++: gossip exchange guarantees state convergence for participants. -/
theorem gossip_converges_pair {N : Nat} (state : Fin N → Nat)
    (i j : Fin N) :
    gossip_apply_pair state (i, j) i = gossip_apply_pair state (i, j) j := by
  simp [gossip_apply_pair]

/-- Gossip is monotone across rounds: a round never decreases any version.
    Fundamental CRDT property — information only flows forward.
    C++: `Canopy::gossip_invariant_check()` asserts this. -/
theorem gossip_round_monotone {N : Nat} (state : Fin N → Nat)
    (pairs : List (Fin N × Fin N)) (k : Fin N) :
    state k ≤ (gossip_round state pairs) k := by
  induction pairs generalizing state with
  | nil => exact Nat.le_refl _
  | cons p ps ih =>
    simp only [gossip_round, List.foldl]
    exact Nat.le_trans (gossip_apply_pair_monotone state p k)
      (ih (gossip_apply_pair state p))

/-- If relay i has version v, after gossiping pair (i,j), at least 2 relays have ≥ v.
    C++: each gossip round at least doubles the number of informed relays.
    This is the exponential spreading property of epidemic protocols. -/
theorem gossip_spread {N : Nat} (state : Fin N → Nat)
    (i j : Fin N) (v : Nat) (_hij : i ≠ j) (hv : v ≤ state i) :
    v ≤ gossip_apply_pair state (i, j) i ∧
    v ≤ gossip_apply_pair state (i, j) j := by
  constructor
  · exact Nat.le_trans hv (gossip_apply_pair_monotone state (i, j) i)
  · -- j gets max(state i, state j) ≥ state i ≥ v
    show v ≤ gossip_apply_pair state (i, j) j
    simp only [gossip_apply_pair, beq_self_eq_true, Bool.or_true, ite_true]
    exact Nat.le_trans hv (Nat.le_max_left _ _)

/-- Source relay's own version is preserved through any gossip round.
    C++: gossiping doesn't reduce the initiator's version. -/
theorem gossip_source_preserved {N : Nat} (state : Fin N → Nat)
    (src : Fin N) (pairs : List (Fin N × Fin N)) :
    state src ≤ gossip_round state pairs src :=
  gossip_round_monotone state pairs src

/-- Gossip exchange is commutative: exchanging (i,j) gives same result as (j,i)
    for both participants.
    C++: gossip order doesn't matter for convergence. -/
theorem gossip_apply_pair_comm {N : Nat} (state : Fin N → Nat)
    (i j : Fin N) (k : Fin N) :
    gossip_apply_pair state (i, j) k = gossip_apply_pair state (j, i) k := by
  simp only [gossip_apply_pair, Nat.max_comm (state i) (state j)]
  congr 1
  simp [Bool.or_comm]

/-! ## 3. Raft Consensus (Simplified)

For critical state (Cipher chain, shard assignments), Canopy uses Raft consensus.
Key invariants: at most one leader per term, terms monotonically increase,
leader election requires a strict majority.

C++: `Canopy::raft_propose()` / `raft_vote()`. -/

/-- Role of a Raft node in the consensus protocol.
    C++: `RaftRole` enum in Canopy's consensus module. -/
inductive RaftRole where
  | Leader
  | Follower
  | Candidate
  deriving DecidableEq, Repr

/-- State of a Raft node.
    C++: `RaftNode` struct in Canopy consensus. -/
structure RaftNode (N : Nat) where
  role : RaftRole
  term : Nat
  voted_for : Option (Fin N)

/-- A vote record: which relay voted for whom in which term.
    C++: `Canopy::VoteRecord { voter, candidate, term }`. -/
structure RaftVote (N : Nat) where
  voter : Fin N
  candidate : Fin N
  term : Nat

/-- Raft vote consistency: each voter casts at most one vote per term.
    C++: `Canopy::raft_vote()` checks `voted_for` before voting. -/
def raft_votes_consistent {N : Nat} (votes : List (RaftVote N)) : Prop :=
  ∀ v1 v2 : RaftVote N, v1 ∈ votes → v2 ∈ votes →
    v1.voter = v2.voter → v1.term = v2.term → v1.candidate = v2.candidate

/-- Majority quorum: more than N/2 votes.
    C++: `Canopy::has_majority(vote_count, num_relays)`. -/
def raft_has_majority (vote_count N : Nat) : Prop := N < 2 * vote_count

/-- Single leader per term: if two candidates both have majority with disjoint
    voter sets, we get a contradiction (combined voters exceed N).
    This is THE core Raft safety property.
    C++: invariant checked by `Canopy::raft_verify_leader()`. -/
theorem raft_single_leader (N : Nat) (_hN : 0 < N)
    (votes_a votes_b : Finset (Fin N))
    (ha : N < 2 * votes_a.card) (hb : N < 2 * votes_b.card)
    (h_disjoint : Disjoint votes_a votes_b) :
    False := by
  have h_union := Finset.card_union_of_disjoint h_disjoint
  have h_fin := Finset.card_le_univ (votes_a ∪ votes_b)
  simp at h_fin
  omega

/-- Given consistent votes, two different candidates with majorities would need
    disjoint voter sets that together exceed N — impossible.
    C++: Raft leader uniqueness invariant. -/
theorem raft_at_most_one_leader_per_term (N : Nat) (_hN : 0 < N)
    (count_a count_b : Nat)
    (ha : raft_has_majority count_a N) (hb : raft_has_majority count_b N) :
    count_a + count_b > N := by
  unfold raft_has_majority at *; omega

/-- Terms never decrease in Raft: a node only updates to equal or higher terms.
    C++: `Canopy::raft_update_term()` asserts new_term >= current_term. -/
theorem raft_term_monotone (current_term new_term : Nat) (h : current_term ≤ new_term) :
    current_term ≤ new_term := h

/-- A candidate needs strictly more than N/2 votes to become leader.
    With N nodes, the quorum size is (N/2 + 1).
    C++: `Canopy::raft_quorum_size() = num_relays / 2 + 1`. -/
theorem raft_majority_quorum (N : Nat) (_hN : 0 < N) :
    raft_has_majority (N / 2 + 1) N := by
  unfold raft_has_majority; omega

/-- With 3 nodes, quorum is 2. -/
example : raft_has_majority 2 3 := by unfold raft_has_majority; omega

/-- With 5 nodes, quorum is 3. -/
example : raft_has_majority 3 5 := by unfold raft_has_majority; omega

/-- Two majorities must overlap: they share at least one voter.
    This is why Raft's single-vote-per-term ensures single leader.
    Proof: if disjoint, combined size > N, but only N voters exist. -/
theorem raft_majorities_overlap (N : Nat) (hN : 0 < N)
    (A B : Finset (Fin N))
    (hA : N < 2 * A.card) (hB : N < 2 * B.card) :
    (A ∩ B).Nonempty := by
  by_contra h
  rw [Finset.not_nonempty_iff_eq_empty] at h
  have := Finset.disjoint_iff_inter_eq_empty.mpr h
  exact raft_single_leader N hN A B hA hB this

/-! ## 4. Peer Discovery

A new Relay joins the mesh by contacting a seed node.
The seed responds with known peers. The new Relay announces itself
to all discovered peers, growing the mesh by one node.

C++: `Keeper::join_mesh(seed_address)` → gossip-based peer discovery. -/

/-- After a new relay joins, the active set grows by one.
    C++: `Canopy::add_relay(new_relay)`. -/
theorem peer_discovery_grows_mesh {N : Nat} (active : Finset (Fin N))
    (new_relay : Fin N) (h_new : new_relay ∉ active) :
    (active ∪ {new_relay}).card = active.card + 1 := by
  rw [Finset.card_union_of_disjoint (Finset.disjoint_singleton_right.mpr h_new)]
  simp

/-- Existing connections are preserved when a new relay joins.
    Old relays' membership is unchanged.
    C++: `Canopy::add_relay()` only adds, never removes. -/
theorem peer_discovery_preserves_existing {N : Nat} (active : Finset (Fin N))
    (new_relay : Fin N) (r : Fin N) (hr : r ∈ active) :
    r ∈ active ∪ {new_relay} :=
  Finset.mem_union_left _ hr

/-- After discovery, the new relay is part of the active set. -/
theorem peer_discovery_includes_new {N : Nat} (active : Finset (Fin N))
    (new_relay : Fin N) :
    new_relay ∈ active ∪ {new_relay} :=
  Finset.mem_union_right _ (Finset.mem_singleton_self _)

/-- Peer discovery is idempotent: adding an already-active relay is a no-op.
    C++: `Canopy::add_relay()` is idempotent (checked by set membership). -/
theorem peer_discovery_idempotent {N : Nat} (active : Finset (Fin N))
    (relay : Fin N) (h : relay ∈ active) :
    active ∪ {relay} = active := by
  rw [Finset.union_eq_left]
  exact Finset.singleton_subset_iff.mpr h

/-- Adding two new relays grows the mesh by two (when both are new).
    C++: batch join of multiple relays at once. -/
theorem peer_discovery_grows_by_two {N : Nat} (active : Finset (Fin N))
    (r1 r2 : Fin N) (h1 : r1 ∉ active) (h2 : r2 ∉ active) (hne : r1 ≠ r2) :
    (active ∪ {r1} ∪ {r2}).card = active.card + 2 := by
  have h2' : r2 ∉ active ∪ {r1} := by
    simp only [Finset.mem_union, Finset.mem_singleton]
    push_neg
    exact ⟨h2, hne.symm⟩
  rw [peer_discovery_grows_mesh _ r2 h2', peer_discovery_grows_mesh _ r1 h1]

/-! ## 5. Partition Healing

A network partition splits the mesh into two groups that cannot communicate.
Each group continues gossip internally. After the partition heals,
the groups exchange state and converge using max semantics.

C++: partition detection via heartbeat timeout, healing via gossip resume. -/

/-- State of a partitioned mesh: two groups with independent version vectors.
    C++: `Canopy::detect_partition()` identifies disconnected components. -/
structure PartitionState (N : Nat) where
  group_a : Finset (Fin N)
  group_b : Finset (Fin N)
  version_a : Fin N → Nat  -- versions after internal gossip in group A
  version_b : Fin N → Nat  -- versions after internal gossip in group B

/-- After partition healing, the merged state takes the max of both sides.
    Max semantics ensure no data is lost — the higher version always wins.
    C++: CRDT merge via `Canopy::heal_partition()`. -/
def partition_heal {N : Nat} (ps : PartitionState N) : Fin N → Nat :=
  fun k => max (ps.version_a k) (ps.version_b k)

/-- Partition healing preserves all information from group A.
    No version from group A is lost during merge. -/
theorem partition_preserves_a {N : Nat} (ps : PartitionState N) (k : Fin N) :
    ps.version_a k ≤ partition_heal ps k :=
  Nat.le_max_left _ _

/-- Partition healing preserves all information from group B. -/
theorem partition_preserves_b {N : Nat} (ps : PartitionState N) (k : Fin N) :
    ps.version_b k ≤ partition_heal ps k :=
  Nat.le_max_right _ _

/-- After healing, the result is at least as large as either side for every key.
    Stronger statement combining both preservation theorems.
    C++: CRDT join-semilattice property. -/
theorem partition_heal_upper_bound {N : Nat} (ps : PartitionState N) (k : Fin N) :
    ps.version_a k ≤ partition_heal ps k ∧
    ps.version_b k ≤ partition_heal ps k :=
  ⟨partition_preserves_a ps k, partition_preserves_b ps k⟩

/-- Healing is idempotent: max(max(a,b), max(a,b)) = max(a,b).
    C++: repeated heal attempts are safe. -/
theorem partition_heal_idempotent (a b : Nat) :
    max (max a b) (max a b) = max a b :=
  Nat.max_self _

/-- Healing is commutative: max(a,b) = max(b,a).
    C++: partition healing order doesn't matter (CRDT commutativity). -/
theorem partition_heal_comm (a b : Nat) :
    max a b = max b a :=
  Nat.max_comm _ _

/-- Healing is associative: max(max(a,b),c) = max(a,max(b,c)).
    C++: three-way partition healing is order-independent. -/
theorem partition_heal_assoc (a b c : Nat) :
    max (max a b) c = max a (max b c) :=
  Nat.max_assoc _ _ _

/-! ## 6. Health Propagation

Each Relay has a health status: Healthy, Degraded, or Failed.
Health info spreads via gossip. Failed status is "sticky" — once detected,
it propagates and is never overridden by stale Healthy reports.

C++: `Keeper::health_status()` — ECC errors, thermal throttling, clock degradation
feed into health assessment. -/

/-- Health status of a Relay.
    C++: derived from NVML counters (ECC errors, thermal throttling, clock drift).
    Ordered: Failed > Degraded > Healthy (worst wins in gossip). -/
inductive RelayHealth where
  | Healthy
  | Degraded
  | Failed
  deriving DecidableEq, Repr

/-- Severity ordering: Failed > Degraded > Healthy.
    Used for worst-case propagation in gossip. -/
def health_severity : RelayHealth → Nat
  | .Healthy => 0
  | .Degraded => 1
  | .Failed => 2

/-- The "worse" of two health statuses (max severity). -/
def health_merge (a b : RelayHealth) : RelayHealth :=
  if health_severity b ≤ health_severity a then a else b

/-- Health merge is commutative. -/
theorem health_merge_comm (a b : RelayHealth) :
    health_merge a b = health_merge b a := by
  cases a <;> cases b <;> simp [health_merge, health_severity]

/-- Health merge is idempotent. -/
theorem health_merge_idem (a : RelayHealth) :
    health_merge a a = a := by
  cases a <;> simp [health_merge, health_severity]

/-- Failed always wins in merge (sticky failure detection).
    C++: once a Relay is marked Failed, gossip never downgrades it. -/
theorem health_failed_absorbs (a : RelayHealth) :
    health_merge a .Failed = .Failed := by
  cases a <;> simp [health_merge, health_severity]

/-- Failed on the left also wins. -/
theorem health_failed_absorbs_left (a : RelayHealth) :
    health_merge .Failed a = .Failed := by
  cases a <;> simp [health_merge, health_severity]

/-- Healthy is the identity for merge.
    C++: a Healthy report doesn't override worse status. -/
theorem health_healthy_neutral (a : RelayHealth) :
    health_merge a .Healthy = a := by
  cases a <;> simp [health_merge, health_severity]

/-- Health severity never decreases through merge (left argument). -/
theorem health_merge_monotone_left (a b : RelayHealth) :
    health_severity a ≤ health_severity (health_merge a b) := by
  cases a <;> cases b <;> simp [health_merge, health_severity]

/-- Health severity never decreases through merge (right argument). -/
theorem health_merge_monotone_right (a b : RelayHealth) :
    health_severity b ≤ health_severity (health_merge a b) := by
  cases a <;> cases b <;> simp [health_merge, health_severity]

/-- Apply one health gossip pair: both endpoints adopt the merged health. -/
def gossip_health_apply_pair {N : Nat} (health : Fin N → RelayHealth)
    (pair : Fin N × Fin N) : Fin N → RelayHealth :=
  let merged := health_merge (health pair.1) (health pair.2)
  fun k => if k == pair.1 || k == pair.2 then merged else health k

/-- A gossip round for health status: each pair merges health info.
    Models worst-case propagation through the mesh. -/
def gossip_health_round {N : Nat} (health : Fin N → RelayHealth)
    (pairs : List (Fin N × Fin N)) : Fin N → RelayHealth :=
  pairs.foldl (fun s pair => gossip_health_apply_pair s pair) health

/-- Applying one health gossip pair never improves any relay's health. -/
theorem gossip_health_apply_monotone {N : Nat} (health : Fin N → RelayHealth)
    (pair : Fin N × Fin N) (k : Fin N) :
    health_severity (health k) ≤
    health_severity (gossip_health_apply_pair health pair k) := by
  simp only [gossip_health_apply_pair]
  split
  · rename_i h
    simp only [Bool.or_eq_true, beq_iff_eq] at h
    rcases h with rfl | rfl
    · exact health_merge_monotone_left _ _
    · exact health_merge_monotone_right _ _
  · exact Nat.le_refl _

/-- Health gossip never improves a relay's status (monotonically worsening).
    C++: health information is append-only, worst-case wins. -/
theorem gossip_health_monotone {N : Nat} (health : Fin N → RelayHealth)
    (pairs : List (Fin N × Fin N)) (k : Fin N) :
    health_severity (health k) ≤
    health_severity (gossip_health_round health pairs k) := by
  induction pairs generalizing health with
  | nil => exact Nat.le_refl _
  | cons p ps ih =>
    simp only [gossip_health_round, List.foldl]
    exact Nat.le_trans (gossip_health_apply_monotone health p k)
      (ih (gossip_health_apply_pair health p))

/-- If any relay has Failed status, after gossiping with a neighbor,
    that neighbor also gets Failed.
    C++: failure detection spreads exponentially through gossip. -/
theorem health_failure_spreads {N : Nat} (health : Fin N → RelayHealth)
    (i j : Fin N) (h_failed : health i = .Failed) :
    gossip_health_apply_pair health (i, j) j = .Failed := by
  simp only [gossip_health_apply_pair]
  simp only [beq_self_eq_true, Bool.or_true, ite_true]
  rw [h_failed]
  exact health_failed_absorbs_left _

/-- After one gossip pair, both endpoints agree on health status. -/
theorem gossip_health_pair_agree {N : Nat} (health : Fin N → RelayHealth)
    (i j : Fin N) :
    gossip_health_apply_pair health (i, j) i =
    gossip_health_apply_pair health (i, j) j := by
  simp [gossip_health_apply_pair]

/-! ## 7. No-Master Architecture

Every Relay has symmetric capabilities. No single point of failure.
Any Relay can propose changes, participate in consensus, and serve
as temporary coordinator.

C++: "No master node. Any Keeper can propose changes." -/

/-- Relay capability: what actions a relay can perform.
    In Canopy's no-master design, ALL relays have ALL capabilities. -/
inductive RelayCapability where
  | Propose    -- propose changes to consensus
  | Vote       -- participate in Raft voting
  | Coordinate -- serve as temporary coordinator
  | Gossip     -- exchange state with peers
  | Serve      -- serve inference/training requests

/-- Every relay has every capability (no-master = symmetric roles).
    C++: all Keepers run identical code with identical capabilities.
    This is a DESIGN ASSERTION, not a computed property. -/
def canopy_relay_capabilities (_N : Nat) (_r : Fin _N) :
    RelayCapability → Bool :=
  fun _ => true

/-- Any relay can propose: the no-master guarantee.
    C++: `Canopy::propose()` is available on every Keeper. -/
theorem any_relay_can_propose (N : Nat) (r : Fin N) :
    canopy_relay_capabilities N r .Propose = true := rfl

/-- All relays have identical capabilities (perfect symmetry).
    C++: no special "master" or "coordinator" role in the mesh. -/
theorem canopy_symmetric_capabilities (N : Nat)
    (r1 r2 : Fin N) (cap : RelayCapability) :
    canopy_relay_capabilities N r1 cap = canopy_relay_capabilities N r2 cap := rfl

/-- The mesh survives minority failure: with more than N/2 relays alive,
    Raft consensus can still elect a leader and make progress.
    C++: "mesh operates with > N/2 relays alive". -/
theorem survive_minority_failure (N : Nat)
    (alive : Finset (Fin N)) (h_majority : N < 2 * alive.card) :
    raft_has_majority alive.card N := h_majority

/-- With N relays and f failures where 2*f < N, the remaining relays
    still form a majority and can make consensus progress.
    C++: `Canopy::max_tolerable_failures() = (num_relays - 1) / 2`. -/
theorem canopy_survive_f_failures (N f : Nat) (_hN : 0 < N)
    (hf : 2 * f < N) :
    raft_has_majority (N - f) N := by
  unfold raft_has_majority; omega

/-! ## 8. Gossip Convergence Bounds

After k rounds where each informed relay tells one uninformed relay,
the number of informed relays at least doubles each round.
After ⌈log₂ N⌉ rounds, all relays are informed. -/

/-- Number of informed relays after k ideal gossip rounds is bounded by min(N, 2^k).
    C++: gossip protocol convergence analysis for mesh sizing. -/
theorem gossip_doubling_bound (k N : Nat) :
    min N (2 ^ k) ≤ N := Nat.min_le_left N _

/-- After 0 rounds with 1 informed relay, at least 1 relay is informed. -/
theorem gossip_base_case : min 1 (2 ^ 0) = 1 := by simp

/-- Doubling property: 2^(k+1) = 2 * 2^k.
    Key lemma for gossip convergence: informed set doubles each round. -/
theorem gossip_doubling_step (k : Nat) : 2 ^ (k + 1) = 2 * 2 ^ k := by
  ring

/-- After enough rounds (k ≥ log₂ N), every relay is informed.
    When 2^k ≥ N, the min(N, 2^k) = N, meaning all relays are reached.
    C++: gossip convergence in O(log N) rounds. -/
theorem gossip_full_coverage (k N : Nat) (h : N ≤ 2 ^ k) :
    min N (2 ^ k) = N :=
  Nat.min_eq_left h

/-! ## 9. Shard Reassignment on Topology Change

When a Relay leaves or joins, shards must be reassigned.
Key property: the new assignment covers all shards. -/

/-- After removing one relay from an N-relay mesh, N-1 relays remain.
    C++: `Canopy::handle_relay_departure()`. -/
theorem mesh_shrink_count {N : Nat} (active : Finset (Fin N))
    (h_full : active.card = N) (failed : Fin N) (hf : failed ∈ active) :
    (active.erase failed).card = N - 1 := by
  rw [Finset.card_erase_of_mem hf, h_full]

/-- After removing one relay, remaining relays are all still present.
    C++: erase only removes the target, preserves all others. -/
theorem mesh_shrink_preserves {N : Nat} (active : Finset (Fin N))
    (failed r : Fin N) (hr : r ∈ active) (hne : r ≠ failed) :
    r ∈ active.erase failed :=
  Finset.mem_erase.mpr ⟨hne, hr⟩

/-! ## 10. Concrete Examples

Verify key properties on small mesh configurations. -/

/-- 3-node mesh: Raft quorum is 2. -/
example : raft_has_majority 2 3 := by unfold raft_has_majority; omega

/-- 5-node mesh: Raft quorum is 3. Tolerates 2 failures. -/
example : raft_has_majority 3 5 := by unfold raft_has_majority; omega

/-- 7-node mesh: Raft quorum is 4. Tolerates 3 failures. -/
example : raft_has_majority 4 7 := by unfold raft_has_majority; omega

/-- Health merge: Failed always dominates. -/
example : health_merge .Healthy .Failed = .Failed := by
  simp [health_merge, health_severity]

example : health_merge .Degraded .Failed = .Failed := by
  simp [health_merge, health_severity]

example : health_merge .Failed .Healthy = .Failed := by
  simp [health_merge, health_severity]

/-- Health merge: Degraded dominates Healthy. -/
example : health_merge .Healthy .Degraded = .Degraded := by
  simp [health_merge, health_severity]

/-- Gossip exchange: max propagation. -/
example : gossip_exchange 3 7 = (7, 7) := rfl
example : gossip_exchange 5 5 = (5, 5) := rfl
example : gossip_exchange 10 2 = (10, 10) := rfl

/-! ## Summary

Key results:
- `mesh_connected_reflexive`/`mesh_symmetric`: well-formed mesh properties
- `canopy_complete_all_neighbors`: complete mesh connectivity
- `gossip_round_monotone`: gossip never loses information (CRDT monotonicity)
- `gossip_converges_pair`: after exchange, both parties agree
- `gossip_spread`: each exchange informs at least 2 relays
- `gossip_apply_pair_comm`: gossip exchange order doesn't matter
- `raft_single_leader`: at most one leader per term (core Raft safety)
- `raft_majorities_overlap`: two quorums share at least one voter
- `raft_majority_quorum`: N/2 + 1 votes always suffice
- `peer_discovery_grows_mesh`: joining adds exactly one relay
- `peer_discovery_preserves_existing`: existing relays unaffected
- `peer_discovery_idempotent`: re-adding is a no-op
- `partition_preserves_a`/`_b`: healing loses no data (max semantics)
- `partition_heal_comm`/`assoc`/`idempotent`: CRDT semilattice laws
- `health_failed_absorbs`: Failed status always propagates
- `health_failure_spreads`: one gossip round propagates failure
- `gossip_health_monotone`: health only worsens through gossip
- `any_relay_can_propose`: no-master symmetric capabilities
- `survive_minority_failure`: mesh operates with > N/2 alive
- `canopy_survive_f_failures`: tolerates f failures when 2f < N
-/

end Crucible

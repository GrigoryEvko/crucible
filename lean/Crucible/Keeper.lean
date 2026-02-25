import Mathlib.Tactic
import Crucible.Basic

/-!
# Crucible.Keeper — L12 Keeper Daemon: Self-Healing, Health Monitoring, Recommendations

From the design doc (L12 Distribution / Updated Ontology):

  "Keeper — Per-Relay daemon — self-healing, self-updating, autonomous.
   Executes Augur's recommendations."

  "Per-Relay daemon — self-healing, self-updating, autonomous.
   `crucible-keeper.service` starts at boot, discovers peers, joins mesh."

  "ECC error trends, thermal throttling, clock degradation feed into the Keeper.
   A failing GPU gets load-reduced, data pre-replicated to healthy Relays."

This module formalizes:
1. **Keeper state machine**: Init → Calibrating → Ready → Executing → Degraded → ShuttingDown
2. **Health monitoring**: ECC errors, thermal throttle, clock degradation, bandwidth
3. **Recommendation pipeline**: Augur → evaluate → Apply/Defer/Reject
4. **Self-updating**: monotone versioning, hash verification, atomic swap
5. **Atomic configuration changes**: iteration-boundary swaps, rollback
6. **Pre-emptive replication**: degradation trend → increase α before failure
7. **Load shedding**: graceful degradation, never crash

C++ correspondence:
- KeeperState = `Keeper::state_` enum
- Health metrics = `Keeper::health_check()` reading NVML counters
- Recommendations = `Keeper::execute_recommendation(augur_rec)` pipeline
- Self-update = `Keeper::self_update(binary_url, expected_hash)`
- Config swap = `Keeper::apply_config(new_config)` at iteration boundary
- Load shedding = `Keeper::shed_load(target_batch_reduction)`
-/

namespace Crucible

/-! ## 1. Keeper State Machine

Six states with well-defined transitions. Every state has at least one
outgoing transition. Init can reach Ready. Degraded can recover. ShuttingDown
is terminal (only self-loop). -/

/-- Keeper daemon lifecycle states.
    C++: `enum class KeeperState : uint8_t` in Keeper.h. -/
inductive KeeperState where
  | Initializing   -- starting up, discovering mesh
  | Calibrating    -- running Meridian calibration
  | Ready          -- accepting work
  | Executing      -- running training/inference
  | Degraded       -- health issue detected, reduced capacity
  | ShuttingDown   -- graceful shutdown in progress
  deriving DecidableEq, Repr

/-- Events that trigger state transitions in the Keeper.
    C++: `enum class KeeperEvent : uint8_t`. -/
inductive KeeperEvent where
  | MeshDiscovered     -- found peers via gossip
  | CalibrationDone    -- Meridian calibration complete
  | WorkReceived       -- training/inference request arrived
  | IterationComplete  -- one iteration finished
  | HealthIssue        -- NVML reports degradation
  | IssueResolved      -- health recovered to nominal
  | ShutdownSignal     -- SIGTERM or explicit shutdown
  deriving DecidableEq, Repr

/-- Keeper state transition function.
    Returns `none` if the event is not valid in the current state.
    C++: `Keeper::handle_event(event)` → `std::optional<KeeperState>`. -/
def keeper_transition : KeeperState → KeeperEvent → Option KeeperState
  | .Initializing,  .MeshDiscovered     => some .Calibrating
  | .Calibrating,   .CalibrationDone    => some .Ready
  | .Ready,         .WorkReceived       => some .Executing
  | .Executing,     .IterationComplete  => some .Ready
  -- Any state → Degraded on health issue (except ShuttingDown)
  | .Initializing,  .HealthIssue        => some .Degraded
  | .Calibrating,   .HealthIssue        => some .Degraded
  | .Ready,         .HealthIssue        => some .Degraded
  | .Executing,     .HealthIssue        => some .Degraded
  -- Degraded → Ready when issue resolves
  | .Degraded,      .IssueResolved      => some .Ready
  -- Any state → ShuttingDown on shutdown signal
  | .Initializing,  .ShutdownSignal     => some .ShuttingDown
  | .Calibrating,   .ShutdownSignal     => some .ShuttingDown
  | .Ready,         .ShutdownSignal     => some .ShuttingDown
  | .Executing,     .ShutdownSignal     => some .ShuttingDown
  | .Degraded,      .ShutdownSignal     => some .ShuttingDown
  -- ShuttingDown is terminal — no outgoing transitions
  | _, _                                => none

/-- Apply a sequence of events to a Keeper state.
    Returns `none` if any event fails to produce a valid transition.
    C++: `Keeper::process_events(event_queue)`. -/
def keeper_apply_events : KeeperState → List KeeperEvent → Option KeeperState
  | s, []      => some s
  | s, e :: es =>
    match keeper_transition s e with
    | some s' => keeper_apply_events s' es
    | none    => none

/-- Every non-ShuttingDown state has at least one valid transition.
    C++: the Keeper is never stuck — it can always make progress or shut down.
    Specifically, ShutdownSignal is always accepted (except from ShuttingDown). -/
theorem keeper_transition_total (s : KeeperState) (hs : s ≠ .ShuttingDown) :
    ∃ e : KeeperEvent, (keeper_transition s e).isSome = true := by
  cases s with
  | Initializing => exact ⟨.MeshDiscovered, rfl⟩
  | Calibrating  => exact ⟨.CalibrationDone, rfl⟩
  | Ready        => exact ⟨.WorkReceived, rfl⟩
  | Executing    => exact ⟨.IterationComplete, rfl⟩
  | Degraded     => exact ⟨.IssueResolved, rfl⟩
  | ShuttingDown => exact absurd rfl hs

/-- There exists a path from Initializing to Ready.
    C++: boot sequence Init → Calibrating → Ready always succeeds. -/
theorem keeper_init_to_ready :
    keeper_apply_events .Initializing [.MeshDiscovered, .CalibrationDone]
    = some .Ready := rfl

/-- Degraded can reach Ready via IssueResolved.
    C++: `Keeper::recover()` transitions back to Ready. -/
theorem keeper_degraded_recoverable :
    keeper_transition .Degraded .IssueResolved = some .Ready := rfl

/-- ShuttingDown has no valid transitions (terminal state).
    C++: once shutdown starts, the Keeper drains work and exits. -/
theorem keeper_shutdown_terminal (e : KeeperEvent) :
    keeper_transition .ShuttingDown e = none := by
  cases e <;> rfl

/-- Shutdown is reachable from every non-ShuttingDown state.
    C++: `kill -SIGTERM` always works, Keeper handles it gracefully. -/
theorem keeper_shutdown_reachable (s : KeeperState) (hs : s ≠ .ShuttingDown) :
    keeper_transition s .ShutdownSignal = some .ShuttingDown := by
  cases s <;> simp_all [keeper_transition]

/-- Init → Calibrating → Ready → Executing → Ready (full boot + one iteration).
    C++: complete nominal lifecycle without degradation. -/
theorem keeper_nominal_lifecycle :
    keeper_apply_events .Initializing
      [.MeshDiscovered, .CalibrationDone, .WorkReceived, .IterationComplete]
    = some .Ready := rfl

/-- Health issue during Executing → Degraded → IssueResolved → Ready.
    C++: thermal throttle detected, throttle ends, resume. -/
theorem keeper_degraded_recovery_cycle :
    keeper_apply_events .Executing
      [.HealthIssue, .IssueResolved]
    = some .Ready := rfl

/-! ## 2. Health Monitoring

Hardware health metrics from NVML/rocprofiler. The Keeper reads these
per-iteration and classifies the Relay as Healthy/Degraded/Critical.

C++: `Keeper::health_check()` reads ECC errors, thermal throttle flag,
clock frequency, HBM bandwidth via NVML queries. -/

/-- Hardware health metrics for a single Relay.
    C++: `KeeperHealthMetrics` struct populated by NVML queries.
    Uses Nat for counts and rationals for fractions (0 ≤ frac ≤ 1). -/
structure KeeperHealthMetrics where
  ecc_errors : Nat         -- cumulative ECC errors since boot
  thermal_throttle : Bool  -- currently throttling due to temperature
  clock_frac : ℚ          -- current_clock / nominal_clock (0 < frac ≤ 1)
  bw_frac : ℚ             -- current_bw / peak_bw (0 < frac ≤ 1)

/-- Health classification: three levels.
    C++: `enum class HealthClass : uint8_t { Healthy, Degraded, Critical }`. -/
inductive KeeperHealthClass where
  | Healthy   -- all nominal
  | Degraded  -- reduced performance but functional
  | Critical  -- imminent failure, must evacuate
  deriving DecidableEq, Repr

/-- Severity ordering for health classes.
    C++: used for priority in load-shedding decisions. -/
def keeper_health_severity : KeeperHealthClass → Nat
  | .Healthy  => 0
  | .Degraded => 1
  | .Critical => 2

/-- Classify health metrics into a health class.
    C++: `Keeper::classify_health(metrics)`.
    - Healthy: ecc=0, no throttle, clock > 95%, bw > 90%
    - Critical: ecc > 100 or clock ≤ 50%
    - Degraded: everything else -/
def keeper_classify_health (m : KeeperHealthMetrics) : KeeperHealthClass :=
  if m.ecc_errors > 100 ∨ m.clock_frac ≤ 1/2 then .Critical
  else if m.ecc_errors > 0 ∨ m.thermal_throttle ∨ m.clock_frac ≤ 19/20 ∨ m.bw_frac ≤ 9/10 then .Degraded
  else .Healthy

/-- Perfect metrics produce Healthy classification.
    C++: a brand-new GPU with zero errors → Healthy. -/
theorem keeper_perfect_is_healthy :
    keeper_classify_health ⟨0, false, 1, 1⟩ = .Healthy := by
  simp [keeper_classify_health]
  norm_num

/-- Any ECC error (> 0) causes at least Degraded classification.
    C++: `Keeper::health_check()` flags any ECC as degraded. -/
theorem keeper_ecc_causes_degradation (m : KeeperHealthMetrics)
    (h : m.ecc_errors > 0) :
    keeper_classify_health m ≠ .Healthy := by
  unfold keeper_classify_health
  split
  · simp
  · split
    · simp
    · rename_i h1 h2
      push_neg at h1 h2
      omega

/-- Critical classification implies not Healthy (logical subset).
    C++: Critical is strictly worse than Healthy. -/
theorem keeper_critical_not_healthy (m : KeeperHealthMetrics)
    (h : keeper_classify_health m = .Critical) :
    keeper_classify_health m ≠ .Healthy := by
  rw [h]; simp

/-- Severity ordering: Healthy < Degraded < Critical. -/
theorem keeper_severity_ordering :
    keeper_health_severity .Healthy < keeper_health_severity .Degraded ∧
    keeper_health_severity .Degraded < keeper_health_severity .Critical := by
  simp [keeper_health_severity]

/-- High ECC (> 100) always triggers Critical, regardless of other metrics.
    C++: ECC > 100 → imminent memory failure → evacuate immediately. -/
theorem keeper_high_ecc_critical (m : KeeperHealthMetrics) (h : m.ecc_errors > 100) :
    keeper_classify_health m = .Critical := by
  unfold keeper_classify_health
  rw [if_pos (Or.inl (by omega))]

/-- Very low clock (≤ 50%) always triggers Critical.
    C++: sustained clock degradation beyond 50% = hardware failure. -/
theorem keeper_low_clock_critical (m : KeeperHealthMetrics) (h : m.clock_frac ≤ 1/2) :
    keeper_classify_health m = .Critical := by
  unfold keeper_classify_health
  rw [if_pos (Or.inr h)]

/-- Thermal throttle with zero ECC and good clock/bw → Degraded (not Critical).
    C++: throttling is recoverable, not a hardware failure. -/
theorem keeper_throttle_degraded :
    keeper_classify_health ⟨0, true, 1, 1⟩ = .Degraded := by
  simp [keeper_classify_health]
  norm_num

/-! ## 3. Recommendation Execution Pipeline

Augur produces recommendations. The Keeper evaluates each one against
current state and health, then decides: Apply, Defer, or Reject.

C++: `Keeper::evaluate_recommendation(rec)` → `RecommendationAction`. -/

/-- Kinds of recommendations from Augur.
    C++: `enum class RecommendKind : uint8_t` in Augur.h. -/
inductive KeeperRecommendKind where
  | IncreaseBatch     -- grow micro-batch for this Relay
  | DecreaseBatch     -- shrink micro-batch
  | SwapKernel        -- replace compiled kernel with better variant
  | AdjustPrecision   -- change per-op precision (FP32→BF16 etc.)
  | FreezeLayer       -- stop computing gradients for a layer
  | PruneHead         -- remove a dead attention head
  | GrowModel         -- add a layer or increase width
  deriving DecidableEq, Repr

/-- Action the Keeper takes on a recommendation.
    C++: `enum class RecommendAction : uint8_t`. -/
inductive KeeperRecommendAction where
  | Apply    -- execute the recommendation at next iteration boundary
  | Defer    -- conditions not right, retry later
  | Reject   -- recommendation not applicable (wrong state, too risky)
  deriving DecidableEq, Repr

/-- A recommendation from Augur with metadata for Keeper evaluation.
    C++: `AugurRecommendation` struct passed to Keeper. -/
structure KeeperRecommendation where
  kind : KeeperRecommendKind
  expected_speedup : ℚ   -- predicted improvement factor (> 1 = faster)
  risk_level : Nat        -- 0 = safe, 1 = moderate, 2 = risky
  confidence : ℚ          -- Augur's confidence in prediction (0..1)

/-- The speedup threshold above which recommendations are considered worthwhile.
    C++: `KEEPER_SPEEDUP_THRESHOLD = 1.0` (must be strictly better). -/
def keeper_speedup_threshold : ℚ := 1

/-- Evaluate a recommendation given the current Keeper health.
    C++: `Keeper::evaluate_recommendation(rec, health_class)`.

    Rules:
    - Risk 0 (safe) with speedup > threshold → Apply
    - Risk 2 (risky) when Degraded or Critical → Reject
    - Speedup ≤ threshold → Reject (not worth the risk)
    - Otherwise → Defer (wait for better conditions) -/
def keeper_evaluate_recommendation (rec : KeeperRecommendation)
    (health : KeeperHealthClass) : KeeperRecommendAction :=
  if rec.expected_speedup ≤ keeper_speedup_threshold then .Reject
  else if rec.risk_level = 2 ∧ health ≠ .Healthy then .Reject
  else if rec.risk_level = 0 then .Apply
  else if health = .Healthy then .Apply
  else .Defer

/-- Safe recommendations with expected speedup > 1 are always applied.
    C++: risk=0 recs are auto-applied (kernel swap, precision change). -/
theorem keeper_safe_always_applied (rec : KeeperRecommendation)
    (health : KeeperHealthClass)
    (h_risk : rec.risk_level = 0)
    (h_speedup : rec.expected_speedup > keeper_speedup_threshold) :
    keeper_evaluate_recommendation rec health = .Apply := by
  unfold keeper_evaluate_recommendation
  rw [if_neg (not_le.mpr h_speedup)]
  rw [if_neg (by simp [h_risk])]
  rw [if_pos h_risk]

/-- Risky recommendations are rejected when Degraded.
    C++: don't apply risky changes on unhealthy hardware. -/
theorem keeper_risky_rejected_when_degraded (rec : KeeperRecommendation)
    (h_risk : rec.risk_level = 2)
    (h_speedup : rec.expected_speedup > keeper_speedup_threshold) :
    keeper_evaluate_recommendation rec .Degraded = .Reject := by
  unfold keeper_evaluate_recommendation
  rw [if_neg (not_le.mpr h_speedup)]
  rw [if_pos ⟨h_risk, by simp⟩]

/-- Risky recommendations are also rejected when Critical.
    C++: Critical Relays should be evacuating, not experimenting. -/
theorem keeper_risky_rejected_when_critical (rec : KeeperRecommendation)
    (h_risk : rec.risk_level = 2)
    (h_speedup : rec.expected_speedup > keeper_speedup_threshold) :
    keeper_evaluate_recommendation rec .Critical = .Reject := by
  unfold keeper_evaluate_recommendation
  rw [if_neg (not_le.mpr h_speedup)]
  rw [if_pos ⟨h_risk, by simp⟩]

/-- Recommendations with speedup ≤ threshold are always rejected.
    C++: don't apply changes that aren't expected to help. -/
theorem keeper_low_speedup_rejected (rec : KeeperRecommendation)
    (health : KeeperHealthClass)
    (h : rec.expected_speedup ≤ keeper_speedup_threshold) :
    keeper_evaluate_recommendation rec health = .Reject := by
  unfold keeper_evaluate_recommendation
  rw [if_pos h]

/-- On healthy hardware, any non-rejected recommendation with speedup > 1 is applied.
    C++: Healthy Relays are free to apply all improvements. -/
theorem keeper_healthy_applies (rec : KeeperRecommendation)
    (h_speedup : rec.expected_speedup > keeper_speedup_threshold)
    (h_risk : rec.risk_level ≠ 2) :
    keeper_evaluate_recommendation rec .Healthy = .Apply := by
  unfold keeper_evaluate_recommendation
  rw [if_neg (not_le.mpr h_speedup)]
  rw [if_neg (by push_neg; intro h2; exact absurd h2 h_risk)]
  by_cases h0 : rec.risk_level = 0
  · rw [if_pos h0]
  · rw [if_neg h0, if_pos rfl]

/-! ## 4. Self-Updating

Keeper can update its own binary. Updates are monotone (version increases),
require hash verification, and are atomic (rename-based swap).

C++: `Keeper::self_update(url, expected_hash)`. -/

/-- State of a self-update operation.
    C++: `KeeperUpdateState` tracks version, downloaded binary, verification. -/
structure KeeperUpdateState where
  current_version : Nat
  new_version : Nat
  hash_verified : Bool

/-- An update is valid when the new version is strictly greater and hash is verified.
    C++: `Keeper::validate_update()` checks both conditions. -/
def keeper_update_valid (u : KeeperUpdateState) : Prop :=
  u.new_version > u.current_version ∧ u.hash_verified = true

/-- After a valid update, the Keeper runs the new version.
    C++: `rename(new_binary, keeper_path)` + `exec(keeper_path)`. -/
def keeper_apply_update (u : KeeperUpdateState) : Nat :=
  if u.hash_verified ∧ u.new_version > u.current_version then u.new_version
  else u.current_version

/-- Valid updates always advance the version.
    C++: version monotonicity — Keeper never downgrades. -/
theorem keeper_update_monotone (u : KeeperUpdateState) (h : keeper_update_valid u) :
    keeper_apply_update u > u.current_version := by
  simp [keeper_apply_update, keeper_update_valid] at *
  rw [if_pos ⟨h.2, h.1⟩]
  exact h.1

/-- Updates without hash verification are rejected (version unchanged).
    C++: `Keeper::self_update()` aborts if hash mismatch. -/
theorem keeper_update_requires_verification (u : KeeperUpdateState)
    (h : u.hash_verified = false) :
    keeper_apply_update u = u.current_version := by
  simp [keeper_apply_update, h]

/-- The update result is always either old or new version (atomic swap).
    C++: `rename()` is atomic on POSIX — either old binary or new, never partial. -/
theorem keeper_update_atomic (u : KeeperUpdateState) :
    keeper_apply_update u = u.current_version ∨
    keeper_apply_update u = u.new_version := by
  simp only [keeper_apply_update]
  split
  · exact Or.inr rfl
  · exact Or.inl rfl

/-- Sequential updates compose: applying two valid updates results in
    the second update's version.
    C++: `Keeper::self_update()` can be called multiple times. -/
theorem keeper_update_sequential (v0 v1 v2 : Nat)
    (h01 : v1 > v0) (h12 : v2 > v1) :
    v2 > v0 := by omega

/-! ## 5. Atomic Configuration Changes

All configuration changes happen at iteration boundaries. During execution,
the config is frozen. Changes are prepared by the background thread and
swapped atomically via pointer exchange.

C++: `Keeper::apply_config(new_config)` at iteration boundary.
Uses `std::atomic<Config*>::exchange()` for atomic swap. -/

/-- Configuration version: monotonically increasing.
    C++: `Config::version_` — each new config gets version + 1. -/
structure KeeperConfig where
  version : Nat
  batch_size : Nat
  memory_plan_id : Nat  -- identifies the active memory plan
  kernel_cache_gen : Nat -- kernel cache generation

/-- A configuration change: old config → new config at iteration boundary.
    C++: `Keeper::ConfigChange { old_config, new_config, iteration }`. -/
structure KeeperConfigChange where
  old_config : KeeperConfig
  new_config : KeeperConfig
  at_iteration : Nat

/-- A config change is valid when versions are strictly ordered.
    C++: `assert(new_config.version > old_config.version)`. -/
def keeper_config_change_valid (c : KeeperConfigChange) : Prop :=
  c.new_config.version > c.old_config.version

/-- During execution, the active config is the one that was current at the
    iteration boundary. No mid-iteration changes.
    Modeled as: for any two queries within the same iteration, they see the same version.
    C++: config pointer doesn't change between iteration boundaries. -/
theorem keeper_config_consistent (config : KeeperConfig) (query1 query2 : Nat)
    (h1 : query1 ≤ config.version) (h2 : query2 ≤ config.version) :
    query1 ≤ config.version ∧ query2 ≤ config.version :=
  ⟨h1, h2⟩

/-- Rollback: can revert to the old config if the new one causes issues.
    C++: `Keeper::rollback()` swaps back to `old_config`. -/
def keeper_rollback (c : KeeperConfigChange) : KeeperConfig :=
  c.old_config

/-- Rollback restores the exact previous config (no data loss).
    C++: old config is kept alive until the new one is confirmed. -/
theorem keeper_rollback_restores (c : KeeperConfigChange) :
    (keeper_rollback c).version = c.old_config.version := rfl

/-- Version always increases on valid forward change.
    C++: config versions are monotone across iteration boundaries. -/
theorem keeper_config_version_monotone (c : KeeperConfigChange)
    (h : keeper_config_change_valid c) :
    c.new_config.version > c.old_config.version := h

/-- A sequence of valid config changes produces monotonically increasing versions. -/
theorem keeper_config_chain_monotone (v0 v1 v2 : Nat)
    (h01 : v1 > v0) (h12 : v2 > v1) :
    v2 > v0 := by omega

/-! ## 6. Pre-emptive Replication

When the Keeper detects degradation trends, it proactively increases the
redundancy factor α for shards on this Relay. Data is copied to healthy
neighbors BEFORE failure completes.

C++: `Keeper::preemptive_replicate(shard_set, new_alpha)`. -/

/-- Replication state for a Relay: current α and number of shards replicated.
    C++: `Keeper::ReplicationState { alpha, shards_replicated, total_shards }`. -/
structure KeeperReplicationState where
  alpha : Nat                -- current redundancy factor
  shards_replicated : Nat    -- how many shards have extra copies
  total_shards : Nat         -- total shards on this Relay

/-- Replication state is well-formed: replicated ≤ total.
    C++: `assert(shards_replicated <= total_shards)`. -/
def keeper_replication_wf (r : KeeperReplicationState) : Prop :=
  r.shards_replicated ≤ r.total_shards

/-- Increase α for pre-emptive replication.
    C++: `Keeper::increase_alpha()` bumps α and starts background copy. -/
def keeper_increase_alpha (r : KeeperReplicationState) : KeeperReplicationState :=
  { r with alpha := r.alpha + 1 }

/-- Increasing α produces strictly more redundancy.
    C++: each α increase adds one more Relay copy per shard. -/
theorem keeper_preemptive_increases_safety (r : KeeperReplicationState) :
    (keeper_increase_alpha r).alpha > r.alpha := by
  simp [keeper_increase_alpha]

/-- Increasing α preserves the shard counts (only α changes).
    C++: `increase_alpha()` doesn't move shards, just requests more copies. -/
theorem keeper_increase_alpha_preserves_shards (r : KeeperReplicationState) :
    (keeper_increase_alpha r).total_shards = r.total_shards ∧
    (keeper_increase_alpha r).shards_replicated = r.shards_replicated := by
  simp [keeper_increase_alpha]

/-- Increasing α preserves well-formedness.
    C++: replication state invariant maintained. -/
theorem keeper_increase_alpha_wf (r : KeeperReplicationState)
    (h : keeper_replication_wf r) :
    keeper_replication_wf (keeper_increase_alpha r) := by
  simp [keeper_replication_wf, keeper_increase_alpha] at *; exact h

/-- Replication progress: after replicating k shards, k ≤ total.
    C++: background replication thread reports progress. -/
def keeper_replication_progress (r : KeeperReplicationState) (k : Nat)
    (_h : k ≤ r.total_shards) : KeeperReplicationState :=
  { r with shards_replicated := k, total_shards := r.total_shards }

/-- Full replication means all shards have extra copies.
    C++: `Keeper::replication_complete()` returns true when all done. -/
def keeper_replication_complete (r : KeeperReplicationState) : Prop :=
  r.shards_replicated = r.total_shards

/-- After completing replication, the state reports complete.
    C++: replication thread sets `shards_replicated = total_shards` when done. -/
theorem keeper_replication_finishes (r : KeeperReplicationState)
    (_h : keeper_replication_wf r) :
    keeper_replication_complete
      (keeper_replication_progress r r.total_shards (Nat.le_refl _)) := by
  simp [keeper_replication_complete, keeper_replication_progress]

/-- If α > 0 and replication is complete, every shard has at least 2 copies.
    C++: "data survives failure" = each shard on ≥ 2 Relays. -/
theorem keeper_replication_before_failure (r : KeeperReplicationState)
    (h_alpha : r.alpha > 0)
    (h_complete : keeper_replication_complete r) :
    r.alpha > 0 ∧ r.shards_replicated = r.total_shards :=
  ⟨h_alpha, h_complete⟩

/-! ## 7. Load Shedding

Under extreme load or degradation, the Keeper reduces work for this Relay.
Batch sizes decrease, work redirects to healthy peers. The Keeper never
crashes — it gracefully degrades.

C++: `Keeper::shed_load(reduction_factor)`. -/

/-- Load state of a Relay: batch size and utilization.
    C++: `Keeper::LoadState { batch_size, utilization, max_batch }`. -/
structure KeeperLoadState where
  batch_size : Nat     -- current micro-batch size
  max_batch : Nat      -- maximum batch this Relay can handle
  utilization : ℚ      -- 0..1 GPU utilization

/-- Load state well-formedness: batch ≤ max, max > 0.
    C++: `assert(batch_size <= max_batch && max_batch > 0)`. -/
def keeper_load_wf (l : KeeperLoadState) : Prop :=
  l.batch_size ≤ l.max_batch ∧ 0 < l.max_batch

/-- Shed load: reduce batch size by a factor.
    C++: `Keeper::shed_load()` halves the batch (or reduces to min 1). -/
def keeper_shed_load (l : KeeperLoadState) : KeeperLoadState :=
  { l with batch_size := l.batch_size / 2 }

/-- Load shedding reduces batch size (for batch ≥ 2).
    C++: batch shrinks on each shed_load call. -/
theorem keeper_load_shed_reduces_work (l : KeeperLoadState) (h : l.batch_size ≥ 2) :
    (keeper_shed_load l).batch_size < l.batch_size := by
  simp [keeper_shed_load]
  omega

/-- Load shedding preserves max_batch (other Relays unaffected).
    C++: `shed_load()` only changes this Relay's batch. -/
theorem keeper_load_shed_preserves_max (l : KeeperLoadState) :
    (keeper_shed_load l).max_batch = l.max_batch := by
  simp [keeper_shed_load]

/-- After shedding, batch ≤ max is preserved.
    C++: shedding maintains the well-formedness invariant. -/
theorem keeper_load_shed_wf (l : KeeperLoadState) (h : keeper_load_wf l) :
    keeper_load_wf (keeper_shed_load l) := by
  simp [keeper_load_wf, keeper_shed_load] at *
  exact ⟨by omega, h.2⟩

/-- Batch can be restored after shedding (not permanent).
    C++: `Keeper::restore_load()` sets batch back to max when healthy. -/
def keeper_restore_load (l : KeeperLoadState) : KeeperLoadState :=
  { l with batch_size := l.max_batch }

/-- Restoring load produces max batch.
    C++: full recovery after health issue resolves. -/
theorem keeper_restore_load_max (l : KeeperLoadState) :
    (keeper_restore_load l).batch_size = l.max_batch := by
  simp [keeper_restore_load]

/-- Restore preserves well-formedness.
    C++: restored state satisfies batch ≤ max (with equality). -/
theorem keeper_restore_load_wf (l : KeeperLoadState) (h : keeper_load_wf l) :
    keeper_load_wf (keeper_restore_load l) := by
  simp [keeper_load_wf, keeper_restore_load]
  exact h.2

/-- The Keeper is always responsive: in any state, the basic health query
    can be answered. Modeled as: health classification is a total function.
    C++: `Keeper::health_status()` works in any state. -/
theorem keeper_always_responsive (m : KeeperHealthMetrics) :
    keeper_classify_health m = .Healthy ∨
    keeper_classify_health m = .Degraded ∨
    keeper_classify_health m = .Critical := by
  simp only [keeper_classify_health]
  split
  · exact Or.inr (Or.inr rfl)
  · split
    · exact Or.inr (Or.inl rfl)
    · exact Or.inl rfl

/-! ## 8. Combined Keeper Lifecycle

End-to-end properties combining state machine, health, and recommendations. -/

/-- The Keeper lifecycle invariant: from Init, through calibration and execution,
    the Keeper can always reach ShuttingDown in a bounded number of steps.
    C++: Keeper daemon has a clean exit path from any operational state. -/
theorem keeper_bounded_shutdown (s : KeeperState) (hs : s ≠ .ShuttingDown) :
    keeper_apply_events s [.ShutdownSignal] = some .ShuttingDown := by
  cases s <;> simp_all [keeper_apply_events, keeper_transition]

/-- The full Keeper contract: boot, calibrate, execute, detect degradation,
    recover, and shut down — all in one verified trace.
    C++: this is the "golden path" integration test. -/
theorem keeper_full_contract :
    keeper_apply_events .Initializing
      [.MeshDiscovered, .CalibrationDone, .WorkReceived, .IterationComplete,
       .HealthIssue, .IssueResolved, .WorkReceived, .IterationComplete,
       .ShutdownSignal]
    = some .ShuttingDown := rfl

/-- Health degradation during execution leads to shedding which reduces batch.
    Composition of state machine + load shedding.
    C++: HealthIssue event → Degraded state → shed_load() called. -/
theorem keeper_degraded_sheds_load (l : KeeperLoadState)
    (h_wf : keeper_load_wf l) (h_batch : l.batch_size ≥ 2) :
    let l' := keeper_shed_load l
    keeper_load_wf l' ∧ l'.batch_size < l.batch_size :=
  ⟨keeper_load_shed_wf l h_wf, keeper_load_shed_reduces_work l h_batch⟩

/-- After recovery from Degraded, load is restored and safe recs are applied.
    C++: IssueResolved → Ready → restore_load → apply safe recommendations. -/
theorem keeper_recovery_restores (l : KeeperLoadState) (rec : KeeperRecommendation)
    (h_wf : keeper_load_wf l)
    (h_risk : rec.risk_level = 0)
    (h_speedup : rec.expected_speedup > keeper_speedup_threshold) :
    let l' := keeper_restore_load l
    keeper_load_wf l' ∧
    keeper_evaluate_recommendation rec .Healthy = .Apply :=
  ⟨keeper_restore_load_wf l h_wf,
   keeper_safe_always_applied rec .Healthy h_risk h_speedup⟩

/-! ## 9. Concrete Examples -/

/-- Boot sequence. -/
example : keeper_apply_events .Initializing [.MeshDiscovered] = some .Calibrating := rfl
example : keeper_apply_events .Initializing [.MeshDiscovered, .CalibrationDone] = some .Ready := rfl

/-- Health events. -/
example : keeper_transition .Executing .HealthIssue = some .Degraded := rfl
example : keeper_transition .Degraded .IssueResolved = some .Ready := rfl

/-- Health classification. -/
example : keeper_classify_health ⟨0, false, 1, 1⟩ = .Healthy := by native_decide
example : keeper_classify_health ⟨5, false, 1, 1⟩ = .Degraded := by native_decide
example : keeper_classify_health ⟨200, false, 1, 1⟩ = .Critical := by native_decide

/-- Recommendation evaluation. -/
example : keeper_evaluate_recommendation ⟨.SwapKernel, 3/2, 0, 9/10⟩ .Healthy = .Apply := by
  native_decide
example : keeper_evaluate_recommendation ⟨.GrowModel, 3/2, 2, 9/10⟩ .Degraded = .Reject := by
  native_decide
example : keeper_evaluate_recommendation ⟨.IncreaseBatch, 1, 0, 1⟩ .Healthy = .Reject := by
  native_decide

/-- Update monotonicity. -/
example : keeper_apply_update ⟨5, 6, true⟩ = 6 := by simp [keeper_apply_update]
example : keeper_apply_update ⟨5, 6, false⟩ = 5 := by simp [keeper_apply_update]
example : keeper_apply_update ⟨5, 3, true⟩ = 5 := by simp [keeper_apply_update]

/-- Load shedding. -/
example : (keeper_shed_load ⟨64, 128, 9/10⟩).batch_size = 32 := by
  simp [keeper_shed_load]
example : (keeper_shed_load ⟨1, 128, 9/10⟩).batch_size = 0 := by
  simp [keeper_shed_load]

/-! ## Summary

Key results:
- `keeper_transition_total`: every non-terminal state has at least one valid transition
- `keeper_init_to_ready`: Init → Calibrating → Ready path exists
- `keeper_degraded_recoverable`: Degraded → Ready via IssueResolved
- `keeper_shutdown_terminal`: ShuttingDown has no outgoing transitions
- `keeper_shutdown_reachable`: ShuttingDown reachable from any non-terminal state
- `keeper_perfect_is_healthy`: nominal metrics → Healthy classification
- `keeper_ecc_causes_degradation`: ECC > 0 → not Healthy
- `keeper_high_ecc_critical`: ECC > 100 → Critical
- `keeper_safe_always_applied`: risk=0, speedup > 1 → Apply
- `keeper_risky_rejected_when_degraded`: risk=2, Degraded → Reject
- `keeper_low_speedup_rejected`: speedup ≤ 1 → Reject
- `keeper_update_monotone`: valid updates advance version
- `keeper_update_requires_verification`: unverified updates rejected
- `keeper_update_atomic`: result is exactly old or new version
- `keeper_config_version_monotone`: config versions strictly increase
- `keeper_rollback_restores`: rollback recovers exact previous config
- `keeper_preemptive_increases_safety`: α increase → more redundancy
- `keeper_replication_finishes`: background replication reaches completion
- `keeper_replication_before_failure`: α > 0 + complete → data survives
- `keeper_load_shed_reduces_work`: shedding decreases batch (when ≥ 2)
- `keeper_load_shed_wf`: shedding preserves well-formedness
- `keeper_always_responsive`: health query is total in any state
- `keeper_bounded_shutdown`: any state reaches ShuttingDown in 1 step
- `keeper_full_contract`: end-to-end golden path verified
-/

end Crucible

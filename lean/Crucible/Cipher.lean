import Mathlib.Tactic

/-!
# Crucible.Cipher -- Persistent State (Event-Sourced Object Store)

Models Cipher.h: git-like content-addressed object store for Merkle DAG nodes.

Directory layout (C++):
  $root/objects/<first2hex>/<remaining14hex>  -- one file per node
  $root/HEAD                                  -- hex string: current active hash
  $root/log                                   -- append-only: "step_id,hash_hex,ts_ns"

The Cipher is Crucible's persistent state system:
- DAG chain (computation graph versions)
- Weight snapshots (model parameters)
- KernelCache (compiled kernels)
- RNG state (Philox master counter)

Event-sourced: the log records (step_id, content_hash, timestamp) triples.
Recovery = load latest snapshot + replay events from that point.

Three storage tiers (C++ design):
- Hot:  other Relays' RAM (from RAID redundancy). Zero-cost recovery from single node failure.
- Warm: local NVMe per Relay. Recovery from reboot: seconds.
- Cold: durable storage (S3/GCS). Recovery from total cluster failure: minutes.

Key specification properties modeled here:
1. Idempotent writes: store() is a no-op if content_hash already exists
2. Append-only log: entries only grow, never deleted
3. Step monotonicity: step_ids in the log are strictly increasing
4. HEAD consistency: HEAD always equals the last committed hash
5. Time travel: hash_at_step(N) returns the latest entry at or before step N
6. Deterministic replay: same snapshot + same events = same state
7. Content addressing: identical computation produces identical hash
-/

namespace Crucible

/-! ## Content Hash

Models C++: `ContentHash` (uint64_t wrapper via `CRUCIBLE_STRONG_HASH`).
Zero is the empty/sentinel value. -/

/-- Content hash identifying a DAG node's computation.
    C++: `ContentHash` -- strong uint64_t wrapper.
    Zero = empty/sentinel (KernelCache empty-slot marker).
    We use Nat for specification (C++ uses uint64_t). -/
structure CipherHash where
  val : Nat
  deriving DecidableEq, Repr

instance : BEq CipherHash where
  beq a b := a.val == b.val

@[simp] theorem CipherHash.beq_iff (a b : CipherHash) : (a == b) = (a.val == b.val) := rfl

/-- The empty/sentinel content hash. C++: `ContentHash{}` (zero-initialized). -/
def CipherHash.empty : CipherHash := { val := 0 }

/-- Whether a CipherHash is non-empty (valid). C++: `operator bool()`. -/
def CipherHash.valid (h : CipherHash) : Bool := h.val != 0

/-! ## Log Entry

Models C++: `Cipher::LogEntry` (private struct):
  struct LogEntry {
    uint64_t    step_id;
    ContentHash hash;
    uint64_t    ts_ns;
  };

The log is append-only. Steps are monotonically increasing.
Timestamps are monotonically increasing (from steady_clock). -/

/-- A single entry in the Cipher's append-only log.
    C++: `Cipher::LogEntry { step_id, hash, ts_ns }`.
    Written to `$root/log` as: "step_id,hash_hex,ts_ns\n". -/
structure CipherLogEntry where
  step_id : Nat         -- C++: uint64_t step_id
  hash    : CipherHash  -- C++: ContentHash hash
  ts_ns   : Nat         -- C++: uint64_t ts_ns (steady_clock nanoseconds)
  deriving DecidableEq, Repr

/-! ## Storage Tier

Models the three-tier Cipher storage hierarchy from the design doc. -/

/-- Storage tier for Cipher state. Each tier trades latency for durability.
    C++ design doc (L13): hot/warm/cold tiers for different recovery scenarios. -/
inductive StorageTier where
  | Hot   -- other Relays' RAM (RAID redundancy). Recovery: ~0ms.
  | Warm  -- local NVMe per Relay. Recovery: seconds.
  | Cold  -- durable storage (S3/GCS). Recovery: minutes.
  deriving DecidableEq, Repr

/-! ## Object Store

Models the content-addressed object store: `$root/objects/<shard>/<hash>`.
Pure function from CipherHash to optional serialized bytes.
Idempotent writes: store() is a no-op if the hash already exists. -/

/-- Content-addressed object store. Pure model of the filesystem layout.
    C++: `obj_path(hash)` maps to `$root/objects/<first2hex>/<remaining14hex>`.
    The store is append-only: objects are never overwritten or deleted. -/
structure ObjectStore where
  objects : CipherHash → Option Nat  -- hash → serialized region id (abstract)

/-- Empty object store. No objects stored yet. -/
def ObjectStore.empty : ObjectStore :=
  { objects := fun _ => none }

/-- Store an object. Idempotent: if the hash already exists, no change.
    C++: `if (std::filesystem::exists(path)) return hash;` -/
def ObjectStore.store (s : ObjectStore) (h : CipherHash) (data : Nat) : ObjectStore :=
  match s.objects h with
  | some _ => s  -- already exists, idempotent
  | none   => { objects := fun h' => if h'.val = h.val then some data else s.objects h' }

/-- Lookup an object by content hash.
    C++: `Cipher::load(content_hash)` reads the file at `obj_path(content_hash)`. -/
def ObjectStore.lookup (s : ObjectStore) (h : CipherHash) : Option Nat :=
  s.objects h

/-! ## Cipher State

Models the complete Cipher state: HEAD pointer, append-only log, object store.

C++ struct layout:
  std::string            root_;
  ContentHash            head_{};
  std::vector<LogEntry>  log_;

The log is the authoritative event source. HEAD is a convenience pointer
to the latest committed hash. -/

/-- The Cipher: content-addressed object store + append-only event log.
    C++: `class Cipher` (Cipher.h). Single-threaded, owned by foreground.

    Invariants (enforced by `WellFormed`):
    - `head` = last log entry's hash (or empty if log is empty)
    - log step_ids are strictly monotonically increasing
    - every log entry's hash exists in the object store -/
structure Cipher where
  head    : CipherHash       -- C++: head_ (current active hash)
  log     : List CipherLogEntry  -- C++: log_ (in-memory log, loaded from disk)
  store   : ObjectStore      -- C++: objects/ directory (content-addressed)

/-- Empty Cipher. C++: default-constructed `Cipher()`. -/
def Cipher.empty : Cipher :=
  { head := CipherHash.empty, log := [], store := ObjectStore.empty }

/-! ## Step Monotonicity

The log's step_ids are strictly monotonically increasing.
C++: `hash_at_step()` relies on this for binary search correctness. -/

/-- Step IDs in a log are strictly increasing.
    C++: binary search in `hash_at_step()` assumes monotonicity. -/
def StepsIncreasing : List CipherLogEntry → Prop
  | [] => True
  | [_] => True
  | e₁ :: e₂ :: rest => e₁.step_id < e₂.step_id ∧ StepsIncreasing (e₂ :: rest)

/-! ## Well-Formedness

The key invariants that the C++ Cipher maintains across all operations. -/

/-- HEAD consistency: HEAD equals the last log entry's hash, or empty if log is empty.
    C++: `advance_head()` always sets `head_ = content_hash` AND appends to log.
    On load: HEAD file overrides log, but for a well-formed Cipher they agree. -/
def HeadConsistent (c : Cipher) : Prop :=
  match c.log.getLast? with
  | none   => c.head = CipherHash.empty
  | some e => c.head = e.hash

/-- Every log entry's hash exists in the object store.
    C++: `store()` is called before `advance_head()`, so the object
    is always persisted before the log records it. -/
def LogObjectsExist (c : Cipher) : Prop :=
  ∀ e ∈ c.log, (c.store.objects e.hash).isSome

/-- Well-formedness: all Cipher invariants hold simultaneously.
    A Cipher produced by the public API always satisfies these. -/
structure Cipher.WellFormed (c : Cipher) : Prop where
  head_consistent : HeadConsistent c
  steps_increasing : StepsIncreasing c.log
  log_objects_exist : LogObjectsExist c

/-! ## Operations

### advance_head

C++: `void advance_head(ContentHash content_hash, uint64_t step_id)`.
Updates HEAD, appends log entry, writes HEAD file and log line.

Precondition: the object must already be stored (caller calls store() first).
Precondition: step_id > all existing step_ids (monotonicity). -/

/-- Advance HEAD to a new content hash at the given step.
    C++: `Cipher::advance_head(content_hash, step_id)`.
    The timestamp is provided externally (C++ uses `steady_clock::now()`). -/
def Cipher.advanceHead (c : Cipher) (h : CipherHash) (step_id ts : Nat) : Cipher :=
  let entry : CipherLogEntry := { step_id := step_id, hash := h, ts_ns := ts }
  { head := h
    log := c.log ++ [entry]
    store := c.store }

/-! ### hash_at_step (Time Travel)

C++: `ContentHash hash_at_step(uint64_t step_id) const`.
Binary-searches the in-memory log for the last entry with step_id <= target.
Returns ContentHash{} if no such entry exists.

We model the search as a fold over the log, keeping the last matching entry.
This is equivalent to the C++ binary search for a monotonic sequence. -/

/-- Find the last log entry with step_id <= target via a fold.
    Helper that accumulates the last matching entry. -/
def hashAtStepAux (target : Nat) : List CipherLogEntry → CipherHash → CipherHash
  | [], acc => acc
  | e :: rest, acc =>
    if e.step_id ≤ target then hashAtStepAux target rest e.hash
    else hashAtStepAux target rest acc

/-- Find the last log entry with step_id <= target. Models C++ binary search.
    C++ algorithm: find last entry where `log_[mid].step_id <= step_id`.
    Returns the hash of that entry, or CipherHash.empty if none found. -/
def Cipher.hashAtStep (c : Cipher) (target : Nat) : CipherHash :=
  hashAtStepAux target c.log CipherHash.empty

/-! ### store (Object Storage)

C++: `ContentHash store(const RegionNode* region, const MetaLog* meta_log)`.
Serializes the region and writes to `$root/objects/<shard>/<hash>`.
Idempotent: if the file already exists, same bytes, skip write. -/

/-- Store an object in the Cipher's object store.
    C++: `Cipher::store(region, meta_log)`.
    Returns the updated Cipher with the object in the store. -/
def Cipher.storeObject (c : Cipher) (h : CipherHash) (data : Nat) : Cipher :=
  { c with store := c.store.store h data }

/-! ### Queries

C++: `head()`, `empty()`, `root()`. -/

/-- Whether the Cipher has any committed state.
    C++: `bool empty() const { return !head_; }`. -/
def Cipher.isEmpty (c : Cipher) : Bool := !c.head.valid

/-! ## Event Sourcing

The Cipher is event-sourced: the log records events (step_id, hash, ts),
and the current state can be reconstructed by replaying events from a snapshot.

This section models the fundamental event-sourcing property:
applying a sequence of events to an initial state produces the same
final state regardless of intermediate snapshots. -/

/-- Apply a sequence of (hash, step_id, ts) events to a Cipher.
    Models replaying the log from a snapshot to reconstruct current state. -/
def Cipher.applyEvents (c : Cipher) : List (CipherHash × Nat × Nat) → Cipher
  | [] => c
  | (h, step, ts) :: rest => (c.advanceHead h step ts).applyEvents rest

/-! ## Properties -/

/-- Empty Cipher is well-formed. -/
theorem Cipher.empty_wf : Cipher.empty.WellFormed where
  head_consistent := by simp [HeadConsistent, Cipher.empty]
  steps_increasing := by simp [StepsIncreasing, Cipher.empty]
  log_objects_exist := by simp [LogObjectsExist, Cipher.empty]

/-- Empty Cipher is empty. -/
theorem Cipher.empty_isEmpty : Cipher.empty.isEmpty = true := by
  simp [Cipher.isEmpty, CipherHash.valid, Cipher.empty, CipherHash.empty]

/-- Store is idempotent: storing the same hash twice is the same as once.
    C++: `if (std::filesystem::exists(path)) return hash;` -/
theorem ObjectStore.store_idempotent (s : ObjectStore) (h : CipherHash) (d₁ d₂ : Nat) :
    (s.store h d₁).store h d₂ = s.store h d₁ := by
  unfold ObjectStore.store
  cases hs : s.objects h with
  | some v => simp [hs]
  | none => simp

/-- Store then lookup returns the stored value.
    C++: `load(content_hash)` returns what `store()` wrote. -/
theorem ObjectStore.store_lookup (s : ObjectStore) (h : CipherHash) (d : Nat)
    (hNew : s.objects h = none) :
    (s.store h d).lookup h = some d := by
  simp [ObjectStore.store, hNew, ObjectStore.lookup]

/-- Store does not affect lookup of other hashes.
    Content addressing: each hash maps to exactly one object. -/
theorem ObjectStore.store_lookup_other (s : ObjectStore) (h₁ h₂ : CipherHash)
    (d : Nat) (hne : h₁ ≠ h₂) :
    (s.store h₁ d).lookup h₂ = s.lookup h₂ := by
  unfold ObjectStore.store ObjectStore.lookup
  split
  · rfl
  · have : h₂.val ≠ h₁.val := by
      intro heq
      apply hne
      cases h₁; cases h₂; simp_all
    simp [this]

/-- advanceHead sets HEAD to the given hash.
    C++: `head_ = content_hash;` -/
theorem Cipher.advanceHead_head (c : Cipher) (h : CipherHash) (step ts : Nat) :
    (c.advanceHead h step ts).head = h := by
  simp [Cipher.advanceHead]

/-- advanceHead appends exactly one entry to the log.
    C++: `log_.push_back({...});` -/
theorem Cipher.advanceHead_log_length (c : Cipher) (h : CipherHash) (step ts : Nat) :
    (c.advanceHead h step ts).log.length = c.log.length + 1 := by
  simp [Cipher.advanceHead, List.length_append]

/-- advanceHead preserves the object store.
    C++: `advance_head()` does not call `store()`. -/
theorem Cipher.advanceHead_store (c : Cipher) (h : CipherHash) (step ts : Nat) :
    (c.advanceHead h step ts).store = c.store := by
  simp [Cipher.advanceHead]

/-- storeObject preserves HEAD and log.
    C++: `store()` does not modify `head_` or `log_`. -/
theorem Cipher.storeObject_head (c : Cipher) (h : CipherHash) (d : Nat) :
    (c.storeObject h d).head = c.head ∧ (c.storeObject h d).log = c.log := by
  simp [Cipher.storeObject]

/-- hashAtStep on an empty log returns empty.
    C++: `if (log_.empty()) return ContentHash{};` -/
theorem Cipher.hashAtStep_empty (target : Nat) :
    Cipher.empty.hashAtStep target = CipherHash.empty := by
  simp [Cipher.hashAtStep, Cipher.empty, hashAtStepAux]

/-- hashAtStep for a step before any log entry returns empty.
    C++: binary search finds no entry with step_id <= target. -/
theorem Cipher.hashAtStep_before_all (c : Cipher) (target : Nat)
    (hBefore : ∀ e ∈ c.log, target < e.step_id) :
    c.hashAtStep target = CipherHash.empty := by
  unfold Cipher.hashAtStep
  suffices ∀ (log : List CipherLogEntry) (acc : CipherHash),
    (∀ e ∈ log, target < e.step_id) → acc = CipherHash.empty →
    hashAtStepAux target log acc = CipherHash.empty from
    this c.log CipherHash.empty hBefore rfl
  intro log
  induction log with
  | nil => intros _ _ hacc; simp [hashAtStepAux, hacc]
  | cons x rest ih =>
    intro acc hAll hacc
    simp [hashAtStepAux]
    have hx := hAll x (by simp)
    have : ¬(x.step_id ≤ target) := by omega
    simp [this]
    exact ih acc (fun e he => hAll e (List.mem_cons_of_mem x he)) hacc

/-- hashAtStep after a single advance returns that hash.
    C++: log has one entry, step_id <= target succeeds. -/
theorem Cipher.hashAtStep_single (h : CipherHash) (step ts target : Nat)
    (hGe : step ≤ target) :
    (Cipher.empty.advanceHead h step ts).hashAtStep target = h := by
  simp [Cipher.hashAtStep, Cipher.advanceHead, Cipher.empty, hashAtStepAux, hGe]

/-- Applying zero events is identity. -/
theorem Cipher.applyEvents_nil (c : Cipher) :
    c.applyEvents [] = c := by
  simp [Cipher.applyEvents]

/-- Applying events is sequential: each event advances HEAD. -/
theorem Cipher.applyEvents_cons (c : Cipher) (h : CipherHash) (step ts : Nat)
    (rest : List (CipherHash × Nat × Nat)) :
    c.applyEvents ((h, step, ts) :: rest) =
      (c.advanceHead h step ts).applyEvents rest := by
  simp [Cipher.applyEvents]

/-- After applying events, HEAD equals the last event's hash.
    This is the fundamental event-sourcing consistency property:
    replaying events produces the correct final state. -/
theorem Cipher.applyEvents_head_last (c : Cipher)
    (events : List (CipherHash × Nat × Nat)) (h : CipherHash)
    (step ts : Nat) :
    (c.applyEvents (events ++ [(h, step, ts)])).head = h := by
  induction events generalizing c with
  | nil => simp [Cipher.applyEvents, Cipher.advanceHead]
  | cons e rest ih =>
    simp only [List.cons_append, Cipher.applyEvents]
    exact ih (c.advanceHead e.1 e.2.1 e.2.2)

/-- Replaying from empty produces the same state as replaying from a snapshot
    at the boundary, provided the same events are applied after the snapshot.
    This is event-sourcing's key property: snapshot + tail = full replay. -/
theorem Cipher.replay_from_snapshot (c : Cipher)
    (pfx sfx : List (CipherHash × Nat × Nat)) :
    (c.applyEvents (pfx ++ sfx)).head =
      ((c.applyEvents pfx).applyEvents sfx).head := by
  induction pfx generalizing c with
  | nil => simp [Cipher.applyEvents]
  | cons e rest ih =>
    simp only [List.cons_append, Cipher.applyEvents]
    exact ih (c.advanceHead e.1 e.2.1 e.2.2)

/-- Cipher operations are deterministic: same inputs produce same outputs.
    C++: no mutable global state, no hardware-dependent behavior.
    This is the foundation of DetSafe for the persistence layer. -/
theorem Cipher.advanceHead_det (c : Cipher) (h : CipherHash) (step ts : Nat) :
    ∀ r₁ r₂,
      c.advanceHead h step ts = r₁ →
      c.advanceHead h step ts = r₂ →
      r₁ = r₂ := by
  intros r₁ r₂ h₁ h₂; rw [← h₁, ← h₂]

/-- Log grows monotonically: advanceHead never removes entries.
    C++: the log is append-only, entries are never deleted. -/
theorem Cipher.advanceHead_log_grows (c : Cipher) (h : CipherHash) (step ts : Nat) :
    c.log.length ≤ (c.advanceHead h step ts).log.length := by
  simp [Cipher.advanceHead, List.length_append]

/-- storeObject is idempotent on the object store.
    Storing the same hash twice does not change the Cipher's store. -/
theorem Cipher.storeObject_idempotent (c : Cipher) (h : CipherHash) (d₁ d₂ : Nat) :
    (c.storeObject h d₁).storeObject h d₂ = c.storeObject h d₁ := by
  simp [Cipher.storeObject]
  exact ObjectStore.store_idempotent c.store h d₁ d₂

/-! ## Version Monotonicity

The Cipher's log entries have strictly increasing step_ids.
This is essential for the correctness of hash_at_step's binary search. -/

/-- StepsIncreasing is preserved when appending an entry with a larger step_id. -/
theorem StepsIncreasing_append_single (log : List CipherLogEntry) (e : CipherLogEntry)
    (hMono : StepsIncreasing log)
    (hGt : ∀ e' ∈ log, e'.step_id < e.step_id) :
    StepsIncreasing (log ++ [e]) := by
  induction log with
  | nil => simp [StepsIncreasing]
  | cons x rest ih =>
    cases rest with
    | nil =>
      simp only [List.nil_append, List.cons_append]
      exact ⟨hGt x (by simp), trivial⟩
    | cons y ys =>
      simp only [List.cons_append] at *
      simp [StepsIncreasing] at hMono
      constructor
      · exact hMono.1
      · exact ih hMono.2 (fun e' he' => hGt e' (List.mem_cons_of_mem x he'))

/-! ## Recovery Model

Models the three-tier recovery strategy from the design doc (L13).
Recovery priority: Hot > Warm > Cold.
Each tier is a Cipher with potentially different lag. -/

/-- Three-tier Cipher state. Models the distributed persistence from L13.
    Hot: zero-lag (RAID across Relays).
    Warm: snapshot-lag (local NVMe, periodic snapshots).
    Cold: archive-lag (S3/GCS, less frequent snapshots). -/
structure TieredCipher where
  hot  : Cipher   -- other Relays' RAM
  warm : Cipher   -- local NVMe
  cold : Cipher   -- S3/GCS

/-- Recovery: pick the most up-to-date available tier.
    C++ design: try hot first (zero cost), then warm (seconds),
    then cold (minutes). -/
def TieredCipher.recover (tc : TieredCipher) : Cipher :=
  if tc.hot.head.valid then tc.hot
  else if tc.warm.head.valid then tc.warm
  else tc.cold

/-- Recovery always returns one of the three tiers. -/
theorem TieredCipher.recover_is_tier (tc : TieredCipher) :
    tc.recover = tc.hot ∨ tc.recover = tc.warm ∨ tc.recover = tc.cold := by
  unfold TieredCipher.recover
  split
  · exact Or.inl rfl
  · split
    · exact Or.inr (Or.inl rfl)
    · exact Or.inr (Or.inr rfl)

/-- If hot tier is available, recovery uses it (fastest path).
    C++: hot tier = other Relays' RAM, ~0ms recovery. -/
theorem TieredCipher.recover_hot_priority (tc : TieredCipher)
    (hHot : tc.hot.head.valid = true) :
    tc.recover = tc.hot := by
  unfold TieredCipher.recover
  simp [hHot]

end Crucible

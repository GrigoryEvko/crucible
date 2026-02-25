import Crucible.Basic

/-!
# Crucible.Ring — SPSC Ring Buffer

Backported from TraceRing.h (not invented — models the actual C++ code).

C++ struct: `struct TraceRing` (~5.25MB total, pre-allocated)
  alignas(64) atomic<uint64_t> head{0};      -- producer writes
  alignas(64) atomic<uint64_t> tail{0};      -- consumer writes
  alignas(64) uint64_t cached_tail_ = 0;     -- producer-local cache
  alignas(64) Entry entries[65536];           -- 64B entries, 4MB
  MetaIndex meta_starts[65536];              -- parallel array (256KB)
  ScopeHash scope_hashes[65536];             -- parallel array (512KB)
  CallsiteHash callsite_hashes[65536];       -- parallel array (512KB)

CAPACITY = 2^16 = 65536. MASK = CAPACITY - 1 = 0xFFFF.
Entry = 64 bytes (one cache line):
  schema_hash(8) + shape_hash(8) + num_inputs(2) + num_outputs(2)
  + num_scalar_args(2) + grad_enabled(1) + inference_mode(1)
  + scalar_values[5](40) = 64

Producer (foreground, ~5ns):
  try_append: entries[h & MASK] = e; head.store(h+1, release);
  cached_tail_ avoids cross-core atomic tail read (~20,000 appends/drain).

Consumer (background):
  drain: split into ≤2 contiguous memcpy runs for wrap-around.
  tail.store(t+count, release);

Invariants:
  tail ≤ head                     (ordering)
  head - tail ≤ CAPACITY          (capacity bound)
  entries[pos & MASK] = entries[pos % cap]  (bitmask indexing)
-/

namespace Crucible

/-- SPSC ring buffer state. Parametric in element type α.
    head/tail are logical positions (monotonically increasing).
    Physical index = pos % cap = pos &&& (cap - 1).
    buf maps physical indices to stored values. -/
structure Ring (α : Type) where
  cap   : Nat                   -- CAPACITY (power of two, 65536)
  head  : Nat                   -- next write position (producer)
  tail  : Nat                   -- next read position (consumer)
  buf   : Fin cap → Option α    -- physical storage
  hPow2 : IsPow2 cap            -- capacity is power of two
  hOrd  : tail ≤ head           -- ordering invariant
  hBnd  : head - tail ≤ cap     -- capacity invariant

/-- Number of items currently stored. C++: `head - tail` (racy diagnostic). -/
def Ring.count (r : Ring α) : Nat := r.head - r.tail

/-- Is the ring full? C++: `h - cached_tail_ >= CAPACITY` (fast path). -/
def Ring.full (r : Ring α) : Prop := r.count = r.cap

/-- Is the ring empty? C++: `h == t` in drain(). -/
def Ring.empty (r : Ring α) : Prop := r.head = r.tail

/-- Push an element (producer only). Requires ring not full.
    C++: `entries[h & MASK] = e; head.store(h+1, release);`
    Also writes parallel arrays (meta_starts, scope/callsite hashes)
    and prefetches the NEXT write slot. Elided here. -/
def Ring.push (r : Ring α) (x : α) (h : ¬r.full) : Ring α where
  cap   := r.cap
  head  := r.head + 1
  tail  := r.tail
  buf   := fun i => if i.val = r.head % r.cap then some x else r.buf i
  hPow2 := r.hPow2
  hOrd  := by have := r.hOrd; omega
  hBnd  := by
    have := r.hBnd; have := r.hOrd
    simp only [Ring.full, Ring.count] at h
    omega

/-- Pop an element (consumer only). Requires ring not empty.
    C++: drain() reads entries[t & MASK], advances tail.
    Returns (element, new ring state). In C++ the drain reads a batch,
    not single elements — this models the atomic unit of one pop. -/
def Ring.pop [Inhabited α] (r : Ring α) (h : ¬r.empty) : α × Ring α :=
  let idx : Fin r.cap := ⟨r.tail % r.cap, Nat.mod_lt _ r.hPow2.pos⟩
  let elem := (r.buf idx).getD default
  (elem,
   { cap   := r.cap
     head  := r.head
     tail  := r.tail + 1
     buf   := fun i => if i = idx then none else r.buf i
     hPow2 := r.hPow2
     hOrd  := by
       have := r.hOrd; simp only [Ring.empty] at h; omega
     hBnd  := by have := r.hBnd; have := r.hOrd; omega })

/-! ## Invariant Preservation -/

/-- Push preserves ordering: tail ≤ head. -/
theorem Ring.push_ord (r : Ring α) (x : α) (h : ¬r.full) :
    (r.push x h).tail ≤ (r.push x h).head := by
  simp only [Ring.push]; have := r.hOrd; omega

/-- Push increases count by exactly 1. -/
theorem Ring.push_count (r : Ring α) (x : α) (h : ¬r.full) :
    (r.push x h).count = r.count + 1 := by
  simp only [Ring.push, Ring.count]; have := r.hOrd; omega

/-- Pop decreases count by exactly 1. -/
theorem Ring.pop_count [Inhabited α] (r : Ring α) (h : ¬r.empty) :
    (r.pop h).2.count = r.count - 1 := by
  simp only [Ring.pop, Ring.count]
  have := r.hOrd; simp only [Ring.empty] at h; omega

/-- Push then pop on an empty ring returns the pushed element.
    FIFO property (base case). -/
theorem Ring.push_pop_identity [DecidableEq α] [Inhabited α]
    (r : Ring α) (x : α) (hf : ¬r.full) (he : r.empty) :
    let r' := r.push x hf
    have hne : ¬r'.empty := by
      simp only [Ring.empty, Ring.push, r']
      simp only [Ring.empty] at he; omega
    let (y, _) := r'.pop hne
    y = x := by
  -- When empty: head = tail, so push writes at head%cap,
  -- pop reads at tail%cap = head%cap. Same physical slot.
  simp only [Ring.empty] at he
  simp only [Ring.push, Ring.pop]
  simp only [he, ite_true, Option.getD_some]

/-- Ring never exceeds capacity (structural invariant). -/
theorem Ring.count_le_cap (r : Ring α) : r.count ≤ r.cap := r.hBnd

/-! ## Physical Indexing Correctness

    C++: `entries[head & MASK]` where MASK = CAPACITY - 1.
    Bitmask indexing produces valid array index in [0, cap).
    Two distinct live positions never alias to the same slot. -/

/-- Physical index is always a valid array index.
    C++: `static_cast<uint32_t>(h) & MASK`. -/
theorem Ring.phys_idx_valid (r : Ring α) (pos : Nat) :
    pos % r.cap < r.cap := Nat.mod_lt pos r.hPow2.pos

/-- Two distinct positions in [0, cap) map to distinct physical slots.
    Prevents data corruption: entries[i & MASK] ≠ entries[j & MASK]
    when i ≠ j and both are in the live range [tail, head). -/
theorem Ring.no_alias (cap : Nat) (hcap : IsPow2 cap)
    (i j : Nat) (hi : i < cap) (hj : j < cap) (hne : i ≠ j) :
    i % cap ≠ j % cap := by
  simp [Nat.mod_eq_of_lt hi, Nat.mod_eq_of_lt hj, hne]

/-! ## Drain: Batch Consumer with Wrap-Around Split

    C++: drain() splits the read into ≤2 contiguous memcpy runs:
      start = t & MASK
      first = min(count, CAPACITY - start)   -- [start, start+first)
      second = count - first                  -- [0, second)
    This avoids per-element copies and leverages hardware prefetch. -/

/-- Drain split covers exactly count elements (no data loss). -/
theorem drain_split_complete (start count cap : Nat) (hcap : 0 < cap)
    (hcount : count ≤ cap) (hstart : start < cap) :
    let first := min count (cap - start)
    let second := count - first
    first + second = count := by
  omega

/-- First run never exceeds remaining slots to end of buffer. -/
theorem drain_first_run_bound (start count cap : Nat) (hstart : start < cap) :
    min count (cap - start) + start ≤ cap := by
  omega

end Crucible

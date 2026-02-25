import Crucible.Basic
import Crucible.MemoryPlan

/-!
# Crucible.Pool -- PoolAllocator Formalization

Backported from PoolAllocator.h (32 bytes in C++).

C++ struct: `PoolAllocator` (32 bytes: 2 ptrs + u64 + 2 x u32)
  void* pool_;             -- one big 256-byte-aligned allocation
  void** ptr_table_;       -- SlotId -> data pointer (pre-built)
  uint64_t pool_bytes_;    -- pool size
  uint32_t num_slots_;     -- table size
  uint32_t num_external_;  -- external slot count (for diagnostics)

Hot path: `slot_ptr(SlotId)` = single 8-byte load from ptr_table_.
Cold path: `init(MemoryPlan*)` builds the table once per plan.

256-byte alignment: matches compute_memory_plan() in BackgroundThread.h.
Critical for CUDA coalescing and AVX-512 vector loads.

External slots (params, data loader outputs) are registered separately
via register_external() -- their memory is owned elsewhere. Internal
slots point into the pool at `base + offset_bytes`.

Key invariants modeled here:
  1. All internal slot offsets are 256-aligned
  2. All internal slots fit within pool bounds (offset <= pool_bytes)
  3. Slot lookup is total for valid SlotIds (i < num_slots)
  4. External slots have no pool offset (modeled as none)
  5. Init from a valid MemoryPlan produces a valid Pool
-/

namespace Crucible

/-! ## Constants -/

/-- Pool alignment: 256 bytes.
    C++: `static constexpr uint32_t ALIGNMENT = 256;`
    = 2^8. Critical for CUDA coalescing and AVX-512 vector loads. -/
def poolAlignment : Nat := 256

/-- Pool alignment is a power of two. Used by bitmask_eq_mod and alignUp. -/
theorem poolAlignment_isPow2 : IsPow2 poolAlignment :=
  ⟨8, rfl⟩

/-- Pool alignment is positive. -/
theorem poolAlignment_pos : 0 < poolAlignment := by decide

/-! ## Pool Slot Entry -/

/-- A single entry in the pointer table.
    Models one element of `ptr_table_[sid]` in PoolAllocator.

    Internal slots: `internal offset` where offset is the byte offset from
    pool base. C++: `ptr_table_[s] = base + plan->slots[s].offset_bytes`.
    External slots: `external` -- memory is owned elsewhere, registered via
    `register_external()` before replay. C++: starts as nullptr (calloc). -/
inductive PoolEntry where
  | internal (offset : Nat) : PoolEntry
  | external : PoolEntry
  deriving DecidableEq

/-- True when the entry is external. -/
def PoolEntry.isExternal : PoolEntry -> Bool
  | .internal _ => false
  | .external => true

/-! ## Pool Structure -/

/-- PoolAllocator state after init().
    Models the initialized allocator with its pointer table fully built
    from a MemoryPlan.

    `poolBytes` = total pool size (one contiguous 256B-aligned allocation).
    `table`     = the pointer table, indexed by SlotId (natural number).
    `numExternal` = count of external slots (diagnostics).

    Invariants (carried as proof fields):
    - hAligned: every internal offset is 256-aligned (CUDA coalescing).
    - hBounds:  every internal offset fits within pool_bytes.
    - hExtCount: numExternal = number of external entries in table. -/
structure Pool where
  poolBytes   : Nat
  table       : List PoolEntry
  numExternal : Nat
  hAligned    : ∀ (idx : Nat) (hidx : idx < table.length) (off : Nat),
                  table[idx]'hidx = PoolEntry.internal off ->
                  off % poolAlignment = 0
  hBounds     : ∀ (idx : Nat) (hidx : idx < table.length) (off : Nat),
                  table[idx]'hidx = PoolEntry.internal off ->
                  off ≤ poolBytes
  hExtCount   : numExternal = (table.filter PoolEntry.isExternal).length

/-- Number of slots in the table.
    C++: `num_slots_`. -/
def Pool.numSlots (p : Pool) : Nat := p.table.length

/-- Slot lookup: O(1) in C++, list index here.
    C++: `ptr_table_[sid.raw()]`.
    Returns the PoolEntry for the given SlotId (as Nat index). -/
def Pool.slotEntry (p : Pool) (sid : Nat) (h : sid < p.table.length) : PoolEntry :=
  p.table[sid]'h

/-- A Pool is initialized (non-empty table).
    C++: `is_initialized()` checks `ptr_table_ != nullptr`. -/
def Pool.isInitialized (p : Pool) : Prop := 0 < p.table.length

/-! ## Init from MemoryPlan -/

/-- Build a PoolEntry from a TensorSlot + SlotAssignment.
    Internal slots get their offset; external slots get `external`.
    C++: the loop in `PoolAllocator::init()`:
      if (!plan->slots[s].is_external)
        ptr_table_[s] = base + plan->slots[s].offset_bytes; -/
def mkPoolEntry (slot : TensorSlot) (assign : SlotAssignment) : PoolEntry :=
  if slot.is_external then PoolEntry.external
  else PoolEntry.internal assign.offset

/-- A MemoryPlan is pool-compatible iff every internal slot has a 256-aligned
    offset. This is the precondition for PoolAllocator::init().
    C++ asserts: `plan->slots[s].offset_bytes % ALIGNMENT == 0`. -/
def MemoryPlan.poolCompatible (p : MemoryPlan) : Prop :=
  ∀ (idx : Nat) (hidx : idx < p.slots.length),
    ¬(p.slots[idx]'hidx).is_external ->
    (p.assignments[idx]'(by have := p.hLen; omega)).offset % poolAlignment = 0

private theorem mkPoolEntry_internal_offset (slot : TensorSlot) (assign : SlotAssignment)
    (off : Nat) (h : mkPoolEntry slot assign = PoolEntry.internal off) :
    ¬slot.is_external ∧ assign.offset = off := by
  unfold mkPoolEntry at h
  split at h
  · exact absurd h (by simp)
  · exact ⟨by assumption, by cases h; rfl⟩

/-- Build a Pool from a valid, pool-compatible MemoryPlan.
    Models the cold path: `PoolAllocator::init(const MemoryPlan* plan)`. -/
def Pool.fromPlan (plan : MemoryPlan) (hCompat : plan.poolCompatible)
    (hValid : plan.valid) : Pool where
  poolBytes := plan.poolBytes
  table := List.ofFn (fun i : Fin plan.slots.length =>
    mkPoolEntry (plan.slots[i]) (plan.assignments[i.val]'(by have := plan.hLen; omega)))
  numExternal := (List.ofFn (fun i : Fin plan.slots.length =>
    mkPoolEntry (plan.slots[i]) (plan.assignments[i.val]'(by have := plan.hLen; omega)))
    |>.filter PoolEntry.isExternal).length
  hAligned := by
    intro idx hidx off heq
    rw [List.length_ofFn] at hidx
    rw [show (List.ofFn (fun i : Fin plan.slots.length =>
      mkPoolEntry (plan.slots[i]) (plan.assignments[i.val]'(by have := plan.hLen; omega))))[idx]'
        (by rw [List.length_ofFn]; exact hidx) =
      mkPoolEntry (plan.slots[idx]'hidx)
        (plan.assignments[idx]'(by have := plan.hLen; omega))
      from List.getElem_ofFn ..] at heq
    have ⟨hNotExt, hOff⟩ := mkPoolEntry_internal_offset _ _ _ heq
    subst hOff
    exact hCompat idx hidx hNotExt
  hBounds := by
    intro idx hidx off heq
    rw [List.length_ofFn] at hidx
    rw [show (List.ofFn (fun i : Fin plan.slots.length =>
      mkPoolEntry (plan.slots[i]) (plan.assignments[i.val]'(by have := plan.hLen; omega))))[idx]'
        (by rw [List.length_ofFn]; exact hidx) =
      mkPoolEntry (plan.slots[idx]'hidx)
        (plan.assignments[idx]'(by have := plan.hLen; omega))
      from List.getElem_ofFn ..] at heq
    have ⟨_, hOff⟩ := mkPoolEntry_internal_offset _ _ _ heq
    subst hOff
    have hi : idx < plan.assignments.length := by have := plan.hLen; omega
    -- hValid.2.1 gives ∀ i : Fin _, assignments[i].offset + ... ≤ poolBytes
    -- Extract the Nat-indexed form: offset ≤ offset + size ≤ poolBytes
    let a := plan.assignments[idx]'hi
    have hb := hValid.2.1 (⟨idx, hi⟩ : Fin plan.assignments.length)
    -- hb : plan.assignments[⟨idx, hi⟩].offset + plan.assignments[⟨idx, hi⟩].size ≤ plan.poolBytes
    -- plan.assignments[⟨idx, hi⟩] is definitionally plan.assignments[idx]'hi = a
    show a.offset ≤ plan.poolBytes
    exact le_trans (Nat.le_add_right a.offset a.size) hb
  hExtCount := rfl

/-! ## Slot Lookup Properties -/

/-- Every internal slot's pointer is 256-aligned.
    C++ assert: `plan->slots[s].offset_bytes % ALIGNMENT == 0`.
    Guarantees CUDA coalesced access from any slot_ptr() call. -/
theorem Pool.slot_aligned (p : Pool) (sid : Nat) (h : sid < p.table.length)
    (off : Nat) (heq : p.table[sid]'h = PoolEntry.internal off) :
    off % poolAlignment = 0 :=
  p.hAligned sid h off heq

/-- Every internal slot fits within the pool.
    C++ assert: `plan->slots[s].offset_bytes + plan->slots[s].nbytes <= pool_bytes`.
    We model the weaker `offset <= pool_bytes` since Pool does not track per-slot sizes. -/
theorem Pool.slot_within_bounds (p : Pool) (sid : Nat) (h : sid < p.table.length)
    (off : Nat) (heq : p.table[sid]'h = PoolEntry.internal off) :
    off ≤ p.poolBytes :=
  p.hBounds sid h off heq

/-- After init, Pool.numSlots matches the plan's slot count. -/
theorem Pool.fromPlan_numSlots (plan : MemoryPlan) (hCompat : plan.poolCompatible)
    (hValid : plan.valid) :
    (Pool.fromPlan plan hCompat hValid).numSlots = plan.slots.length := by
  simp [Pool.fromPlan, Pool.numSlots, List.length_ofFn]

/-- After init, Pool.poolBytes matches the plan's pool_bytes. -/
theorem Pool.fromPlan_poolBytes (plan : MemoryPlan) (hCompat : plan.poolCompatible)
    (hValid : plan.valid) :
    (Pool.fromPlan plan hCompat hValid).poolBytes = plan.poolBytes :=
  rfl

/-! ## Register External -/

/-- Register an external tensor's pointer (model: replace entry at index).
    C++: `register_external(SlotId sid, void* ptr)` sets
    `ptr_table_[sid.raw()] = ptr`.

    In the model, we record the offset the external pointer represents.
    Returns none if the index is out of bounds. -/
def Pool.registerExternal (p : Pool) (sid : Nat) (off : Nat)
    (hAlignedOff : off % poolAlignment = 0)
    (hBoundsOff : off ≤ p.poolBytes) :
    Option Pool :=
  if h : sid < p.table.length then
    some {
      poolBytes := p.poolBytes
      table := p.table.set sid (PoolEntry.internal off)
      numExternal := ((p.table.set sid (PoolEntry.internal off)).filter PoolEntry.isExternal).length
      hAligned := by
        intro idx hidx off' heq
        rw [List.length_set] at hidx
        rw [List.getElem_set] at heq
        split at heq
        · cases heq; exact hAlignedOff
        · exact p.hAligned idx hidx off' heq
      hBounds := by
        intro idx hidx off' heq
        rw [List.length_set] at hidx
        rw [List.getElem_set] at heq
        split at heq
        · cases heq; exact hBoundsOff
        · exact p.hBounds idx hidx off' heq
      hExtCount := rfl
    }
  else none

/-- Registering an external slot preserves num_slots.
    C++: register_external() doesn't change table size. -/
theorem Pool.registerExternal_preserves_numSlots
    (p : Pool) (sid : Nat) (off : Nat)
    (hA : off % poolAlignment = 0) (hB : off ≤ p.poolBytes)
    (p' : Pool)
    (h : p.registerExternal sid off hA hB = some p') :
    p'.numSlots = p.numSlots := by
  unfold registerExternal at h
  split at h
  · cases h; simp [Pool.numSlots, List.length_set]
  · exact absurd h (by simp)

/-- Registering an external slot preserves pool_bytes.
    C++: register_external() doesn't reallocate the pool. -/
theorem Pool.registerExternal_preserves_poolBytes
    (p : Pool) (sid : Nat) (off : Nat)
    (hA : off % poolAlignment = 0) (hB : off ≤ p.poolBytes)
    (p' : Pool)
    (h : p.registerExternal sid off hA hB = some p') :
    p'.poolBytes = p.poolBytes := by
  unfold registerExternal at h
  split at h
  · cases h; rfl
  · exact absurd h (by simp)

/-! ## Detach -/

/-- A detached pool buffer. Models DetachedPool in C++.
    Holds the raw allocation after PoolAllocator::detach().
    The original allocator is reset to empty. -/
structure DetachedPool where
  base  : Nat  -- pool base address (opaque in model)
  bytes : Nat  -- pool size

/-- Detach the pool buffer from the allocator.
    C++: `DetachedPool detach()` -- moves pool ownership out,
    resets allocator to empty. Used by CrucibleContext::switch_region()
    to keep old pool data alive while initializing a new pool. -/
def Pool.detach (p : Pool) : DetachedPool × Pool :=
  let detached : DetachedPool := { base := 0, bytes := p.poolBytes }
  let empty : Pool := {
    poolBytes := 0
    table := []
    numExternal := 0
    hAligned := by intro _ h; exact absurd h (by simp)
    hBounds := by intro _ h; exact absurd h (by simp)
    hExtCount := rfl
  }
  (detached, empty)

/-- After detach, the allocator is empty. -/
theorem Pool.detach_empty (p : Pool) :
    (p.detach).2.numSlots = 0 := by
  simp [Pool.detach, Pool.numSlots]

/-- After detach, the detached buffer has the original pool size. -/
theorem Pool.detach_preserves_bytes (p : Pool) :
    (p.detach).1.bytes = p.poolBytes := by
  simp [Pool.detach]

/-! ## Empty Pool -/

/-- The empty (uninitialized) pool. Models the default-constructed state.
    C++: `PoolAllocator() = default;` -- all fields zero/null. -/
def Pool.empty : Pool where
  poolBytes := 0
  table := []
  numExternal := 0
  hAligned := by intro _ h; exact absurd h (by simp)
  hBounds := by intro _ h; exact absurd h (by simp)
  hExtCount := rfl

/-- The empty pool is not initialized.
    C++: `is_initialized()` returns false when `ptr_table_ == nullptr`. -/
theorem Pool.empty_not_initialized : ¬Pool.empty.isInitialized := by
  simp [Pool.empty, Pool.isInitialized]

/-! ## Alignment Propagation -/

/-- If the plan's sweep-line uses poolAlignment, then poolCompatible holds.
    This connects MemoryPlan's alignment property to PoolAllocator's requirement.
    C++: compute_memory_plan() aligns all offsets to ALIGNMENT=256. -/
theorem MemoryPlan.aligned_implies_poolCompatible (plan : MemoryPlan)
    (hAlign : ∀ (idx : Nat) (hidx : idx < plan.slots.length),
      ¬(plan.slots[idx]'hidx).is_external ->
      (plan.assignments[idx]'(by have := plan.hLen; omega)).offset % poolAlignment = 0) :
    plan.poolCompatible :=
  hAlign

/-- Pool alignment divides cache-line alignment (64 | 256).
    256 = 4 * 64. Every pool-aligned address is also cache-line aligned. -/
theorem poolAlignment_div_cacheline : 64 ∣ poolAlignment := ⟨4, rfl⟩

/-- Pool alignment is 2^8. Useful for bitmask_eq_mod applications. -/
theorem poolAlignment_eq_pow2 : poolAlignment = 2 ^ 8 := rfl

/-- An offset that is poolAlignment-aligned is also aligned to any
    power-of-two divisor of poolAlignment (1, 2, 4, 8, 16, 32, 64, 128, 256).
    C++: 256-aligned offsets satisfy max_align_t (16B), cache-line (64B), etc. -/
theorem aligned_divisor (off d : Nat) (hd : d ∣ poolAlignment)
    (hoff : off % poolAlignment = 0) : off % d = 0 := by
  obtain ⟨k, hk⟩ := hd
  obtain ⟨m, hm⟩ := Nat.dvd_of_mod_eq_zero hoff
  rw [hm, hk, show d * k * m = d * (k * m) from by ring]
  exact Nat.mul_mod_right d (k * m)

end Crucible

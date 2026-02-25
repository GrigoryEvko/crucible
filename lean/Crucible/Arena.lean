import Crucible.Basic

/-!
# Crucible.Arena — Multi-Block Bump-Pointer Allocator

Backported from Arena.h (not invented — models the actual C++ code).

C++ class: `class Arena` (≤64 bytes, fits one cache line)
  Hot fields (every alloc):
    char* cur_block_;        -- current block pointer
    uintptr_t cur_base_;     -- reinterpret_cast<uintptr_t>(cur_block_) cached
    size_t offset_;          -- cursor within current block
    size_t end_offset_;      -- usable size of current block
  Cold fields (slow path only):
    size_t block_size_;      -- default block size (1MB)
    vector<char*> blocks_;   -- all blocks (for destructor)

Allocation: ~2ns (pointer bump + bitwise AND alignment).
  aligned_addr = (cur_base_ + offset_ + align - 1) & ~(align - 1)
  aligned_offset = aligned_addr - cur_base_
  if aligned_offset + size ≤ end_offset_:
    ptr = cur_block_ + aligned_offset
    offset_ = aligned_offset + size
  else:
    alloc_slow_(size, align)  -- new block

Key design: alignment against ABSOLUTE address (cur_base_ + offset_),
not block-relative offset. std::malloc guarantees max_align_t (16B),
but Arena needs 64B (cache-line) and 256B (PoolAllocator CUDA coalescing).

Effect system: requires fx::Alloc capability — cannot be called from
the hot path (foreground dispatch). Only background thread allocates.
-/

namespace Crucible

/-- Arena state: models the current block of a multi-block allocator.
    Previous blocks are frozen (their allocations are immutable).
    Multi-block behavior = sequence of single-block transitions.

    `base` = absolute base address of current block (cur_base_ in C++).
    `offset` = cursor position within block (offset_ in C++).
    `capacity` = usable size of current block (end_offset_ in C++). -/
structure Arena where
  base     : Nat   -- absolute base address (cur_base_)
  offset   : Nat   -- cursor within block (offset_)
  capacity : Nat   -- block size (end_offset_)
  hBound   : offset ≤ capacity  -- cursor within block

/-- Bytes remaining in the current block.
    C++: `end_offset_ - offset_`. -/
def Arena.remaining (a : Arena) : Nat := a.capacity - a.offset

/-- Allocation request: size + alignment.
    C++: `Arena::alloc(fx::Alloc, size_t size, size_t align)`. -/
structure AllocRequest where
  size  : Nat
  align : Nat
  hAlign : 0 < align

/-- Result of a successful allocation. -/
structure Allocation where
  addr : Nat   -- aligned absolute address
  size : Nat   -- bytes allocated

/-- Attempt to allocate from the current block.
    Returns (new arena state, allocation) or none if block exhausted.
    None = C++ calls alloc_slow_ (malloc new block, retry).

    C++ code being modeled:
      uintptr_t aligned_addr = (cur_base_ + offset_ + align - 1) & ~(align - 1);
      size_t aligned = static_cast<size_t>(aligned_addr - cur_base_);
      if (aligned + size <= end_offset_) { ... }
      else { return alloc_slow_(size, align); } -/
def Arena.alloc (a : Arena) (req : AllocRequest) :
    Option (Arena × Allocation) :=
  -- Align against absolute address (cur_base_ + offset_)
  let aligned_addr := alignUp (a.base + a.offset) req.align
  let aligned_offset := aligned_addr - a.base
  let new_offset := aligned_offset + req.size
  if h : new_offset ≤ a.capacity then
    some (
      ⟨a.base, new_offset, a.capacity, by omega⟩,
      ⟨aligned_addr, req.size⟩)
  else
    none

/-! ## Allocation Properties -/

/-- Every allocation is aligned to the requested alignment.
    This is THE correctness property of Arena::alloc().
    C++ relies on this for cache-line (64B) and CUDA (256B) alignment. -/
theorem Arena.alloc_aligned (a : Arena) (req : AllocRequest)
    (a' : Arena) (alloc : Allocation)
    (h : a.alloc req = some (a', alloc)) :
    alloc.addr % req.align = 0 := by
  unfold Arena.alloc at h
  simp only at h
  split at h
  · simp only [Option.some.injEq, Prod.mk.injEq] at h
    obtain ⟨_, rfl⟩ := h
    exact alignUp_aligned (a.base + a.offset) req.align req.hAlign
  · exact absurd h (by simp)

/-- Cursor only moves forward (monotonicity).
    C++: Arena pointers never dangle until the Arena is destroyed.
    No individual deallocation — only bulk free in destructor. -/
theorem Arena.alloc_cursor_monotone (a : Arena) (req : AllocRequest)
    (a' : Arena) (alloc : Allocation)
    (h : a.alloc req = some (a', alloc)) :
    a.offset ≤ a'.offset := by
  unfold Arena.alloc at h
  simp only at h
  split at h
  case isTrue hfit =>
    simp only [Option.some.injEq, Prod.mk.injEq] at h
    obtain ⟨rfl, _⟩ := h
    dsimp only
    have hge := alignUp_ge (a.base + a.offset) req.align req.hAlign
    omega
  case isFalse => exact absurd h (by simp)

/-- Sequential allocations don't overlap (MemSafe).
    Two allocs from the same block produce disjoint address ranges.
    C++: no use-after-free from aliased arena regions. -/
theorem Arena.alloc_disjoint (a : Arena) (r1 r2 : AllocRequest)
    (a1 a2 : Arena) (al1 al2 : Allocation)
    (h1 : a.alloc r1 = some (a1, al1))
    (h2 : a1.alloc r2 = some (a2, al2))
    (hs1 : 0 < r1.size) (_hs2 : 0 < r2.size) :
    ∀ x y, al1.addr ≤ x → x < al1.addr + al1.size →
            al2.addr ≤ y → y < al2.addr + al2.size → x ≠ y := by
  -- al1 occupies [alignUp(base+off, a1), alignUp(base+off, a1) + r1.size)
  -- al2 starts at alignUp(base + new_off1, a2) ≥ base + new_off1 = alignUp(...) + r1.size
  -- So al2.addr ≥ al1.addr + al1.size → disjoint
  unfold Arena.alloc at h1
  simp only at h1
  split at h1
  case isTrue hfit1 =>
    simp only [Option.some.injEq, Prod.mk.injEq] at h1
    obtain ⟨rfl, rfl⟩ := h1
    unfold Arena.alloc at h2
    simp only at h2
    split at h2
    case isTrue hfit2 =>
      simp only [Option.some.injEq, Prod.mk.injEq] at h2
      obtain ⟨_, rfl⟩ := h2
      dsimp only
      have hge1 := alignUp_ge (a.base + a.offset) r1.align r1.hAlign
      have hge2 := alignUp_ge
        (a.base + (alignUp (a.base + a.offset) r1.align - a.base + r1.size))
        r2.align r2.hAlign
      intros x y hx1 hx2 hy1 hy2
      omega
    case isFalse => exact absurd h2 (by simp)
  case isFalse => exact absurd h1 (by simp)

/-- All allocated bytes are within the arena block.
    C++: the bound check `new_offset ≤ end_offset_` in alloc(). -/
theorem Arena.alloc_within_bounds (a : Arena) (req : AllocRequest)
    (a' : Arena) (alloc : Allocation)
    (h : a.alloc req = some (a', alloc)) :
    a.base ≤ alloc.addr ∧ alloc.addr + alloc.size ≤ a.base + a.capacity := by
  unfold Arena.alloc at h
  simp only at h
  split at h
  case isTrue hfit =>
    simp only [Option.some.injEq, Prod.mk.injEq] at h
    obtain ⟨_, rfl⟩ := h
    dsimp only
    have hge := alignUp_ge (a.base + a.offset) req.align req.hAlign
    constructor <;> omega
  case isFalse => exact absurd h (by simp)

/-- OOM is detected before execution: alloc returns none.
    C++: Keeper checks plan.pool_bytes ≤ device_memory before execution.
    If alloc returns some, the allocation is guaranteed within bounds. -/
theorem Arena.alloc_safe (a : Arena) (req : AllocRequest) :
    (a.alloc req).isSome = true →
    ∃ a' alloc, a.alloc req = some (a', alloc) ∧
      alloc.addr + alloc.size ≤ a.base + a.capacity := by
  intro hSome
  have ⟨⟨a', alloc⟩, hEq⟩ := Option.isSome_iff_exists.mp hSome
  exact ⟨a', alloc, hEq, (Arena.alloc_within_bounds a req a' alloc hEq).2⟩

end Crucible

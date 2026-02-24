import Mathlib.Data.Nat.Bitwise
import Mathlib.Data.Nat.Log
import Mathlib.Tactic

/-!
# Crucible.Basic — Foundational Arithmetic

Backported from actual C++ code — these are the math primitives used
throughout the Crucible runtime.

Power-of-two predicates: TraceRing.h (CAPACITY=2^16), Arena.h (block alignment).
Alignment: Arena.h (`alignUp` models `(ptr + align - 1) & ~(align - 1)`).
Bitmask ≡ modulo: TraceRing.h (`entries[head & MASK]` ≡ `entries[head % CAPACITY]`).
Saturation arithmetic: Types.h uses std::mul_sat/add_sat for overflow-safe size math.

Key result: `bitmask_eq_mod` — x % 2^k = x &&& (2^k - 1).
This is why SPSC uses `head & MASK` (1 cycle) not `head % CAPACITY` (10-30 cycles).
-/

namespace Crucible

/-! ## Power of Two -/

/-- Ring buffer capacities and Arena alignments must be powers of two.
    C++: TraceRing CAPACITY = 2^16, KernelCache capacity asserted pow2,
    PoolAllocator ALIGNMENT = 256 = 2^8. -/
def IsPow2 (n : Nat) : Prop := ∃ k : Nat, n = 2 ^ k

theorem pow2_pos (k : Nat) : 0 < 2 ^ k := by positivity

theorem IsPow2.pos {n : Nat} (h : IsPow2 n) : 0 < n := by
  obtain ⟨k, rfl⟩ := h; exact pow2_pos k

theorem IsPow2.ne_zero {n : Nat} (h : IsPow2 n) : n ≠ 0 := by
  have := h.pos; omega

/-! ## Alignment — Arena bump-pointer advancement -/

/-- Round `ptr` up to next multiple of `align`.
    C++: `(ptr + align - 1) & ~(align - 1)` for power-of-two align.
    Model uses division (equivalent for all positive alignments).

    In Arena.h, called as: `alignUp(cur_base_ + offset_, align)`.
    Aligns against the ABSOLUTE address, not block-relative offset. -/
def alignUp (ptr align : Nat) : Nat :=
  ((ptr + align - 1) / align) * align

/-- alignUp produces aligned result.
    Proves Arena::alloc() returns correctly aligned pointers.
    Used with align = 16 (max_align_t), 64 (cache line), 256 (CUDA). -/
theorem alignUp_aligned (ptr align : Nat) (_h : 0 < align) :
    alignUp ptr align % align = 0 := by
  simp [alignUp]

/-- alignUp never decreases pointer. Arena cursor only moves forward.
    C++: cursor monotonicity — alloc() never returns an address
    before the current cursor position. -/
theorem alignUp_ge (ptr align : Nat) (h : 0 < align) :
    ptr ≤ alignUp ptr align := by
  simp only [alignUp]
  -- ceil(ptr/align) * align >= ptr. Proof: by Nat.div_add_mod,
  -- align * ((ptr+a-1)/a) + (ptr+a-1)%a = ptr+a-1.
  -- Since (ptr+a-1)%a < a, we get align * ((ptr+a-1)/a) >= ptr.
  have hdiv_mod := Nat.div_add_mod (ptr + align - 1) align
  have hmod_lt := Nat.mod_lt (ptr + align - 1) h
  rw [show (ptr + align - 1) / align * align
      = align * ((ptr + align - 1) / align) from Nat.mul_comm _ _]
  omega

/-- Disjointness: regions [a, a+s₁) and [b, b+s₂) don't overlap when separated.
    Fundamental lemma used by Arena.alloc_disjoint and MemoryPlan non-overlap. -/
theorem disjoint_regions (a b s₁ s₂ : Nat) (h : a + s₁ ≤ b) :
    ∀ x y, a ≤ x → x < a + s₁ → b ≤ y → y < b + s₂ → x ≠ y := by
  intros x y hx1 hx2 hy1 hy2; omega

/-! ## Bitmask ≡ Modulo — The SPSC indexing theorem -/

/-- x % 2^k = x &&& (2^k - 1). THE ring buffer property.
    C++: `entries[head & MASK]` where `MASK = CAPACITY - 1`.
    TraceRing: CAPACITY = 65536 = 2^16, MASK = 0xFFFF.
    Z3: `prove_ring.cpp` verifies ∀ head,tail. bitmask == modulo. -/
theorem bitmask_eq_mod (x k : Nat) :
    x % (2 ^ k) = x &&& (2 ^ k - 1) := by
  -- Bitwise extensionality: two Nats are equal iff all testBits agree.
  -- LHS bit i: (x % 2^k).testBit i = decide(i < k) && x.testBit i
  -- RHS bit i: (x &&& (2^k-1)).testBit i = x.testBit i && (2^k-1).testBit i
  --          = x.testBit i && decide(i < k)
  apply Nat.eq_of_testBit_eq
  intro i
  rw [Nat.testBit_mod_two_pow, Nat.testBit_land, Nat.testBit_two_pow_sub_one]
  cases (decide (i < k)) <;> simp

/-- Bitmask produces values in [0, 2^k). Array bounds safety.
    Ensures entries[head & MASK] is always a valid index. -/
theorem bitmask_lt (x k : Nat) :
    x % (2 ^ k) < 2 ^ k := Nat.mod_lt x (pow2_pos k)

/-! ## Saturation Arithmetic — std::mul_sat / std::add_sat model

    C++: Arena::alloc_array uses `std::mul_sat(n, sizeof(T))`
    to prevent overflow on size calculations. BackgroundThread uses
    `std::sub_sat`, `std::add_sat` throughout. -/

def addSat (a b max : Nat) : Nat := min (a + b) max
def mulSat (a b max : Nat) : Nat := min (a * b) max

theorem addSat_le (a b max : Nat) : addSat a b max ≤ max := Nat.min_le_right _ _
theorem mulSat_le (a b max : Nat) : mulSat a b max ≤ max := Nat.min_le_right _ _

theorem addSat_eq (a b max : Nat) (h : a + b ≤ max) :
    addSat a b max = a + b := Nat.min_eq_left h

theorem mulSat_eq (a b max : Nat) (h : a * b ≤ max) :
    mulSat a b max = a * b := Nat.min_eq_left h

end Crucible

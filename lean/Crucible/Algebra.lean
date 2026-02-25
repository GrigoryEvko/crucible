import Mathlib.Tactic
import Mathlib.Data.Nat.Bitwise
import Crucible.Basic

/-!
# Crucible.Algebra — Algebraic Structure Proofs

From z3_enhancement.md and l15_addition.txt (L15 Axiom, "Algebraic verification"):

Crucible's distributed execution depends on algebraic laws being satisfied.
These aren't academic — they're the CORRECTNESS CONDITIONS for parallelism:

- **Hash combining** must be a monoid (associative + identity).
  If not → parallel hashing gives different results than sequential → KernelCache
  returns wrong kernels.  (z3_enhancement.md: "Monoids and Semirings")

- **ScalarType promotion** must be a lattice (commutative + associative + idempotent).
  If not → type inference can oscillate → non-termination.
  (l15_addition.txt: "Algebraic verification")

- **Gradient accumulation** must be a commutative monoid.
  If not → all-reduce order matters → non-determinism across Relays.
  (l15_addition.txt: "Algebraic verification")

- **Merkle hash** trees use XOR combining which is self-inverse.
  This enables O(1) incremental updates: XOR out old child, XOR in new.
  (z3_enhancement.md: "Free Monads — Computation as Data")

C++ enforces these via concepts: `Lattice<L>`, `Monoid<M>`, `Semiring<S>`.
consteval proves laws for test values. Z3 proves them universally.
Lean proves them for ALL values with full mathematical rigor.
-/

namespace Crucible

/-! ## ScalarType Promotion Lattice

C++ (Types.h): `enum class ScalarType : int8_t` with promotion rules.
PyTorch type promotion: `promoteTypes(a, b)` selects output dtype.

Modeled as `Fin 14` ordered by promotion rank. The total order gives:
- `sup` = type promotion (wider type wins)
- `inf` = type narrowing (narrower type wins)
- Lattice laws inherited from Fin's LinearOrder (Mathlib).

The real PyTorch promotion has subtleties (Float16 vs BFloat16 are "peers"),
but Crucible's C++ code uses a strict rank ordering for determinism. -/

/-- ScalarType promotion rank. Fin 14 inherits LinearOrder → Lattice.
    C++: element_size() ordering with type-category priority. -/
abbrev ScalarRank := Fin 14

namespace ScalarRank

-- Named constructors matching C++ ScalarType ordinals (Types.h)
abbrev undefined : ScalarRank := ⟨0, by omega⟩
abbrev bool_     : ScalarRank := ⟨1, by omega⟩
abbrev uint8     : ScalarRank := ⟨2, by omega⟩
abbrev int8      : ScalarRank := ⟨3, by omega⟩
abbrev int16     : ScalarRank := ⟨4, by omega⟩
abbrev int32     : ScalarRank := ⟨5, by omega⟩
abbrev int64     : ScalarRank := ⟨6, by omega⟩
abbrev float16   : ScalarRank := ⟨7, by omega⟩
abbrev bfloat16  : ScalarRank := ⟨8, by omega⟩
abbrev float32   : ScalarRank := ⟨9, by omega⟩
abbrev float64   : ScalarRank := ⟨10, by omega⟩
abbrev complex32 : ScalarRank := ⟨11, by omega⟩
abbrev complex64 : ScalarRank := ⟨12, by omega⟩
abbrev complex128: ScalarRank := ⟨13, by omega⟩

/-- Undefined is the bottom element: promotion with any type yields that type.
    C++: ScalarType::Undefined used as "no type yet" sentinel. -/
theorem undefined_is_bot : ∀ x : ScalarRank, undefined ⊔ x = x := by decide

/-- Complex128 is the top: promotion with any type yields Complex128. -/
theorem complex128_is_top : ∀ x : ScalarRank, complex128 ⊔ x = complex128 := by decide

-- Key promotion examples matching PyTorch behavior
example : (int32 ⊔ float32 : ScalarRank) = float32 := by decide
example : (float16 ⊔ float64 : ScalarRank) = float64 := by decide
example : (bool_ ⊔ int64 : ScalarRank) = int64 := by decide
example : (float32 ⊔ float32 : ScalarRank) = float32 := by decide  -- idempotent

/-- Promotion is commutative.
    Inherited from Fin's LinearOrder but stated explicitly for clarity.
    C++: promoteTypes(a, b) == promoteTypes(b, a). -/
theorem promote_comm (a b : ScalarRank) : a ⊔ b = b ⊔ a := sup_comm a b

/-- Promotion is associative.
    C++: promoteTypes(promoteTypes(a, b), c) == promoteTypes(a, promoteTypes(b, c)). -/
theorem promote_assoc (a b c : ScalarRank) : a ⊔ b ⊔ c = a ⊔ (b ⊔ c) := sup_assoc a b c

/-- Promotion is idempotent: a ⊔ a = a.
    C++: promoteTypes(x, x) == x. -/
theorem promote_idem (a : ScalarRank) : a ⊔ a = a := sup_idem a

/-- Narrowing (meet) is commutative. -/
theorem narrow_comm (a b : ScalarRank) : a ⊓ b = b ⊓ a := inf_comm a b

/-- Absorption: a ⊔ (a ⊓ b) = a.
    The two operations are dual — a key lattice law. -/
theorem absorption_sup_inf (a b : ScalarRank) : a ⊔ (a ⊓ b) = a := sup_inf_self

/-- Absorption: a ⊓ (a ⊔ b) = a. -/
theorem absorption_inf_sup (a b : ScalarRank) : a ⊓ (a ⊔ b) = a := inf_sup_self

end ScalarRank

/-! ## Hash XOR Commutative Monoid

C++ (MerkleDag.h): Merkle hash combines content_hash with child hashes via XOR:
  `merkle_hash = fmix64(content_hash ⊕ child₁.merkle ⊕ child₂.merkle ⊕ ...)`

The XOR combining step forms (Nat, ⊕, 0) — a commutative monoid.
This is the CORRECTNESS CONDITION for parallel hash computation:
different Relays may compute child hashes in different order,
but XOR commutativity guarantees the same result.

The final `fmix64` is a bijection applied AFTER combining,
so combining order independence is what matters. -/

section HashMonoid

/-- XOR is associative: (a ⊕ b) ⊕ c = a ⊕ (b ⊕ c).
    Crucible: hash combining order within a level doesn't matter. -/
theorem hash_xor_assoc (a b c : Nat) : (a ^^^ b) ^^^ c = a ^^^ (b ^^^ c) :=
  Nat.xor_assoc a b c

/-- XOR is commutative: a ⊕ b = b ⊕ a.
    Crucible: child hash order doesn't matter. -/
theorem hash_xor_comm (a b : Nat) : a ^^^ b = b ^^^ a :=
  Nat.xor_comm a b

/-- XOR has right identity 0: a ⊕ 0 = a.
    Crucible: empty child list contributes nothing. -/
theorem hash_xor_zero_right (a : Nat) : a ^^^ 0 = a :=
  Nat.xor_zero a

/-- XOR has left identity 0. -/
theorem hash_xor_zero_left (a : Nat) : 0 ^^^ a = a := by
  rw [hash_xor_comm]; exact hash_xor_zero_right a

/-- XOR is self-inverse: a ⊕ a = 0.
    Crucible: XOR-out old child, XOR-in new = O(1) incremental Merkle update. -/
theorem hash_xor_self_cancel (a : Nat) : a ^^^ a = 0 :=
  Nat.xor_self a

/-- Any permutation of children gives the same combined hash.
    Proof: commutativity + associativity → full permutation invariance.
    Stated for 3 children as the base case. -/
theorem merkle_combine_perm₃ (a b c : Nat) :
    (a ^^^ b) ^^^ c = (a ^^^ c) ^^^ b := by
  rw [hash_xor_assoc, hash_xor_comm b c, ← hash_xor_assoc]

/-- XOR with same child twice cancels (involution property).
    Crucible: this enables incremental Merkle DAG updates.
    To update child k: new_combined = old_combined ⊕ old_child_k ⊕ new_child_k. -/
theorem merkle_incremental_update (base old_child new_child : Nat) :
    (base ^^^ old_child) ^^^ old_child ^^^ new_child = base ^^^ new_child := by
  -- ((base ⊕ old) ⊕ old) ⊕ new = base ⊕ new
  -- Step 1: reassociate the double-XOR to expose old ⊕ old
  have h : (base ^^^ old_child) ^^^ old_child = base := by
    rw [Nat.xor_assoc, Nat.xor_self, Nat.xor_zero]
  rw [h]

/-- XOR combining is order-independent for two elements.
    Base case for fold-order independence.
    Crucible: two child hashes combined in either order give the same result. -/
theorem hash_combine_order₂ (base a b : Nat) :
    base ^^^ a ^^^ b = base ^^^ b ^^^ a := by
  rw [Nat.xor_assoc, Nat.xor_comm a b, ← Nat.xor_assoc]

/-- XOR fold over empty list is identity.
    Crucible: leaf node with no children has merkle_hash = content_hash. -/
theorem hash_fold_nil (base : Nat) :
    [].foldl (· ^^^ ·) base = base := rfl

/-- XOR fold over singleton is one XOR.
    Crucible: node with one child XORs content_hash with child merkle. -/
theorem hash_fold_singleton (base h : Nat) :
    [h].foldl (· ^^^ ·) base = base ^^^ h := rfl

end HashMonoid

/-! ## Gradient Commutative Monoid

C++ (L12 Distribution): Gradient accumulation via all-reduce.
For deterministic replay (L13 DetSafe), all-reduce must produce the
same result regardless of reduction order across Relays.

Modeled over ℚ (exact rational arithmetic). In real floating-point,
addition is NOT associative — this is a fundamental limitation.
The model captures the IDEAL behavior that Crucible approximates
via deterministic reduction order + Kahan summation. -/

section GradientMonoid

/-- Gradient summation over ℚ is commutative: a + b = b + a.
    Crucible: all-reduce(a, b) = all-reduce(b, a). -/
theorem grad_add_comm (a b : ℚ) : a + b = b + a := add_comm a b

/-- Gradient summation is associative: (a + b) + c = a + (b + c).
    Crucible: nested all-reduce gives same result as flat. -/
theorem grad_add_assoc (a b c : ℚ) : a + b + c = a + (b + c) := add_assoc a b c

/-- Zero gradient is identity: a + 0 = a.
    Crucible: Relay contributing zero gradient doesn't affect result. -/
theorem grad_add_zero (a : ℚ) : a + 0 = a := add_zero a

/-- Gradient scaling distributes over addition: c·(a + b) = c·a + c·b.
    Crucible: scaling by 1/N for averaging commutes with summation.
    This is why mean-reduce = sum-reduce / N. -/
theorem grad_scale_distrib (c a b : ℚ) : c * (a + b) = c * a + c * b :=
  mul_add c a b

/-- Average of N equal gradients equals the gradient itself.
    Crucible: when all Relays have the same gradient (e.g. after
    convergence), all-reduce averaging is a no-op. -/
theorem grad_avg_uniform (g : ℚ) (n : ℕ) (hn : 0 < n) :
    (n : ℚ) * g / (n : ℚ) = g := by
  have hn' : (n : ℚ) ≠ 0 := Nat.cast_ne_zero.mpr (by omega)
  field_simp

end GradientMonoid

/-! ## Galois Connection: Tensor ↔ TensorMeta

From z3_enhancement.md ("Galois Connections — Soundness of Abstractions"):

Every time Crucible abstracts — TensorMeta from actual Tensor, shadow handles
from real data, MemoryPlan from actual allocations — that's an abstraction
function α. Soundness means: if the abstract analysis says property P,
the concrete execution satisfies P.

C++ (L4 Tensors): ConductorTensorImpl (shadow handle) carries TensorMeta
with correct shape/strides/dtype/device. The actual data is written
asynchronously by compiled kernels. The Galois connection guarantees
that the shadow metadata is SOUND w.r.t. the actual tensor. -/

section GaloisConnection

/-- A shape descriptor: dimensions of a tensor. -/
abbrev Shape := List Nat

/-- Number of elements in a tensor with given shape. -/
def Shape.numel (s : Shape) : Nat := s.prod

/-- Abstract tensor metadata.
    C++ (MetaLog.h): TensorMeta = 144 bytes
    (sizes[8], strides[8], data_ptr, ndim, dtype, device). -/
structure AbstractMeta where
  shape : Shape
  deriving DecidableEq

/-- Concrete tensor: shape + element count satisfying product constraint. -/
structure ConcreteTensor where
  shape : Shape
  numel : Nat
  valid : numel = shape.prod

/-- Abstraction function α: extract shape from concrete tensor.
    C++ (L3 Operations): snapshot TensorMeta from actual tensor. -/
def abstract (t : ConcreteTensor) : AbstractMeta := ⟨t.shape⟩

/-- Concretization: a concrete tensor matches an abstract meta
    iff their shapes agree. -/
def concretizes (m : AbstractMeta) (t : ConcreteTensor) : Prop :=
  t.shape = m.shape

/-- Galois soundness: abstracting a tensor then checking against
    a meta is equivalent to checking the tensor against that meta.
    α(t) = m ↔ t ∈ γ(m). -/
theorem galois_sound (t : ConcreteTensor) (m : AbstractMeta) :
    abstract t = m ↔ concretizes m t := by
  cases m with | mk s =>
  simp [abstract, concretizes]

/-- Abstraction preserves element count: if shapes match,
    the concrete tensor has the right number of elements. -/
theorem abstract_preserves_numel (t : ConcreteTensor) (m : AbstractMeta)
    (h : concretizes m t) : t.numel = m.shape.numel := by
  rw [t.valid, Shape.numel]; exact congrArg List.prod h

/-- Composing two abstractions is an abstraction.
    Crucible: shadow handle → meta → plan is a composed abstraction. -/
theorem abstract_compose (t : ConcreteTensor) :
    abstract t = ⟨t.shape⟩ := rfl

end GaloisConnection

/-! ## DAG Transform Composition

From z3_enhancement.md ("Category Theory — Composition Correctness"):

DAG transformations (fusion, scheduling, memory planning, pruning)
form a category where:
- Objects = DAGs (computation graphs)
- Morphisms = transformations
- Composition = applying transforms in sequence
- Identity = no-op transform

The key property: composition is associative and has identity.
This means the order of applying independent transforms doesn't
affect the final result (when transforms commute).

C++ (L6 Merkle DAG): atomic swap mechanism applies transforms
at iteration boundaries. Each transform produces a new DAG
with new content_hash and merkle_hash. -/

section DagTransforms

/-- Abstract DAG type (content-addressed by merkle_hash). -/
structure DAG where
  merkle_hash : Nat
  num_nodes : Nat

/-- A DAG transform: function from DAG to DAG.
    C++: background thread builds new DAG → atomic pointer swap. -/
def DagTransform := DAG → DAG

/-- Identity transform: no change. -/
def DagTransform.id : DagTransform := fun d => d

/-- Compose two transforms: apply f then g. -/
def DagTransform.comp (g f : DagTransform) : DagTransform := g ∘ f

/-- Composition is associative: (h ∘ g) ∘ f = h ∘ (g ∘ f).
    Crucible: three-pass optimization = one-pass giving same result. -/
theorem transform_comp_assoc (f g h : DagTransform) :
    DagTransform.comp (DagTransform.comp h g) f =
    DagTransform.comp h (DagTransform.comp g f) := rfl

/-- Identity is left-neutral: id ∘ f = f. -/
theorem transform_id_left (f : DagTransform) :
    DagTransform.comp DagTransform.id f = f := rfl

/-- Identity is right-neutral: f ∘ id = f. -/
theorem transform_id_right (f : DagTransform) :
    DagTransform.comp f DagTransform.id = f := rfl

/-- Invertible transform: if f has an inverse g, then f ∘ g = id.
    C++: DAG versioning allows rollback (g undoes f). -/
def DagTransform.Invertible (f : DagTransform) : Prop :=
  ∃ g : DagTransform, DagTransform.comp f g = DagTransform.id ∧
                        DagTransform.comp g f = DagTransform.id

/-- Composing two invertible transforms gives an invertible transform.
    Crucible: if fusion is rollbackable and scheduling is rollbackable,
    then fusion-then-scheduling is rollbackable. -/
theorem invertible_comp (f g : DagTransform)
    (hf : DagTransform.Invertible f) (hg : DagTransform.Invertible g) :
    DagTransform.Invertible (DagTransform.comp g f) := by
  obtain ⟨f_inv, hfl, hfr⟩ := hf
  obtain ⟨g_inv, hgl, hgr⟩ := hg
  refine ⟨DagTransform.comp f_inv g_inv, ?_, ?_⟩
  · -- (g ∘ f) ∘ (f⁻¹ ∘ g⁻¹) = id
    funext d
    simp only [DagTransform.comp, DagTransform.id, Function.comp_def]
    have hf_eq : ∀ x, f (f_inv x) = x := congr_fun hfl
    have hg_eq : ∀ x, g (g_inv x) = x := congr_fun hgl
    rw [hf_eq, hg_eq]
  · -- (f⁻¹ ∘ g⁻¹) ∘ (g ∘ f) = id
    funext d
    simp only [DagTransform.comp, DagTransform.id, Function.comp_def]
    have hf_eq : ∀ x, f_inv (f x) = x := congr_fun hfr
    have hg_eq : ∀ x, g_inv (g x) = x := congr_fun hgr
    rw [hg_eq, hf_eq]

end DagTransforms

/-! ## Interval Non-Overlap Composition

From l15_addition.txt (L15 Axiom, "Sweep-line non-overlap"):
  "For N tensors with arbitrary lifetimes, sizes, alignments — no two
   simultaneously-live tensors share memory."

Key algebraic property: non-overlap of memory assignments is preserved
under composition of independent memory plans.

Extends MemoryPlan.lean with algebraic composition theorems. -/

section IntervalAlgebra

/-- Memory interval: half-open [lo, hi). -/
structure Interval where
  lo : Nat
  hi : Nat
  valid : lo ≤ hi

/-- Two intervals are disjoint (don't overlap). -/
def Interval.disjoint (a b : Interval) : Prop :=
  a.hi ≤ b.lo ∨ b.hi ≤ a.lo

/-- Disjointness is symmetric. -/
theorem Interval.disjoint_symm (a b : Interval) :
    a.disjoint b ↔ b.disjoint a := by
  simp [Interval.disjoint]; tauto

/-- A memory plan: list of intervals, all pairwise disjoint. -/
structure MemPlan where
  intervals : List Interval
  pairwise_disjoint : ∀ i j, i ∈ intervals → j ∈ intervals → i ≠ j → i.disjoint j

/-- Empty plan has no overlaps (trivially). -/
def MemPlan.empty : MemPlan where
  intervals := []
  pairwise_disjoint := by intros; contradiction

/-- Total memory needed: max of all interval endpoints. -/
def MemPlan.total_bytes (p : MemPlan) : Nat :=
  p.intervals.foldl (fun acc i => max acc i.hi) 0

/-- Filtering a valid plan yields a valid plan.
    Crucible: activation checkpointing removes some tensors from the plan —
    the remaining tensors are still pairwise disjoint. -/
def MemPlan.filter (p : MemPlan) (pred : Interval → Bool) : MemPlan where
  intervals := p.intervals.filter pred
  pairwise_disjoint := fun i j hi hj hne =>
    p.pairwise_disjoint i j
      (List.mem_of_mem_filter hi) (List.mem_of_mem_filter hj) hne

/-- Concatenating two plans with disjoint address ranges yields a valid plan.
    Crucible: merging memory plans from different Relays (L12 Distribution). -/
theorem MemPlan.concat_disjoint (p q : MemPlan)
    (h_cross : ∀ i j, i ∈ p.intervals → j ∈ q.intervals → i.disjoint j) :
    ∀ i j, i ∈ p.intervals ++ q.intervals →
           j ∈ p.intervals ++ q.intervals → i ≠ j → i.disjoint j := by
  intro i j hi hj hne
  simp only [List.mem_append] at hi hj
  rcases hi with hip | hiq <;> rcases hj with hjp | hjq
  · exact p.pairwise_disjoint i j hip hjp hne
  · exact h_cross i j hip hjq
  · exact (Interval.disjoint_symm j i).mp (h_cross j i hjp hiq)
  · exact q.pairwise_disjoint i j hiq hjq hne

end IntervalAlgebra

end Crucible

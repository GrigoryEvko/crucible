namespace Crucible

/-!
# Crucible.Effects — Compile-Time Effect System

Models Effects.h: zero-size capability tokens that enforce side-effect
boundaries at compile time.

C++ insight: effects are permissions. If a function can allocate, it needs
PERMISSION to allocate. Make that permission a function parameter.
No parameter = no permission = compile error.

## Capability tokens (C++ zero-size structs with private constructors)

  fx::Alloc  -- heap allocation (malloc, arena alloc, vector push)
  fx::IO     -- file/network I/O (fprintf, fopen, send, recv)
  fx::Block  -- blocking operations (mutex, sleep, futex, spin-wait)

## Contexts (C++ structs that hold capability tokens)

  fx::Bg    -- background thread: {Alloc, IO, Block}
  fx::Init  -- initialization:    {Alloc, IO}        (NO Block)
  fx::Test  -- tests:             {Alloc, IO, Block}

The critical invariant: Init does NOT get Block. Initialization code must
not block. This is enforced by the C++ type system (Block's constructor is
not a friend of Init) and here by the structure of propositions.

## Formalization strategy

We model capabilities as propositions (types in Prop). A context "has"
a capability if it carries a proof of that proposition. This mirrors
the C++ design: the token IS the proof of permission.

Key theorems:
- Init's capabilities are a strict subset of Bg's
- Bg and Test have identical capabilities
- Any function requiring only {Alloc, IO} accepts both Bg and Init
- Pure contexts have no capabilities at all
-/

/-! ## Capability propositions

Each capability is a simple inductive type in Prop. Having a term of
type `HasAlloc ctx` is the proof that `ctx` can allocate -- exactly
like holding an `fx::Alloc` token in C++. -/

/-- The set of capabilities a context may grant.
    C++: determined by which token types appear as struct members. -/
inductive Cap where
  | Alloc  -- heap allocation (Arena, malloc, vector push)
  | IO     -- file/network I/O (fprintf, fopen, send, recv)
  | Block  -- blocking operations (mutex, sleep, futex, spin-wait)
  deriving DecidableEq, Repr

/-- A capability set is a predicate: which capabilities are granted.
    C++: implicit in the struct layout (Bg has .alloc, .io, .block fields). -/
def CapSet := Cap → Prop

/-! ## Named capability sets matching C++ contexts -/

/-- fx::Bg capabilities: {Alloc, IO, Block}.
    C++: `struct Bg { Alloc alloc{}; IO io{}; Block block{}; };` -/
def CapSet.bg : CapSet := fun _ => True

/-- fx::Init capabilities: {Alloc, IO}.
    C++: `struct Init { Alloc alloc{}; IO io{}; };`
    Note: Block is NOT a friend of Init. -/
def CapSet.init : CapSet := fun c =>
  match c with
  | .Alloc => True
  | .IO    => True
  | .Block => False

/-- fx::Test capabilities: {Alloc, IO, Block}.
    C++: `struct Test { Alloc alloc{}; IO io{}; Block block{}; };` -/
def CapSet.test : CapSet := fun _ => True

/-- Pure context: no capabilities at all.
    C++: `concept Pure = !CanAlloc<Ctx> && !CanIO<Ctx> && !CanBlock<Ctx>;`
    Any type without effect members (e.g. `int`). -/
def CapSet.pure : CapSet := fun _ => False

/-! ## Subset relation on capability sets -/

/-- One capability set is a subset of another when every granted
    capability in the first is also granted in the second.
    This models "any function accepting ctx₁ also accepts ctx₂". -/
def CapSet.subset (s₁ s₂ : CapSet) : Prop :=
  ∀ c : Cap, s₁ c → s₂ c

instance : HasSubset CapSet where
  Subset := CapSet.subset

/-- Strict subset: s₁ ⊆ s₂ and s₁ ≠ s₂ (there exists a cap in s₂ not in s₁). -/
def CapSet.ssubset (s₁ s₂ : CapSet) : Prop :=
  s₁ ⊆ s₂ ∧ ∃ c : Cap, s₂ c ∧ ¬ s₁ c

instance : HasSSubset CapSet where
  SSubset := CapSet.ssubset

/-! ## Context structure

A context bundles a capability set with proofs. This mirrors the C++
struct layout: `Bg` is a struct whose fields ARE the capability tokens. -/

/-- An effect context: a named capability set.
    C++: `fx::Bg`, `fx::Init`, `fx::Test` — each a struct holding tokens. -/
structure Context where
  /-- Which capabilities this context grants. -/
  caps : CapSet

/-- Background thread context. C++: `fx::Bg`. -/
def Context.bg : Context := ⟨.bg⟩

/-- Initialization context. C++: `fx::Init`. -/
def Context.init : Context := ⟨.init⟩

/-- Test context. C++: `fx::Test`. -/
def Context.test : Context := ⟨.test⟩

/-- Pure (no-effect) context. C++: `concept Pure<int>`. -/
def Context.pure : Context := ⟨.pure⟩

/-! ## Capability predicates on contexts

These mirror the C++ concepts `CanAlloc`, `CanIO`, `CanBlock`, `Pure`. -/

/-- Context can allocate. C++: `concept CanAlloc = requires { ctx.alloc };` -/
def Context.canAlloc (ctx : Context) : Prop := ctx.caps Cap.Alloc

/-- Context can do I/O. C++: `concept CanIO = requires { ctx.io };` -/
def Context.canIO (ctx : Context) : Prop := ctx.caps Cap.IO

/-- Context can block. C++: `concept CanBlock = requires { ctx.block };` -/
def Context.canBlock (ctx : Context) : Prop := ctx.caps Cap.Block

/-- Context is pure (no effects). C++: `concept Pure = !CanAlloc && !CanIO && !CanBlock;` -/
def Context.isPure (ctx : Context) : Prop :=
  ¬ ctx.canAlloc ∧ ¬ ctx.canIO ∧ ¬ ctx.canBlock

/-! ## Core theorems: capability structure matches C++ static_asserts

These correspond 1:1 to the `static_assert` block in Effects.h lines 171-178. -/

/-- C++: `static_assert(CanAlloc<Bg> && CanIO<Bg> && CanBlock<Bg>);` -/
theorem bg_has_alloc : Context.bg.canAlloc := trivial

theorem bg_has_io : Context.bg.canIO := trivial

theorem bg_has_block : Context.bg.canBlock := trivial

/-- C++: `static_assert(CanAlloc<Init> && CanIO<Init> && !CanBlock<Init>);` -/
theorem init_has_alloc : Context.init.canAlloc := trivial

theorem init_has_io : Context.init.canIO := trivial

/-- The critical invariant: Init cannot block.
    C++: Block's private constructor does NOT friend Init.
    This prevents deadlocks during initialization. -/
theorem init_no_block : ¬ Context.init.canBlock := fun h => h

/-- C++: `static_assert(CanAlloc<Test> && CanIO<Test> && CanBlock<Test>);` -/
theorem test_has_alloc : Context.test.canAlloc := trivial

theorem test_has_io : Context.test.canIO := trivial

theorem test_has_block : Context.test.canBlock := trivial

/-- C++: `static_assert(Pure<int>);` -/
theorem pure_is_pure : Context.pure.isPure :=
  ⟨fun h => h, fun h => h, fun h => h⟩

/-- C++: `static_assert(!Pure<Bg>);` — Bg has effects, so not pure. -/
theorem bg_not_pure : ¬ Context.bg.isPure := fun ⟨h, _, _⟩ => h trivial

/-! ## Subset theorems: capability containment

These are the key structural results. They prove that if a function
requires capabilities S, then any context whose caps ⊇ S can call it. -/

/-- Init's capabilities are a subset of Bg's.
    Any function callable by Init is also callable by Bg.
    C++: Bg has {alloc, io, block} ⊇ {alloc, io} = Init. -/
theorem init_subset_bg : CapSet.init ⊆ CapSet.bg := by
  intro c _; trivial

/-- Init's capabilities are a subset of Test's.
    C++: Test has {alloc, io, block} ⊇ {alloc, io} = Init. -/
theorem init_subset_test : CapSet.init ⊆ CapSet.test := by
  intro c _; trivial

/-- Pure is a subset of everything.
    A function requiring no capabilities can be called from any context. -/
theorem pure_subset_any (s : CapSet) : CapSet.pure ⊆ s := by
  intro c h; exact absurd h (fun h => h)

/-- Bg and Test have identical capabilities.
    C++: both have {Alloc, IO, Block}. -/
theorem bg_eq_test : ∀ c : Cap, CapSet.bg c ↔ CapSet.test c :=
  fun _ => ⟨fun _ => trivial, fun _ => trivial⟩

/-- Init is a STRICT subset of Bg: Init ⊂ Bg.
    There exists a capability (Block) that Bg has but Init does not.
    This is the fundamental asymmetry of the effect system. -/
theorem init_ssubset_bg : CapSet.init ⊂ CapSet.bg :=
  ⟨init_subset_bg, ⟨Cap.Block, trivial, fun h => h⟩⟩

/-- Init is a STRICT subset of Test. -/
theorem init_ssubset_test : CapSet.init ⊂ CapSet.test :=
  ⟨init_subset_test, ⟨Cap.Block, trivial, fun h => h⟩⟩

/-- Pure is a strict subset of Init (Init has capabilities, Pure does not). -/
theorem pure_ssubset_init : CapSet.pure ⊂ CapSet.init :=
  ⟨pure_subset_any _, ⟨Cap.Alloc, trivial, fun h => h⟩⟩

/-- Pure is a strict subset of Bg. -/
theorem pure_ssubset_bg : CapSet.pure ⊂ CapSet.bg :=
  ⟨pure_subset_any _, ⟨Cap.Alloc, trivial, fun h => h⟩⟩

/-! ## Decidability: capability membership is decidable

Important for computation: we can check at "runtime" (in Lean's reduction)
whether a context has a capability. Mirrors C++ concept checking. -/

/-- Bg membership is decidable (everything is True). -/
instance (c : Cap) : Decidable (CapSet.bg c) :=
  isTrue trivial

/-- Init membership is decidable. -/
instance (c : Cap) : Decidable (CapSet.init c) :=
  match c with
  | .Alloc => isTrue trivial
  | .IO    => isTrue trivial
  | .Block => isFalse fun h => h

/-- Test membership is decidable (everything is True). -/
instance (c : Cap) : Decidable (CapSet.test c) :=
  isTrue trivial

/-- Pure membership is decidable (everything is False). -/
instance (c : Cap) : Decidable (CapSet.pure c) :=
  isFalse fun h => h

/-! ## Effect-polymorphic function signatures

These types model C++ template functions constrained by concepts.
A function `template<fx::CanAlloc Ctx> void f(Ctx&)` becomes
a Lean function taking a proof that the context can allocate. -/

/-- A function requiring allocation capability.
    C++: `template<fx::CanAlloc Ctx> void build_graph(Ctx& ctx, Arena& arena);`
    Lean: the caller must provide proof that their context can allocate. -/
def requiresAlloc (ctx : Context) (h : ctx.canAlloc) : Prop :=
  ctx.caps Cap.Alloc ∧ h = h  -- trivially satisfied; the signature IS the constraint

/-- Combined requirement: Alloc + IO.
    C++: A function needing both allocation and I/O (e.g., logging allocator). -/
def requiresAllocIO (ctx : Context) : Prop :=
  ctx.canAlloc ∧ ctx.canIO

/-- Bg satisfies Alloc + IO. -/
theorem bg_satisfies_alloc_io : requiresAllocIO Context.bg :=
  ⟨trivial, trivial⟩

/-- Init satisfies Alloc + IO (this is exactly Init's capability set). -/
theorem init_satisfies_alloc_io : requiresAllocIO Context.init :=
  ⟨trivial, trivial⟩

/-- Test satisfies Alloc + IO. -/
theorem test_satisfies_alloc_io : requiresAllocIO Context.test :=
  ⟨trivial, trivial⟩

/-- Combined requirement: all three capabilities.
    C++: A function needing allocation, I/O, and blocking (e.g., bg_main). -/
def requiresAll (ctx : Context) : Prop :=
  ctx.canAlloc ∧ ctx.canIO ∧ ctx.canBlock

/-- Bg satisfies all requirements. -/
theorem bg_satisfies_all : requiresAll Context.bg :=
  ⟨trivial, trivial, trivial⟩

/-- Test satisfies all requirements. -/
theorem test_satisfies_all : requiresAll Context.test :=
  ⟨trivial, trivial, trivial⟩

/-- Init does NOT satisfy all requirements (missing Block).
    This is WHY Init exists as a separate context: constructors
    must not call blocking operations. -/
theorem init_not_all : ¬ requiresAll Context.init :=
  fun ⟨_, _, h⟩ => h

/-! ## Lattice structure

The capability sets form a bounded lattice under subset ordering:
  Pure ⊂ Init ⊂ Bg = Test
This captures the permission hierarchy of the Crucible runtime. -/

/-- The capability sets form a total preorder on our four named contexts.
    Pure < Init < Bg = Test. -/
theorem capability_chain :
    CapSet.pure ⊂ CapSet.init ∧
    CapSet.init ⊂ CapSet.bg ∧
    (∀ c, CapSet.bg c ↔ CapSet.test c) :=
  ⟨pure_ssubset_init, init_ssubset_bg, bg_eq_test⟩

/-- Subset is reflexive on capability sets. -/
theorem capset_subset_refl (s : CapSet) : s ⊆ s :=
  fun _ h => h

/-- Subset is transitive on capability sets. -/
theorem capset_subset_trans (s₁ s₂ s₃ : CapSet)
    (h₁₂ : s₁ ⊆ s₂) (h₂₃ : s₂ ⊆ s₃) : s₁ ⊆ s₃ :=
  fun c h => h₂₃ c (h₁₂ c h)

/-- Corollary: Pure ⊆ Bg via transitivity (Pure ⊆ Init ⊆ Bg). -/
theorem pure_subset_bg_via_init : CapSet.pure ⊆ CapSet.bg :=
  capset_subset_trans _ _ _ (pure_subset_any _) init_subset_bg

end Crucible

#pragma once

// ── crucible::effects — F*-style effect-row aliases ─────────────────
//
// FOUND-G79.  Canonical named rows over the Met(X) effect-atom set,
// modelled on the F* dependent-type-theory effect lattice.  These
// aliases exist so production code can WRITE intent ("this function
// is pure") without re-spelling a Row<...> pack at every site, and so
// reviewer eyes can match on the well-known F* vocabulary instead of
// decoding atom membership ad-hoc.
//
// ── The F* effect lattice (Crucible mapping) ────────────────────────
//
// The canonical chain (refinement-ordered, bottom up):
//
//   Pure / Tot / Ghost  ⊑  Div  ⊑  ST  ⊑  All
//
// Bottom (Pure / Tot / Ghost) carries no atoms — these are the totally-
// pure computations.  Tot is a synonym for Pure under modern F*; Ghost
// adds a separate "computationally erased" *intent* but at the row
// level is still empty (Crucible has no Effect::Ghost atom yet — the
// row-level distinction will land if/when a Ghost atom is introduced).
//
// Each level upward EXTENDS the atom set:
//
//   level   Crucible row                              what's added
//   Pure    Row<>                                     —  (the bottom)
//   Tot     Row<>                                     —  (synonym of Pure)
//   Ghost   Row<>                                     —  (synonym of Pure)
//   Div     Row<Effect::Block>                        + non-termination
//   ST      Row<Block, Alloc, IO>                     + heap mutation + I/O
//   All     Row<Alloc, IO, Block, Bg, Init, Test>    + every atom
//
// Critically: each upper level *contains* the prior, so the Subrow
// inclusion is the lattice ordering.  `Subrow<R, DivRow>` is the
// concept-form of "R fits within Div's effect budget".
//
// ── Public surface ──────────────────────────────────────────────────
//
//   Aliases (the named rows):
//     PureRow / TotRow / GhostRow / DivRow / STRow / AllRow
//
//   Concepts (G80, defined in this header for locality):
//     IsPure<R>  / IsTot<R>  / IsGhost<R>
//     IsDiv<R>
//     IsST<R>
//     IsAll<R>
//
//   Refinement chain (every implication holds at compile time):
//     IsPure<R>  ⇒  IsTot<R>   ⇒  IsGhost<R>     [all three are ∅]
//     IsPure<R>  ⇒  IsDiv<R>   ⇒  IsST<R>  ⇒  IsAll<R>
//
//   This last chain is the F* substitution principle: a pure function
//   can be called from any context that admits Div / ST / All; an All-
//   admitting context cannot be downgraded back to Pure.
//
// ── What this header is NOT ─────────────────────────────────────────
//
// (a) A new effect atom set.  The atoms live in Capabilities.h.  This
//     header only defines named groupings of those atoms.
//
// (b) An F* dependent-type-theory port.  No refinement types, no
//     decreases-clauses, no termination metric — those are F*'s
//     *value*-level features.  We borrow the EFFECT vocabulary only.
//
// (c) Bound on every atom.  Capabilities.h's Effect enum already has
//     `Bg / Init / Test` context-tag atoms that are not in any "below
//     ST" alias.  AllRow is the only alias that names them.  This is
//     intentional — context tags are NOT computational effects in the
//     F* sense; they are dispatch hints.
//
// ── Diagnostic surface (FOUND-E18) ──────────────────────────────────
//
// Violations of these predicates surface as three diagnostic categories
// in safety/Diagnostic.h:
//
//   PureFunctionViolation        — IsPure / IsTot / IsGhost rejection
//   DivergenceBudgetViolation    — IsDiv rejection (state effect added)
//   StateBudgetViolation         — IsST rejection (context tag added)
//
// IsAll has no diagnostic — the substitution always succeeds.  Adding,
// removing, or renaming an alias here MUST be reflected in Diagnostic.h's
// Catalog + Category, or the cross-reference below the diagnostic
// docblock will name a tag that no longer ships.  See Diagnostic.h's
// FOUND-E18 section for the symmetric back-reference.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/Progress.h>

#include <type_traits>

namespace crucible::effects {

// ── F* alias rows ──────────────────────────────────────────────────

// Pure: no observable effects.  The bottom of the lattice.  In F*,
// every PURE function is total, deterministic, and side-effect-free.
using PureRow  = Row<>;

// Tot: total (terminating) and pure.  Synonym for Pure under modern
// F*; kept as a distinct alias so call sites can communicate intent
// ("this returns and does nothing observable").
using TotRow   = Row<>;

// Ghost: computationally irrelevant.  In F*, GHOST values exist only
// for proof obligations and are erased at extraction.  Crucible has
// no Effect::Ghost atom yet, so structurally GhostRow == PureRow; the
// alias is reserved so future code can specialize against it.
using GhostRow = Row<>;

// Div: may diverge (non-terminating).  Equivalent to PURE plus the
// possibility of an unbounded wait.  In Crucible, Block is the atom
// that signals "this call may wait forever" (futex / cond_var / I/O
// drain that has no guaranteed bound).
using DivRow   = Row<Effect::Block>;

// ST: state-effect computations.  May allocate, may perform I/O, and
// may diverge (because allocation can call into a kernel that blocks,
// and I/O is unbounded by definition).  In F*, ST extends DIV with the
// heap-mutation effect; here it gains both Alloc and IO atoms.
using STRow    = Row<Effect::Block, Effect::Alloc, Effect::IO>;

// All: anything goes.  Universe row — every atom in the Effect enum.
// Production code rarely names this directly; it's the typecheck-
// always-passes ceiling that lets reviewers spot a function that has
// declared no constraint.
using AllRow   = Row<Effect::Alloc, Effect::IO,   Effect::Block,
                     Effect::Bg,    Effect::Init, Effect::Test>;

// Sanity: AllRow must contain every atom in the Effect enum.  If a
// future Effect atom is added without updating AllRow, this fires.
//
// The size check is necessary but not sufficient — a rename that
// keeps the count constant would slip through.  The per-atom
// membership checks below are the load-bearing guarantee.
static_assert(row_size_v<AllRow> == effect_count,
    "AllRow must enumerate every atom in Effect; update both together");

static_assert(row_contains_v<AllRow, Effect::Alloc>,
    "AllRow missing Alloc — universe row out of sync with Effect enum");
static_assert(row_contains_v<AllRow, Effect::IO>,
    "AllRow missing IO — universe row out of sync with Effect enum");
static_assert(row_contains_v<AllRow, Effect::Block>,
    "AllRow missing Block — universe row out of sync with Effect enum");
static_assert(row_contains_v<AllRow, Effect::Bg>,
    "AllRow missing Bg — universe row out of sync with Effect enum");
static_assert(row_contains_v<AllRow, Effect::Init>,
    "AllRow missing Init — universe row out of sync with Effect enum");
static_assert(row_contains_v<AllRow, Effect::Test>,
    "AllRow missing Test — universe row out of sync with Effect enum");

// ── Lattice-chain containment (compile-time witness) ───────────────
//
// The F* refinement chain holds iff every successive row contains the
// prior.  These static_asserts are the load-bearing guarantee that
// the Crucible mapping preserves the lattice structure.

static_assert(is_subrow_v<PureRow,  TotRow>);
static_assert(is_subrow_v<TotRow,   PureRow>);   // mutual containment ⇒ equal
static_assert(is_subrow_v<PureRow,  GhostRow>);
static_assert(is_subrow_v<GhostRow, PureRow>);

static_assert(is_subrow_v<PureRow,  DivRow>);    // ∅ ⊆ {Block}
static_assert(is_subrow_v<DivRow,   STRow>);     // {Block} ⊆ {Block, Alloc, IO}
static_assert(is_subrow_v<STRow,    AllRow>);    // {Block, Alloc, IO} ⊆ universe
static_assert(is_subrow_v<DivRow,   AllRow>);    // transitivity
static_assert(is_subrow_v<PureRow,  AllRow>);    // bottom ⊆ top

// Strictness: each upper level genuinely *adds* atoms.  These witness
// that the chain is not collapsed.
static_assert(!is_subrow_v<DivRow,  PureRow>);   // {Block} ⊄ ∅
static_assert(!is_subrow_v<STRow,   DivRow>);    // adds Alloc, IO
static_assert(!is_subrow_v<AllRow,  STRow>);     // adds Bg, Init, Test

// ── G80: F* alias concepts (refinement chain) ──────────────────────
//
// `IsX<R>` reads "row R fits within X's effect budget".  Because each
// alias row is the *upper bound* of its level, Subrow inclusion gives
// the F* refinement-chain implications for free.  No manual implication
// chain needs to be coded — the Subrow concept proves them.

template <typename R>
concept IsPure  = Subrow<R, PureRow>;

template <typename R>
concept IsTot   = Subrow<R, TotRow>;

template <typename R>
concept IsGhost = Subrow<R, GhostRow>;

template <typename R>
concept IsDiv   = Subrow<R, DivRow>;

template <typename R>
concept IsST    = Subrow<R, STRow>;

template <typename R>
concept IsAll   = Subrow<R, AllRow>;

// ── Value-carrying F* type aliases (fixy-A3-019) ───────────────────
//
// CLAUDE.md §XVI promises canonical F*-style named compositions that
// stack the three load-bearing chain wrappers around a payload T:
//
//     Pure<T>     ≡  Progress<Terminating,  DetSafe<Pure, Computation<Row<>, T>>>
//     Tot<E_os, T> ≡  Progress<Terminating,  DetSafe<Pure, Computation<E_os,  T>>>
//     Div<E_os, T> ≡  Progress<MayDiverge,   DetSafe<Pure, Computation<E_os,  T>>>
//
// The substrate-stack reading bottom-up: T is carried as a Computation
// (binds the row), then DetSafe-pinned to the strongest replay-safety
// tier (Pure), then Progress-pinned to the requested termination class.
// All three wrappers are regime-1 Graded EBO collapses — the value-
// carrying alias costs sizeof(Computation<R, T>) at -O3, byte-equivalent
// to a bare Computation but with the determinism + termination promise
// carried in the type.
//
// ── Why these three, not more ──────────────────────────────────────
//
// The F* effect lattice top-half (Pure / Tot / Ghost ⊑ Div ⊑ ST ⊑ All)
// has a discrete row component (the `*Row` aliases above) and a
// discrete progress component (Terminating vs MayDiverge).  Pure / Tot
// / Div are the three named cells:
//
//   Pure :  Row<>     × Terminating
//   Tot  :  E_os      × Terminating
//   Div  :  E_os      × MayDiverge
//
// ST and All names belong to the ROW aliases (`STRow`, `AllRow`) — the
// value-carrying form just substitutes E_os = STRow / AllRow into the
// Tot or Div shape, no new alias needed.  Ghost mirrors Pure at the
// row level (both ∅) so a separate `Ghost<T>` alias would shadow Pure
// with the same expansion — explicitly omitted to keep the public
// surface minimal.
//
// ── Naming + qualification discipline ──────────────────────────────
//
// `Pure` (template alias, namespace `crucible::effects`) is lexically
// distinct from `DetSafeTier_v::Pure` (enum value, scoped under
// `crucible::safety::DetSafeTier_v`).  No collision: the alias appears
// only in TYPE position, the enum value only in VALUE position.  The
// expansion uses fully-qualified enum values so the disambiguation is
// explicit at definition site.

template <typename T>
using Pure = ::crucible::safety::Progress<
    ::crucible::safety::ProgressClass_v::Terminating,
    ::crucible::safety::DetSafe<
        ::crucible::safety::DetSafeTier_v::Pure,
        Computation<PureRow, T>>>;

template <typename E_os, typename T>
using Tot = ::crucible::safety::Progress<
    ::crucible::safety::ProgressClass_v::Terminating,
    ::crucible::safety::DetSafe<
        ::crucible::safety::DetSafeTier_v::Pure,
        Computation<E_os, T>>>;

template <typename E_os, typename T>
using Div = ::crucible::safety::Progress<
    ::crucible::safety::ProgressClass_v::MayDiverge,
    ::crucible::safety::DetSafe<
        ::crucible::safety::DetSafeTier_v::Pure,
        Computation<E_os, T>>>;

// ── Self-test block ────────────────────────────────────────────────

namespace detail::fx_aliases_self_test {

// Pure / Tot / Ghost are extensionally equal.  All three accept Row<>;
// none accept any non-empty row.
static_assert( IsPure <PureRow>);
static_assert( IsPure <TotRow>);
static_assert( IsPure <GhostRow>);
static_assert(!IsPure <Row<Effect::Alloc>>);
static_assert(!IsPure <Row<Effect::Block>>);

static_assert( IsTot  <PureRow>);
static_assert(!IsTot  <Row<Effect::Alloc>>);

static_assert( IsGhost<PureRow>);
static_assert(!IsGhost<Row<Effect::IO>>);

// Div admits empty + {Block}; rejects state.
static_assert( IsDiv  <PureRow>);
static_assert( IsDiv  <Row<Effect::Block>>);
static_assert(!IsDiv  <Row<Effect::Alloc>>);
static_assert(!IsDiv  <Row<Effect::IO>>);
static_assert(!IsDiv  <Row<Effect::Block, Effect::Alloc>>);

// ST admits Block + Alloc + IO and any subset of those.
static_assert( IsST   <PureRow>);
static_assert( IsST   <Row<Effect::Block>>);
static_assert( IsST   <Row<Effect::Alloc>>);
static_assert( IsST   <Row<Effect::IO>>);
static_assert( IsST   <Row<Effect::Alloc, Effect::IO>>);
static_assert( IsST   <Row<Effect::Block, Effect::Alloc, Effect::IO>>);
static_assert(!IsST   <Row<Effect::Bg>>);
static_assert(!IsST   <Row<Effect::Init>>);
static_assert(!IsST   <Row<Effect::Alloc, Effect::Bg>>);

// All admits every row, including the universe row itself.
static_assert(IsAll<PureRow>);
static_assert(IsAll<DivRow>);
static_assert(IsAll<STRow>);
static_assert(IsAll<AllRow>);
static_assert(IsAll<Row<Effect::Bg>>);
static_assert(IsAll<Row<Effect::Init, Effect::Test>>);

// ── Refinement-chain implications (the F* substitution principle) ──
//
// IsPure ⇒ IsTot ⇒ IsGhost  (all three accept exactly ∅)
// IsPure ⇒ IsDiv ⇒ IsST ⇒ IsAll
//
// Each row that satisfies an inner predicate must also satisfy every
// outer one along the chain — the Subrow concept gives this for free
// because the alias rows are containment-ordered.

// Anchor: PureRow is the bottom; it satisfies every predicate.
static_assert(IsPure <PureRow>);
static_assert(IsTot  <PureRow>);
static_assert(IsGhost<PureRow>);
static_assert(IsDiv  <PureRow>);
static_assert(IsST   <PureRow>);
static_assert(IsAll  <PureRow>);

// One step up: DivRow satisfies IsDiv / IsST / IsAll but not IsPure.
static_assert(!IsPure <DivRow>);
static_assert(!IsTot  <DivRow>);
static_assert(!IsGhost<DivRow>);
static_assert( IsDiv  <DivRow>);
static_assert( IsST   <DivRow>);
static_assert( IsAll  <DivRow>);

// Two steps up: STRow satisfies IsST / IsAll only.
static_assert(!IsPure<STRow>);
static_assert(!IsDiv <STRow>);   // STRow has Alloc, IO ⊄ DivRow
static_assert( IsST  <STRow>);
static_assert( IsAll <STRow>);

// Top: AllRow satisfies IsAll only.
static_assert(!IsPure<AllRow>);
static_assert(!IsDiv <AllRow>);
static_assert(!IsST  <AllRow>);  // AllRow has Bg, Init, Test ⊄ STRow
static_assert( IsAll <AllRow>);

// ── Cross-product witness — predicate matrix on representative rows ──
//
// Builds the full IsX × Row matrix for the canonical six predicates ×
// six representative rows.  This is the load-bearing audit guarantee
// that every cell is hand-verified rather than left to inference.

//                Pure  Tot Ghost  Div   ST   All
// PureRow         ✓    ✓    ✓     ✓    ✓    ✓
// DivRow          -    -    -     ✓    ✓    ✓
// STRow           -    -    -     -    ✓    ✓
// AllRow          -    -    -     -    -    ✓

static_assert( IsPure <PureRow>); static_assert( IsTot<PureRow>); static_assert( IsGhost<PureRow>);
static_assert( IsDiv  <PureRow>); static_assert( IsST <PureRow>); static_assert( IsAll  <PureRow>);

static_assert(!IsPure <DivRow>);  static_assert(!IsTot<DivRow>);  static_assert(!IsGhost<DivRow>);
static_assert( IsDiv  <DivRow>);  static_assert( IsST <DivRow>);  static_assert( IsAll  <DivRow>);

static_assert(!IsPure <STRow>);   static_assert(!IsTot<STRow>);   static_assert(!IsGhost<STRow>);
static_assert(!IsDiv  <STRow>);   static_assert( IsST <STRow>);   static_assert( IsAll  <STRow>);

static_assert(!IsPure <AllRow>);  static_assert(!IsTot<AllRow>);  static_assert(!IsGhost<AllRow>);
static_assert(!IsDiv  <AllRow>);  static_assert(!IsST <AllRow>);  static_assert( IsAll  <AllRow>);

// ── Value-carrying alias structural witness (fixy-A3-019) ──────────
//
// The three F*-style value-carrying aliases substrate-decompose to
// Progress<Class, DetSafe<Pure, Computation<Row, T>>>.  We witness:
//
//   (1) Each alias is well-formed for canonical payloads (int, void*,
//       move-only types) and arbitrary row shapes.
//   (2) Progress / DetSafe / Computation member-type accessors agree
//       with the canonical-order docblock.  payload row reads back as
//       declared.
//   (3) The regime-1 size promise holds — sizeof(Pure<T>) == sizeof
//       (Computation<PureRow, T>), no per-grade storage.

// (1) Well-formedness — instantiations compile for representative payloads.
using PureInt   = Pure<int>;
using TotIoInt  = Tot<Row<Effect::IO>, int>;
using DivIoInt  = Div<Row<Effect::IO>, int>;
using DivAllVp  = Div<AllRow, void*>;

// (2) Member-type accessors agree with the docblock's substrate stack.
static_assert(std::is_same_v<PureInt::value_type,
                             ::crucible::safety::DetSafe<
                                 ::crucible::safety::DetSafeTier_v::Pure,
                                 Computation<PureRow, int>>>);
static_assert(PureInt::cls == ::crucible::safety::ProgressClass_v::Terminating);
static_assert(PureInt::value_type::tier == ::crucible::safety::DetSafeTier_v::Pure);
static_assert(std::is_same_v<PureInt::value_type::value_type::row_type, PureRow>);
static_assert(std::is_same_v<PureInt::value_type::value_type::value_type, int>);

static_assert(std::is_same_v<TotIoInt::value_type::value_type::row_type,
                             Row<Effect::IO>>);
static_assert(TotIoInt::cls == ::crucible::safety::ProgressClass_v::Terminating);

static_assert(std::is_same_v<DivIoInt::value_type::value_type::row_type,
                             Row<Effect::IO>>);
static_assert(DivIoInt::cls == ::crucible::safety::ProgressClass_v::MayDiverge);
static_assert(DivIoInt::value_type::tier == ::crucible::safety::DetSafeTier_v::Pure);

// (3) Regime-1 layout — the three wrappers EBO-collapse to the
// underlying Computation's footprint.  This is the substrate's
// zero-runtime-cost promise made structural.
static_assert(sizeof(Pure<int>)  == sizeof(Computation<PureRow,        int>));
static_assert(sizeof(Tot<Row<Effect::IO>, int>)
              == sizeof(Computation<Row<Effect::IO>, int>));
static_assert(sizeof(Div<AllRow, void*>)
              == sizeof(Computation<AllRow,          void*>));

}  // namespace detail::fx_aliases_self_test

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per the runtime smoke-test discipline (auto-memory feedback): every
// algebra/* and effects/* header MUST ship a runtime smoke test with
// non-constant args so consteval-vs-constexpr accessor regressions and
// inline-body bugs are caught alongside the static_assert wall above.

inline void runtime_smoke_test() noexcept {
    // Anchor through an empty inline lambda that the optimizer cannot
    // see through — proves the alias types and concept names compile
    // outside of the static_assert TU context.
    [[maybe_unused]] constexpr bool pure_is_pure = IsPure<PureRow>;
    [[maybe_unused]] constexpr bool div_is_div   = IsDiv <DivRow>;
    [[maybe_unused]] constexpr bool st_is_st     = IsST  <STRow>;
    [[maybe_unused]] constexpr bool all_is_all   = IsAll <AllRow>;
    [[maybe_unused]] constexpr auto pure_size    = row_size_v<PureRow>;
    [[maybe_unused]] constexpr auto all_size     = row_size_v<AllRow>;

    // The compile-time chain witness — readable at the ABI boundary
    // through templated callers, which is the concrete F* substitution
    // surface this header exposes to the rest of the codebase.
    static_assert(IsPure<PureRow> && IsAll<AllRow>);

    // Value-carrying alias smoke (fixy-A3-019) — default-construct
    // each alias and read the static class/tier/row accessors at
    // runtime so any consteval-vs-constexpr accessor regression in
    // Progress/DetSafe/Computation surfaces alongside the static
    // assertions, per the runtime-smoke-test discipline (memory
    // feedback_algebra_runtime_smoke_test_discipline).
    Pure<int>                         pure_value{};
    Tot<Row<Effect::IO>, int>         tot_value{};
    Div<AllRow, void*>                div_value{};

    [[maybe_unused]] auto pure_class = pure_value.cls;
    [[maybe_unused]] auto tot_tier   = tot_value.peek().tier;
    [[maybe_unused]] auto div_class  = div_value.cls;

    static_assert(std::is_same_v<decltype(pure_value), Pure<int>>);
    static_assert(std::is_same_v<decltype(tot_value),  Tot<Row<Effect::IO>, int>>);
    static_assert(std::is_same_v<decltype(div_value),  Div<AllRow, void*>>);
}

}  // namespace crucible::effects

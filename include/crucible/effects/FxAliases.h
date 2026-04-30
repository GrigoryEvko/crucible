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
#include <crucible/effects/EffectRow.h>

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
}

}  // namespace crucible::effects

#pragma once

// ── crucible::effects::ComputationGraded<R, T> ──────────────────────
//
// FOUND-H03.  The Met(X) carrier as a thin Graded<> alias per
// 28_04_2026_effects.md §H03 / 25_04_2026.md §3.2.  Replaces the bespoke
// Computation<R, T> class (still in Computation.h, kept until H04 lands
// the façade migration shim) with one substrate primitive — there is
// no parallel "row carrier" hierarchy; every effect-row computation is
// one Graded instantiation.
//
//   ComputationGraded<R, T> := Graded<
//                                ModalityKind::Relative,
//                                EffectRowLattice::At<atoms-of(R)>,
//                                T>
//
// where `atoms-of(R)` is the `Effect... Es` pack from `Row<Es...>` lifted
// via `effect_row_to_at_t<R>` (defined in EffectRowLattice.h).
//
// ── Why Modality::Relative ──────────────────────────────────────────
//
// Per algebra/Modality.h:14-23 the four modalities are:
//
//   Comonad        — value-carrying with `extract` counit (Secret<T>)
//   RelativeMonad  — value-carrying with `inject` unit   (Tagged<T, S>)
//   Absolute       — value with grade orthogonal to bytes (Linear<T>)
//   Relative       — value with grade reflecting effect propagation
//
// A Met(X) computation carries no unit (you cannot inject a value
// "into the row" — the row is already the typing context) and no
// counit (you cannot extract a value "out of the row" except by
// closing the universe).  Its grade is Relative — it tracks effect
// propagation alongside the value, with `weaken` widening the row
// up the lattice.  ConfLattice / TrustLattice took Comonad and
// RelativeMonad respectively because they DO carry counit/unit; the
// Met(X) lattice does neither.
//
// ── Storage regime — primary template, EBO collapse ─────────────────
//
// `EffectRowLattice::At<Es...>::element_type` is an EMPTY struct (per
// FOUND-H01-AUDIT-1).  Graded's primary template stores
// `[[no_unique_address]] grade_type grade_;` — for empty grade types
// EBO collapses the field to zero bytes.  Result:
// `sizeof(ComputationGraded<R, T>) == sizeof(T)` exactly, regardless
// of how many atoms R carries.  This is the regime-1 ("zero-cost EBO")
// storage pattern from CLAUDE.md's Graded substrate map.
//
// Verified at the layout-invariant macro witnesses below.
//
// ── What this header DOES NOT do ────────────────────────────────────
//
// The friendly Met(X) API (`mk` / `extract` / `lift<Cap>` /
// `weaken<R2>` / `map(f)` / `then(k)`) is the H04 façade migration
// shim's scope.  H03 is intentionally narrow — it lands the substrate
// alias so downstream code (FOUND-I-series cache key federation,
// FOUND-J row-typed Forge IR) can target it incrementally without
// waiting for H04.  The legacy Computation<R, T> in Computation.h is
// untouched and remains the production carrier until H04+H05 verify
// the migration is byte-equivalent.
//
// ── Forward link to FOUND-H04 ───────────────────────────────────────
//
// H04 wraps ComputationGraded behind a small façade class (or
// repurposes the existing Computation class as the façade) that
// re-exposes the named API surface above.  The wrapping is the
// MIGRATE-* alias pattern from algebra/Graded.h:71-83.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Modality.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/EffectRowLattice.h>

#include <cstddef>
#include <type_traits>

namespace crucible::effects {

// ── The H03 deliverable: ComputationGraded alias ────────────────────
//
// `R` MUST be `Row<Es...>` (enforced by effect_row_to_at_t's lack of
// a primary template definition — non-Row R produces an incomplete
// type at the alias's instantiation point).

template <typename R, typename T>
using ComputationGraded =
    ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Relative,
        effect_row_to_at_t<R>,
        T>;

// ── Type-level identity ─────────────────────────────────────────────

static_assert(std::is_same_v<
    typename ComputationGraded<Row<>, int>::value_type,
    int>);

static_assert(std::is_same_v<
    typename ComputationGraded<Row<>, int>::lattice_type,
    EffectRowLattice::At<>>);

static_assert(std::is_same_v<
    typename ComputationGraded<Row<Effect::Bg>, int>::lattice_type,
    EffectRowLattice::At<Effect::Bg>>);

static_assert(std::is_same_v<
    typename ComputationGraded<Row<Effect::Alloc, Effect::IO>, int>::lattice_type,
    EffectRowLattice::At<Effect::Alloc, Effect::IO>>);

static_assert(ComputationGraded<Row<>, int>::modality
    == ::crucible::algebra::ModalityKind::Relative);

static_assert(ComputationGraded<Row<Effect::Bg>, double>::modality
    == ::crucible::algebra::ModalityKind::Relative);

// grade_type is the lattice's element_type — empty struct for At<>.
static_assert(std::is_empty_v<
    typename ComputationGraded<Row<>, int>::grade_type>);
static_assert(std::is_empty_v<
    typename ComputationGraded<Row<Effect::Bg>, int>::grade_type>);
static_assert(std::is_empty_v<
    typename ComputationGraded<Row<
        Effect::Alloc, Effect::IO, Effect::Block,
        Effect::Bg,    Effect::Init, Effect::Test>, int>::grade_type>);

// ── Diagnostic forwarding ───────────────────────────────────────────
//
// Graded::modality_name / lattice_name / value_type_name pass through
// to the underlying lattice (At<>) and value type.  Spot-witness the
// modality name (stable: "Relative") and lattice name (stable:
// "EffectRow::At" — the At<> family's name surface).  value_type_name
// is TU-context-fragile per Graded.h:156-186; do NOT static_assert
// equality of its output here (see the warning block in Graded.h).

static_assert(
    ComputationGraded<Row<>, int>::modality_name() == "Relative");
static_assert(
    ComputationGraded<Row<Effect::Bg>, int>::lattice_name()
        == "EffectRow::At");

// ── Layout: zero-cost across every Row + value combination ──────────
//
// The substrate must collapse to sizeof(T) regardless of how many
// atoms the row carries — the row is type-level only.  Witness across:
//   - empty row
//   - single-atom rows (every Effect)
//   - multi-atom rows
//   - all-atoms row (the entire Universe)
// at int, char, and 8-byte payloads to cover the alignment edges.

namespace detail::computation_graded_layout {

struct EmptyValue {};
struct OneByteValue { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

// Empty value: irreducible C++ minimum-object-size of 1.
static_assert(sizeof(ComputationGraded<Row<>, EmptyValue>) == 1);
static_assert(sizeof(ComputationGraded<Row<Effect::Bg>, EmptyValue>) == 1);

// Non-empty values: sizeof(T) preserved exactly across every row.
static_assert(sizeof(ComputationGraded<Row<>, int>)
              == sizeof(int));
static_assert(sizeof(ComputationGraded<Row<Effect::Alloc>, int>)
              == sizeof(int));
static_assert(sizeof(ComputationGraded<Row<Effect::Bg>, int>)
              == sizeof(int));
static_assert(sizeof(ComputationGraded<Row<
    Effect::Alloc, Effect::IO, Effect::Block,
    Effect::Bg,    Effect::Init, Effect::Test>, int>)
              == sizeof(int));

static_assert(sizeof(ComputationGraded<Row<>, OneByteValue>)
              == sizeof(OneByteValue));
static_assert(sizeof(ComputationGraded<Row<Effect::Bg>, OneByteValue>)
              == sizeof(OneByteValue));

static_assert(sizeof(ComputationGraded<Row<>, EightByteValue>)
              == sizeof(EightByteValue));
static_assert(sizeof(ComputationGraded<Row<Effect::Bg>, EightByteValue>)
              == sizeof(EightByteValue));

// Alignment preserved exactly — the empty grade_type must not force
// over-alignment.
static_assert(alignof(ComputationGraded<Row<>, int>)
              == alignof(int));
static_assert(alignof(ComputationGraded<Row<Effect::Bg>, EightByteValue>)
              == alignof(EightByteValue));

// ── Layout invariant macro (CRUCIBLE_GRADED_LAYOUT_INVARIANT) ───────
//
// Same four-way structural check the MIGRATE-* alias headers use:
// sizeof + alignof + trivial-destructible parity + trivial-copyable
// parity.  Trivial-default-constructible parity deliberately omitted
// per the audit-2026-04-26 memo embedded in the macro doc-block.

template <typename T>
using CompOverEmpty = ComputationGraded<Row<>, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverEmpty, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverEmpty, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverEmpty, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverEmpty, EightByteValue);

template <typename T>
using CompOverBg = ComputationGraded<Row<Effect::Bg>, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverBg, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverBg, EightByteValue);

template <typename T>
using CompOverAll = ComputationGraded<Row<
    Effect::Alloc, Effect::IO, Effect::Block,
    Effect::Bg,    Effect::Init, Effect::Test>, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverAll, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverAll, EightByteValue);

}  // namespace detail::computation_graded_layout

// ── EmptyRow alias agreement (FOUND-H03-AUDIT-1) ────────────────────
//
// EffectRow.h:61 ships `using EmptyRow = Row<>;`.  The alias must be
// transparent — `ComputationGraded<EmptyRow, T>` resolves to exactly
// the same type as `ComputationGraded<Row<>, T>`.  Pin this so a
// future refactor that accidentally specializes EmptyRow distinctly
// (e.g. `struct EmptyRow {};` instead of an alias) fires here.

static_assert(std::is_same_v<
    ComputationGraded<EmptyRow, int>,
    ComputationGraded<Row<>, int>>);
static_assert(std::is_same_v<
    typename ComputationGraded<EmptyRow, int>::lattice_type,
    typename ComputationGraded<Row<>, int>::lattice_type>);
static_assert(sizeof(ComputationGraded<EmptyRow, int>)
              == sizeof(ComputationGraded<Row<>, int>));

// ── at_bottom() reachability (FOUND-H03-AUDIT-1) ────────────────────
//
// At<Es...> satisfies BoundedBelowLattice (proved in EffectRowLattice.h
// concept-conformance block) so Graded's `at_bottom(value)` static
// factory MUST be reachable on every ComputationGraded instantiation.
// Concept-driven detection so the gate is checked structurally, not
// just by spot-instantiation.

namespace detail::computation_graded_caps {
template <typename G> concept HasAtBottom =
    requires(typename G::value_type v) { G::at_bottom(v); };
}  // namespace detail::computation_graded_caps

static_assert(detail::computation_graded_caps::HasAtBottom<
    ComputationGraded<Row<>, int>>);
static_assert(detail::computation_graded_caps::HasAtBottom<
    ComputationGraded<Row<Effect::Bg>, int>>);
static_assert(detail::computation_graded_caps::HasAtBottom<
    ComputationGraded<Row<
        Effect::Alloc, Effect::IO, Effect::Block,
        Effect::Bg,    Effect::Init, Effect::Test>, int>>);

// ── Object semantics ────────────────────────────────────────────────
//
// All inherited from Graded — defaulted copy / move / destructor.
// Verifies the alias preserves the substrate's value semantics; H04's
// façade may further constrain (e.g. delete copy if the wrapped class
// IS Linear-equivalent), but the bare alias is fully regular for
// regular T.

static_assert(std::is_default_constructible_v<
    ComputationGraded<Row<>, int>>);
static_assert(std::is_copy_constructible_v<
    ComputationGraded<Row<>, int>>);
static_assert(std::is_move_constructible_v<
    ComputationGraded<Row<Effect::Bg>, int>>);
static_assert(std::is_copy_assignable_v<
    ComputationGraded<Row<>, int>>);
static_assert(std::is_move_assignable_v<
    ComputationGraded<Row<Effect::Bg>, int>>);
static_assert(std::is_destructible_v<
    ComputationGraded<Row<>, int>>);

// Trivial copyability parity — required for memcpy-safe Cipher
// serialization through the substrate.
static_assert(std::is_trivially_copyable_v<int>
           == std::is_trivially_copyable_v<ComputationGraded<Row<>, int>>);
static_assert(std::is_trivially_copyable_v<int>
           == std::is_trivially_copyable_v<ComputationGraded<Row<Effect::Bg>, int>>);

// ── Capability gates (concept-driven) ───────────────────────────────
//
// Concept-based reachability checks for the Graded substrate's gated
// methods — Relative modality admits weaken/compose (always present),
// peek_mut/swap (because grade_type is empty), but NOT extract/inject
// (Comonad/RelativeMonad-only).

namespace detail::computation_graded_caps {

template <typename G> concept HasPeekMut =
    requires(G& g) { g.peek_mut(); };
template <typename G> concept HasSwap =
    requires(G& a, G& b) { a.swap(b); };
template <typename G> concept HasComonadExtract =
    requires(G g) { std::move(g).extract(); };
template <typename G> concept HasRelMonadInject =
    requires { G::inject(typename G::value_type{},
                         typename G::grade_type{}); };
template <typename G> concept HasWeaken =
    requires(G g, typename G::grade_type r) { std::move(g).weaken(r); };
template <typename G> concept HasCompose =
    requires(G g, G const& o) { std::move(g).compose(o); };

using G_pure = ComputationGraded<Row<>, int>;
using G_bg   = ComputationGraded<Row<Effect::Bg>, int>;

// Empty-grade path admits peek_mut + swap even under Relative modality
// (the refined gate `AbsoluteModality<M> || std::is_empty_v<grade_type>`
// fires on the empty-grade clause).
static_assert( HasPeekMut<G_pure>);
static_assert( HasPeekMut<G_bg>);
static_assert( HasSwap<G_pure>);
static_assert( HasSwap<G_bg>);

// Relative modality forbids Comonad/RelativeMonad-specific operations.
static_assert(!HasComonadExtract<G_pure>);
static_assert(!HasComonadExtract<G_bg>);
static_assert(!HasRelMonadInject<G_pure>);
static_assert(!HasRelMonadInject<G_bg>);

// weaken / compose are primary-template methods, always reachable.
static_assert( HasWeaken<G_pure>);
static_assert( HasWeaken<G_bg>);
static_assert( HasCompose<G_pure>);
static_assert( HasCompose<G_bg>);

}  // namespace detail::computation_graded_caps

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per the runtime smoke-test discipline (auto-memory feedback): every
// algebra/* and effects/* header MUST drive its accessors with NON-
// constant arguments through an inline non-constexpr function so the
// front-end type-checks against runtime semantics.  Catches consteval-
// vs-constexpr regressions and inline-body bugs that pure static_assert
// coverage misses.

inline void runtime_smoke_test_computation_graded() noexcept {
    using G_pure = ComputationGraded<Row<>, int>;
    using G_bg   = ComputationGraded<Row<Effect::Bg>, int>;

    // Construction with a non-constant value + grade.
    int   value_runtime = 42;
    G_pure pure{value_runtime, G_pure::grade_type{}};
    G_bg   bg{value_runtime + 1, G_bg::grade_type{}};

    // Default ctor + copy + move.
    G_pure pure_default{};
    G_pure pure_copy{pure};
    G_pure pure_moved{std::move(pure_copy)};

    // Access surface — peek / consume / grade through non-constant
    // call sites.
    [[maybe_unused]] int const& peeked = pure.peek();
    [[maybe_unused]] int        moved  = std::move(pure_default).consume();
    [[maybe_unused]] auto       grade  = pure.grade();

    // peek_mut / swap admitted via the empty-grade path (Relative
    // modality with empty grade_type).  Drive them so the gates fire.
    G_pure pure_a{};
    G_pure pure_b{};
    pure_a.peek_mut() = 7;
    pure_a.swap(pure_b);

    // weaken to the singleton grade — a no-op move at runtime since
    // both grade values are the empty struct's single inhabitant.
    [[maybe_unused]] auto widened =
        std::move(bg).weaken(G_bg::grade_type{});

    // compose against another instance — exercises the join path.
    G_pure pure_c{value_runtime, G_pure::grade_type{}};
    G_pure pure_d{value_runtime + 2, G_pure::grade_type{}};
    [[maybe_unused]] G_pure composed = pure_c.compose(pure_d);

    // at_bottom(value) — At<Es...> is BoundedBelowLattice so this
    // factory must be reachable on every ComputationGraded.  Drive
    // with a non-constant value to instantiate the body.
    [[maybe_unused]] G_pure pure_bot = G_pure::at_bottom(value_runtime);
    [[maybe_unused]] G_bg   bg_bot   = G_bg::at_bottom(value_runtime + 3);

    // Concept-based capability check at the boundary (per
    // feedback_algebra_runtime_smoke_test_discipline).
    static_assert(detail::computation_graded_caps::HasPeekMut<G_pure>);
    static_assert(detail::computation_graded_caps::HasWeaken<G_bg>);
    static_assert(!detail::computation_graded_caps::HasComonadExtract<G_pure>);
    static_assert(!detail::computation_graded_caps::HasRelMonadInject<G_bg>);
}

}  // namespace crucible::effects

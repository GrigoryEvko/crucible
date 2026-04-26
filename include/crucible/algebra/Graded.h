#pragma once

// ── crucible::algebra::Graded<Modality, Lattice, T> ─────────────────
//
// THE foundational primitive of the 25_04_2026.md §2 refactor.  Every
// safety wrapper that decorates a value (Linear, Refined, Tagged,
// Secret, Monotonic, AppendOnly, SharedPermission, plus future
// Stale, Budgeted, TimeOrdered, SealedRefined) folds into a
// `Graded<M, L, T>` instantiation under this template.
//
//   Axiom coverage: every axiom inherits from the wrapped T plus the
//                   lattice's structural witnesses.  Linearity comes
//                   from QttSemiring (grade 1); predicate refinement
//                   from BoolLattice; classification from ConfLattice;
//                   trust provenance from TrustLattice; etc.  The
//                   Graded class adds no axiom obligations of its own
//                   — it composes them.
//   Runtime cost:   zero when both T AND LatticeElement<L> are empty
//                   types (e.g. Linear<Tag> with QTT::At<1> grade).
//                   sizeof(T) when T is non-empty and grade is empty
//                   (e.g. Linear<int>).  sizeof(T) +
//                   sizeof(LatticeElement<L>) when both carry runtime
//                   state (e.g. SharedPermission<Tag> with rational
//                   grade).  Both fields use [[no_unique_address]] so
//                   the runtime cost matches the underlying primitives
//                   exactly.  Verified codebase-wide by ALGEBRA-15
//                   (#460) under -O3.
//
// Design — type-level vs runtime grade:
//
//   For STATIC-grade lattices (QttSemiring::At<1>, BoolLattice<Pred>,
//   ConfLattice's two-point lattice), the LatticeElement is an empty
//   class (e.g. `std::integral_constant<int, 1>`), grade_ collapses
//   to zero bytes via EBO, and the grade is encoded entirely at the
//   type level via the lattice template parameter L itself.
//
//   For DYNAMIC-grade lattices (FractionalLattice with rational
//   shares, StalenessSemiring with ℕ ∪ ∞ values), LatticeElement is
//   a runtime-sized type, grade_ stores the per-instance grade, and
//   `weaken` / `compose` operate on it at runtime.
//
//   Both shapes share the same source — ALGEBRA-3 ships ONE Graded
//   that accommodates both via [[no_unique_address]].  No template
//   specialization, no opt-in trait — the lattice's element type is
//   the discriminator.
//
// Public API:
//
//   Construction:
//     Graded(value, grade)           — explicit grade
//     Graded::at_bottom(value)       — uses L::bottom() as initial grade
//                                      (only when BoundedBelowLattice<L>)
//     inject(value, grade)           — RelativeMonad-form unit
//   Access:
//     peek() const&                  — borrow inner value
//     consume() &&                   — move inner value out (rvalue this)
//     grade() const                  — current lattice element
//   Operations:
//     extract() &&                   — Comonad counit (requires Comonad)
//     weaken(new_grade)              — contract-checked widening
//     compose(other)                 — grade ⊕ other.grade, *this's value
//
// Note for MIGRATE-* alias implementers:
//
//   Graded's copy/move semantics are DEFAULTED — copying preserves
//   both value and grade.  Aliases that need stricter semantics
//   (e.g. Linear<T> = Graded<Absolute, QttSemiring::At<1>, T> must
//   delete copy to enforce linearity) CANNOT be a bare type alias.
//   They must wrap Graded in a class that re-imposes the discipline:
//
//       template <typename T>
//       class Linear {
//           Graded<Absolute, QttSemiring::At<1>, T> impl_;
//       public:
//           Linear(const Linear&) = delete("Linear<T> is move-only");
//           // ... selective re-export of impl_'s API
//       };
//
//   The 25_04_2026.md §2.3 "using Linear = Graded<...>" is shorthand
//   that hides this wrapping; MIGRATE-1 (#461) makes the wrapping
//   explicit.  Aliases for non-linear shapes (Refined, Tagged,
//   Secret, ...) CAN be bare aliases — Graded's defaulted semantics
//   match the existing wrappers' defaulted semantics in safety/.
//
// See ALGEBRA-1 (Modality.h), ALGEBRA-2 (Lattice.h), ALGEBRA-4..15
// (concrete lattice instantiations under lattices/).  Lean
// formalization in lean/Crucible/Algebra/Graded.lean per LEAN-1
// (#490).
//
// ── C++26 arsenal usage in this header ──────────────────────────────
//
//   P2900R14 contracts (pre/post)        — invariant checking on
//                                           weaken/compose/at_bottom/
//                                           inject; contract-evaluation-
//                                           semantic per TU
//   P2996R13 reflection (^^T, splice)    — value_type_name() emits T's
//                                           reflected display string
//   P3491R3 define_static_array          — paired with template for in
//                                           Modality.h / Capabilities.h
//                                           name-coverage assertions
//   [[no_unique_address]] (C++20)        — EBO collapse on inner_ AND
//                                           grade_ for empty types
//   [[nodiscard]] at class + method      — captured at construction;
//                                           [[nodiscard]] returns
//   `requires` clauses on overloads      — copy_constructible<T> SFINAE
//                                           gates const& weaken/compose
//                                           cleanly for move-only T
//   std::is_layout_compatible_v          — strengthened layout macro
//                                           verifies sizeof AND alignof
//                                           AND layout-compat with T

#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/Modality.h>

#include <contracts>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra {

// ── Graded class template ───────────────────────────────────────────
template <ModalityKind M, Lattice L, typename T>
class [[nodiscard]] Graded {
    static_assert(IsModality<M>,
        "Graded<M, L, T>: M must be one of Comonad / RelativeMonad / "
        "Absolute / Relative.  See algebra/Modality.h.");

public:
    // ── Public type aliases ─────────────────────────────────────────
    static constexpr ModalityKind modality = M;

    using modality_kind_type = ModalityKind;
    using lattice_type       = L;
    using value_type         = T;
    using grade_type         = LatticeElement<L>;

private:
    // ── Layout (NSDMI per InitSafe; EBO for empty types) ────────────
    [[no_unique_address]] T          inner_{};
    [[no_unique_address]] grade_type grade_{};

public:
    // ── Diagnostic ──────────────────────────────────────────────────
    [[nodiscard]] static consteval std::string_view modality_name() noexcept {
        return ::crucible::algebra::modality_name(M);
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return ::crucible::algebra::lattice_name<L>();
    }
    // Reflection-derived (P2996R13): emits T's actual type name as it
    // appears in source.  Used by SessionDiagnostic / Cipher serialize
    // / debug print to identify what Graded wraps.
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return std::meta::display_string_of(^^T);
    }

    // ── Object semantics (defaulted; implicit noexcept inferred from T
    //     and grade_type — explicit noexcept on `= default` would be
    //     ill-formed if either had a throwing default ctor) ──────────
    constexpr Graded()                         = default;
    constexpr Graded(const Graded&)            = default;
    constexpr Graded(Graded&&)                 = default;
    constexpr Graded& operator=(const Graded&) = default;
    constexpr Graded& operator=(Graded&&)      = default;
    ~Graded()                                  = default;

    // ── Explicit construction with value + grade ────────────────────
    //
    // Both arguments by value; std::move into the members.  For empty
    // grade types the move is a no-op (EBO-collapsed); for non-empty
    // grades (FractionalLattice's rational, StalenessSemiring's int)
    // moving avoids an extra copy when the caller already had an
    // rvalue.
    constexpr Graded(T value, grade_type grade) noexcept(
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_move_constructible_v<grade_type>)
        : inner_{std::move(value)}, grade_{std::move(grade)} {}

    // ── Construction at the lattice's bottom element (when bounded) ─
    //
    // Convenience constructor for BoundedBelowLattice<L>: omits the
    // grade argument and starts at L::bottom().  Used by alias
    // wrappers whose default position is "no information accumulated"
    // (e.g. AppendOnly<T> starting at the empty-prefix lattice
    // bottom; Monotonic<T, std::less<>> starting at MIN).
    [[nodiscard]] static constexpr Graded at_bottom(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        requires BoundedBelowLattice<L>
    {
        // post (r: equivalent<L>(r.grade(), L::bottom())) — wanted, but
        // GCC 16.0.1 ICEs (cp/pt.cc:17244) on template-dependent
        // function calls in post predicates.  See feedback memory
        // gcc16_c26_contract_gotchas for the limitation.  The body is
        // a direct return Graded{..., L::bottom()} so the post would
        // be tautologically true anyway.
        return Graded{std::move(value), L::bottom()};
    }

    // ── Access ──────────────────────────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return inner_;
    }
    [[nodiscard]] constexpr T consume() && noexcept(
        std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(inner_);
    }
    [[nodiscard]] constexpr grade_type grade() const noexcept(
        std::is_nothrow_copy_constructible_v<grade_type>)
    {
        return grade_;
    }

    // ── Comonad: counit (extract from a Comonad-form value) ─────────
    //
    // Aliased wrappers may rename this — Secret<T>::declassify<Policy>()
    // is the named counit with audit-discoverable policy tag.  The
    // bare Graded::extract() is the unnamed counit.
    [[nodiscard]] constexpr T extract() && noexcept(
        std::is_nothrow_move_constructible_v<T>)
        requires ComonadModality<M>
    {
        return std::move(inner_);
    }

    // ── RelativeMonad: unit (inject into a RelativeMonad-form value) ─
    //
    // Grade taken `const` per P2900R14 — by-value postcondition
    // parameters must be const.  Local copy `g` is non-const so we
    // can std::move it into the constructor; copy is no-op for empty
    // grade types and one small-struct copy for non-empty.
    [[nodiscard]] static constexpr Graded inject(T value, grade_type grade) noexcept(
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_move_constructible_v<grade_type>)
        requires RelativeMonadModality<M>
    {
        // post (r: equivalent<L>(r.grade(), grade)) — wanted, blocked
        // by GCC 16 ICE on template-dependent post predicates.
        return Graded{std::move(value), std::move(grade)};
    }

    // ── Lattice operations ──────────────────────────────────────────

    // weaken: widen the grade.  Contract-checks `L::leq(current, new)`
    // — weakening only goes UP the lattice, never DOWN.  The const&
    // overload is gated on copy_constructible<T> so move-only T types
    // (the eventual Linear<T>) cleanly fall through to the && overload
    // instead of producing a noisy copy-deleted error.
    //
    // No `post` — GCC 16.0.1 ICEs (cp/pt.cc:17244) on template-
    // dependent function calls in post predicates.  When the GCC bug
    // is fixed, add: post (r: equivalent<L>(r.grade(), new_grade))
    //
    // C++26 clause order: noexcept → requires → pre → body.
    [[nodiscard]] constexpr Graded weaken(grade_type new_grade) const&
        noexcept(std::is_nothrow_copy_constructible_v<T> &&
                 std::is_nothrow_copy_constructible_v<grade_type>)
        requires std::copy_constructible<T>
        pre (L::leq(grade_, new_grade))
    {
        return Graded{inner_, new_grade};
    }

    [[nodiscard]] constexpr Graded weaken(grade_type new_grade) &&
        noexcept(std::is_nothrow_move_constructible_v<T> &&
                 std::is_nothrow_move_constructible_v<grade_type>)
        pre (L::leq(grade_, new_grade))
    {
        return Graded{std::move(inner_), std::move(new_grade)};
    }

    // compose: join grades via L::join.  Value comes from *this; the
    // other Graded contributes only its grade.  Right-biased on value
    // for symmetry with the Reader-monad analogy.  Same const&-vs-&&
    // ref-qualifier discipline as weaken.  No `post` for the same
    // GCC 16 ICE reason as weaken.
    [[nodiscard]] constexpr Graded compose(Graded const& other) const&
        noexcept(std::is_nothrow_copy_constructible_v<T> &&
                 std::is_nothrow_copy_constructible_v<grade_type>)
        requires std::copy_constructible<T>
    {
        return Graded{inner_, L::join(grade_, other.grade_)};
    }

    [[nodiscard]] constexpr Graded compose(Graded const& other) &&
        noexcept(std::is_nothrow_move_constructible_v<T> &&
                 std::is_nothrow_copy_constructible_v<grade_type>)
    {
        return Graded{std::move(inner_), L::join(grade_, other.grade_)};
    }
};

// ── Layout invariant macro (used by MIGRATE-* alias headers) ───────
//
// Each MIGRATE-1..11 alias header invokes this macro at instantiation
// witnesses to prove the wrapper carries zero overhead vs the
// underlying T.  Three-way check:
//
//   1. sizeof(Graded<T>)  == sizeof(T)
//   2. alignof(Graded<T>) == alignof(T)
//   3. std::is_layout_compatible_v<Graded<T>, T>  (C++26 P2674R3)
//
// All three must hold for the EBO-collapse path to truly be
// transparent.  Failures point to the offending alias + T pair.
#define CRUCIBLE_GRADED_LAYOUT_INVARIANT(GradedAlias, T_)                   \
    static_assert(sizeof(GradedAlias<T_>) == sizeof(T_),                    \
                  "Graded alias " #GradedAlias " over " #T_                 \
                  ": sizeof mismatch — review [[no_unique_address]] usage " \
                  "and lattice element type");                              \
    static_assert(alignof(GradedAlias<T_>) == alignof(T_),                  \
                  "Graded alias " #GradedAlias " over " #T_                 \
                  ": alignof mismatch — over-aligned grade_type forced "    \
                  "wrapper alignment > T's alignment")

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::graded_self_test {

using ::crucible::algebra::detail::lattice_self_test::TrivialBoolLattice;

// A trivially-empty-grade lattice — used to demonstrate the EBO
// collapse path (sizeof(Graded<...>) == sizeof(T) when both inner
// AND grade are empty).  Concrete static-grade lattices like
// QttSemiring::At<N> ship per ALGEBRA-4..14; this in-house witness
// proves the layout invariant in the absence of those headers.
//
// Operations are `constexpr` (NOT consteval) per the convention in
// algebra/Lattice.h — Graded's `pre (L::leq(...))` is evaluated at
// runtime under enforce semantic and must be able to call leq with
// non-constant arguments.
struct TrivialEmptyLattice {
    using element_type = std::integral_constant<int, 1>;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
    [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
    [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "TrivialEmpty"; }
};
static_assert(Lattice<TrivialEmptyLattice>);
static_assert(BoundedLattice<TrivialEmptyLattice>);
static_assert(std::is_empty_v<TrivialEmptyLattice::element_type>);

struct EmptyValue {};
struct OneByteValue { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

// Type instantiation under each modality.
using GComonad   = Graded<ModalityKind::Comonad,       TrivialBoolLattice, EmptyValue>;
using GRelMonad  = Graded<ModalityKind::RelativeMonad, TrivialBoolLattice, EmptyValue>;
using GAbsolute  = Graded<ModalityKind::Absolute,      TrivialBoolLattice, EmptyValue>;
using GRelative  = Graded<ModalityKind::Relative,      TrivialBoolLattice, EmptyValue>;

static_assert(std::is_default_constructible_v<GComonad>);
static_assert(std::is_default_constructible_v<GRelMonad>);
static_assert(std::is_default_constructible_v<GAbsolute>);
static_assert(std::is_default_constructible_v<GRelative>);

// Type aliases reachable.
static_assert(std::is_same_v<GAbsolute::value_type,   EmptyValue>);
static_assert(std::is_same_v<GAbsolute::lattice_type, TrivialBoolLattice>);
static_assert(std::is_same_v<GAbsolute::grade_type,   bool>);
static_assert(GAbsolute::modality == ModalityKind::Absolute);

// Diagnostic names propagate.
static_assert(GAbsolute::modality_name() == "Absolute");
static_assert(GAbsolute::lattice_name()  == "TrivialBool");
static_assert(GComonad::modality_name()  == "Comonad");

// ── Layout: dynamic grade (TrivialBool's bool element) ─────────────
//
// EmptyValue + 1B grade collapses inner_ via EBO, so total = 1B grade.
static_assert(sizeof(GAbsolute) == 1);

// 1B value + 1B grade → adjacent fields, total = 2B (no padding needed).
using GOneByte = Graded<ModalityKind::Absolute, TrivialBoolLattice, OneByteValue>;
static_assert(sizeof(GOneByte) == 2);

// 8B value + 1B grade with 8B alignment → grade after value forces
// padding to next multiple of alignof(value); sizeof = 16.  This is
// the worst-case dynamic-grade overhead and demonstrates why static-
// grade lattices (empty element_type, EBO-collapsed) are preferred
// for hot-path wrappers.
using GEightByte = Graded<ModalityKind::Absolute, TrivialBoolLattice, EightByteValue>;
static_assert(sizeof(GEightByte) == 16);

// ── Layout: static (empty) grade — the EBO-collapse path ────────────
//
// TrivialEmptyLattice's element_type is empty (std::integral_constant
// is an empty class).  Both inner_ and grade_ EBO-collapse when the
// value is also empty; non-empty value preserves its size exactly.
using GEmptyGrade_Empty    = Graded<ModalityKind::Absolute, TrivialEmptyLattice, EmptyValue>;
using GEmptyGrade_OneByte  = Graded<ModalityKind::Absolute, TrivialEmptyLattice, OneByteValue>;
using GEmptyGrade_EightB   = Graded<ModalityKind::Absolute, TrivialEmptyLattice, EightByteValue>;

static_assert(sizeof(GEmptyGrade_Empty)    == 1);                          // C++ minimum-object-size
static_assert(sizeof(GEmptyGrade_OneByte)  == sizeof(OneByteValue));       // EBO grade
static_assert(sizeof(GEmptyGrade_EightB)   == sizeof(EightByteValue));     // EBO grade

// Construction with value + grade.
constexpr GOneByte g_at_top{OneByteValue{}, true};
static_assert(g_at_top.grade() == true);
static_assert(g_at_top.peek().c == 0);

// at_bottom convenience for bounded lattices.
constexpr GOneByte g_bot = GOneByte::at_bottom(OneByteValue{});
static_assert(g_bot.grade() == false);  // TrivialBool::bottom() is false

// weaken: widen grade.  false ⊑ true is legal.
constexpr GOneByte g_weakened = g_bot.weaken(true);
static_assert(g_weakened.grade() == true);

// compose: lattice join.  bot ⊕ top = top.
constexpr GOneByte g_composed = g_bot.compose(g_at_top);
static_assert(g_composed.grade() == true);

// ── Capability gates via concept (concepts SFINAE cleanly; inline
//     `requires(...) { ... }` against member-function constraints
//     emits a hard error in some GCC versions, hence the indirection) ─
template <typename G> concept CanExtract = requires(G g) { std::move(g).extract(); };
template <typename G> concept CanInject  = requires { G::inject(typename G::value_type{}, typename G::grade_type{}); };

// Comonad-only: extract counit reachable iff modality == Comonad.
static_assert( CanExtract<GComonad>);
static_assert(!CanExtract<GAbsolute>);
static_assert(!CanExtract<GRelMonad>);
static_assert(!CanExtract<GRelative>);

// RelativeMonad-only: inject unit reachable iff modality == RelativeMonad.
static_assert( CanInject<GRelMonad>);
static_assert(!CanInject<GComonad>);
static_assert(!CanInject<GAbsolute>);
static_assert(!CanInject<GRelative>);

// Layout invariant macro fires correctly on the EBO-collapsed path.
template <typename T> using AbsoluteOverEmpty =
    Graded<ModalityKind::Absolute, TrivialEmptyLattice, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbsoluteOverEmpty, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbsoluteOverEmpty, EightByteValue);

// ── Move-only T compatibility ───────────────────────────────────────
//
// For wrappers like the eventual Linear<T> the inner T may itself be
// move-only.  The const& overloads of weaken/compose are gated on
// std::copy_constructible<T> so move-only T cleanly forces &&
// (rvalue this) — no noisy copy-deleted error cascade.
struct MoveOnlyValue {
    int v{0};
    constexpr MoveOnlyValue() = default;
    constexpr MoveOnlyValue(int x) noexcept : v{x} {}
    MoveOnlyValue(const MoveOnlyValue&) = delete;
    MoveOnlyValue(MoveOnlyValue&&) noexcept = default;
    MoveOnlyValue& operator=(const MoveOnlyValue&) = delete;
    MoveOnlyValue& operator=(MoveOnlyValue&&) noexcept = default;
};

using GMoveOnly = Graded<ModalityKind::Absolute, TrivialEmptyLattice, MoveOnlyValue>;

// const& overloads SFINAE away for move-only T; only && remains.
template <typename G> concept HasConstWeaken =
    requires(G const& g, typename G::grade_type r) { g.weaken(r); };
template <typename G> concept HasRvalueWeaken =
    requires(G g, typename G::grade_type r) { std::move(g).weaken(r); };

static_assert( HasConstWeaken<GOneByte>);    // copyable T → both available
static_assert( HasRvalueWeaken<GOneByte>);
static_assert(!HasConstWeaken<GMoveOnly>);   // move-only T → const& gated off
static_assert( HasRvalueWeaken<GMoveOnly>);  // && always works

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Forces evaluation through a non-constexpr function so the lattice
// operations called from Graded::weaken's `pre()` are exercised at
// RUNTIME — catches the consteval-vs-constexpr trap that pure
// static_assert-only tests miss.
//
// The function is `inline` (not constexpr) so the body must compile
// against runtime semantics.  TU optimizer almost certainly elides
// the call entirely under -O3, but the front-end still type-checks.
inline void runtime_smoke_test() {
    OneByteValue value{42};
    GOneByte initial{value, false};                         // runtime ctor
    GOneByte widened   = initial.weaken(true);              // runtime weaken (lvalue this)
    GOneByte composed  = initial.compose(widened);          // runtime compose (lvalue this)
    GOneByte moved     = std::move(widened).weaken(true);   // runtime weaken (rvalue this)
    GOneByte mcomposed = std::move(initial).compose(composed);  // runtime compose (rvalue this)

    // Use the results so the optimizer can't elide the calls.
    [[maybe_unused]] bool g1 = composed.grade();
    [[maybe_unused]] bool g2 = moved.grade();
    [[maybe_unused]] bool g3 = mcomposed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(mcomposed).consume().c;
}

}  // namespace detail::graded_self_test

}  // namespace crucible::algebra

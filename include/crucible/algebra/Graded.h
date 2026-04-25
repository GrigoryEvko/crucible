#pragma once

// ── crucible::algebra::Graded<Modality, Lattice, T> ─────────────────
//
// THE foundational primitive of the 25_04_2026.md §2 refactor.  Every
// safety wrapper that decorates a value (Linear, Refined, Tagged,
// Secret, Monotonic, AppendOnly, SharedPermission, plus future
// Stale, Budgeted, TimeOrdered, SealedRefined) folds into a
// `Graded<M, L, T>` instantiation under this template.
//
//   Axiom coverage: every axiom is INHERITED from the wrapped T plus
//                   the lattice's structural witnesses.  Linearity
//                   comes from QttSemiring (grade 1); predicate
//                   refinement from BoolLattice; classification from
//                   ConfLattice; trust provenance from TrustLattice;
//                   etc.  The Graded class adds no axiom obligations
//                   of its own — it composes them.
//   Runtime cost:   zero.  `[[no_unique_address]]` on inner_ plus
//                   empty-class optimization on the lattice's element
//                   type collapses sizeof(Graded<M, L, T>) ==
//                   sizeof(T) when both T and LatticeElement<L> are
//                   empty (e.g. Linear<Tag>); equals sizeof(T) for
//                   non-empty T with empty lattice (Linear<int>);
//                   equals sizeof(T) + sizeof(LatticeElement<L>) when
//                   both carry runtime state.  Verified codebase-wide
//                   by ALGEBRA-15 (#460) under -O3.
//
// STATUS: stub.  Class type is COMPLETE so that the MIGRATE-1..11
// alias declarations under safety/ compile NOW.  Operation bodies
// land in ALGEBRA-3 (#448); the declarations below are
// `= delete("...")` so any call site fails to compile with the
// implementation-task pointer.  This shape is deliberate:
//
//   - alias declarations work today
//     (`using Linear<T> = Graded<Absolute, QTT::At<1>, T>;`)
//   - per-axiom static_asserts verify layout NOW
//     (`static_assert(sizeof(Linear<int>) == sizeof(int))`)
//   - any user that tries to call an operation gets the precise
//     pointer to the implementation task in the diagnostic
//   - migration-verification harness MIGRATE-12 (#472) only needs
//     to swap deleted bodies for real ones; no API redesign
//
// Operations to land per ALGEBRA-3 (#448):
//   mk<r>(T)           — construct at lattice element r
//   extract()          — counit (Comonad form); returns T
//   inject(T)          — relative-monad unit (RelativeMonad form)
//   weaken<s>()        — lattice ⊑-monotone widening
//   compose<s>(other)  — joins two graded values via L::join
//   join_R<s>()        — collapses Graded<Graded<…>> to Graded<⊕>
//
// See ALGEBRA-1 (Modality.h), ALGEBRA-2 (Lattice.h), ALGEBRA-4..15
// (concrete lattice instantiations under lattices/).  Lean
// formalization in lean/Crucible/Algebra/Graded.lean per LEAN-1
// (#490).

#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/Modality.h>

#include <string_view>
#include <type_traits>

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

    // ── Layout (NSDMI per InitSafe) ─────────────────────────────────
    //
    // The wrapped value lives in `inner_`.  EBO via `[[no_unique_address]]`
    // collapses sizeof(Graded) when T (or the lattice's element type
    // when stored separately by ALGEBRA-3) is empty.
    [[no_unique_address]] T inner_{};

    // ── Diagnostic name emitters ────────────────────────────────────
    [[nodiscard]] static consteval std::string_view modality_name() noexcept {
        return ::crucible::algebra::modality_name(M);
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return ::crucible::algebra::lattice_name<L>();
    }

    // ── Object semantics (defaulted) ────────────────────────────────
    //
    // Per-modality copy/move discipline (e.g. Linear deletes copy) is
    // imposed by the per-lattice wrapper aliases, not by Graded itself.
    // A bare Graded preserves T's semantics.
    //
    // No explicit noexcept on the defaulted special members: per
    // C++26, an explicit noexcept that disagrees with the implicit
    // exception-spec of the defaulted function is ill-formed.  The
    // compiler infers the correct spec from T (`= default` propagates
    // T's exception guarantees automatically).
    constexpr Graded()                         = default;
    constexpr Graded(const Graded&)            = default;
    constexpr Graded(Graded&&)                 = default;
    constexpr Graded& operator=(const Graded&) = default;
    constexpr Graded& operator=(Graded&&)      = default;
    ~Graded()                                  = default;

    // ── Public operations (declared; bodies land in ALGEBRA-3 #448) ─
    //
    // Calling any of the below before #448 ships fails to compile with
    // the reason string pointing at the implementation task.  The
    // declarations are themselves load-bearing: MIGRATE-1..11 alias
    // headers must reference the Graded API surface for type checks.

    template <grade_type R>
    [[nodiscard]] static consteval Graded mk(T x) noexcept
        = delete("Graded::mk: implementation deferred to ALGEBRA-3 (#448)");

    [[nodiscard]] constexpr T extract() const noexcept
        requires ComonadModality<M>
        = delete("Graded::extract: implementation deferred to ALGEBRA-3 (#448)");

    [[nodiscard]] static constexpr Graded inject(T x) noexcept
        requires RelativeMonadModality<M>
        = delete("Graded::inject: implementation deferred to ALGEBRA-3 (#448)");

    template <grade_type S>
    [[nodiscard]] consteval Graded weaken() const noexcept
        = delete("Graded::weaken: implementation deferred to ALGEBRA-3 (#448)");

    template <grade_type S>
    [[nodiscard]] consteval Graded compose(Graded other) const noexcept
        = delete("Graded::compose: implementation deferred to ALGEBRA-3 (#448)");

    // ── ALGEBRA-3 design hole: runtime-grade lattices ───────────────
    //
    // The current shape encodes the lattice grade purely at the type
    // level (NTTP on factory, type-encoded by the lattice itself for
    // QTT-style discrete grades).  Lattices whose grade VARIES AT
    // RUNTIME (e.g. FractionalLattice for SharedPermission, where
    // distinct instances hold distinct fractional shares) cannot use
    // this shape directly.  The 25_04_2026.md §2.3 sketch is silent
    // on this question.  ALGEBRA-3 (#448) must decide:
    //   - extend Graded with an optional runtime grade_ field, OR
    //   - keep Graded as type-level only and let SharedPermission be
    //     a struct that COMBINES Graded with a runtime share counter,
    //     not an alias of Graded.
    // Either choice preserves the public-API of MIGRATE-7 — the
    // distinction is invisible to callers of SharedPermission.
};

// ── Layout invariant macro (used by MIGRATE-* alias headers) ───────
//
// Each MIGRATE-1..11 alias header invokes this macro at instantiation
// witnesses to prove the wrapper carries zero overhead vs the
// underlying T.  Failures point to the offending alias + T pair.
#define CRUCIBLE_GRADED_LAYOUT_INVARIANT(GradedAlias, T_)                   \
    static_assert(sizeof(GradedAlias<T_>) == sizeof(T_),                    \
                  "Graded alias " #GradedAlias " over " #T_                 \
                  " violates the zero-overhead contract; review "           \
                  "[[no_unique_address]] usage and lattice element type")

// ── Self-test: layout invariant on the trivial witness ─────────────
//
// Uses the TrivialBoolLattice witness from Lattice.h's self-test
// namespace.  Proves that a complete Graded<> type instantiates and
// that EBO collapses both inner_ and the empty layout under -O3.
namespace detail::graded_self_test {

using ::crucible::algebra::detail::lattice_self_test::TrivialBoolLattice;

struct EmptyValue {};
struct OneByteValue { char c{0}; };
struct EightByteValue { std::uint64_t v{0}; };

// Type instantiates cleanly under each modality.
using GComonad      = Graded<ModalityKind::Comonad,       TrivialBoolLattice, EmptyValue>;
using GRelMonad     = Graded<ModalityKind::RelativeMonad, TrivialBoolLattice, EmptyValue>;
using GAbsolute     = Graded<ModalityKind::Absolute,      TrivialBoolLattice, EmptyValue>;
using GRelative     = Graded<ModalityKind::Relative,      TrivialBoolLattice, EmptyValue>;

static_assert(std::is_default_constructible_v<GComonad>);
static_assert(std::is_default_constructible_v<GRelMonad>);
static_assert(std::is_default_constructible_v<GAbsolute>);
static_assert(std::is_default_constructible_v<GRelative>);

// Type aliases are exposed and correct.
static_assert(std::is_same_v<GAbsolute::value_type,   EmptyValue>);
static_assert(std::is_same_v<GAbsolute::lattice_type, TrivialBoolLattice>);
static_assert(std::is_same_v<GAbsolute::grade_type,   bool>);
static_assert(GAbsolute::modality == ModalityKind::Absolute);

// Diagnostic names propagate.
static_assert(GAbsolute::modality_name() == "Absolute");
static_assert(GAbsolute::lattice_name()  == "TrivialBool");
static_assert(GComonad::modality_name()  == "Comonad");

// Layout invariant: empty T over empty lattice element collapses
// (Graded itself has at least 1 byte of identity per the C++ object
// model — that's a property of any non-base class, not a defect).
static_assert(sizeof(GAbsolute) == 1);

// Layout invariant: non-empty T preserves its size exactly.
using GOneByte = Graded<ModalityKind::Absolute, TrivialBoolLattice, OneByteValue>;
static_assert(sizeof(GOneByte) == sizeof(OneByteValue));

using GEightByte = Graded<ModalityKind::Absolute, TrivialBoolLattice, EightByteValue>;
static_assert(sizeof(GEightByte) == sizeof(EightByteValue));

// The macro fires correctly (placeholder usage).
template <typename T> using LinearWitness =
    Graded<ModalityKind::Absolute, TrivialBoolLattice, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(LinearWitness, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(LinearWitness, EightByteValue);

}  // namespace detail::graded_self_test

}  // namespace crucible::algebra

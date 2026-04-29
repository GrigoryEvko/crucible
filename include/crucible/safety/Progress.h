#pragma once

// ── crucible::safety::Progress<ProgressClass_v Class, T> ────────────
//
// Type-pinned termination-class wrapper.  A value of type T whose
// termination/progress guarantee (MayDiverge ⊑ Terminating ⊑
// Productive ⊑ Bounded) is fixed at the type level via the non-type
// template parameter Class.  FIFTH and FINAL Month-2 chain wrapper
// from 28_04_2026_effects.md §4.3.5 (FOUND-G34) — closes the
// Month-2 first-pass catalog.
//
// Captures `term` / `nterm` aspects of the session-type stack's
// φ-family per session_types.md III.L7 (Task #346 SessionSafety.h
// — currently unshipped).  Composes orthogonally with the four
// sister chain wrappers (DetSafe / HotPath / Wait / MemOrder) via
// wrapper-nesting.
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     ProgressLattice::At<Class>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<Class>::element_type
//                 is empty, sizeof(Progress<Class, T>) == sizeof(T))
//
//   Use cases (per 28_04 §4.3.5 + FORGE.md §5):
//     - Forge phases (INGEST/ANALYZE/REWRITE/FUSE/LOWER/TILE/MEMPLAN
//       /COMPILE/SCHEDULE/EMIT/DISTRIBUTE/VALIDATE) — declared
//       `Bounded` (FORGE.md §5 hard wall-clock budgets, e.g.
//       Phase A budget = 50ms, Phase H budget = 500ms)
//     - CNTP heartbeat handlers — declared `Bounded` (must respond
//       within the heartbeat interval)
//     - BackgroundThread::drain — declared `Productive` (every
//       iteration drains at least one entry or signals quiescence)
//     - Lean proof tactics (offline tooling) — declared
//       `Terminating` (eventually halts, no time bound)
//     - Inferlet user code — declared `MayDiverge` (escape hatch
//       per Pie SOSP 2025 inferlet pattern)
//
//   The bug class caught: a function declared `Bounded` (e.g., a
//   Forge phase implementation) accidentally containing an
//   unbounded subroutine call.  Today caught by review or runtime
//   budget enforcement after the fact; with the wrapper, becomes
//   a compile error if the unbounded subroutine's row leaks
//   through the dispatcher's admission gate.
//
//   Axiom coverage:
//     TypeSafe — ProgressClass_v is a strong scoped enum;
//                cross-class mismatches are compile errors via
//                the relax<WeakerClass>() and
//                satisfies<RequiredClass> gates.
//     ThreadSafe — Bounded discipline composes with HotPath /
//                  Wait / MemOrder for full hot-path admission
//                  fencing.
//     MemSafe — defaulted copy/move; T's move semantics carry
//               through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(Progress<Class, T>) == sizeof(T).  Verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.
//
// ── Why Modality::Absolute ─────────────────────────────────────────
//
// A termination-class pin is a STATIC property of WHAT TIME-BOUND
// the function promises.  Mirrors the four sister chain wrappers —
// all Absolute over At<>-pinned grades.
//
// ── Class-conversion API: relax + satisfies ────────────────────────
//
// Progress subsumption-direction (per ProgressLattice.h docblock):
//
//   Bottom = MayDiverge (weakest claim — escape hatch).
//   Top    = Bounded    (strongest claim — hard wall-clock budget).
//
// For USE, the direction is REVERSED:
//
//   A producer at a STRONGER class (Bounded) satisfies a consumer
//   at a WEAKER class (Productive).  Stronger termination claim
//   serves weaker requirement.  A Progress<Bounded, T> can be
//   relaxed to Progress<Productive, T> — the wall-clock-bounded
//   value trivially satisfies the productive-progress requirement.
//
//   The converse is forbidden: a Progress<MayDiverge, T> CANNOT
//   become a Progress<Bounded, T> — the may-diverge value carries
//   no termination guarantee at all; relaxing the type to claim
//   Bounded compliance would defeat the wall-clock discipline.
//   No `tighten()` method exists.
//
// API:
//   - relax<WeakerClass>() &  / && — convert to a less-strict
//                                    class; compile error if
//                                    WeakerClass > Class.
//   - satisfies<RequiredClass>     — static predicate.
//   - cls (static constexpr)       — the pinned ProgressClass_v
//                                    value.
//
// SEMANTIC NOTE on the "relax" naming: for Progress, "weakening
// the class" means accepting MORE permissive termination behavior
// (going down the chain Bounded ← Productive ← Terminating ←
// MayDiverge).  Calling `relax<MayDiverge>()` on a Bounded-pinned
// value means "I'm OK treating this Bounded value as may-diverge-
// tolerable here" — a downgrade of the TERMINATION GUARANTEE.
// The API uses `relax` for uniformity with the four sister chain
// wrappers.
//
// `Graded::weaken` on the substrate goes UP the lattice — that
// operation has NO MEANINGFUL SEMANTICS for a type-pinned class
// and would be the LOAD-BEARING BUG: a MayDiverge value claiming
// Bounded compliance would defeat the wall-clock discipline.
// Hidden by the wrapper.
//
// See FOUND-G33 (algebra/lattices/ProgressLattice.h) for the
// underlying substrate; 28_04_2026_effects.md §4.3.5 for the
// production-call-site rationale; session_types.md III.L7 for
// the φ-family connection; FORGE.md §5 for the Bounded class
// production usage.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/ProgressLattice.h>

#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the ProgressClass enum into the safety:: namespace under
// `ProgressClass_v`.  No name collision — the wrapper class is
// `Progress`, not `ProgressClass`.
using ::crucible::algebra::lattices::ProgressLattice;
using ProgressClass_v = ::crucible::algebra::lattices::ProgressClass;

template <ProgressClass_v Class, typename T>
class [[nodiscard]] Progress {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = ProgressLattice::At<Class>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned class — exposed as a static constexpr for callers
    // doing class-aware dispatch without instantiating the wrapper.
    static constexpr ProgressClass_v cls = Class;

private:
    graded_type impl_;

public:

    // ── Construction ────────────────────────────────────────────────
    constexpr Progress() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit Progress(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Progress(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr Progress(const Progress&)            = default;
    constexpr Progress(Progress&&)                 = default;
    constexpr Progress& operator=(const Progress&) = default;
    constexpr Progress& operator=(Progress&&)      = default;
    ~Progress()                                    = default;

    [[nodiscard]] friend constexpr bool operator==(
        Progress const& a, Progress const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    // ── Diagnostic names ────────────────────────────────────────────
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    // ── Read-only access ────────────────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return impl_.peek();
    }

    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // ── swap ────────────────────────────────────────────────────────
    constexpr void swap(Progress& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(Progress& a, Progress& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── satisfies<RequiredClass> ───────────────────────────────────
    template <ProgressClass_v RequiredClass>
    static constexpr bool satisfies = ProgressLattice::leq(RequiredClass, Class);

    // ── relax<WeakerClass> ─────────────────────────────────────────
    template <ProgressClass_v WeakerClass>
        requires (ProgressLattice::leq(WeakerClass, Class))
    [[nodiscard]] constexpr Progress<WeakerClass, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Progress<WeakerClass, T>{this->peek()};
    }

    template <ProgressClass_v WeakerClass>
        requires (ProgressLattice::leq(WeakerClass, Class))
    [[nodiscard]] constexpr Progress<WeakerClass, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Progress<WeakerClass, T>{
            std::move(impl_).consume()};
    }
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace progress {
    template <typename T> using Bounded     = Progress<ProgressClass_v::Bounded,     T>;
    template <typename T> using Productive  = Progress<ProgressClass_v::Productive,  T>;
    template <typename T> using Terminating = Progress<ProgressClass_v::Terminating, T>;
    template <typename T> using MayDiverge  = Progress<ProgressClass_v::MayDiverge,  T>;
}  // namespace progress

// ── Layout invariants ───────────────────────────────────────────────
namespace detail::progress_layout {

template <typename T> using BoundedP    = Progress<ProgressClass_v::Bounded,     T>;
template <typename T> using ProductiveP = Progress<ProgressClass_v::Productive,  T>;
template <typename T> using MayDivergeP = Progress<ProgressClass_v::MayDiverge,  T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedP,    char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedP,    int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedP,    double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ProductiveP, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ProductiveP, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(MayDivergeP, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(MayDivergeP, double);

}  // namespace detail::progress_layout

static_assert(sizeof(Progress<ProgressClass_v::Bounded,     int>)    == sizeof(int));
static_assert(sizeof(Progress<ProgressClass_v::Productive,  int>)    == sizeof(int));
static_assert(sizeof(Progress<ProgressClass_v::Terminating, int>)    == sizeof(int));
static_assert(sizeof(Progress<ProgressClass_v::MayDiverge,  int>)    == sizeof(int));
static_assert(sizeof(Progress<ProgressClass_v::Bounded,     double>) == sizeof(double));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::progress_self_test {

using BoundedInt    = Progress<ProgressClass_v::Bounded,     int>;
using ProductiveInt = Progress<ProgressClass_v::Productive,  int>;
using TermInt       = Progress<ProgressClass_v::Terminating, int>;
using DivergeInt    = Progress<ProgressClass_v::MayDiverge,  int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr BoundedInt p_default{};
static_assert(p_default.peek() == 0);
static_assert(p_default.cls == ProgressClass_v::Bounded);

inline constexpr BoundedInt p_explicit{42};
static_assert(p_explicit.peek() == 42);

inline constexpr BoundedInt p_in_place{std::in_place, 7};
static_assert(p_in_place.peek() == 7);

// ── Pinned class accessor ──────────────────────────────────────────
static_assert(BoundedInt::cls    == ProgressClass_v::Bounded);
static_assert(ProductiveInt::cls == ProgressClass_v::Productive);
static_assert(TermInt::cls       == ProgressClass_v::Terminating);
static_assert(DivergeInt::cls    == ProgressClass_v::MayDiverge);

// ── satisfies<RequiredClass> — subsumption-up direction ───────────
//
// Chain: MayDiverge ⊑ Terminating ⊑ Productive ⊑ Bounded.
// satisfies<R> means leq(R, Self), i.e., R is below or equal to Self.
//
// Bounded (chain top, index 3) satisfies every consumer.  THIS IS
// THE LOAD-BEARING POSITIVE TEST: Bounded-tier values pass every
// concept gate (including the strict deadline-sensitive gate).
static_assert(BoundedInt::satisfies<ProgressClass_v::Bounded>);     // self
static_assert(BoundedInt::satisfies<ProgressClass_v::Productive>);  // below
static_assert(BoundedInt::satisfies<ProgressClass_v::Terminating>); // below
static_assert(BoundedInt::satisfies<ProgressClass_v::MayDiverge>);  // below (bottom)

// Productive (index 2) satisfies: Productive (self) + Terminating +
// MayDiverge (below).  FAILS on Bounded (above).
static_assert( ProductiveInt::satisfies<ProgressClass_v::Productive>);  // self
static_assert( ProductiveInt::satisfies<ProgressClass_v::Terminating>); // below
static_assert( ProductiveInt::satisfies<ProgressClass_v::MayDiverge>);  // below
static_assert(!ProductiveInt::satisfies<ProgressClass_v::Bounded>,      // above ✗
    "Productive MUST NOT satisfy Bounded — Productive guarantees "
    "progress per step but no wall-clock bound.  If this fires, "
    "productive-but-unbounded functions could silently flow into "
    "deadline-sensitive call sites (Forge phases, CNTP heartbeats).");

// Terminating (index 1) satisfies: Terminating (self) + MayDiverge.
// FAILS on Productive and Bounded.
static_assert( TermInt::satisfies<ProgressClass_v::Terminating>);
static_assert( TermInt::satisfies<ProgressClass_v::MayDiverge>);
static_assert(!TermInt::satisfies<ProgressClass_v::Productive>);
static_assert(!TermInt::satisfies<ProgressClass_v::Bounded>);

// MayDiverge (chain bottom) satisfies only MayDiverge — escape-hatch
// values are admissible only in escape-hatch contexts.  THE LOAD-
// BEARING REJECTION: MayDiverge values cannot pass any gate
// requiring termination.
static_assert( DivergeInt::satisfies<ProgressClass_v::MayDiverge>);
static_assert(!DivergeInt::satisfies<ProgressClass_v::Terminating>,
    "MayDiverge MUST NOT satisfy Terminating — this is the load-"
    "bearing rejection that Forge phase admission gates depend on. "
    "If this fires, Inferlet user code (or any may-diverge "
    "subroutine) could silently flow into a Forge phase declared "
    "Bounded, breaking the FORGE.md §5 wall-clock budget.");
static_assert(!DivergeInt::satisfies<ProgressClass_v::Productive>);
static_assert(!DivergeInt::satisfies<ProgressClass_v::Bounded>);

// ── relax<WeakerClass> — DOWN-the-lattice conversion ─────────────
inline constexpr auto from_bounded_to_productive =
    BoundedInt{42}.relax<ProgressClass_v::Productive>();
static_assert(from_bounded_to_productive.peek() == 42);
static_assert(from_bounded_to_productive.cls == ProgressClass_v::Productive);

inline constexpr auto from_bounded_to_diverge =
    BoundedInt{99}.relax<ProgressClass_v::MayDiverge>();
static_assert(from_bounded_to_diverge.peek() == 99);
static_assert(from_bounded_to_diverge.cls == ProgressClass_v::MayDiverge);

inline constexpr auto from_term_to_diverge =
    TermInt{7}.relax<ProgressClass_v::MayDiverge>();
static_assert(from_term_to_diverge.peek() == 7);

inline constexpr auto from_productive_to_self =
    ProductiveInt{8}.relax<ProgressClass_v::Productive>();   // identity
static_assert(from_productive_to_self.peek() == 8);

// SFINAE-style detector — proves the requires-clause's correctness.
template <typename W, ProgressClass_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert( can_relax<BoundedInt,    ProgressClass_v::Productive>);   // ✓ down
static_assert( can_relax<BoundedInt,    ProgressClass_v::MayDiverge>);   // ✓ down (full chain)
static_assert( can_relax<BoundedInt,    ProgressClass_v::Bounded>);      // ✓ self
static_assert( can_relax<ProductiveInt, ProgressClass_v::Terminating>);  // ✓ down
static_assert( can_relax<ProductiveInt, ProgressClass_v::Productive>);   // ✓ self
static_assert(!can_relax<ProductiveInt, ProgressClass_v::Bounded>,        // ✗ up
    "relax<Bounded> on a Productive-pinned wrapper MUST be rejected "
    "— claiming a stronger termination guarantee than the source "
    "provides defeats the deadline-sensitive admission gate.");
static_assert(!can_relax<TermInt,       ProgressClass_v::Productive>);   // ✗ up
static_assert(!can_relax<DivergeInt,    ProgressClass_v::Terminating>,    // ✗ up
    "relax<Terminating> on a MayDiverge-pinned wrapper MUST be "
    "rejected — THIS IS THE LOAD-BEARING REJECTION for the Forge "
    "phase wall-clock discipline.  Without it, Inferlet user code "
    "could claim Terminating compliance and silently flow into "
    "Forge phases requiring Bounded-or-stronger termination.");
static_assert(!can_relax<DivergeInt,    ProgressClass_v::Bounded>);      // ✗ up
// MayDiverge reflexivity — chain endpoint admits relax to itself.
static_assert( can_relax<DivergeInt,    ProgressClass_v::MayDiverge>);   // ✓ self at bottom

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(BoundedInt::value_type_name().ends_with("int"));
static_assert(BoundedInt::lattice_name()    == "ProgressLattice::At<Bounded>");
static_assert(ProductiveInt::lattice_name() == "ProgressLattice::At<Productive>");
static_assert(TermInt::lattice_name()       == "ProgressLattice::At<Terminating>");
static_assert(DivergeInt::lattice_name()    == "ProgressLattice::At<MayDiverge>");

// ── swap exchanges T values within the same class pin ────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_class() noexcept {
    BoundedInt a{10};
    BoundedInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_class());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    BoundedInt a{10};
    BoundedInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

// ── peek_mut allows in-place mutation ─────────────────────────────
[[nodiscard]] consteval bool peek_mut_works() noexcept {
    BoundedInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

// ── operator== — same-class, same-T comparison ───────────────────
[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    BoundedInt a{42};
    BoundedInt b{42};
    BoundedInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

struct NoEqualityT {
    int v{0};
    NoEqualityT() = default;
    explicit NoEqualityT(int x) : v{x} {}
    NoEqualityT(NoEqualityT&&) = default;
    NoEqualityT& operator=(NoEqualityT&&) = default;
    NoEqualityT(NoEqualityT const&) = delete;
    NoEqualityT& operator=(NoEqualityT const&) = delete;
};

template <typename W>
concept can_equality_compare = requires(W const& a, W const& b) {
    { a == b } -> std::convertible_to<bool>;
};

static_assert( can_equality_compare<BoundedInt>);
static_assert(!can_equality_compare<Progress<ProgressClass_v::Bounded, NoEqualityT>>);

static_assert(!std::is_copy_constructible_v<Progress<ProgressClass_v::Bounded, NoEqualityT>>,
    "Progress<Class, T> must transitively inherit T's copy-deletion.");
static_assert(std::is_move_constructible_v<Progress<ProgressClass_v::Bounded, NoEqualityT>>);

// ── relax reflexivity ─────────────────────────────────────────────
[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    BoundedInt a{99};
    auto b = a.relax<ProgressClass_v::Bounded>();
    return b.peek() == 99 && b.cls == ProgressClass_v::Bounded;
}
static_assert(relax_to_self_is_identity());

// ── relax<>() && works on move-only T ─────────────────────────────
struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

template <typename W, ProgressClass_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, ProgressClass_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using BoundedMoveOnly = Progress<ProgressClass_v::Bounded, MoveOnlyT>;
static_assert( can_relax_rvalue<BoundedMoveOnly, ProgressClass_v::Productive>,
    "relax<>() && MUST work for move-only T.");
static_assert(!can_relax_lvalue<BoundedMoveOnly, ProgressClass_v::Productive>,
    "relax<>() const& on move-only T MUST be rejected.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    BoundedMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<ProgressClass_v::Productive>();
    return dst.peek().v == 77 && dst.cls == ProgressClass_v::Productive;
}
static_assert(relax_move_only_works());

// ── Stable-name introspection ────────────────────────────────────
static_assert(BoundedInt::value_type_name().size() > 0);
static_assert(BoundedInt::lattice_name().size() > 0);
static_assert(BoundedInt::lattice_name().starts_with("ProgressLattice::At<"));

// ── Convenience aliases resolve correctly ────────────────────────
static_assert(progress::Bounded<int>::cls     == ProgressClass_v::Bounded);
static_assert(progress::Productive<int>::cls  == ProgressClass_v::Productive);
static_assert(progress::Terminating<int>::cls == ProgressClass_v::Terminating);
static_assert(progress::MayDiverge<int>::cls  == ProgressClass_v::MayDiverge);

static_assert(std::is_same_v<progress::Bounded<double>,
                             Progress<ProgressClass_v::Bounded, double>>);

// ── Forge phase admission simulation — THE LOAD-BEARING SCENARIO ─
//
// The dispatcher's Forge phase admission gate (per FORGE.md §5
// hard wall-clock budgets + 28_04 §6.4) declares
// `requires Progress::satisfies<Bounded>`.  Concrete simulation:
//
//   template <typename T>
//   void forge_phase_site(Progress<Bounded, T>);
//
// Below: the concept is_forge_phase_admissible proves that
//   Bounded values PASS the gate (✓)
//   Productive / Terminating / MayDiverge are REJECTED (✓ — load-
//   bearing for FORGE.md §5)

template <typename W>
concept is_forge_phase_admissible =
    W::template satisfies<ProgressClass_v::Bounded>;

static_assert( is_forge_phase_admissible<BoundedInt>,
    "Bounded-tier value MUST pass the Forge phase admission gate.");
static_assert(!is_forge_phase_admissible<ProductiveInt>,
    "Productive-tier value MUST be REJECTED at the Forge phase "
    "admission gate — productive guarantees per-step progress but "
    "no wall-clock bound, which is insufficient for FORGE.md §5 "
    "phase budgets (Phase A = 50ms, Phase H = 500ms, etc.).");
static_assert(!is_forge_phase_admissible<TermInt>,
    "Terminating-tier value MUST be REJECTED at the Forge phase "
    "gate — terminating guarantees eventual halt but no time bound.");
static_assert(!is_forge_phase_admissible<DivergeInt>,
    "MayDiverge-tier value MUST be REJECTED at the Forge phase "
    "gate — escape-hatch values cannot enter deadline-sensitive "
    "compilation phases.");

// Productive admission gate (relaxed — for BackgroundThread::drain).
template <typename W>
concept is_productive_admissible =
    W::template satisfies<ProgressClass_v::Productive>;

static_assert( is_productive_admissible<BoundedInt>);     // stronger ✓
static_assert( is_productive_admissible<ProductiveInt>);  // self ✓
static_assert(!is_productive_admissible<TermInt>,
    "Terminating value MUST be REJECTED at the Productive gate "
    "(BackgroundThread::drain) — every iteration must drain at "
    "least one entry, not just eventually halt.");
static_assert(!is_productive_admissible<DivergeInt>);

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    BoundedInt a{};
    BoundedInt b{42};
    BoundedInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    if (BoundedInt::cls != ProgressClass_v::Bounded) {
        std::abort();
    }

    BoundedInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    BoundedInt sx{1};
    BoundedInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    BoundedInt source{77};
    auto relaxed_copy = source.relax<ProgressClass_v::Productive>();
    auto relaxed_move = std::move(source).relax<ProgressClass_v::MayDiverge>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    [[maybe_unused]] bool s1 = BoundedInt::satisfies<ProgressClass_v::Productive>;
    [[maybe_unused]] bool s2 = DivergeInt::satisfies<ProgressClass_v::Bounded>;

    BoundedInt eq_a{42};
    BoundedInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    BoundedInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    progress::Bounded<int>     alias_bounded{123};
    progress::Productive<int>  alias_prod{456};
    progress::MayDiverge<int>  alias_diverge{789};
    [[maybe_unused]] auto bv = alias_bounded.peek();
    [[maybe_unused]] auto pv = alias_prod.peek();
    [[maybe_unused]] auto dv = alias_diverge.peek();

    [[maybe_unused]] bool can_bounded_pass    = is_forge_phase_admissible<BoundedInt>;
    [[maybe_unused]] bool can_productive_pass = is_forge_phase_admissible<ProductiveInt>;
    [[maybe_unused]] bool can_diverge_pass    = is_forge_phase_admissible<DivergeInt>;
}

}  // namespace detail::progress_self_test

}  // namespace crucible::safety

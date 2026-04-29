#pragma once

// ── crucible::safety::Crash<CrashClass_v Class, T> ──────────────────
//
// Type-pinned function-failure-mode wrapper.  A value of type T
// whose producing function's failure-mode strength (Abort ⊑ Throw ⊑
// ErrorReturn ⊑ NoThrow) is fixed at the type level via the
// non-type template parameter Class.  Tenth chain wrapper from
// 28_04_2026_effects.md §4.3.10 (FOUND-G59) — composes directly
// with the nine sister wrappers in canonical wrapper-nesting order.
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     CrashLattice::At<Class>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<Class>::element_type
//                 is empty, sizeof(Crash<Class, T>) == sizeof(T))
//
//   Use cases (per 28_04 §4.3.10 + bridges/CrashTransport.h):
//     - OneShotFlag::peek-guarded boundaries → consume Crash<Abort, T>
//                                                with recovery code
//     - Cipher tier promotion (NoThrow path)  → produces Crash<NoThrow>
//     - TraceLoader I/O (recoverable failure) → produces Crash<ErrorReturn>
//     - Vessel adapter (PyTorch may throw)    → produces Crash<Throw>
//     - Keeper init/shutdown (may abort)      → produces Crash<Abort>
//
//   The bug class caught: a refactor that publishes an Abort-class
//   value (e.g., from a Keeper init path that called crucible_abort)
//   into a function expecting NoThrow guarantees, bypassing the
//   recovery-aware OneShotFlag check.  Today caught only by the
//   Keeper noticing process state inconsistency at recovery time;
//   with the wrapper, becomes a compile error at the call boundary
//   because the function's `requires NoThrow` rejects Crash<Abort>.
//
//   ORTHOGONAL TO every sister wrapper:
//     - HotPath captures execution-budget tier
//     - CipherTier captures storage-residency tier
//     - ResidencyHeat captures cache-residency tier
//     - DetSafe captures determinism-safety tier
//     - AllocClass captures allocator class
//     - Vendor captures backend identity
//     - Crash captures FAILURE-MODE STRENGTH — orthogonal to all
//       the above.  An NV kernel may be HotPath<Hot> AND
//       CipherTier<Warm> AND DetSafe<Pure> AND Crash<NoThrow>;
//       failure-mode adds a TENTH independent typed axis.
//
//   Axiom coverage:
//     TypeSafe — CrashClass_v is a strong scoped enum;
//                cross-class mismatches are compile errors via the
//                relax<WeakerClass>() and satisfies<RequiredClass>
//                gates.
//     DetSafe — orthogonal axis; Crash does NOT itself enforce
//                determinism.  Composes via wrapper-nesting.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(Crash<Class, T>) == sizeof(T).  Verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.  At<Class>::
//     element_type is empty; Graded's [[no_unique_address]] grade_
//     EBO-collapses; the wrapper is byte-equivalent to the bare T
//     at -O3.
//
// ── Why Modality::Absolute ─────────────────────────────────────────
//
// A failure-mode pin is a STATIC property of the producing
// function's contract — not a context the value carries
// independent of its origin.  The bytes themselves carry no
// information about the producer's failure mode; the wrapper
// carries that information at the TYPE level.  Mirrors the
// nine sister chain/partial-order wrappers — all Absolute
// modalities over At<>-pinned grades.
//
// ── Tier-conversion API: relax + satisfies ─────────────────────────
//
// Crash subsumption-direction (per CrashLattice.h docblock):
//
//   leq(weaker, stronger) reads "weaker-claim function is below
//   stronger-claim function in the lattice."
//   Bottom = Abort (weakest claim — function may crash);
//   Top = NoThrow (strongest claim — function never fails).
//
// For USE, the direction is REVERSED:
//
//   A producer at a STRONGER class (NoThrow) satisfies a consumer
//   at a WEAKER class (ErrorReturn).  Stronger guarantee subsumes
//   weaker requirement.  A Crash<NoThrow, T> can be relaxed to
//   Crash<ErrorReturn, T> — the never-fails value trivially
//   satisfies the may-return-error gate (the gate's recovery code
//   is dead code in this case, but the gate admits).
//
//   The converse is forbidden: a Crash<Abort, T> CANNOT become a
//   Crash<NoThrow, T> — the abort-prone value would defeat the
//   recovery-free admission discipline.  No `tighten()` method
//   exists; the only way to obtain a Crash<NoThrow, T> is to
//   construct one at a genuinely-NoThrow producing site.
//
// API:
//
//   - relax<WeakerClass>() &  / && — convert to a less-strict class;
//                                    compile error if WeakerClass >
//                                    Class.
//   - satisfies<RequiredClass>     — static predicate: does this
//                                    wrapper's pinned class subsume
//                                    the required class?  Equivalent
//                                    to leq(RequiredClass, Class).
//   - crash_class (static constexpr)— the pinned CrashClass_v value.
//
// SEMANTIC NOTE on the "relax" naming: for Crash, "weakening the
// class" means accepting MORE permissive failure modes (going down
// the chain Abort ← Throw ← ErrorReturn ← NoThrow).  Calling
// `relax<Abort>()` on a NoThrow-pinned value means "I'm OK treating
// this NoThrow value at a recovery-aware Abort gate" — a downgrade
// of the failure-mode guarantee, not the value's bytes.
//
// `Graded::weaken` on the substrate goes UP the lattice (stronger
// promise) — that operation has NO MEANINGFUL SEMANTICS for a
// type-pinned class and would be the LOAD-BEARING BUG: an Abort-
// classified value claiming NoThrow compatibility would defeat the
// recovery discipline.  Hidden by the wrapper.
//
// See FOUND-G58 (algebra/lattices/CrashLattice.h) for the underlying
// substrate; 28_04_2026_effects.md §4.3.10 + §4.7 for the
// production-call-site rationale and the canonical wrapper-nesting
// story; bridges/CrashTransport.h::CrashWatchedHandle for the
// runtime mechanism this wrapper type-fences.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/CrashLattice.h>

#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the CrashClass enum into the safety:: namespace under
// `CrashClass_v`.  No name collision — the wrapper class is `Crash`,
// not `CrashClass`.  Naming convention matches HotPathTier_v /
// CipherTierTag_v / etc. from sibling wrappers.
using ::crucible::algebra::lattices::CrashLattice;
using CrashClass_v = ::crucible::algebra::lattices::CrashClass;

template <CrashClass_v Class, typename T>
class [[nodiscard]] Crash {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = CrashLattice::At<Class>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned class — exposed as a static constexpr for callers
    // doing class-aware dispatch without instantiating the wrapper.
    static constexpr CrashClass_v crash_class = Class;

private:
    graded_type impl_;

public:

    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at the pinned class.
    //
    // SEMANTIC NOTE: a default-constructed Crash<NoThrow, T> claims
    // its T{} bytes were produced by a function with no failure
    // mode.  For trivially-zero T, this is vacuously true.  For
    // non-trivial T or non-zero T{}, the claim becomes meaningful
    // only if the wrapper is constructed in a context that
    // genuinely honors the class (e.g., a constexpr arithmetic
    // helper returning Crash<NoThrow, int>).  Production callers
    // SHOULD prefer the explicit-T constructor at class-anchored
    // production sites; the default ctor exists for compatibility
    // with std::array<Crash<NoThrow, T>, N> / struct-field
    // default-init contexts.
    constexpr Crash() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    // Explicit construction from a T value.  The most common
    // production pattern — a class-anchored production site
    // constructs the wrapper at the appropriate failure-mode class.
    constexpr explicit Crash(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    // In-place construction.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Crash(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    // Defaulted copy/move/destroy — Crash IS COPYABLE within the
    // same class pin.
    constexpr Crash(const Crash&)            = default;
    constexpr Crash(Crash&&)                 = default;
    constexpr Crash& operator=(const Crash&) = default;
    constexpr Crash& operator=(Crash&&)      = default;
    ~Crash()                                 = default;

    // Equality: compares value bytes within the SAME class pin.
    // Cross-class comparison rejected at overload resolution.
    [[nodiscard]] friend constexpr bool operator==(
        Crash const& a, Crash const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
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
    constexpr void swap(Crash& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(Crash& a, Crash& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── satisfies<RequiredClass> — static subsumption check ───────
    //
    // True iff this wrapper's pinned class is at least as strong as
    // RequiredClass.  Stronger guarantee subsumes weaker requirement
    // (a NoThrow-pinned value is admissible at an ErrorReturn-
    // accepting consumer because never-fails trivially satisfies
    // may-return-error).
    //
    // Use:
    //   static_assert(Crash<CrashClass_v::NoThrow, T>
    //                     ::satisfies<CrashClass_v::ErrorReturn>);
    //   // ✓ — NoThrow subsumes ErrorReturn
    //
    //   static_assert(!Crash<CrashClass_v::Abort, T>
    //                      ::satisfies<CrashClass_v::NoThrow>);
    //   // ✓ — Abort does NOT subsume NoThrow
    template <CrashClass_v RequiredClass>
    static constexpr bool satisfies =
        CrashLattice::leq(RequiredClass, Class);

    // ── relax<WeakerClass> — convert to a less-strict class ───────
    //
    // Returns a Crash<WeakerClass, T> carrying the same value bytes.
    // Allowed iff WeakerClass ≤ Class in the lattice (the weaker
    // class is below or equal to the pinned class).  Stronger
    // guarantee still serves weaker requirement.
    //
    // Compile error when WeakerClass > Class — would CLAIM a stronger
    // failure-mode guarantee than the source provides.
    template <CrashClass_v WeakerClass>
        requires (CrashLattice::leq(WeakerClass, Class))
    [[nodiscard]] constexpr Crash<WeakerClass, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Crash<WeakerClass, T>{this->peek()};
    }

    template <CrashClass_v WeakerClass>
        requires (CrashLattice::leq(WeakerClass, Class))
    [[nodiscard]] constexpr Crash<WeakerClass, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Crash<WeakerClass, T>{
            std::move(impl_).consume()};
    }
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace crash {
    template <typename T> using Abort       = Crash<CrashClass_v::Abort,       T>;
    template <typename T> using Throw       = Crash<CrashClass_v::Throw,       T>;
    template <typename T> using ErrorReturn = Crash<CrashClass_v::ErrorReturn, T>;
    template <typename T> using NoThrow     = Crash<CrashClass_v::NoThrow,     T>;
}  // namespace crash

// ── Layout invariants ───────────────────────────────────────────────
namespace detail::crash_layout {

template <typename T> using NoThrowC     = Crash<CrashClass_v::NoThrow,     T>;
template <typename T> using ErrorReturnC = Crash<CrashClass_v::ErrorReturn, T>;
template <typename T> using ThrowC       = Crash<CrashClass_v::Throw,       T>;
template <typename T> using AbortC       = Crash<CrashClass_v::Abort,       T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoThrowC,     char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoThrowC,     int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoThrowC,     double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ErrorReturnC, char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ErrorReturnC, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ErrorReturnC, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ThrowC,       char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ThrowC,       int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ThrowC,       double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbortC,       char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbortC,       int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbortC,       double);

}  // namespace detail::crash_layout

static_assert(sizeof(Crash<CrashClass_v::NoThrow,     char>)   == sizeof(char));
static_assert(sizeof(Crash<CrashClass_v::NoThrow,     int>)    == sizeof(int));
static_assert(sizeof(Crash<CrashClass_v::NoThrow,     double>) == sizeof(double));
static_assert(sizeof(Crash<CrashClass_v::ErrorReturn, char>)   == sizeof(char));
static_assert(sizeof(Crash<CrashClass_v::ErrorReturn, int>)    == sizeof(int));
static_assert(sizeof(Crash<CrashClass_v::ErrorReturn, double>) == sizeof(double));
static_assert(sizeof(Crash<CrashClass_v::Throw,       char>)   == sizeof(char));
static_assert(sizeof(Crash<CrashClass_v::Throw,       int>)    == sizeof(int));
static_assert(sizeof(Crash<CrashClass_v::Throw,       double>) == sizeof(double));
static_assert(sizeof(Crash<CrashClass_v::Abort,       char>)   == sizeof(char));
static_assert(sizeof(Crash<CrashClass_v::Abort,       int>)    == sizeof(int));
static_assert(sizeof(Crash<CrashClass_v::Abort,       double>) == sizeof(double));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::crash_self_test {

using NoThrowInt     = Crash<CrashClass_v::NoThrow,     int>;
using ErrorReturnInt = Crash<CrashClass_v::ErrorReturn, int>;
using ThrowInt       = Crash<CrashClass_v::Throw,       int>;
using AbortInt       = Crash<CrashClass_v::Abort,       int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr NoThrowInt n_default{};
static_assert(n_default.peek() == 0);
static_assert(n_default.crash_class == CrashClass_v::NoThrow);

inline constexpr NoThrowInt n_explicit{42};
static_assert(n_explicit.peek() == 42);

inline constexpr NoThrowInt n_in_place{std::in_place, 7};
static_assert(n_in_place.peek() == 7);

// ── Pinned class accessor ────────────────────────────────────────
static_assert(NoThrowInt::crash_class     == CrashClass_v::NoThrow);
static_assert(ErrorReturnInt::crash_class == CrashClass_v::ErrorReturn);
static_assert(ThrowInt::crash_class       == CrashClass_v::Throw);
static_assert(AbortInt::crash_class       == CrashClass_v::Abort);

// ── satisfies<RequiredClass> — subsumption-up direction ──────────
//
// NoThrow subsumes EVERY consumer.  THE LOAD-BEARING POSITIVE.
static_assert(NoThrowInt::satisfies<CrashClass_v::NoThrow>);
static_assert(NoThrowInt::satisfies<CrashClass_v::ErrorReturn>);
static_assert(NoThrowInt::satisfies<CrashClass_v::Throw>);
static_assert(NoThrowInt::satisfies<CrashClass_v::Abort>);

// ErrorReturn satisfies ErrorReturn / Throw / Abort; FAILS on NoThrow.
static_assert( ErrorReturnInt::satisfies<CrashClass_v::ErrorReturn>);   // self
static_assert( ErrorReturnInt::satisfies<CrashClass_v::Throw>);         // weaker
static_assert( ErrorReturnInt::satisfies<CrashClass_v::Abort>);         // weakest
static_assert(!ErrorReturnInt::satisfies<CrashClass_v::NoThrow>,        // STRONGER fails ✓
    "ErrorReturn MUST NOT satisfy NoThrow — this is the load-bearing "
    "rejection that the OneShotFlag-skipping NoThrow-only admission "
    "gate depends on.  If this fires, an error-returning function's "
    "value could enter a fast path that assumes no failure recovery "
    "is needed, defeating the type-fenced 8th-axiom-style discipline "
    "Crash provides.");

// Throw satisfies Throw + Abort.
static_assert( ThrowInt::satisfies<CrashClass_v::Throw>);
static_assert( ThrowInt::satisfies<CrashClass_v::Abort>);
static_assert(!ThrowInt::satisfies<CrashClass_v::ErrorReturn>);
static_assert(!ThrowInt::satisfies<CrashClass_v::NoThrow>);

// Abort satisfies only Abort.
static_assert( AbortInt::satisfies<CrashClass_v::Abort>);
static_assert(!AbortInt::satisfies<CrashClass_v::Throw>);
static_assert(!AbortInt::satisfies<CrashClass_v::ErrorReturn>);
static_assert(!AbortInt::satisfies<CrashClass_v::NoThrow>);

// ── relax<WeakerClass> — DOWN-the-lattice conversion ─────────────
inline constexpr auto from_nothrow_to_errorreturn =
    NoThrowInt{42}.relax<CrashClass_v::ErrorReturn>();
static_assert(from_nothrow_to_errorreturn.peek() == 42);
static_assert(from_nothrow_to_errorreturn.crash_class == CrashClass_v::ErrorReturn);

inline constexpr auto from_nothrow_to_abort =
    NoThrowInt{99}.relax<CrashClass_v::Abort>();
static_assert(from_nothrow_to_abort.peek() == 99);
static_assert(from_nothrow_to_abort.crash_class == CrashClass_v::Abort);

inline constexpr auto from_errorreturn_to_throw =
    ErrorReturnInt{7}.relax<CrashClass_v::Throw>();
static_assert(from_errorreturn_to_throw.peek() == 7);

inline constexpr auto from_errorreturn_to_self =
    ErrorReturnInt{8}.relax<CrashClass_v::ErrorReturn>();   // identity
static_assert(from_errorreturn_to_self.peek() == 8);

// SFINAE-style detector — proves the requires-clause's correctness.
template <typename W, CrashClass_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

// NoThrow can relax to anything.
static_assert( can_relax<NoThrowInt,     CrashClass_v::NoThrow>);
static_assert( can_relax<NoThrowInt,     CrashClass_v::ErrorReturn>);
static_assert( can_relax<NoThrowInt,     CrashClass_v::Throw>);
static_assert( can_relax<NoThrowInt,     CrashClass_v::Abort>);

// ErrorReturn can relax to ErrorReturn / Throw / Abort.
static_assert( can_relax<ErrorReturnInt, CrashClass_v::ErrorReturn>);
static_assert( can_relax<ErrorReturnInt, CrashClass_v::Throw>);
static_assert( can_relax<ErrorReturnInt, CrashClass_v::Abort>);
static_assert(!can_relax<ErrorReturnInt, CrashClass_v::NoThrow>,
    "relax<NoThrow> on an ErrorReturn-pinned wrapper MUST be "
    "rejected — claiming NoThrow from an ErrorReturn source would "
    "defeat the recovery-aware admission discipline (the consumer "
    "would skip checking std::expected error states).");

// Throw can relax to Throw / Abort.
static_assert( can_relax<ThrowInt, CrashClass_v::Throw>);
static_assert( can_relax<ThrowInt, CrashClass_v::Abort>);
static_assert(!can_relax<ThrowInt, CrashClass_v::ErrorReturn>);
static_assert(!can_relax<ThrowInt, CrashClass_v::NoThrow>);

// Abort can relax only to Abort (reflexive at the bottom).
static_assert( can_relax<AbortInt, CrashClass_v::Abort>);
static_assert(!can_relax<AbortInt, CrashClass_v::Throw>);
static_assert(!can_relax<AbortInt, CrashClass_v::ErrorReturn>);
static_assert(!can_relax<AbortInt, CrashClass_v::NoThrow>,
    "relax<NoThrow> on an Abort-pinned wrapper MUST be rejected — "
    "an abort-prone value claiming NoThrow guarantees would defeat "
    "the entire recovery discipline; OneShotFlag-guarded boundaries "
    "would silently admit values from functions that may have "
    "killed the process.");

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(NoThrowInt::value_type_name().ends_with("int"));
static_assert(NoThrowInt::lattice_name()     == "CrashLattice::At<NoThrow>");
static_assert(ErrorReturnInt::lattice_name() == "CrashLattice::At<ErrorReturn>");
static_assert(ThrowInt::lattice_name()       == "CrashLattice::At<Throw>");
static_assert(AbortInt::lattice_name()       == "CrashLattice::At<Abort>");

// ── swap exchanges T values within the same class pin ───────────
//
// Audit-pass extension: exercise the swap surface at all four classes
// (not just NoThrow), because a refactor that broke swap for any one
// class would silently regress mutability discipline at production
// call sites pinned at that class.
template <typename W>
[[nodiscard]] consteval bool swap_exchanges_within(int x, int y) noexcept {
    W a{x};
    W b{y};
    a.swap(b);
    return a.peek() == y && b.peek() == x;
}
static_assert(swap_exchanges_within<NoThrowInt>(10, 20));
static_assert(swap_exchanges_within<ErrorReturnInt>(11, 21));
static_assert(swap_exchanges_within<ThrowInt>(12, 22));
static_assert(swap_exchanges_within<AbortInt>(13, 23));

template <typename W>
[[nodiscard]] consteval bool free_swap_within(int x, int y) noexcept {
    W a{x};
    W b{y};
    using std::swap;
    swap(a, b);
    return a.peek() == y && b.peek() == x;
}
static_assert(free_swap_within<NoThrowInt>(10, 20));
static_assert(free_swap_within<ErrorReturnInt>(11, 21));
static_assert(free_swap_within<ThrowInt>(12, 22));
static_assert(free_swap_within<AbortInt>(13, 23));

// ── peek_mut allows in-place mutation ─────────────────────────────
template <typename W>
[[nodiscard]] consteval bool peek_mut_works_for(int initial, int target) noexcept {
    W a{initial};
    a.peek_mut() = target;
    return a.peek() == target;
}
static_assert(peek_mut_works_for<NoThrowInt>(10, 99));
static_assert(peek_mut_works_for<ErrorReturnInt>(11, 88));
static_assert(peek_mut_works_for<ThrowInt>(12, 77));
static_assert(peek_mut_works_for<AbortInt>(13, 66));

// ── operator== — same-class, same-T comparison ────────────────────
//
// Audit-pass extension: exercise operator== at all four classes.
// The friend operator== is per-instantiation; if a refactor weakened
// the per-class restriction (e.g., made it a non-friend free template
// over Class), cross-class comparison would silently start compiling.
// Per-class verification ensures the invariant holds at every pin.
template <typename W>
[[nodiscard]] consteval bool equality_compares_value_bytes_for() noexcept {
    W a{42};
    W b{42};
    W c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes_for<NoThrowInt>());
static_assert(equality_compares_value_bytes_for<ErrorReturnInt>());
static_assert(equality_compares_value_bytes_for<ThrowInt>());
static_assert(equality_compares_value_bytes_for<AbortInt>());

// SFINAE: operator== is only present when T has its own ==.
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

static_assert( can_equality_compare<NoThrowInt>);
static_assert(!can_equality_compare<Crash<CrashClass_v::NoThrow, NoEqualityT>>);

// NoEqualityT has DELETED copy ctor — Crash<NoThrow, NoEqualityT>
// must inherit that deletion.
static_assert(!std::is_copy_constructible_v<Crash<CrashClass_v::NoThrow, NoEqualityT>>,
    "Crash<Class, T> must transitively inherit T's copy-deletion. "
    "If this fires, NoEqualityT's deleted copy ctor is no longer "
    "visible through the wrapper.");
static_assert(std::is_move_constructible_v<Crash<CrashClass_v::NoThrow, NoEqualityT>>);

// ── relax reflexivity ─────────────────────────────────────────────
[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    NoThrowInt a{99};
    auto b = a.relax<CrashClass_v::NoThrow>();
    return b.peek() == 99 && b.crash_class == CrashClass_v::NoThrow;
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

template <typename W, CrashClass_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, CrashClass_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using NoThrowMoveOnly = Crash<CrashClass_v::NoThrow, MoveOnlyT>;
static_assert( can_relax_rvalue<NoThrowMoveOnly, CrashClass_v::ErrorReturn>,
    "relax<>() && MUST work for move-only T — the rvalue overload "
    "moves through consume(), no copy required.");
static_assert(!can_relax_lvalue<NoThrowMoveOnly, CrashClass_v::ErrorReturn>,
    "relax<>() const& on move-only T MUST be rejected — the const& "
    "overload requires copy_constructible<T>.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    NoThrowMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<CrashClass_v::ErrorReturn>();
    return dst.peek().v == 77 && dst.crash_class == CrashClass_v::ErrorReturn;
}
static_assert(relax_move_only_works());

// ── Stable-name introspection (FOUND-E07/H06 surface) ────────────
static_assert(NoThrowInt::value_type_name().size() > 0);
static_assert(NoThrowInt::lattice_name().size() > 0);
static_assert(NoThrowInt::lattice_name().starts_with("CrashLattice::At<"));

// ── Convenience aliases resolve correctly ────────────────────────
static_assert(crash::NoThrow<int>::crash_class     == CrashClass_v::NoThrow);
static_assert(crash::ErrorReturn<int>::crash_class == CrashClass_v::ErrorReturn);
static_assert(crash::Throw<int>::crash_class       == CrashClass_v::Throw);
static_assert(crash::Abort<int>::crash_class       == CrashClass_v::Abort);

static_assert(std::is_same_v<crash::NoThrow<double>,
                             Crash<CrashClass_v::NoThrow, double>>);

// ── OneShotFlag-skipping admission simulation — load-bearing ─────
//
// Production: the dispatcher's NoThrow fast path skips OneShotFlag
// reads for values it knows came from no-failure functions.
// Consumer concept:
//   `requires Crash<X>::satisfies<NoThrow>`
// admits ONLY NoThrow-pinned values.

template <typename W>
concept is_nothrow_admissible = W::template satisfies<CrashClass_v::NoThrow>;

static_assert( is_nothrow_admissible<NoThrowInt>,
    "NoThrow value MUST pass the OneShotFlag-skipping NoThrow gate.");
static_assert(!is_nothrow_admissible<ErrorReturnInt>,
    "ErrorReturn value MUST be REJECTED at the NoThrow-only gate — "
    "this is the LOAD-BEARING TEST.  Without this rejection, an "
    "error-returning function's value could enter the OneShotFlag-"
    "skipping fast path and the dispatcher would silently admit "
    "a possibly-failed std::expected.");
static_assert(!is_nothrow_admissible<ThrowInt>);
static_assert(!is_nothrow_admissible<AbortInt>,
    "Abort value MUST be REJECTED at the NoThrow-only gate — admitting "
    "a possibly-aborted value would defeat the entire recovery "
    "discipline.");

// ── Recovery-required admission simulation ────────────────────────
//
// A second admission simulation: Keeper recovery paths require
// Crash<Abort-or-anything> — they want to read EVERY value
// regardless of failure mode (because some recovery checks need
// access to abort-classified data).

template <typename W>
concept is_recovery_admissible = W::template satisfies<CrashClass_v::Abort>;

static_assert( is_recovery_admissible<NoThrowInt>,
    "NoThrow value MUST pass the recovery-admissible gate (NoThrow "
    "subsumes Abort — a never-fails value is trivially admissible "
    "even at the most permissive recovery gate).");
static_assert( is_recovery_admissible<ErrorReturnInt>);
static_assert( is_recovery_admissible<ThrowInt>);
static_assert( is_recovery_admissible<AbortInt>,
    "Abort value MUST pass the recovery-admissible gate (self).");

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    // Construction paths.
    NoThrowInt a{};
    NoThrowInt b{42};
    NoThrowInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    // Static class accessor at runtime.
    if (NoThrowInt::crash_class != CrashClass_v::NoThrow) {
        std::abort();
    }

    // peek_mut.
    NoThrowInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    // Swap at runtime.
    NoThrowInt sx{1};
    NoThrowInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    // relax<WeakerClass> — both overloads.
    NoThrowInt source{77};
    auto relaxed_copy = source.relax<CrashClass_v::ErrorReturn>();
    auto relaxed_move = std::move(source).relax<CrashClass_v::Abort>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    // satisfies<...> at runtime.
    [[maybe_unused]] bool s1 = NoThrowInt::satisfies<CrashClass_v::ErrorReturn>;
    [[maybe_unused]] bool s2 = ErrorReturnInt::satisfies<CrashClass_v::NoThrow>;

    // operator== — same-class.
    NoThrowInt eq_a{42};
    NoThrowInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    // Move-construct from consumed inner.
    NoThrowInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    // Convenience-alias instantiation.
    crash::NoThrow<int>     alias_nothrow{123};
    crash::ErrorReturn<int> alias_errret{456};
    crash::Throw<int>       alias_throw{789};
    crash::Abort<int>       alias_abort{0};
    [[maybe_unused]] auto vn = alias_nothrow.peek();
    [[maybe_unused]] auto ve = alias_errret.peek();
    [[maybe_unused]] auto vt = alias_throw.peek();
    [[maybe_unused]] auto vab = alias_abort.peek();

    // Admission simulations at runtime.
    [[maybe_unused]] bool can_nothrow_pass  = is_nothrow_admissible<NoThrowInt>;
    [[maybe_unused]] bool can_recovery_pass = is_recovery_admissible<AbortInt>;
}

}  // namespace detail::crash_self_test

}  // namespace crucible::safety

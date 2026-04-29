#pragma once

// ── crucible::algebra::lattices::CrashLattice ───────────────────────
//
// Four-tier total-order chain lattice over the failure-mode-strength
// spectrum.  The grading axis underlying the Crash wrapper from
// 28_04_2026_effects.md §4.3.10 (FOUND-G58).
//
// Citation: BSYZ22 crash-stop session types (Bocchi, Yoshida 2022)
// + bridges/CrashTransport.h::CrashWatchedHandle (already implements
// the runtime mechanism — this wrapper makes it type-level visible).
//
// THE LOAD-BEARING USE CASE: production OneShotFlag::peek()-guarded
// boundaries.  Today the Keeper reads a runtime OneShotFlag to
// decide "did the producing function abort?  Run recovery."  With
// the wrapper, the producing function's failure-mode promise is
// type-pinned: a `Crash<NoThrow, T>` value bypasses the OneShotFlag
// check entirely (the type guarantees no abort happened), while a
// `Crash<Abort, T>` value MUST flow through recovery-aware code.
//
// Composes orthogonally with the eight sister chain/partial-order
// wrappers (DetSafe / HotPath / Wait / MemOrder / Progress /
// AllocClass / CipherTier / ResidencyHeat) and with the partial-
// order Vendor wrapper via wrapper-nesting per 28_04 §4.7.
//
// ── The classification ──────────────────────────────────────────────
//
//     NoThrow      — Function provides STRONGEST guarantee: no
//                     failure mode.  Admitted at every consumer
//                     gate — including the strictest "no recovery
//                     code needed" gate.  Production: pure
//                     arithmetic helpers; getters; constexpr
//                     evaluators; the bulk of Mimic IR-emit code.
//     ErrorReturn  — Function may return an error via std::expected
//                     (or similar typed-error channel).  Caller
//                     MUST check the return value; type system
//                     enforces this via [[nodiscard]].  No abort,
//                     no exception unwind.  Production: TraceLoader
//                     I/O paths; Cipher tier promotion; Forge phase
//                     boundaries.
//     Throw        — Function may throw a C++ exception.  Caller
//                     must have a try/catch in the unwind path or
//                     accept terminate() at noexcept boundaries.
//                     ESSENTIALLY BANNED IN CRUCIBLE — the project
//                     compiles with -fno-exceptions (CLAUDE.md §III)
//                     so Throw is reserved for future / external
//                     boundary code only (e.g., a Vessel adapter
//                     receiving from a PyTorch path that uses
//                     exceptions internally).
//     Abort        — Function may call crucible_abort() / std::abort
//                     / process termination.  WEAKEST guarantee —
//                     admitted at the FEWEST consumer gates.
//                     Production: Keeper init / shutdown boundaries;
//                     the contract violation handler; arena
//                     exhaustion; static initialization fiasco
//                     guards.  Consumed only by recovery-aware
//                     paths that read OneShotFlag.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class CrashClass ∈ {Abort, Throw, ErrorReturn, NoThrow}.
// Order:   Abort ⊑ Throw ⊑ ErrorReturn ⊑ NoThrow.
//
// Bottom = Abort     (weakest claim — function may kill the process;
//                     admits only at recovery-aware OneShotFlag-
//                     guarded gates).
// Top    = NoThrow   (strongest claim — function has no failure
//                     mode; admits at every gate).
// Join   = max       (the more-restricted of two providers).
// Meet   = min       (the more-permissive of two providers).
//
// ── Direction convention ────────────────────────────────────────────
//
// Stronger guarantee = HIGHER in the lattice.  `leq(weak, strong)`
// reads "a weaker-claim function is below a stronger-claim function
// in the lattice".  A NoThrow-classified value is admissible
// everywhere because it's the strongest possible promise (function
// will not fail).
//
//   Crash<NoThrow>::satisfies<ErrorReturn>  = leq(ErrorReturn, NoThrow)  = true ✓
//   Crash<Abort>::satisfies<NoThrow>        = leq(NoThrow, Abort)        = false ✓
//   Crash<NoThrow>::satisfies<Abort>        = leq(Abort, NoThrow)        = true ✓
//
// ── Why Throw ⊑ ErrorReturn (the non-obvious linearization) ────────
//
// Both Throw and ErrorReturn describe "function failed but did not
// kill the process".  In a strict sense they are orthogonal
// obligations on the caller (try/catch frame vs return-value check).
// We linearize them as Throw ⊑ ErrorReturn because:
//
//   1. Crucible compiles with -fno-exceptions (CLAUDE.md §III); a
//      function that documents itself as Throw cannot exist in the
//      production tree.  Ranking Throw below ErrorReturn means
//      `Crash<ErrorReturn>::satisfies<Throw>` is TRUE, which is
//      vacuous-but-safe (no Throw-required gate exists).
//   2. ErrorReturn is type-system-enforced ([[nodiscard]] +
//      std::expected) — the caller's obligation is statically
//      visible.  Throw is control-flow-enforced — the caller's
//      obligation is dynamic.  The static enforcement is "stronger"
//      in the sense of providing more compile-time information,
//      hence "higher" in the lattice.
//
// A future maintainer who needs Throw to be incomparable with
// ErrorReturn (e.g., for a Vessel adapter that genuinely supports
// both) should refactor CrashLattice into a partial-order, mirroring
// the VendorLattice precedent.  Until that need arises, the chain
// linearization is sound + sufficient for the OneShotFlag use case.
//
// ── DIVERGENCE FROM 28_04_2026_effects.md §4.3.10 SPEC ─────────────
//
// Spec puts NoThrow=0, Throw=1, Abort=2, ErrorReturn=3 — an enum
// ordinal that does NOT reflect any meaningful semantic ordering.
// This implementation REORDERS the enum to:
//
//   Abort       = 0 (bottom)
//   Throw       = 1
//   ErrorReturn = 2
//   NoThrow     = 3 (top)
//
// matching the universal project convention (bottom = weakest claim;
// see HotPath / DetSafe / Wait / MemOrder / Progress / AllocClass /
// CipherTier / ResidencyHeat for the precedent — all eight sister
// chain lattices invert spec ordinals to align with the convention).
// The SEMANTIC contract from the spec ("BSYZ22 crash-stop;
// CrashWatchedHandle runtime mechanism; OneShotFlag-guarded
// boundaries") is preserved exactly.
//
//   Axiom coverage:
//     TypeSafe — CrashClass is a strong scoped enum;
//                cross-class mixing requires `std::to_underlying`.
//   Runtime cost:
//     leq / join / meet — single integer compare; the four-element
//     domain compiles to a 1-byte field.  When wrapped at a fixed
//     type-level class via `CrashLattice::At<CrashClass::NoThrow>`,
//     the grade EBO-collapses to zero bytes.
//
// ── At<T> singleton sub-lattice ─────────────────────────────────────
//
// Mirrors the eight sister chain lattices: a per-CrashClass
// singleton sub-lattice with empty element_type for regime-1 EBO
// collapse.
//
// See FOUND-G58 (this file) for the lattice; FOUND-G59
// (safety/Crash.h) for the type-pinned wrapper;
// 28_04_2026_effects.md §4.3.10 for the production-call-site
// rationale; bridges/CrashTransport.h::CrashWatchedHandle for the
// runtime mechanism this wrapper type-fences.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── CrashClass — chain over function failure-mode strength ────────
//
// Ordinal convention: Abort=0 (bottom) ... NoThrow=3 (top), per
// project convention (bottom=0=weakest).  INVERTS the spec's
// ordinal hint; semantic contract preserved.  Same shape as the
// eight sister chain lattices.
enum class CrashClass : std::uint8_t {
    Abort       = 0,    // bottom: may abort process — admits nowhere
    Throw       = 1,    // may throw — banned in -fno-exceptions tree
    ErrorReturn = 2,    // may return error via std::expected
    NoThrow     = 3,    // top: no failure mode — admits everywhere
};

inline constexpr std::size_t crash_class_count =
    std::meta::enumerators_of(^^CrashClass).size();

[[nodiscard]] consteval std::string_view crash_class_name(
    CrashClass c) noexcept {
    switch (c) {
        case CrashClass::Abort:       return "Abort";
        case CrashClass::Throw:       return "Throw";
        case CrashClass::ErrorReturn: return "ErrorReturn";
        case CrashClass::NoThrow:     return "NoThrow";
        default:                      return std::string_view{
            "<unknown CrashClass>"};
    }
}

// ── Full CrashLattice (chain order) ─────────────────────────────────
struct CrashLattice : ChainLatticeOps<CrashClass> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return CrashClass::Abort;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return CrashClass::NoThrow;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "CrashLattice";
    }

    template <CrashClass C>
    struct At {
        struct element_type {
            using crash_class_value_type = CrashClass;
            [[nodiscard]] constexpr operator crash_class_value_type() const noexcept {
                return C;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr CrashClass crash_class = C;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (C) {
                case CrashClass::Abort:       return "CrashLattice::At<Abort>";
                case CrashClass::Throw:       return "CrashLattice::At<Throw>";
                case CrashClass::ErrorReturn: return "CrashLattice::At<ErrorReturn>";
                case CrashClass::NoThrow:     return "CrashLattice::At<NoThrow>";
                default:                      return "CrashLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace crash_class {
    using AbortClass       = CrashLattice::At<CrashClass::Abort>;
    using ThrowClass       = CrashLattice::At<CrashClass::Throw>;
    using ErrorReturnClass = CrashLattice::At<CrashClass::ErrorReturn>;
    using NoThrowClass     = CrashLattice::At<CrashClass::NoThrow>;
}  // namespace crash_class

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::crash_lattice_self_test {

static_assert(crash_class_count == 4,
    "CrashClass catalog diverged from {Abort, Throw, ErrorReturn, "
    "NoThrow}; confirm intent and update the dispatcher's failure-"
    "mode admission gates + OneShotFlag-guarded boundary checks.");

[[nodiscard]] consteval bool every_crash_class_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^CrashClass));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (crash_class_name([:en:]) ==
            std::string_view{"<unknown CrashClass>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_crash_class_has_name(),
    "crash_class_name() switch missing arm for at least one class.");

static_assert(Lattice<CrashLattice>);
static_assert(BoundedLattice<CrashLattice>);
static_assert(Lattice<crash_class::AbortClass>);
static_assert(Lattice<crash_class::ThrowClass>);
static_assert(Lattice<crash_class::ErrorReturnClass>);
static_assert(Lattice<crash_class::NoThrowClass>);
static_assert(BoundedLattice<crash_class::NoThrowClass>);

static_assert(!UnboundedLattice<CrashLattice>);
static_assert(!Semiring<CrashLattice>);

static_assert(std::is_empty_v<crash_class::AbortClass::element_type>);
static_assert(std::is_empty_v<crash_class::ThrowClass::element_type>);
static_assert(std::is_empty_v<crash_class::ErrorReturnClass::element_type>);
static_assert(std::is_empty_v<crash_class::NoThrowClass::element_type>);

// EXHAUSTIVE coverage — (CrashClass)³ = 64 triples.
static_assert(verify_chain_lattice_exhaustive<CrashLattice>(),
    "CrashLattice's chain-order axioms must hold at every "
    "(CrashClass)³ triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<CrashLattice>(),
    "CrashLattice's chain order must satisfy distributivity at "
    "every (CrashClass)³ triple.");

// Direct order witnesses.
static_assert( CrashLattice::leq(CrashClass::Abort,       CrashClass::Throw));
static_assert( CrashLattice::leq(CrashClass::Throw,       CrashClass::ErrorReturn));
static_assert( CrashLattice::leq(CrashClass::ErrorReturn, CrashClass::NoThrow));
static_assert( CrashLattice::leq(CrashClass::Abort,       CrashClass::NoThrow));  // transitive
static_assert(!CrashLattice::leq(CrashClass::NoThrow,     CrashClass::Abort));
static_assert(!CrashLattice::leq(CrashClass::NoThrow,     CrashClass::ErrorReturn));
static_assert(!CrashLattice::leq(CrashClass::ErrorReturn, CrashClass::Throw));
static_assert(!CrashLattice::leq(CrashClass::Throw,       CrashClass::Abort));

static_assert(CrashLattice::bottom() == CrashClass::Abort);
static_assert(CrashLattice::top()    == CrashClass::NoThrow);

static_assert(CrashLattice::join(CrashClass::Abort, CrashClass::NoThrow)
              == CrashClass::NoThrow);
static_assert(CrashLattice::join(CrashClass::Throw, CrashClass::Abort)
              == CrashClass::Throw);
static_assert(CrashLattice::meet(CrashClass::Abort, CrashClass::NoThrow)
              == CrashClass::Abort);
static_assert(CrashLattice::meet(CrashClass::ErrorReturn, CrashClass::NoThrow)
              == CrashClass::ErrorReturn);

static_assert(CrashLattice::name() == "CrashLattice");
static_assert(crash_class::AbortClass::name()       == "CrashLattice::At<Abort>");
static_assert(crash_class::ThrowClass::name()       == "CrashLattice::At<Throw>");
static_assert(crash_class::ErrorReturnClass::name() == "CrashLattice::At<ErrorReturn>");
static_assert(crash_class::NoThrowClass::name()     == "CrashLattice::At<NoThrow>");

[[nodiscard]] consteval bool every_at_crash_class_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^CrashClass));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (CrashLattice::At<([:en:])>::name() ==
            std::string_view{"CrashLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_crash_class_has_name(),
    "CrashLattice::At<C>::name() switch missing an arm.");

static_assert(crash_class::AbortClass::crash_class       == CrashClass::Abort);
static_assert(crash_class::ThrowClass::crash_class       == CrashClass::Throw);
static_assert(crash_class::ErrorReturnClass::crash_class == CrashClass::ErrorReturn);
static_assert(crash_class::NoThrowClass::crash_class     == CrashClass::NoThrow);

// ── Layout invariants ───────────────────────────────────────────────
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T_>
using NoThrowGraded = Graded<ModalityKind::Absolute,
                             crash_class::NoThrowClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoThrowGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoThrowGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoThrowGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoThrowGraded, double);

template <typename T_>
using ErrorReturnGraded = Graded<ModalityKind::Absolute,
                                 crash_class::ErrorReturnClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ErrorReturnGraded, EightByteValue);

template <typename T_>
using AbortGraded = Graded<ModalityKind::Absolute,
                           crash_class::AbortClass, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbortGraded, EightByteValue);

inline void runtime_smoke_test() {
    CrashClass a = CrashClass::Abort;
    CrashClass b = CrashClass::NoThrow;
    [[maybe_unused]] bool       l1   = CrashLattice::leq(a, b);
    [[maybe_unused]] CrashClass j1   = CrashLattice::join(a, b);
    [[maybe_unused]] CrashClass m1   = CrashLattice::meet(a, b);
    [[maybe_unused]] CrashClass bot  = CrashLattice::bottom();
    [[maybe_unused]] CrashClass topv = CrashLattice::top();

    CrashClass middle = CrashClass::ErrorReturn;
    [[maybe_unused]] CrashClass j2 = CrashLattice::join(middle, a);   // ErrorReturn
    [[maybe_unused]] CrashClass m2 = CrashLattice::meet(middle, b);   // ErrorReturn

    OneByteValue v{42};
    NoThrowGraded<OneByteValue> initial{v, crash_class::NoThrowClass::bottom()};
    auto widened   = initial.weaken(crash_class::NoThrowClass::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(crash_class::NoThrowClass::top());

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    crash_class::NoThrowClass::element_type e{};
    [[maybe_unused]] CrashClass rec = e;
}

}  // namespace detail::crash_lattice_self_test

}  // namespace crucible::algebra::lattices

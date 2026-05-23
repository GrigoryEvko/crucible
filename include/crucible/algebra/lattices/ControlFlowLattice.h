#pragma once

// ── crucible::algebra::lattices::ControlFlowLattice ─────────────────
//
// SCAFFOLDING header for FIXY-V-239.  Ships the single sub-axis enum
// `ControlFlow` + its `ChainLatticeOps`-based lattice algebra +
// `At<T>` singleton + reflection-driven self-test for the
// `DimensionAxis::ControlFlow` axis (dim 24, Tier-S Semiring,
// 2026-05-23).  V-242 wraps it as a `safety/ControlFlow.h` Graded
// carrier; V-243 adds the CollisionCatalog cross-axis rules; V-244
// (fixy/grant/Ctrl.h) routes throws / abort / longjmp / exit /
// coroutine grants onto this axis.
//
// ── Why a dedicated ControlFlow axis (DimensionAxis::ControlFlow, dim 24) ─
//
// Before V-238/V-239, "what non-local control-flow escape can this
// function perform" was implicit in the Met(X) effect row and the
// `-fno-exceptions` build posture — a binary "exceptions are off, so
// nothing throws".  That collapses five structurally distinct escape
// surfaces into one bit and cannot drive several real gates:
//
//   1. **Forge phase E.RecipeSelect** admits a kernel to the foreground
//      hot path ONLY if its escape capability ⊑ `Pure`.  An effect-row
//      bit cannot distinguish "always returns normally" (hot-path-safe)
//      from "may longjmp across an FFI boundary" (RAII-unsafe, must not
//      run where destructors are load-bearing).
//
//   2. **Warden signal-handler / deadline-watchdog audit** must know
//      whether a function is reachable from an async signal handler.
//      Code on a signal path must be ⊑ `MaySignal` AND async-signal-
//      safe; only this axis makes "on a signal path" expressible at
//      the type level.
//
//   3. **CSL `permission_fork` body gate** (V-087 already rejects
//      `grant::ctrl::throws`): a forked child body that may throw or
//      longjmp across the jthread boundary is structurally unsound.
//      The axis lets the fork gate reject it by the body's declared
//      ControlFlow tier rather than a one-off grant scan.
//
//   4. **Cipher emergency_flush on the abort path**: `crucible_abort`
//      declares `AbortOnly`; the flush it invokes must itself be
//      ⊑ `AbortOnly` (no throw / longjmp that would re-enter a dying
//      process).  Folding onto the effect row would lose the
//      abort-vs-unwind-vs-jump distinction this gate depends on.
//
// ── Tier classification (Tier-S Semiring with par=join) ─────────────
//
// ControlFlow is `TierKind::Semiring` per `tier_of_axis(ControlFlow)`.
// The composition reading is "control-flow-escape union":
//
//   * Two call sites composing in parallel admit the JOIN (the wider
//     escape capability) of their declared tiers.  If site A may throw
//     and site B may longjmp, the parallel composition may do either,
//     i.e. MayLongjmp (the larger of the two on the chain).
//   * Two call sites composing in sequence likewise admit the JOIN — a
//     sequence's escape set is the union of its components' escape sets.
//
// This par/seq reading parallels Met(X)'s effect-row union but at the
// granularity of NON-LOCAL CONTROL TRANSFER rather than memory effect.
//
// ── Chain order — subset-inclusion of allowed control-flow escapes ──
//
// Ordinal 0 = Pure (smallest escape set — the function ALWAYS returns
// normally to its immediate caller; no abort, no throw, no longjmp, no
// signal).  Ordinal 4 = MaySignal (largest set — the function may, in
// addition to everything below, raise / deliver an asynchronous
// signal).  Each step UP the chain strictly subsumes the previous
// tier's escape capability; the chain is a total order.
//
// Reading the chain bottom-to-top, every tier permits every escape of
// every tier below it PLUS its own:
//
//   Pure ⊏ AbortOnly ⊏ ThrowOnly ⊏ MayLongjmp ⊏ MaySignal
//
// A function declaring `ControlFlow = X` ASSERTS its actual escape
// capability set ⊆ X's allowed set.  Hot-path admission
// `control_flow ⊑ Pure` therefore requires the function to claim
// EXACTLY Pure (the bottom of the chain) — Crucible's `-fno-exceptions`
// foreground hot path IS Pure by construction.
//
// Why each step is strictly "more disruptive / harder to reason about"
// than the one below it (the production rationale for the order):
//
//   Pure       = 0 — always returns normally; the ONLY control transfer
//                     is the normal return.  Bottom of the chain; the
//                     hot-path target.
//   AbortOnly  = 1 — may call std::abort / std::terminate /
//                     __builtin_trap: process-terminating, NO unwinding,
//                     NO recovery.  Just above Pure because abort is the
//                     SIMPLEST non-local transfer to reason about —
//                     exactly one outcome (process death), no handler
//                     search, no destructor ordering.  crucible_abort /
//                     CRUCIBLE_INVARIANT / the contract-violation
//                     handler live here.
//   ThrowOnly  = 2 — may throw a C++ exception: a non-local UNWINDING
//                     transfer to a matching handler that DOES run
//                     destructors (RAII-safe).  Strictly above AbortOnly
//                     because a throw has MANY outcomes (caught at any
//                     enclosing handler, rethrown, converted) vs abort's
//                     single outcome — more control-flow states to
//                     reason about.  Under -fno-exceptions this tier
//                     classifies boundary / host-interop code (the
//                     Vessel adapter that sees framework exceptions) and
//                     subsumes AbortOnly (a throw with no handler calls
//                     std::terminate).
//   MayLongjmp = 3 — may call longjmp: a non-local JUMP that bypasses
//                     the normal return path AND does NOT run C++
//                     destructors (RAII-UNSAFE — jumping over a
//                     non-trivial automatic object is UB).  Strictly
//                     above ThrowOnly precisely because longjmp SKIPS
//                     the cleanup a throw guarantees: a function that
//                     may longjmp can leak / corrupt where a throwing
//                     one would not.  C-library setjmp/longjmp interop
//                     (libjpeg / libpng-style error escapes) reached
//                     through FFI.
//   MaySignal  = 4 — may raise / deliver a signal (raise, kill(self),
//                     or run inside an async signal handler): an
//                     ASYNCHRONOUS control transfer that can interrupt
//                     at ANY instruction boundary, subject to
//                     async-signal-safety constraints.  Top of the
//                     chain — subsumes every SYNCHRONOUS escape below it
//                     and adds non-deterministic timing.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — `ControlFlow` is a strong scoped enum (`enum class
//                : uint8_t`); cross-axis mixing requires
//                `std::to_underlying` and surfaces at the call site.
//   InitSafe — every enumerator has an explicit ordinal; reflection-
//                driven coverage tests fire automatically if a switch
//                arm is forgotten.
//   DetSafe  — lattice operations are `constexpr` (not `consteval`)
//                so a runtime Graded carrier can enforce its
//                `pre (L::leq(...))` precondition under enforce.
//   LeakSafe — zero-state enum; no resources.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// V-239 scaffolding: zero cost.  The enum compiles to a single uint8_t
// per value; the `At<T>` singleton's element_type is empty and
// EBO-collapses to 0 bytes via `[[no_unique_address]]` at every future
// use site (V-242 wrapper, V-244 grants).
//
// ── Forward references ─────────────────────────────────────────────
//
//   FIXY-V-242 — safety/ControlFlow.h: Graded<Absolute, At<T>, P>
//                carrier wiring this lattice to the value level.
//   FIXY-V-243 — safety/CollisionCatalog.h: the control-flow cross-axis
//                rules (e.g. HotPath × ControlFlow ≥ AbortOnly reject).
//   FIXY-V-244 — fixy/grant/Ctrl.h: throws / abort / longjmp / exit /
//                coroutine grant tags, each routing through which_dim<>
//                to DimensionAxis::ControlFlow.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// ── ControlFlow — non-local control-flow escape taxonomy ────────────
//
// Chain ordering: each tier strictly subsumes the escape capability of
// every tier below it (capability-superset total order).  Ordinal 0 =
// smallest set (Pure, function always returns normally); ordinal 4 =
// largest set (MaySignal, function may also raise an async signal).
enum class ControlFlow : std::uint8_t {
    Pure       = 0,  // bottom — always returns normally; no non-local escape
    AbortOnly  = 1,  // may std::abort / terminate / __builtin_trap (no unwind)
    ThrowOnly  = 2,  // may throw a C++ exception (unwinds, runs destructors)
    MayLongjmp = 3,  // may longjmp (jump, SKIPS destructors — RAII-unsafe)
    MaySignal  = 4,  // top — may raise / deliver an async signal
};

[[nodiscard]] consteval std::string_view control_flow_name(ControlFlow t) noexcept {
    switch (t) {
        case ControlFlow::Pure:       return "Pure";
        case ControlFlow::AbortOnly:  return "AbortOnly";
        case ControlFlow::ThrowOnly:  return "ThrowOnly";
        case ControlFlow::MayLongjmp: return "MayLongjmp";
        case ControlFlow::MaySignal:  return "MaySignal";
        default:                      return std::string_view{"<unknown ControlFlow>"};
    }
}

struct ControlFlowLattice : ChainLatticeOps<ControlFlow> {
    [[nodiscard]] static constexpr ControlFlow bottom() noexcept { return ControlFlow::Pure; }
    [[nodiscard]] static constexpr ControlFlow top()    noexcept { return ControlFlow::MaySignal; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "ControlFlowLattice"; }

    template <ControlFlow T>
    struct At {
        struct element_type {
            using control_flow_value_type = ControlFlow;
            [[nodiscard]] constexpr operator control_flow_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr ControlFlow tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case ControlFlow::Pure:       return "ControlFlowLattice::At<Pure>";
                case ControlFlow::AbortOnly:  return "ControlFlowLattice::At<AbortOnly>";
                case ControlFlow::ThrowOnly:  return "ControlFlowLattice::At<ThrowOnly>";
                case ControlFlow::MayLongjmp: return "ControlFlowLattice::At<MayLongjmp>";
                case ControlFlow::MaySignal:  return "ControlFlowLattice::At<MaySignal>";
                default:                      return "ControlFlowLattice::At<?>";
            }
        }
    };
};

// ── Self-test (V-239 scaffolding sanity) ────────────────────────────
namespace detail::control_flow_lattice_self_test {

// Catalog cardinality — the escape chain has exactly 5 tiers.
inline constexpr std::size_t control_flow_count =
    std::meta::enumerators_of(^^ControlFlow).size();

static_assert(control_flow_count == 5,
    "ControlFlow diverged from {Pure, AbortOnly, ThrowOnly, MayLongjmp, "
    "MaySignal} per V-239 §taxonomy.  Adding a new escape tier requires "
    "(a) appending at the next free ordinal (append-only per FOUND-I04 "
    "Universe extension rule), (b) the matching control_flow_name() "
    "switch arm, (c) the matching At<T> singleton name() arm.  Reusing "
    "an existing ordinal would silently change every stored row_hash "
    "(federation cache key) without warning.");

// Bottom-element pin — ordinal 0 is the smallest escape set (Pure: the
// function always returns normally, the hot-path-safest claim).
static_assert(std::to_underlying(ControlFlow::Pure) == 0);

// Top-element pin — ordinal 4 is the largest set (MaySignal).
static_assert(std::to_underlying(ControlFlow::MaySignal) == 4);

// Underlying type pin — uint8_t (mirrors SyscallFamily; lets a future
// effect-row bridge derive indices without zero-extending).
static_assert(std::is_same_v<std::underlying_type_t<ControlFlow>, std::uint8_t>);

// Reflection-driven name coverage — every enumerator must resolve to a
// non-sentinel, non-empty name.  Auto-extends if the enum grows.
[[nodiscard]] consteval bool every_control_flow_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^ControlFlow));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = control_flow_name([:en:]);
        if (n == std::string_view{"<unknown ControlFlow>"}) return false;
        if (n.empty())                                      return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_control_flow_has_name(),
    "control_flow_name() switch missing an arm for at least one "
    "ControlFlow enumerator.  Add the arm or the new tier leaks the "
    "'<unknown ControlFlow>' sentinel.");

// Concept conformance — chain lattice satisfies Lattice + BoundedLattice
// and NOT Semiring (chain order has no independent ⊕/⊗ structure; the
// "Tier-S Semiring" classification is the AXIS tier in DimensionTraits.h,
// describing par=join composition semantics — NOT a Semiring concept on
// the lattice itself).
static_assert(::crucible::algebra::Lattice<ControlFlowLattice>);
static_assert(::crucible::algebra::BoundedLattice<ControlFlowLattice>);
static_assert(!::crucible::algebra::Semiring<ControlFlowLattice>);

// Exhaustive lattice-axiom verifier on (axis)³ triples.  Chain orders
// are always distributive — failure indicates a leq/join/meet defect.
static_assert(verify_chain_lattice_exhaustive<ControlFlowLattice>(),
    "ControlFlowLattice chain-order lattice axioms failed at some triple "
    "— leq/join/meet defect.");
static_assert(verify_chain_lattice_distributive_exhaustive<ControlFlowLattice>(),
    "ControlFlowLattice chain failed distributivity check — leq/join/meet "
    "defect.");

// Bottom / top pins on the lattice surface (catches "someone reordered
// the enum and the lattice failed to follow" drift).
static_assert(ControlFlowLattice::bottom() == ControlFlow::Pure);
static_assert(ControlFlowLattice::top()    == ControlFlow::MaySignal);

// Lattice top-level diagnostic name pin.
static_assert(ControlFlowLattice::name() == std::string_view{"ControlFlowLattice"});

// Strict-chain order pin (bottom ⊏ top witness).  Combined with the
// exhaustive axiom verifier above, the chain direction is structurally
// locked.
static_assert( ControlFlowLattice::leq(ControlFlow::Pure, ControlFlow::MaySignal));
static_assert(!ControlFlowLattice::leq(ControlFlow::MaySignal, ControlFlow::Pure));

// Mid-chain ordering — every tier strictly subsumes the previous.
static_assert(ControlFlowLattice::leq(ControlFlow::Pure,       ControlFlow::AbortOnly));
static_assert(ControlFlowLattice::leq(ControlFlow::AbortOnly,  ControlFlow::ThrowOnly));
static_assert(ControlFlowLattice::leq(ControlFlow::ThrowOnly,  ControlFlow::MayLongjmp));
static_assert(ControlFlowLattice::leq(ControlFlow::MayLongjmp, ControlFlow::MaySignal));

// Reverse direction must fail for non-equal pairs.
static_assert(!ControlFlowLattice::leq(ControlFlow::AbortOnly,  ControlFlow::Pure));
static_assert(!ControlFlowLattice::leq(ControlFlow::MaySignal,  ControlFlow::MayLongjmp));

// Join semantics — par=join (strictest-wins / wider-escape-dominates).
// Composing a throwing site with a longjmping site yields MayLongjmp
// (the wider escape capability).
static_assert(ControlFlowLattice::join(ControlFlow::ThrowOnly, ControlFlow::MayLongjmp)
              == ControlFlow::MayLongjmp);
// Meet semantics — and=meet (tighter-floor).  Meeting a permissive
// binding with a tight admission policy yields the tight floor.
static_assert(ControlFlowLattice::meet(ControlFlow::MaySignal, ControlFlow::AbortOnly)
              == ControlFlow::AbortOnly);

// At<T> singleton — empty element_type for EBO collapse at every use
// site.  V-242's `Graded<Absolute, At<T>, P>` relies on this for
// zero-byte overhead.
static_assert(std::is_empty_v<ControlFlowLattice::At<ControlFlow::Pure>::element_type>);
static_assert(std::is_empty_v<ControlFlowLattice::At<ControlFlow::AbortOnly>::element_type>);
static_assert(std::is_empty_v<ControlFlowLattice::At<ControlFlow::ThrowOnly>::element_type>);
static_assert(std::is_empty_v<ControlFlowLattice::At<ControlFlow::MayLongjmp>::element_type>);
static_assert(std::is_empty_v<ControlFlowLattice::At<ControlFlow::MaySignal>::element_type>);

// At<T>::tier pins the enum value at the type level — what V-242+
// wrappers key on for compile-time admission decisions.
static_assert(ControlFlowLattice::At<ControlFlow::ThrowOnly>::tier == ControlFlow::ThrowOnly);

// Runtime smoke test — per feedback_algebra_runtime_smoke_test_discipline
// memory: pure static_asserts can mask consteval/SFINAE/inline-body
// bugs; runtime ops with non-constant arguments catch them.
inline void control_flow_lattice_runtime_smoke_test() {
    // Pin operands to the chain's extremes, then call leq/join/meet so
    // the optimizer cannot collapse the call to a compile-time fold.
    ControlFlow a = ControlFlow::Pure;
    ControlFlow b = ControlFlow::MaySignal;
    [[maybe_unused]] bool        rl = ControlFlowLattice::leq(a, b);
    [[maybe_unused]] ControlFlow rj = ControlFlowLattice::join(a, b);
    [[maybe_unused]] ControlFlow rm = ControlFlowLattice::meet(a, b);

    // Mid-chain witnesses.
    ControlFlow c = ControlFlow::ThrowOnly;
    ControlFlow d = ControlFlow::MayLongjmp;
    [[maybe_unused]] ControlFlow rj2 = ControlFlowLattice::join(c, d);
    [[maybe_unused]] ControlFlow rm2 = ControlFlowLattice::meet(c, d);

    // At<T>::element_type round-trip — verify the singleton's conversion
    // materializes the right tier at runtime, not just at consteval.
    ControlFlowLattice::At<ControlFlow::AbortOnly>::element_type abort_pin{};
    [[maybe_unused]] ControlFlow abort_recovered = abort_pin;
}

}  // namespace detail::control_flow_lattice_self_test

}  // namespace crucible::algebra::lattices

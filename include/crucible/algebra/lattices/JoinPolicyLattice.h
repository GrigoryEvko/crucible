#pragma once

// ── crucible::algebra::lattices::JoinPolicyLattice ──────────────────
//
// Six-element total-order lattice over fork-join engagement tiers —
// the foundation for safety::JoinPolicy<P, T> per the V-078..V-080
// substrate arc (V-078 lattice, V-079 Graded carrier + row_hash + IsJoinPolicy
// concept, V-080 ThreadLocalRef typed wrapper).  JoinPolicy encodes the
// STRUCTURAL-CONCURRENCY ENGAGEMENT that a parent scope maintains with
// the children it spawns; the lattice composes engagement strictness
// across nested fork sections and downcasts to weaker tiers when
// composing inside a looser outer policy.
//
// ── The six tiers ───────────────────────────────────────────────────
//
//     FORGET             — no handle returned; spawn was fire-and-
//                          forget.  The parent has no way to observe
//                          child completion.  The BOTTOM of the lattice;
//                          weakest engagement, used for genuinely
//                          autonomous workers (logger drain, telemetry
//                          uploader) that outlive their spawner.
//                          Equivalent to `std::thread::detach` called
//                          at spawn time, with the additional API-
//                          shape claim that no handle was ever surfaced.
//     DETACH             — handle was surfaced to the caller and then
//                          explicitly released (.detach() or moved into
//                          an anonymous sink).  Runtime indistinguishable
//                          from FORGET, but PROVENANCE differs: the
//                          caller made a deliberate choice rather than
//                          being denied a handle by the spawn API.
//                          Marks tasks the caller could have observed
//                          but chose not to.  Subsumes FORGET.
//     ABANDON            — handle held to parent scope exit, then
//                          abandoned (parent does NOT wait, does NOT
//                          cancel, does NOT join).  Children may still
//                          be running when the parent destructor fires;
//                          their results are silently discarded.
//                          Distinguishable from DETACH because the
//                          handle outlived the spawn — bugs caused by
//                          dangling references to abandoned children
//                          look different in postmortems than fire-
//                          and-forget bugs.
//     CANCEL             — handle held to parent scope exit, then
//                          cancellation REQUEST (jthread::request_stop
//                          via std::stop_token, or platform equivalent).
//                          Parent does NOT wait for completion;
//                          cancellation is best-effort.  Children that
//                          cooperate observe the request quickly;
//                          children that ignore the request behave as
//                          if ABANDONed.  This is the looseness ceiling
//                          for soft-realtime workloads where missed
//                          deadlines must not block the parent.
//     WAIT_DEADLINE      — parent waits up to a bounded deadline.  Each
//                          unfinished child at the deadline degrades to
//                          one of the looser policies (typically CANCEL,
//                          but the runtime may choose ABANDON if
//                          cooperative-cancellation is unavailable).
//                          This is the structured-concurrency entry
//                          point with hard wall-clock budgets — used
//                          by request-handler fork sections that must
//                          respond within an SLA.
//     JOIN_ALL           — parent waits unconditionally for every
//                          child to complete.  The TOP of the lattice;
//                          full CSL parallel-rule discipline as encoded
//                          by `permissions/PermissionFork.h` — the
//                          parent's Permission token is returned only
//                          after every child has joined.  The default
//                          for production fork sections; deviations
//                          require justification at the spawn site.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Carrier: enum class JoinPolicy over the six tiers above.
// Order:   FORGET ⊑ DETACH ⊑ ABANDON ⊑ CANCEL ⊑ WAIT_DEADLINE ⊑ JOIN_ALL.
//
// Bottom = FORGET    (loosest — no engagement made).
// Top    = JOIN_ALL  (strictest — full structured concurrency).
// Join   = max       (composing engagements STRENGTHENS — a parent
//                     that requires JOIN_ALL nested under a CANCEL
//                     scope still gets JOIN_ALL behavior for its own
//                     children; the stricter inner discipline wins
//                     because the outer is only an upper bound on
//                     looseness, not a downgrade mandate).
// Meet   = min       (relaxation under a looser outer dominates — if
//                     a region asks for CANCEL semantics but the outer
//                     scope only guarantees DETACH, the realizable
//                     contract drops to DETACH; weaker promises
//                     propagate when computing the realizable
//                     guarantee at a given depth).
//
// ── Direction convention (matches WitnessLattice / ConfLattice) ─────
//
// Stronger engagement = higher in the lattice.  `leq(loose, strict)
// = true` reads "the looser tier is subsumed by the stricter tier" —
// a JOIN_ALL parent satisfies any consumer asking for WAIT_DEADLINE,
// CANCEL, ABANDON, DETACH, or FORGET semantics, because JOIN_ALL
// PROVES every guarantee the looser policies promise plus more.
//
// ── Modality is Comonad (matches WitnessLattice) ────────────────────
//
// JoinPolicy<P, T> uses Comonad form per V-079:
//   using JoinPolicy<P, T> = Graded<Comonad, JoinPolicyLattice::At<P>, T>;
//
// Comonad counit `extract` is always available — observing the value
// as plain T is sound because the policy is METADATA about how the
// parent engages with the value's producer, NOT a filter that hides
// the value from observation.  Mirrors WitnessLattice's Comonad
// rationale: the discipline is at the PRODUCER side (only a parent
// that actually joined can mint a JOIN_ALL-tagged result), not the
// consumer side (observers may inspect the value plainly).
//
// ── At<P>: singleton sub-lattice at a fixed type-level tier ─────────
//
// JoinPolicyLattice::At<JoinPolicy::JOIN_ALL> is a single-element
// sub-lattice with EMPTY element_type.  Used by safety::JoinPolicy<P, T>
// (V-079): a `JoinPolicy<join_all, T>` value is always at the
// JOIN_ALL tier — the tier IS the type parameter, encoded at the
// type level via the template-singleton pattern.  Empty element_type
// + [[no_unique_address]] in Graded gives sizeof(JoinPolicy<P, T>)
// == sizeof(T), matching the substrate's zero-overhead guarantee.
//
//   Axiom coverage:
//     TypeSafe — JoinPolicy is a strong enum (`enum class : uint8_t`);
//                conversion to underlying requires std::to_underlying,
//                blocking accidental integer math on tier values.
//     DetSafe — every operation is `constexpr` (NOT `consteval`) so
//                Graded's runtime `pre (L::leq(...))` precondition can
//                fire under the `enforce` contract semantic.
//   Runtime cost:
//     leq / join / meet — single integer compare + select; six-element
//     domain compiles to a 1-byte field with a single branch.  When
//     wrapped at a fixed type-level tier via JoinPolicyLattice::At<P>,
//     the grade EBO-collapses to zero bytes — matching the
//     WitnessLattice::At<W> / ConsistencyLattice::At<C> shape.
//
// References:
//   O'Hearn (2007).               "Resources, concurrency and local
//                                  reasoning."  TCS 375(1-3): 271-307.
//                                  (Concurrent Separation Logic, the
//                                  parallel composition rule that
//                                  JOIN_ALL realizes structurally.)
//   Brookes (2007).               "A semantics for concurrent separation
//                                  logic."  TCS 375(1-3): 227-270.
//                                  (Frame rule under parallel composition;
//                                  the soundness basis for JOIN_ALL as
//                                  a Permission-recovering primitive.)
//   Smith, Nathaniel J. (2018).   "Notes on structured concurrency, or:
//                                  Go statement considered harmful."
//                                  (Coined "structured concurrency"; the
//                                  WAIT_DEADLINE / JOIN_ALL tiers
//                                  realize the discipline.)
//   ISO C++ P0660R10 (2019).      "Stop Token and Joining Thread,
//                                  Rev 10."  (std::jthread + stop_token
//                                  — the runtime substrate for the
//                                  CANCEL / JOIN_ALL tiers.)
//   CLAUDE.md §IX                — "Permission discipline — CSL-typed
//                                  concurrency."  PermissionFork is
//                                  the canonical JOIN_ALL implementor
//                                  in Crucible today; this lattice
//                                  surfaces the engagement spectrum so
//                                  weaker policies can land as a
//                                  Graded-typed alternative.
//   permissions/PermissionFork.h — current substrate; encodes JOIN_ALL
//                                  only.  Looser tiers (CANCEL,
//                                  WAIT_DEADLINE, ABANDON, DETACH,
//                                  FORGET) are the V-079 carrier's
//                                  motivation.
//
// See ALGEBRA-6 (ConfLattice — Comonad convention), ALGEBRA-14
// (ConsistencyLattice — chain convention), FIXY-V-053 (WitnessLattice
// — direct template), and ChainLattice.h for the inherited ops.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── JoinPolicy tier ─────────────────────────────────────────────────
enum class JoinPolicy : std::uint8_t {
    FORGET         = 0,   // weakest  — no handle, no observation
    DETACH         = 1,   // handle returned then explicitly released
    ABANDON        = 2,   // handle held to scope exit, abandoned
    CANCEL         = 3,   // handle held to scope exit, cancel requested
    WAIT_DEADLINE  = 4,   // bounded wait, degrade on timeout
    JOIN_ALL       = 5,   // strongest — unconditional wait for every child
};

// Cardinality + diagnostic name via reflection — auto-bumps on
// future tier extensions; reflection-based name-coverage assertion
// catches missing switch arms.
inline constexpr std::size_t join_policy_count =
    std::meta::enumerators_of(^^JoinPolicy).size();

[[nodiscard]] consteval std::string_view join_policy_name(JoinPolicy p) noexcept {
    switch (p) {
        case JoinPolicy::FORGET:        return "FORGET";
        case JoinPolicy::DETACH:        return "DETACH";
        case JoinPolicy::ABANDON:       return "ABANDON";
        case JoinPolicy::CANCEL:        return "CANCEL";
        case JoinPolicy::WAIT_DEADLINE: return "WAIT_DEADLINE";
        case JoinPolicy::JOIN_ALL:      return "JOIN_ALL";
        default:                        return std::string_view{"<unknown JoinPolicy>"};
    }
}

// ── Full JoinPolicyLattice (chain order) ────────────────────────────
//
// Inherits leq/join/meet from ChainLatticeOps<JoinPolicy> per the
// ChainLattice.h dedup convention (audit Tier-2 dedup; see
// WitnessLattice / ConsistencyLattice for the same shape).
struct JoinPolicyLattice : ChainLatticeOps<JoinPolicy> {
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return JoinPolicy::FORGET;
    }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return JoinPolicy::JOIN_ALL;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "JoinPolicyLattice";
    }

    // ── At<P>: singleton sub-lattice at a fixed type-level tier ─────
    //
    // Used by safety::JoinPolicy<P, T> (V-079):
    //   using JoinPolicy<P, T> = Graded<Comonad, JoinPolicyLattice::At<P>, T>;
    //
    // Empty element_type — sizeof(JoinPolicyLattice::At<P>::element_type)
    // collapses via EBO inside Graded, giving the V-079 wrapper
    // sizeof(JoinPolicy<P, T>) == sizeof(T).  Mirrors the
    // WitnessLattice::At<W>::element_type pattern (WitnessLattice.h:194).
    template <JoinPolicy P>
    struct At {
        struct element_type {
            using join_policy_value_type = JoinPolicy;
            [[nodiscard]] constexpr operator join_policy_value_type() const noexcept {
                return P;
            }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
                return true;
            }
        };

        static constexpr JoinPolicy tier = P;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
        [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (P) {
                case JoinPolicy::FORGET:        return "JoinPolicyLattice::At<FORGET>";
                case JoinPolicy::DETACH:        return "JoinPolicyLattice::At<DETACH>";
                case JoinPolicy::ABANDON:       return "JoinPolicyLattice::At<ABANDON>";
                case JoinPolicy::CANCEL:        return "JoinPolicyLattice::At<CANCEL>";
                case JoinPolicy::WAIT_DEADLINE: return "JoinPolicyLattice::At<WAIT_DEADLINE>";
                case JoinPolicy::JOIN_ALL:      return "JoinPolicyLattice::At<JOIN_ALL>";
                default:                        return "JoinPolicyLattice::At<?>";
            }
        }
    };
};

// ── Convenience aliases ─────────────────────────────────────────────
//
// Suffixed `Tier` to avoid collision with JoinPolicy::FORGET /
// DETACH / ABANDON / CANCEL / WAIT_DEADLINE / JOIN_ALL enumerators in
// user code that does `using namespace ...`.  Matches the
// witness::FormallyVerifiedTier / consistency::EventualTier convention.
namespace join_policy {
    using ForgetTier         = JoinPolicyLattice::At<JoinPolicy::FORGET>;
    using DetachTier         = JoinPolicyLattice::At<JoinPolicy::DETACH>;
    using AbandonTier        = JoinPolicyLattice::At<JoinPolicy::ABANDON>;
    using CancelTier         = JoinPolicyLattice::At<JoinPolicy::CANCEL>;
    using WaitDeadlineTier   = JoinPolicyLattice::At<JoinPolicy::WAIT_DEADLINE>;
    using JoinAllTier        = JoinPolicyLattice::At<JoinPolicy::JOIN_ALL>;
}  // namespace join_policy

// ── Self-test (compile-time + reflection-driven name coverage) ──────
namespace detail::join_policy_lattice_self_test {

// Cardinality + reflection-based name coverage.
static_assert(join_policy_count == 6,
    "JoinPolicy catalog diverged from {FORGET, DETACH, ABANDON, CANCEL, "
    "WAIT_DEADLINE, JOIN_ALL}; confirm intent.  Adding a tier between "
    "CANCEL and WAIT_DEADLINE (e.g. CANCEL_WAIT_BOUNDED) requires "
    "updating the V-079 JoinPolicy<> alias' tier shortcuts AND any "
    "downstream consumer's CollisionCatalog rule.");

[[nodiscard]] consteval bool every_join_policy_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^JoinPolicy));
    // -Wshadow fires on `template for` bodies because GCC 16 unrolls
    // the loop into successive scopes that each declare the same
    // induction variable; suppress locally for the loop body only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (join_policy_name([:en:]) == std::string_view{"<unknown JoinPolicy>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_join_policy_has_name(),
    "join_policy_name() switch missing arm for at least one JoinPolicy "
    "tier — add the arm or the new tier leaks the '<unknown JoinPolicy>' "
    "sentinel into runtime observer's debug output.");

// Concept conformance — full lattice + each At<P> sub-lattice.
static_assert(Lattice<JoinPolicyLattice>);
static_assert(BoundedLattice<JoinPolicyLattice>);
static_assert(Lattice<join_policy::ForgetTier>);
static_assert(Lattice<join_policy::DetachTier>);
static_assert(Lattice<join_policy::AbandonTier>);
static_assert(Lattice<join_policy::CancelTier>);
static_assert(Lattice<join_policy::WaitDeadlineTier>);
static_assert(Lattice<join_policy::JoinAllTier>);
static_assert(BoundedLattice<join_policy::JoinAllTier>);

// Negative concept assertions — pin JoinPolicyLattice's character.
static_assert(!UnboundedLattice<JoinPolicyLattice>);
static_assert(!Semiring<JoinPolicyLattice>);

// Empty element_type for EBO collapse — load-bearing for V-079's
// JoinPolicy<P, T> zero-overhead guarantee.
static_assert(std::is_empty_v<join_policy::ForgetTier::element_type>);
static_assert(std::is_empty_v<join_policy::DetachTier::element_type>);
static_assert(std::is_empty_v<join_policy::AbandonTier::element_type>);
static_assert(std::is_empty_v<join_policy::CancelTier::element_type>);
static_assert(std::is_empty_v<join_policy::WaitDeadlineTier::element_type>);
static_assert(std::is_empty_v<join_policy::JoinAllTier::element_type>);

// EXHAUSTIVE lattice-axiom + distributivity coverage over
// (JoinPolicy)³ = 216 triples each.  Both verifiers extracted into
// ChainLattice.h (audit Tier-2 dedup) — the helpers handle reflection
// over the underlying enum, so adding a new JoinPolicy tier auto-
// extends coverage with no per-lattice code change.
static_assert(verify_chain_lattice_exhaustive<JoinPolicyLattice>(),
    "JoinPolicyLattice's chain-order lattice axioms must hold at every "
    "(JoinPolicy)³ triple — failure indicates a defect in leq/join/meet "
    "or in the underlying enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<JoinPolicyLattice>(),
    "JoinPolicyLattice's chain order must satisfy distributivity at every "
    "(JoinPolicy)³ triple — a chain order always does, so failure would "
    "indicate a defect in join or meet.");

// Direct order witnesses — the entire chain is strictly increasing.
static_assert( JoinPolicyLattice::leq(JoinPolicy::FORGET,        JoinPolicy::DETACH));
static_assert( JoinPolicyLattice::leq(JoinPolicy::DETACH,        JoinPolicy::ABANDON));
static_assert( JoinPolicyLattice::leq(JoinPolicy::ABANDON,       JoinPolicy::CANCEL));
static_assert( JoinPolicyLattice::leq(JoinPolicy::CANCEL,        JoinPolicy::WAIT_DEADLINE));
static_assert( JoinPolicyLattice::leq(JoinPolicy::WAIT_DEADLINE, JoinPolicy::JOIN_ALL));
static_assert( JoinPolicyLattice::leq(JoinPolicy::FORGET,        JoinPolicy::JOIN_ALL));  // transitive endpoints
static_assert(!JoinPolicyLattice::leq(JoinPolicy::JOIN_ALL,      JoinPolicy::FORGET));
static_assert(!JoinPolicyLattice::leq(JoinPolicy::WAIT_DEADLINE, JoinPolicy::CANCEL));
static_assert(!JoinPolicyLattice::leq(JoinPolicy::JOIN_ALL,      JoinPolicy::WAIT_DEADLINE));
static_assert(!JoinPolicyLattice::leq(JoinPolicy::ABANDON,       JoinPolicy::DETACH));

// Pin bottom / top to the chain endpoints.
static_assert(JoinPolicyLattice::bottom() == JoinPolicy::FORGET);
static_assert(JoinPolicyLattice::top()    == JoinPolicy::JOIN_ALL);

// Join strengthens (max); meet weakens (min).
static_assert(JoinPolicyLattice::join(JoinPolicy::FORGET,        JoinPolicy::JOIN_ALL)      == JoinPolicy::JOIN_ALL);
static_assert(JoinPolicyLattice::join(JoinPolicy::DETACH,        JoinPolicy::CANCEL)        == JoinPolicy::CANCEL);
static_assert(JoinPolicyLattice::join(JoinPolicy::ABANDON,       JoinPolicy::WAIT_DEADLINE) == JoinPolicy::WAIT_DEADLINE);
static_assert(JoinPolicyLattice::meet(JoinPolicy::FORGET,        JoinPolicy::JOIN_ALL)      == JoinPolicy::FORGET);
static_assert(JoinPolicyLattice::meet(JoinPolicy::CANCEL,        JoinPolicy::JOIN_ALL)      == JoinPolicy::CANCEL);
static_assert(JoinPolicyLattice::meet(JoinPolicy::WAIT_DEADLINE, JoinPolicy::JOIN_ALL)      == JoinPolicy::WAIT_DEADLINE);

// Diagnostic names — full lattice + per-tier At<P>::name() coverage.
static_assert(JoinPolicyLattice::name() == "JoinPolicyLattice");
static_assert(join_policy::ForgetTier::name()       == "JoinPolicyLattice::At<FORGET>");
static_assert(join_policy::DetachTier::name()       == "JoinPolicyLattice::At<DETACH>");
static_assert(join_policy::AbandonTier::name()      == "JoinPolicyLattice::At<ABANDON>");
static_assert(join_policy::CancelTier::name()       == "JoinPolicyLattice::At<CANCEL>");
static_assert(join_policy::WaitDeadlineTier::name() == "JoinPolicyLattice::At<WAIT_DEADLINE>");
static_assert(join_policy::JoinAllTier::name()      == "JoinPolicyLattice::At<JOIN_ALL>");
static_assert(join_policy_name(JoinPolicy::FORGET)        == "FORGET");
static_assert(join_policy_name(JoinPolicy::DETACH)        == "DETACH");
static_assert(join_policy_name(JoinPolicy::ABANDON)       == "ABANDON");
static_assert(join_policy_name(JoinPolicy::CANCEL)        == "CANCEL");
static_assert(join_policy_name(JoinPolicy::WAIT_DEADLINE) == "WAIT_DEADLINE");
static_assert(join_policy_name(JoinPolicy::JOIN_ALL)      == "JOIN_ALL");

// Reflection-driven coverage check on At<P>::name().
[[nodiscard]] consteval bool every_at_join_policy_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^JoinPolicy));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (JoinPolicyLattice::At<([:en:])>::name() ==
            std::string_view{"JoinPolicyLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_join_policy_has_name(),
    "JoinPolicyLattice::At<P>::name() switch missing an arm for at "
    "least one tier — add the arm or the new tier leaks the "
    "'JoinPolicyLattice::At<?>' sentinel.");

// Convenience aliases resolve correctly.
static_assert(join_policy::ForgetTier::tier        == JoinPolicy::FORGET);
static_assert(join_policy::DetachTier::tier        == JoinPolicy::DETACH);
static_assert(join_policy::AbandonTier::tier       == JoinPolicy::ABANDON);
static_assert(join_policy::CancelTier::tier        == JoinPolicy::CANCEL);
static_assert(join_policy::WaitDeadlineTier::tier  == JoinPolicy::WAIT_DEADLINE);
static_assert(join_policy::JoinAllTier::tier       == JoinPolicy::JOIN_ALL);

// At<P>::element_type → JoinPolicy conversion recovers the type-level tier.
static_assert(static_cast<JoinPolicy>(join_policy::ForgetTier::element_type{})        == JoinPolicy::FORGET);
static_assert(static_cast<JoinPolicy>(join_policy::DetachTier::element_type{})        == JoinPolicy::DETACH);
static_assert(static_cast<JoinPolicy>(join_policy::AbandonTier::element_type{})       == JoinPolicy::ABANDON);
static_assert(static_cast<JoinPolicy>(join_policy::CancelTier::element_type{})        == JoinPolicy::CANCEL);
static_assert(static_cast<JoinPolicy>(join_policy::WaitDeadlineTier::element_type{})  == JoinPolicy::WAIT_DEADLINE);
static_assert(static_cast<JoinPolicy>(join_policy::JoinAllTier::element_type{})       == JoinPolicy::JOIN_ALL);

// ── Layout invariants on Graded<Comonad, At<P>, T> ──────────────────
//
// Mirrors WitnessLattice's FormallyVerifiedGraded pattern (
// WitnessLattice.h:373-389) — pins the V-079 JoinPolicy<P, T>
// wrapper's zero-overhead guarantee across arithmetic and aggregate
// payload types.  Critical for permission_fork-shaped sites where the
// policy must not bloat the child-result tuple.
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

template <typename T>
using JoinAllGraded =
    Graded<ModalityKind::Comonad, join_policy::JoinAllTier, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(JoinAllGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(JoinAllGraded, EightByteValue);
// Arithmetic-T witnesses — pin macro correctness across the trivially-
// default-constructible-T axis (matches WitnessLattice's Tier-2 audit
// discipline).  Critical because JoinPolicy<JOIN_ALL, int> is a likely
// V-079 use case for fork-section accumulator returns.
CRUCIBLE_GRADED_LAYOUT_INVARIANT(JoinAllGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(JoinAllGraded, double);

// Mid-chain tier — verifies the alias works at every tier in the
// chain (not just the top).  CANCEL is the canonical mid-tier
// CSL engagement (request-but-don't-wait).
template <typename T>
using CancelGraded =
    Graded<ModalityKind::Comonad, join_policy::CancelTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CancelGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CancelGraded, EightByteValue);

// Bottom-tier verification — FORGET tier must compose cleanly with
// the carrier even though it claims nothing; load-bearing for the
// V-079 default-policy ergonomic (fire-and-forget workers).
template <typename T>
using ForgetGraded =
    Graded<ModalityKind::Comonad, join_policy::ForgetTier, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ForgetGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ForgetGraded, EightByteValue);

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// lattice ops AND Graded::weaken / compose / extract with non-constant
// arguments at runtime.  Catches consteval-vs-constexpr traps the
// static_assert tests miss; per feedback_header_only_static_assert_
// blind_spot memory, the sentinel TU in test/ forces the bodies
// through under project warnings-as-errors.
inline void runtime_smoke_test() {
    // Full JoinPolicyLattice ops at runtime.
    JoinPolicy a = JoinPolicy::FORGET;
    JoinPolicy b = JoinPolicy::JOIN_ALL;
    [[maybe_unused]] bool       l1   = JoinPolicyLattice::leq(a, b);
    [[maybe_unused]] JoinPolicy j1   = JoinPolicyLattice::join(a, b);
    [[maybe_unused]] JoinPolicy m1   = JoinPolicyLattice::meet(a, b);
    [[maybe_unused]] JoinPolicy bot  = JoinPolicyLattice::bottom();
    [[maybe_unused]] JoinPolicy top  = JoinPolicyLattice::top();

    // Mid-tier ops — chains through the middle of the lattice.
    JoinPolicy mid_cancel  = JoinPolicy::CANCEL;
    JoinPolicy mid_abandon = JoinPolicy::ABANDON;
    [[maybe_unused]] JoinPolicy j2 = JoinPolicyLattice::join(mid_cancel, b);            // JOIN_ALL
    [[maybe_unused]] JoinPolicy m2 = JoinPolicyLattice::meet(mid_cancel, a);            // FORGET
    [[maybe_unused]] JoinPolicy j3 = JoinPolicyLattice::join(mid_abandon, mid_cancel);  // CANCEL
    [[maybe_unused]] JoinPolicy m3 = JoinPolicyLattice::meet(mid_abandon, mid_cancel);  // ABANDON

    // Graded<Comonad, JoinAllTier, T> at runtime.
    OneByteValue v{42};
    JoinAllGraded<OneByteValue> initial{
        v, join_policy::JoinAllTier::bottom()};
    auto widened   = initial.weaken(join_policy::JoinAllTier::top());
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(
                         join_policy::JoinAllTier::top());

    // Comonad counit (extract) — always available, observing the
    // value as plain T does NOT require declassifying the policy.
    auto extracted = std::move(composed).extract();

    [[maybe_unused]] auto g  = rv_widen.grade();
    [[maybe_unused]] auto vc = extracted.c;

    // Conversion: At<JoinPolicy::JOIN_ALL>::element_type →
    // JoinPolicy at runtime.  Mirrors the WitnessLattice /
    // ConsistencyLattice pattern for downstream diagnostic /
    // serialization paths.
    join_policy::JoinAllTier::element_type e{};
    [[maybe_unused]] JoinPolicy recovered = e;
}

}  // namespace detail::join_policy_lattice_self_test

}  // namespace crucible::algebra::lattices

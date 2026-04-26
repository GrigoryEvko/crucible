#pragma once

// ── crucible::algebra::lattices::HappensBeforeLattice<N, Tag> ───────
//
// The vector-clock partial-order lattice (Lamport 1978, Mattern 1988,
// Fidge 1991).  The canonical algebraic primitive for distributed
// event ordering — captures full causal precedence in a system of N
// participants and DETECTS concurrency (events that are causally
// independent and could have happened in either order).
//
// ── The mathematical object ─────────────────────────────────────────
//
// Carrier: ℕ^N — N-tuples of natural numbers ("vector clocks").
// Order: pointwise ≤ — `a ⊑ b iff ∀i. a[i] ≤ b[i]`.
//
// (ℕ^N, ≤_pointwise) is a DISTRIBUTIVE LATTICE:
//   - bottom = (0, 0, ..., 0)
//   - top    = (∞, ∞, ..., ∞) — synthesized as (UINT64_MAX, ...)
//   - join (a ⊔ b)[i] = max(a[i], b[i])  — causal supremum / merge
//   - meet (a ⊓ b)[i] = min(a[i], b[i])  — latest common ancestor
//   - distributive: a ∧ (b ∨ c) = (a∧b) ∨ (a∧c)  (Birkhoff: every
//     finite distributive lattice embeds in a power set; vector
//     clocks ARE a power set per process — slot p tracks "events
//     from p observed so far" — so distributivity is automatic).
//
// ── The distributed-systems vocabulary on top ───────────────────────
//
// Beyond the Lattice concept's leq/join/meet/bottom/top, vector
// clocks export the canonical operations distributed-systems engineers
// reach for:
//
//   happens_before(a, b)  — a → b: strict causal precedence.
//                            leq(a, b) ∧ a ≠ b.  Equivalently, the
//                            asymmetric part of leq.
//
//   is_concurrent(a, b)   — a ∥ b: causal independence.  !leq(a, b)
//                            ∧ !leq(b, a).  This is THE distinctive
//                            feature of vector clocks vs. Lamport
//                            scalar clocks: scalar clocks are
//                            totally ordered (no concurrency
//                            detection); vector clocks form a
//                            partial order with concurrency.
//
//   comparable(a, b)      — leq either direction.  The complement
//                            of is_concurrent (modulo equality).
//
//   successor_at(v, p)    — increment slot p.  The LOCAL EVENT
//                            operation: when process p observes a
//                            local event, its clock slot bumps.
//                            Monotone (the result is ≥ input under
//                            leq); not a lattice homomorphism.
//
//   causal_merge(local,   — join then bump my slot.  The RECEIVE
//                received,   EVENT operation: when process me
//                me)         receives a message carrying the
//                            sender's clock, me's clock takes the
//                            element-wise max with the received
//                            clock and then increments slot me.
//                            Composite of join + successor_at.
//
// ── N parametricity ─────────────────────────────────────────────────
//
//   N=1:   degenerates to a Lamport scalar clock — totally ordered.
//          Concurrency detection is vacuous (no two distinct vectors
//          are concurrent in N=1).  Verified explicitly in self-test.
//
//   N>1:   genuine partial order — concurrent events exist.
//          E.g. {1,0,...} ∥ {0,1,...} for any N≥2.
//
//   N=0:   forbidden via static_assert; an empty vector clock has no
//          algebraic content.
//
// ── Tag parametricity ───────────────────────────────────────────────
//
// `HappensBeforeLattice<N, Tag>` carries an optional Tag template
// parameter for cross-protocol distinction.  Two clocks with the
// same N but different Tag are DISTINCT TYPES, preventing accidental
// mixing of (e.g.) ReplayClock and KernelOrderClock vectors in code
// that should keep them separate.  Default Tag = void for the common
// case.
//
// ── Storage regime ──────────────────────────────────────────────────
//
// Per the Graded storage-regime taxonomy (memory rule
// feedback_graded_storage_regimes), HappensBeforeLattice<N>::element_
// type is `array<uint64_t, N>` — non-empty, non-T, not derivable
// from arbitrary T.  This is REGIME #4 (genuine 2-field).  Wrappers
// using HappensBeforeLattice<N> as their grade pay N×8 bytes per
// instance; for N=8 (typical small cluster) that's 64 bytes.  This
// is the FIRST lattice in Crucible whose element_type is genuinely
// substantial — validates the substrate handles the residual case
// correctly with real data, not just hypothetically.
//
//   Axiom coverage:
//     TypeSafe — N and Tag are template parameters; cross-N and
//                cross-Tag mismatches are compile errors.
//     DetSafe — every operation is constexpr; no runtime
//               nondeterminism.
//   Runtime cost:
//     leq / join / meet — O(N) loops.  For typical N≤16 the loop
//     vectorizes cleanly under -O3.
//     successor_at / causal_merge — O(N) (constant-time when N is
//     known at compile time).
//
// ── References ──────────────────────────────────────────────────────
//
//   Lamport, L. (1978). "Time, Clocks, and the Ordering of Events
//      in a Distributed System."  CACM 21(7): 558-565.  The
//      foundational scalar-clock paper.
//   Mattern, F. (1988). "Virtual Time and Global States of
//      Distributed Systems."  Workshop on Parallel and Distributed
//      Algorithms.  Vector clocks as a partial-order lattice.
//   Fidge, C. (1991). "Logical Time in Distributed Computing
//      Systems."  IEEE Computer 24(8): 28-33.  Independent
//      formulation; "Mattern-Fidge clocks" is the joint name.
//   Birkhoff, G. (1937). "Rings of Sets."  Duke Math J. 3: 443-454.
//      The representation theorem proving every finite distributive
//      lattice embeds in a power set — vector clocks satisfy this
//      because each slot tracks a power-set element ("events
//      observed").
//
// See ALGEBRA-13 (#458), ALGEBRA-2 (Lattice.h) for the verifier
// helpers; MIGRATE-10 (#470) for TimeOrdered<T> which builds on
// this lattice; 25_04_2026.md §4 for the Cipher::ReplayLog use case.

#include <crucible/algebra/Lattice.h>
#include <crucible/Platform.h>

#include <array>
#include <compare>       // std::partial_ordering for operator<=>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <string_view>
#include <utility>       // std::index_sequence for make_clock helper

namespace crucible::algebra::lattices {

// ── HappensBeforeLattice<N, Tag> ────────────────────────────────────
//
// N participants; optional Tag for cross-protocol type distinction.
// Default Tag = void for the common case (just "vector clock of size
// N", no protocol disambiguation needed).
template <std::size_t N, typename Tag = void>
struct HappensBeforeLattice {
    static_assert(N > 0,
        "HappensBeforeLattice<0> is forbidden — an empty vector clock "
        "has no algebraic content.  Use N >= 1; N=1 reduces to a "
        "Lamport scalar clock.");

    // ── element_type ────────────────────────────────────────────────
    //
    // Wrapped std::array so we can default operator==, expose a
    // partial-order spaceship operator, and provide a domain-specific
    // bounds-checked accessor without polluting the array type itself.
    // Aggregate-initializable: `element_type{{1, 2, 3}}`.
    //
    // `operator<=>` is the CANONICAL C++20 idiom for vector clocks —
    // they are the textbook motivating example in the standard for
    // `std::partial_ordering` (cppreference cites them by name).  The
    // semantic mapping:
    //
    //   leq(a, b) ∧ leq(b, a)  →  partial_ordering::equivalent
    //   leq(a, b) ∧ !leq(b, a) →  partial_ordering::less        (a → b)
    //   !leq(a, b) ∧ leq(b, a) →  partial_ordering::greater     (b → a)
    //   !leq(a, b) ∧ !leq(b, a)→  partial_ordering::unordered   (a ∥ b)
    //
    // Asymmetric vs the lattice's leq: leq is the unidirectional
    // "is a ⊑ b" question; <=> returns the FULL partial order in one
    // pass (less / equivalent / greater / unordered).  Both are
    // available — leq for algebraic-axiom code that already commits
    // to one direction, <=> for client code that wants idiomatic
    // C++20 comparison syntax.  std::partial_ordering propagates
    // through `if (a < b)` / `if (a == b)` correctly: concurrent
    // elements satisfy NEITHER `< 0` nor `> 0` nor `== 0`.
    //
    // We do NOT default `operator<=>` because the std::array carrier
    // would yield `strong_ordering` (lexicographic) — semantically
    // wrong for vector clocks.  The hand-written body computes the
    // pointwise leq partial order, which is the correct semantics.
    //
    // Also: defaulting `<=>` does NOT auto-supply `operator==` for
    // non-strong orderings, so `operator==` remains explicitly
    // `= default`.
    struct element_type {
        std::array<std::uint64_t, N> clock{};

        [[nodiscard]] constexpr bool operator==(element_type const&) const noexcept = default;

        [[nodiscard]] constexpr std::partial_ordering
        operator<=>(element_type const& other) const noexcept
        {
            bool self_leq_other  = true;
            bool other_leq_self  = true;
            for (std::size_t i = 0; i < N; ++i) {
                if (clock[i] > other.clock[i]) self_leq_other = false;
                if (other.clock[i] > clock[i]) other_leq_self = false;
                if (!self_leq_other && !other_leq_self) break;  // short-circuit on definite ∥
            }
            if (self_leq_other && other_leq_self) return std::partial_ordering::equivalent;
            if (self_leq_other)                   return std::partial_ordering::less;
            if (other_leq_self)                   return std::partial_ordering::greater;
            return std::partial_ordering::unordered;
        }

        // Domain-specific accessor — read-only by design.  Mutation
        // goes through the lattice's successor_at / causal_merge
        // helpers to preserve the algebraic discipline (clocks should
        // only advance, never reset).
        //
        // Bounds-checked via contract.  Under `semantic=enforce` the
        // pre fires; under `semantic=ignore` (hot-path TUs) it
        // compiles to `[[assume(p < N)]]` per CLAUDE.md §XII, giving
        // the optimizer the bound for free.  Same discipline as
        // ProductLattice's pointwise accessors.
        [[nodiscard]] constexpr std::uint64_t operator[](std::size_t p) const noexcept
            pre (p < N)
        {
            return clock[p];
        }
    };

    static constexpr std::size_t process_count = N;
    using process_id_type  = std::size_t;
    using clock_value_type = std::uint64_t;
    using tag_type         = Tag;

    // ── Bounded structure ───────────────────────────────────────────
    //
    // bottom = zero vector — the "nothing observed" initial state.
    // top    = max-uint64 vector — the synthesized ceiling.  No
    //          mathematical top exists in (ℕ^N, ≤) because ℕ has no
    //          maximum, but the Lattice concept needs a witness for
    //          the at_top queries; UINT64_MAX is the practical
    //          ceiling (any clock that reached it would already have
    //          overflowed).
    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return element_type{};  // zero-init
    }

    [[nodiscard]] static constexpr element_type top() noexcept {
        element_type result;
        for (std::size_t i = 0; i < N; ++i) {
            result.clock[i] = std::numeric_limits<std::uint64_t>::max();
        }
        return result;
    }

    // ── Lattice ops (pointwise) ─────────────────────────────────────
    //
    // leq is the product order: a ⊑ b iff every slot of a is ≤ the
    // corresponding slot of b.  Short-circuit on first mismatch.
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            if (a.clock[i] > b.clock[i]) return false;
        }
        return true;
    }

    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        element_type r;
        for (std::size_t i = 0; i < N; ++i) {
            r.clock[i] = a.clock[i] >= b.clock[i] ? a.clock[i] : b.clock[i];
        }
        return r;
    }

    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        element_type r;
        for (std::size_t i = 0; i < N; ++i) {
            r.clock[i] = a.clock[i] <= b.clock[i] ? a.clock[i] : b.clock[i];
        }
        return r;
    }

    // ── Distributed-systems vocabulary ──────────────────────────────
    //
    // happens_before(a, b) — strict version of leq.  This IS the
    // canonical "→" relation in the distributed-systems literature:
    // a → b iff every observation captured in a is also captured in
    // b, and b captures at least one more.  Asymmetric, irreflexive,
    // transitive — the strict order on the lattice.
    [[nodiscard]] static constexpr bool happens_before(
        element_type a, element_type b) noexcept
    {
        return leq(a, b) && !(a == b);
    }

    // is_concurrent(a, b) — neither happens-before the other.  THE
    // distinctive feature of vector clocks vs. scalar Lamport clocks:
    // scalar clocks are totally ordered (every pair is comparable);
    // vector clocks form a genuine partial order with antichains.
    // For N=1 this is vacuously false (any two distinct scalars are
    // ordered).
    [[nodiscard]] static constexpr bool is_concurrent(
        element_type a, element_type b) noexcept
    {
        return !leq(a, b) && !leq(b, a);
    }

    // comparable(a, b) — causally ordered in EITHER direction.
    // Complement of is_concurrent (modulo equality, which is in both
    // since equal elements are leq-comparable both ways).
    [[nodiscard]] static constexpr bool comparable(
        element_type a, element_type b) noexcept
    {
        return leq(a, b) || leq(b, a);
    }

    // successor_at(v, p) — increment slot p.  Models a LOCAL EVENT
    // at process p: p observes its own event, bumps its slot.
    // Monotone (`leq(v, successor_at(v, p))` always holds) but NOT a
    // lattice homomorphism (it's a tag-indexed family of monotone
    // functions, one per process).
    //
    // PRECONDITIONS (two clauses, both contract-checked):
    //   1. p < N — bounds-check on the process-id index.  Without
    //      this, `v.clock[p]` is UB for p ≥ N under -fno-exceptions
    //      + -O3.  Under enforce semantic the violation aborts;
    //      under ignore (hot-path TUs) it compiles to a no-op while
    //      still admitting `[[assume]]`-style inference downstream.
    //   2. v[p] != UINT64_MAX — overflow guard.  Silent overflow
    //      would break monotonicity (`leq(v, successor_at(v, p))`
    //      would fail when v.clock[p] wraps to 0).  Catches the
    //      pathological 2^64-event-on-one-process case before
    //      undefined-behavior territory.
    [[nodiscard]] static constexpr element_type successor_at(
        element_type v, std::size_t p) noexcept
        pre (p < N)
        pre (v.clock[p] != std::numeric_limits<std::uint64_t>::max())
    {
        v.clock[p] += 1;
        return v;
    }

    // causal_merge(local, received, me) — the canonical RECEIVE
    // EVENT vector-clock update.  Equivalent to:
    //
    //     successor_at(join(local, received), me)
    //
    // Models: when process `me` receives a message carrying
    // `received` clock, me's local clock becomes the element-wise
    // max with `received` (it has now observed everything received
    // had observed) and then me's slot increments (the receive
    // itself is a new local event).
    //
    // This is the most semantically-loaded operation in the vector-
    // clock vocabulary — it composes lattice join (causal merge of
    // observations) with successor_at (the new local event).  Ships
    // as a named operation rather than requiring callers to
    // hand-compose the pattern.
    //
    // PRECONDITION: me < N — bounds-check on the process-id index
    // for a clean diagnostic at THIS call site.  The overflow guard
    // (max(local[me], received[me]) != UINT64_MAX) is INTENTIONALLY
    // delegated to the inner successor_at — its own pre re-evaluates
    // the same condition on `join(local, received).clock[me]` (which
    // by the pointwise-max axiom equals max(local[me], received[me])).
    // Originally we duplicated the check at this level for an O(1)
    // projection vs. the inner's full-join evaluation, but successor_at
    // already takes the joined element_type by value (the join is
    // computed regardless), so the inner pre fires on the already-
    // computed slot at zero additional cost.  Single source of truth.
    [[nodiscard]] static constexpr element_type causal_merge(
        element_type local, element_type received, std::size_t me) noexcept
        pre (me < N)
    {
        return successor_at(join(local, received), me);
    }

    // ── Diagnostic ──────────────────────────────────────────────────
    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "HappensBeforeLattice";
    }
};

// ── make_clock<HB> factory — variadic vector-clock construction ─────
//
// Ergonomic helper for the verbose `HappensBeforeLattice<N, Tag>::
// element_type{{a, b, c, d}}` double-brace pattern.  Lets callers
// write `make_clock<HB4>(1, 0, 0, 0)` instead.  Exact-arity
// requirement (sizeof...(Slots) == HB::process_count) is a hard
// static_assert — passing too few or too many slots is a compile
// error at the call site.
//
// All slot values are converted to std::uint64_t at the boundary
// (the underlying clock_value_type).  Implicit narrowing is rejected
// by the std::convertible_to constraint.
//
// Usage examples:
//   auto c1 = make_clock<HB4>(1, 0, 0, 0);
//   auto c2 = make_clock<HappensBeforeLattice<8>>(0, 0, 0, 0, 0, 0, 0, 0);
//   auto c3 = make_clock<HBReplay>(2, 1, 0, 0);  // Tag-distinguished
template <typename HB, typename... Slots>
    requires (sizeof...(Slots) == HB::process_count)
          && (std::convertible_to<Slots, typename HB::clock_value_type> && ...)
[[nodiscard]] constexpr typename HB::element_type make_clock(Slots... slots) noexcept {
    return typename HB::element_type{{
        static_cast<typename HB::clock_value_type>(slots)...
    }};
}

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::happens_before_self_test {

// ── N=4 instantiation: small fleet, exhaustive coverage ─────────────
using HB4 = HappensBeforeLattice<4>;

// Concept conformance — positive AND negative.
//
// Positive: HappensBeforeLattice IS a Lattice + bounded both ways
// (we synthesize ⊥ = zero vector and ⊤ = max-uint64 vector).
static_assert(Lattice<HB4>);
static_assert(BoundedLattice<HB4>);
static_assert(BoundedBelowLattice<HB4>);
static_assert(BoundedAboveLattice<HB4>);

// Negative: pin the lattice's CHARACTER by asserting what it is NOT.
// Without these, a future refactor that (e.g.) added a stray top()
// removal could silently demote the lattice and downstream wrappers
// would lose the at_top check that bounded-lattice rollups rely on.
//
//  - Not Unbounded: we DO publish bottom() AND top(), so the
//    Unbounded concept (which requires both to be absent) MUST
//    reject HB4.
//  - Not Semiring: vector clocks have no canonical multiplicative
//    structure (no obvious `mul` that distributes over `join`); the
//    type does not publish add/mul/zero/one and the Semiring
//    concept gate MUST reject it.  Locks in "this is a lattice,
//    not a ring" at the type level so future Stale<> /
//    StalenessSemiring users can't accidentally instantiate
//    Graded over HB4 expecting semiring composition.
static_assert(!UnboundedLattice<HB4>);
static_assert(!Semiring<HB4>);

// Element-type structure.
static_assert(!std::is_empty_v<HB4::element_type>);
static_assert(sizeof(HB4::element_type) == sizeof(std::uint64_t) * 4);
static_assert(HB4::process_count == 4);

// element_type publishes the C++20 partial-order spaceship; the
// concept gate fires at template-substitution time so that downstream
// generic code (e.g. Cipher::ReplayLog comparing two vector-clock
// timestamps idiomatically) can rely on `a <= b` / `a == b` syntax.
static_assert(std::three_way_comparable<HB4::element_type, std::partial_ordering>);

// ── Witnesses ──────────────────────────────────────────────────────

// Bottom and top.
inline constexpr HB4::element_type hb4_bot{};                              // (0,0,0,0)
inline constexpr HB4::element_type hb4_top = HB4::top();                   // (max,max,max,max)

// Strictly ordered chain: a → b → c.
inline constexpr HB4::element_type hb4_a{{1, 0, 0, 0}};
inline constexpr HB4::element_type hb4_b{{1, 1, 0, 0}};
inline constexpr HB4::element_type hb4_c{{2, 2, 1, 0}};

// Concurrent pair: x ∥ y (neither leq the other).
inline constexpr HB4::element_type hb4_x{{2, 0, 1, 0}};
inline constexpr HB4::element_type hb4_y{{0, 2, 0, 1}};

// ── Lattice axioms (bounded rollup at multiple witness triples) ────
static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_bot, hb4_bot, hb4_bot));
static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_bot, hb4_a, hb4_top));
static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_a, hb4_b, hb4_c));
static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_x, hb4_y, hb4_top));
static_assert(verify_bounded_lattice_axioms_at<HB4>(hb4_a, hb4_x, hb4_y));

// ── Distributive lattice axiom (Birkhoff witness) ──────────────────
//
// Vector-clock product order IS distributive — verified at multiple
// triples covering ordered, concurrent, and mixed cases.  Distinct
// from the bounded rollup; the verifier here is the new addition to
// Lattice.h shipped alongside this lattice.
static_assert(verify_distributive_lattice<HB4>(hb4_a, hb4_b, hb4_c));
static_assert(verify_distributive_lattice<HB4>(hb4_x, hb4_y, hb4_a));
static_assert(verify_distributive_lattice<HB4>(hb4_bot, hb4_top, hb4_x));
static_assert(verify_distributive_lattice<HB4>(hb4_a, hb4_a, hb4_y));

// ── Order discipline ──────────────────────────────────────────────

// Strictly ordered chain: leq holds, happens_before holds, NOT concurrent.
static_assert( HB4::leq(hb4_a, hb4_b));
static_assert( HB4::leq(hb4_b, hb4_c));
static_assert( HB4::leq(hb4_a, hb4_c));      // transitive
static_assert( HB4::happens_before(hb4_a, hb4_b));
static_assert( HB4::happens_before(hb4_b, hb4_c));
static_assert( HB4::happens_before(hb4_a, hb4_c));
static_assert(!HB4::is_concurrent(hb4_a, hb4_b));
static_assert(!HB4::is_concurrent(hb4_a, hb4_c));
static_assert( HB4::comparable(hb4_a, hb4_b));
static_assert( HB4::comparable(hb4_a, hb4_c));

// Reverse direction: NOT leq, NOT happens_before.
static_assert(!HB4::leq(hb4_b, hb4_a));
static_assert(!HB4::happens_before(hb4_b, hb4_a));

// Reflexive: any vector is leq itself, but NOT happens_before itself.
static_assert( HB4::leq(hb4_a, hb4_a));
static_assert(!HB4::happens_before(hb4_a, hb4_a));   // strict order
static_assert(!HB4::is_concurrent(hb4_a, hb4_a));    // identical, not concurrent

// ── Direct bottom / top order witnesses ──────────────────────────
//
// The bounded-lattice axiom rollups (verify_bounded_lattice_axioms_at)
// already prove `join(bot, x) == x` and `meet(top, x) == x`, but those
// are ALGEBRAIC IDENTITIES.  The corresponding ORDER witnesses
// (`leq(bot, x)` for every x, `leq(x, top)` for every x) are the same
// fact viewed through the partial-order lens.  Asserting both lenses
// independently catches a class of regression where a future refactor
// might make join/meet still pass identity laws while subtly breaking
// the leq witness (e.g., by swapping the order of arguments inside
// leq).  Cheap to assert, hard to derive in your head when debugging.
static_assert( HB4::leq(hb4_bot, hb4_a));
static_assert( HB4::leq(hb4_bot, hb4_b));
static_assert( HB4::leq(hb4_bot, hb4_c));
static_assert( HB4::leq(hb4_bot, hb4_x));
static_assert( HB4::leq(hb4_bot, hb4_y));
static_assert( HB4::leq(hb4_bot, hb4_top));
static_assert( HB4::leq(hb4_a,   hb4_top));
static_assert( HB4::leq(hb4_b,   hb4_top));
static_assert( HB4::leq(hb4_c,   hb4_top));
static_assert( HB4::leq(hb4_x,   hb4_top));
static_assert( HB4::leq(hb4_y,   hb4_top));

// Pin top()/bottom() values explicitly — guards against a regression
// where (e.g.) top() returns the zero vector by accident.  The
// rollups would still pass (because verify_top_identity tests
// algebraically) but downstream callers expecting top to be the
// genuine ceiling would break silently.
static_assert(hb4_bot == HB4::element_type{{0, 0, 0, 0}});
static_assert(hb4_top == HB4::element_type{
    std::numeric_limits<std::uint64_t>::max(),
    std::numeric_limits<std::uint64_t>::max(),
    std::numeric_limits<std::uint64_t>::max(),
    std::numeric_limits<std::uint64_t>::max()
});

// ── operator<=> result coverage ──────────────────────────────────
//
// The four partial_ordering inhabitants must each be reachable.  If
// any reduces to a different value, the spaceship implementation
// has drifted from the lattice's leq.  Tests all four cells
// explicitly:
//
//   bot  <=> bot   == equivalent
//   a    <=> b     == less          (a → b)
//   b    <=> a     == greater       (b → a)
//   x    <=> y     == unordered     (x ∥ y)
static_assert((hb4_bot <=> hb4_bot) == std::partial_ordering::equivalent);
static_assert((hb4_a   <=> hb4_a  ) == std::partial_ordering::equivalent);
static_assert((hb4_a   <=> hb4_b  ) == std::partial_ordering::less);
static_assert((hb4_b   <=> hb4_a  ) == std::partial_ordering::greater);
static_assert((hb4_a   <=> hb4_c  ) == std::partial_ordering::less);     // transitive
static_assert((hb4_x   <=> hb4_y  ) == std::partial_ordering::unordered);
static_assert((hb4_y   <=> hb4_x  ) == std::partial_ordering::unordered); // symmetric ∥
static_assert((hb4_bot <=> hb4_top) == std::partial_ordering::less);     // ⊥ < ⊤
static_assert((hb4_top <=> hb4_bot) == std::partial_ordering::greater);

// And the idiomatic C++20 client syntax flows correctly through
// partial_ordering's bool conversions: concurrent elements satisfy
// NEITHER < nor > nor == — defends against a recurring beginner
// mistake "well a is not greater than b, so a <= b."
static_assert(  hb4_a   <  hb4_b);
static_assert(  hb4_a   <= hb4_b);
static_assert(  hb4_b   >  hb4_a);
static_assert(  hb4_a   == hb4_a);
static_assert(!(hb4_x   <  hb4_y));   // concurrent: not less
static_assert(!(hb4_x   >  hb4_y));   // concurrent: not greater
static_assert(!(hb4_x   == hb4_y));   // concurrent: not equal
static_assert(!(hb4_x   <= hb4_y));   // concurrent: not <=
static_assert(!(hb4_y   <= hb4_x));   // concurrent: not <= other way

// ── Chain-plus-concurrent cross-witnesses ────────────────────────
//
// The witnesses above are arranged in two disjoint groups (chain a→b→c
// and concurrent pair x∥y).  Crucially the chain and the concurrent
// pair INTERACT — slot-0 of x is greater than slot-0 of all of a,b,c
// (x.clock = {2,0,1,0} vs a={1,0,0,0}).  Pin those interactions
// explicitly so a refactor that re-wires which witness is which
// doesn't silently break downstream tests.
//
// a's clock is {1,0,0,0}; x's is {2,0,1,0}.  x ⊒ a pointwise
// (x.clock[0]=2>1, x.clock[2]=1>0, others equal).  So a → x.
static_assert( HB4::leq(hb4_a, hb4_x));
static_assert( HB4::happens_before(hb4_a, hb4_x));
static_assert(!HB4::is_concurrent(hb4_a, hb4_x));

// y's clock is {0,2,0,1}.  a={1,0,0,0}.  Mismatched: a > y in slot 0,
// y > a in slot 1.  Concurrent.
static_assert( HB4::is_concurrent(hb4_a, hb4_y));
static_assert(!HB4::leq(hb4_a, hb4_y));
static_assert(!HB4::leq(hb4_y, hb4_a));

// b={1,1,0,0} vs y={0,2,0,1}: y > b in slots 1 and 3, b > y in
// slot 0.  Concurrent.
static_assert( HB4::is_concurrent(hb4_b, hb4_y));

// x={2,0,1,0} vs c={2,2,1,0}: c ⊒ x pointwise.  So x → c.  This is
// the key chain-plus-concurrent fact — x is concurrent with y but
// strictly precedes c.  The vector-clock partial order has antichains
// embedded in chains, validating the partial-order shape.
static_assert( HB4::leq(hb4_x, hb4_c));
static_assert( HB4::happens_before(hb4_x, hb4_c));
static_assert(!HB4::is_concurrent(hb4_x, hb4_c));

// ── Concurrency — THE distinctive vector-clock feature ──────────────
//
// {2,0,1,0} ∥ {0,2,0,1}: x has more in slots 0 and 2, y has more in
// slots 1 and 3.  Neither is leq the other; both are leq the join.
static_assert(!HB4::leq(hb4_x, hb4_y));
static_assert(!HB4::leq(hb4_y, hb4_x));
static_assert( HB4::is_concurrent(hb4_x, hb4_y));
static_assert( HB4::is_concurrent(hb4_y, hb4_x));    // symmetric
static_assert(!HB4::happens_before(hb4_x, hb4_y));
static_assert(!HB4::happens_before(hb4_y, hb4_x));
static_assert(!HB4::comparable(hb4_x, hb4_y));

// Both x and y are leq their join (the "causal merge" point).
static_assert( HB4::leq(hb4_x, HB4::join(hb4_x, hb4_y)));
static_assert( HB4::leq(hb4_y, HB4::join(hb4_x, hb4_y)));
static_assert( HB4::join(hb4_x, hb4_y) == HB4::element_type{{2, 2, 1, 1}});

// Their meet is the latest common ancestor.
static_assert( HB4::meet(hb4_x, hb4_y) == HB4::element_type{{0, 0, 0, 0}});

// ── successor_at: monotone, advances exactly one slot ───────────────
inline constexpr HB4::element_type hb4_a_after_p0 = HB4::successor_at(hb4_a, 0);
static_assert(hb4_a_after_p0 == HB4::element_type{{2, 0, 0, 0}});
static_assert( HB4::leq(hb4_a, hb4_a_after_p0));
static_assert( HB4::happens_before(hb4_a, hb4_a_after_p0));
static_assert(!HB4::leq(hb4_a_after_p0, hb4_a));

// successor_at on a different slot — advances p, leaves others.
inline constexpr HB4::element_type hb4_a_after_p2 = HB4::successor_at(hb4_a, 2);
static_assert(hb4_a_after_p2 == HB4::element_type{{1, 0, 1, 0}});
static_assert( HB4::leq(hb4_a, hb4_a_after_p2));

// Two successors on different slots are CONCURRENT — local events
// at distinct processes with no causal link are independent.
static_assert( HB4::is_concurrent(hb4_a_after_p0, hb4_a_after_p2));

// ── causal_merge: the receive-event composite ───────────────────────
//
// Process 0 receives a message from a sender whose clock was hb4_y
// = {0, 2, 0, 1}.  Local clock was hb4_a = {1, 0, 0, 0}.  After
// causal_merge:
//   - join({1,0,0,0}, {0,2,0,1}) = {1,2,0,1}
//   - then bump slot 0          = {2,2,0,1}
inline constexpr HB4::element_type hb4_received_y = HB4::causal_merge(hb4_a, hb4_y, 0);
static_assert(hb4_received_y == HB4::element_type{{2, 2, 0, 1}});

// Post-merge clock observes BOTH the local prior AND the sender's history.
static_assert( HB4::leq(hb4_a, hb4_received_y));    // saw local prior
static_assert( HB4::leq(hb4_y, hb4_received_y));    // saw sender's

// The receive event itself is a NEW event — strictly after both
// inputs in the causal order.
static_assert( HB4::happens_before(hb4_a, hb4_received_y));
static_assert( HB4::happens_before(hb4_y, hb4_received_y));

// ── N=1 degenerate case (Lamport scalar clock) ──────────────────────
//
// With a single participant the lattice is totally ordered — no two
// distinct vectors are concurrent.  Worth exercising explicitly
// because the N=1 case is the boundary at which the vector-clock
// abstraction collapses to its scalar predecessor — if the
// implementation accidentally embedded a "must have ≥ 2 slots"
// assumption (e.g., in a loop bound that started at 1, or in an
// is_concurrent helper that required two distinct slots), N=1 would
// expose it while N=4 would mask it.
using HB1 = HappensBeforeLattice<1>;
inline constexpr HB1::element_type hb1_zero{{0}};
inline constexpr HB1::element_type hb1_one {{1}};
inline constexpr HB1::element_type hb1_two {{2}};

static_assert( HB1::leq(hb1_zero, hb1_one));
static_assert( HB1::leq(hb1_one, hb1_two));
static_assert( HB1::happens_before(hb1_zero, hb1_two));
static_assert(!HB1::is_concurrent(hb1_zero, hb1_one));
static_assert(!HB1::is_concurrent(hb1_one, hb1_two));
static_assert( HB1::comparable(hb1_zero, hb1_two));   // total order

// Bounded-lattice axioms still hold for N=1 (degenerate case is a
// valid lattice — the chain {0, 1, 2} is a sublattice of (ℕ, ≤)).
static_assert(verify_bounded_lattice_axioms_at<HB1>(hb1_zero, hb1_one, hb1_two));
static_assert(verify_distributive_lattice<HB1>(hb1_zero, hb1_one, hb1_two));

// ── N=1 successor_at and causal_merge coverage ──────────────────────
//
// The 4-slot witnesses above exercise these via slot p=0 (the local
// process), but exercising them explicitly at N=1 confirms the
// degenerate path doesn't accidentally short-circuit on the array
// length.  Process 0 is the only valid p for N=1.
inline constexpr HB1::element_type hb1_zero_after_succ = HB1::successor_at(hb1_zero, 0);
static_assert(hb1_zero_after_succ == HB1::element_type{{1}});
static_assert(hb1_zero_after_succ == hb1_one);
static_assert( HB1::happens_before(hb1_zero, hb1_zero_after_succ));

// causal_merge in the degenerate N=1 case reduces to the receive
// event for a Lamport clock: max(local, received) + 1.
inline constexpr HB1::element_type hb1_merged = HB1::causal_merge(hb1_one, hb1_two, 0);
static_assert(hb1_merged == HB1::element_type{{3}});  // max(1, 2) + 1 = 3
static_assert( HB1::leq(hb1_one, hb1_merged));
static_assert( HB1::leq(hb1_two, hb1_merged));
static_assert( HB1::happens_before(hb1_one, hb1_merged));
static_assert( HB1::happens_before(hb1_two, hb1_merged));

// operator<=> on N=1 is total: every two distinct elements are
// strictly comparable (no `unordered` reachable).  The pre-condition
// for `unordered` requires a slot where each side dominates — N=1
// has only one slot, so dominance is total.
static_assert((hb1_zero <=> hb1_one) == std::partial_ordering::less);
static_assert((hb1_two  <=> hb1_one) == std::partial_ordering::greater);
static_assert((hb1_one  <=> hb1_one) == std::partial_ordering::equivalent);

// ── Cross-Tag distinction ──────────────────────────────────────────
//
// Two HappensBeforeLattice instantiations with the same N but
// different Tag are DIFFERENT TYPES.  This prevents accidental mixing
// of vectors from different protocols (e.g., ReplayClock vs
// KernelOrderClock should never be join-able even though both might
// be N=4).
struct ReplayClockTag {};
struct KernelOrderClockTag {};

using HBReplay   = HappensBeforeLattice<4, ReplayClockTag>;
using HBKernel   = HappensBeforeLattice<4, KernelOrderClockTag>;
using HBDefault  = HappensBeforeLattice<4>;  // Tag=void

static_assert(!std::is_same_v<HBReplay, HBKernel>);
static_assert(!std::is_same_v<HBReplay, HBDefault>);
static_assert(!std::is_same_v<HBKernel, HBDefault>);
static_assert(!std::is_same_v<typename HBReplay::element_type,
                              typename HBKernel::element_type>);

// But the underlying storage shape is identical.
static_assert(sizeof(HBReplay::element_type) == sizeof(HBKernel::element_type));
static_assert(sizeof(HBReplay::element_type) == sizeof(HBDefault::element_type));

// ── Diagnostic name ────────────────────────────────────────────────
static_assert(HB4::name()      == "HappensBeforeLattice");
static_assert(HBReplay::name() == "HappensBeforeLattice");

// ── make_clock factory: variadic ergonomic construction ────────────
//
// Pins the make_clock helper at every relevant arity (N=1, N=4) and
// across Tag-distinguished instantiations (HBReplay).  The variadic
// helper shaves the double-brace boilerplate off every production
// caller — ALGEBRA-15-tier audit improvement.
static_assert(make_clock<HB4>(1, 0, 0, 0) == HB4::element_type{{1, 0, 0, 0}});
static_assert(make_clock<HB4>(0, 0, 0, 0) == HB4::bottom());
static_assert(make_clock<HB4>(2, 2, 1, 0) == hb4_c);
static_assert(make_clock<HB1>(7)          == HB1::element_type{{7}});

// Cross-Tag distinction propagates through make_clock — same slot
// values, different Tag → structurally different element_type.
static_assert(std::is_same_v<
    decltype(make_clock<HBReplay>(1, 0, 0, 0)),
    HBReplay::element_type>);
static_assert(!std::is_same_v<
    decltype(make_clock<HBReplay>(1, 0, 0, 0)),
    decltype(make_clock<HBKernel>(1, 0, 0, 0))>);

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: every
// algebra/lattices/ header MUST exercise lattice ops with non-constant
// arguments to catch consteval/SFINAE traps that pure static_assert
// tests miss.  HappensBeforeLattice's array-based element_type makes
// it more sensitive to constexpr-vs-runtime divergences than empty
// or scalar element types.
inline void runtime_smoke_test() {
    // Non-constant inputs.
    HB4::element_type a{{1, 0, 0, 0}};
    HB4::element_type b{{1, 1, 0, 0}};
    HB4::element_type x{{2, 0, 1, 0}};
    HB4::element_type y{{0, 2, 0, 1}};

    // Lattice ops at runtime.
    [[maybe_unused]] bool                l_ab = HB4::leq(a, b);
    [[maybe_unused]] HB4::element_type   j_ab = HB4::join(a, b);
    [[maybe_unused]] HB4::element_type   m_xy = HB4::meet(x, y);

    // Bounded ops at runtime.
    [[maybe_unused]] HB4::element_type   bot  = HB4::bottom();
    [[maybe_unused]] HB4::element_type   top  = HB4::top();

    // Distributed-systems vocabulary at runtime.
    [[maybe_unused]] bool                hb_ab    = HB4::happens_before(a, b);
    [[maybe_unused]] bool                conc_xy  = HB4::is_concurrent(x, y);
    [[maybe_unused]] bool                comp_ab  = HB4::comparable(a, b);

    // Successor at process 0.  Exercises the bounds-check pre and
    // the overflow-guard pre at runtime under the test target's
    // enforce semantic.
    [[maybe_unused]] HB4::element_type   succ_a   = HB4::successor_at(a, 0);

    // Causal merge: receive y at process 0.  Exercises the optimized
    // O(1) std::max projection in the overflow pre.
    [[maybe_unused]] HB4::element_type   merged   = HB4::causal_merge(a, y, 0);

    // operator[] with bounds-checked pre — exercises the contract at
    // runtime.  Every slot is reachable; access slot 3 to exercise
    // the high end of the bound.
    [[maybe_unused]] std::uint64_t       slot0    = a[0];
    [[maybe_unused]] std::uint64_t       slot3    = a[3];

    // operator<=> at runtime — exercises the partial-order spaceship
    // through both ordered (a vs b) and concurrent (x vs y) paths.
    // Capture as `std::partial_ordering` to confirm the return type
    // is compile-time-stable; the bool projection then exercises the
    // partial_ordering → bool conversion the client syntax relies on.
    [[maybe_unused]] std::partial_ordering ord_ab = a <=> b;
    [[maybe_unused]] std::partial_ordering ord_xy = x <=> y;
    [[maybe_unused]] bool                  lt_ab  = (a < b);
    [[maybe_unused]] bool                  ne_xy  = !(x == y);

    // N=1 degenerate path — same operator coverage as N=4 to confirm
    // both shapes type-check at runtime.
    HB1::element_type s0{{0}};
    HB1::element_type s1{{1}};
    [[maybe_unused]] bool                 l_s     = HB1::leq(s0, s1);
    [[maybe_unused]] HB1::element_type    next_s  = HB1::successor_at(s0, 0);
    [[maybe_unused]] HB1::element_type    merged1 = HB1::causal_merge(s0, s1, 0);
    [[maybe_unused]] std::partial_ordering ord_s1 = s0 <=> s1;
}

}  // namespace detail::happens_before_self_test

}  // namespace crucible::algebra::lattices

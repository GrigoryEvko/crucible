#pragma once

// ── crucible::safety::Stale<T> ──────────────────────────────────────
//
// MIGRATE-8: a value of type T paired with a staleness grade τ ∈ ℕ ∪ ∞.
// Built directly on the algebra/StalenessSemiring.h ALGEBRA-11
// semiring; second production wrapper (after TimeOrdered) exercising
// Graded's regime-#4 storage with a non-trivial element_type — but at
// half the per-instance cost (8 bytes vs N×8 for vector clocks).
//
//   Substrate:  Graded<ModalityKind::Absolute,
//                       StalenessSemiring,
//                       T>
//
//   Use case:   §8 ASGD admission gate from 25_04_2026.md.  The
//               criterion `η · λ_max(H) · τ ≤ critical` cross-
//               corroborated across three independent fields
//               (Mishchenko-Bach-Even-Woodworth convergence theory,
//               Yu-Chen-Poor SDDE framework, thermo-survey SDDE
//               coupling) controls async-SGD stability.  `τ` is the
//               staleness grade carried by Stale<GradientShard> at
//               admission; the gate fires on the combined grade
//               after composing a chain of asynchronous updates.
//
//   Axiom coverage:
//     TypeSafe — strong wrapper over StalenessSemiring's
//                element_type; cross-modality / cross-grade mismatches
//                are compile errors via the underlying Graded gates.
//     DetSafe — every operation is constexpr where the underlying
//                semiring is constexpr.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//   Runtime cost:
//     sizeof(Stale<T>) == sizeof(T) + sizeof(uint64_t) + alignment
//     padding.  For T = double, 16 bytes (8 + 8, no padding).  For
//     T = void*, 16 bytes on 64-bit.  Half the cost of TimeOrdered
//     <T, 1> (which stores a 1-element std::array but pays the same).
//
// ── Lattice vs semiring vocabulary on the wrapper ───────────────────
//
// StalenessSemiring is the ONE shipped lattice that's ALSO a semiring,
// because staleness has TWO natural composition operations:
//
//   - LATTICE join (= max):  pessimistic worst-case watermark of two
//                            independent staleness measurements.
//                            "Across these N replicas, the worst
//                            staleness observed is τ_max."  Used by
//                            Augur's drift detector.
//
//   - SEMIRING multiplication (= saturating add): staleness PROPAGATES
//                            through dependency chains.  "Stage A
//                            produced output at τ=3; stage B consumed
//                            it AND added τ=2 of its own; stage B's
//                            output is at τ=5."  This IS the §8
//                            admission-gate's "compose" semantic.
//
// Stale<T>'s wrapper exposes BOTH as named methods:
//
//   .combine_max(other)  — lattice join, pessimistic.
//   .compose_add(other)  — semiring mul (saturating), additive.
//
// Lattice join is what Graded's bare `compose` does (it uses L::join
// uniformly across all lattice grades).  Stale<T>::compose_add adds
// the second view via the underlying semiring's `mul` operation.
// Both are documented at the call site rather than overloading
// `compose` ambiguously — distributed-systems literature distinguishes
// "watermark fold" from "chain accumulation" and Stale's API
// preserves that distinction.
//
// ── Why not move-only ───────────────────────────────────────────────
//
// Same rationale as TimeOrdered (MIGRATE-10): the Absolute modality
// over a non-Linearity grade encodes a value's STALENESS POSITION
// (a property of identity, not ownership).  Two Stale events with
// identical (value, τ) ARE the same event; copying represents replay,
// not duplication of ownership.
//
// See ALGEBRA-11 (#456, StalenessSemiring.h) for the underlying
// substrate; 25_04_2026.md §8 for the ASGD admission-gate use case;
// MIGRATE-10 (TimeOrdered.h) for the wrapper-pattern this mirrors.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/StalenessSemiring.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename T>
class [[nodiscard]] Stale {
    using semiring_type = ::crucible::algebra::lattices::StalenessSemiring;
    using graded_type   = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        semiring_type,
        T>;

    graded_type impl_;

public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type    = T;
    using semiring_t    = semiring_type;
    using staleness_t   = typename semiring_type::element_type;

    // ── Construction ────────────────────────────────────────────────
    //
    // Default: value at T{}, staleness at fresh (τ=0).  The most
    // permissive starting position — the value is freshly produced
    // with no asynchronous lag accumulated.
    constexpr Stale() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, semiring_type::bottom()} {}

    // Explicit construction with both value and staleness.  The most
    // common production pattern — caller has a value freshly produced
    // at a known staleness and binds them.
    constexpr Stale(T value, staleness_t tau) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), tau} {}

    // In-place construction of T inside Stale, paired with a τ.
    // Mirrors the std::in_place_t pattern from Linear/TimeOrdered.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr Stale(std::in_place_t, staleness_t tau, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), tau} {}

    // Convenience factory: value at fresh.  The most common
    // construction site — a freshly-produced value enters the
    // pipeline at τ=0.
    [[nodiscard]] static constexpr Stale fresh(T value)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Stale{std::move(value), semiring_type::bottom()};
    }

    // Convenience factory: value at infinite staleness.  Used for
    // sentinel events where the value's staleness is genuinely
    // unbounded (e.g. a checkpoint loaded from cold storage with
    // unknown lag relative to the current step).
    [[nodiscard]] static constexpr Stale at_infinity(T value)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Stale{std::move(value), semiring_type::top()};
    }

    // Convenience factory: value at finite τ=n.  pre via the
    // staleness::at constructor — rejects the ∞ sentinel value.
    [[nodiscard]] static constexpr Stale at(T value, std::uint64_t n)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Stale{std::move(value),
                     ::crucible::algebra::lattices::staleness::at(n)};
    }

    // Defaulted copy/move/destroy — Stale is COPYABLE (vs Linear<T>
    // which deletes copy).  Same rationale as TimeOrdered.
    constexpr Stale(const Stale&)            = default;
    constexpr Stale(Stale&&)                 = default;
    constexpr Stale& operator=(const Stale&) = default;
    constexpr Stale& operator=(Stale&&)      = default;
    ~Stale()                                 = default;

    // Equality: compares BOTH value and staleness, mirroring
    // TimeOrdered's discipline.  Two events at the same staleness with
    // different payloads are unequal; two events at different
    // staleness with the same payload are also unequal.
    [[nodiscard]] friend constexpr bool operator==(
        Stale const& a, Stale const& b) noexcept(
        noexcept(a.peek() == b.peek())
        && noexcept(a.staleness() == b.staleness()))
    {
        return a.peek() == b.peek() && a.staleness() == b.staleness();
    }

    // ── Diagnostic name (forwarded from Graded substrate) ──────────
    //
    // Returns T's display string via reflection (P2996R13).  Used for
    // debug printing ("what is this Stale wrapping?") without
    // requiring the caller to dereference and introspect.  Pairs with
    // protocol_name / lattice_name patterns in the broader Graded
    // substrate's diagnostic surface.
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }

    // ── Swap (forwarded from Graded substrate) ─────────────────────
    //
    // Standard exchange — swaps both value AND staleness grade between
    // two Stale instances.  Forwards to Graded::swap which is gated on
    // AbsoluteModality — sound here because the staleness grade is
    // orthogonal to T's identity (swapping the values doesn't violate
    // the per-event staleness bookkeeping; it just exchanges which
    // event is at which storage cell).
    //
    // The friend overload enables ADL-style `swap(a, b)` calls that
    // generic algorithms route through.
    constexpr void swap(Stale& other)
        noexcept(std::is_nothrow_swappable_v<T>
                 && std::is_nothrow_swappable_v<staleness_t>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(Stale& a, Stale& b)
        noexcept(std::is_nothrow_swappable_v<T>
                 && std::is_nothrow_swappable_v<staleness_t>)
    {
        a.swap(b);
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

    [[nodiscard]] constexpr staleness_t staleness() const noexcept {
        return impl_.grade();
    }

    // Diagnostic predicates forwarded from staleness_t — read directly
    // for the §8 admission-gate's `if (s.is_finite()) admit(...)`
    // pattern without the caller needing to extract the grade.
    [[nodiscard]] constexpr bool is_fresh() const noexcept {
        return staleness() == semiring_type::bottom();
    }
    [[nodiscard]] constexpr bool is_finite() const noexcept {
        return staleness().is_finite();
    }
    [[nodiscard]] constexpr bool is_infinite() const noexcept {
        return staleness().is_infinite();
    }

    // ── Mutable T access ────────────────────────────────────────────
    //
    // peek_mut forwards to Graded::peek_mut, gated on AbsoluteModality
    // — sound here because the staleness grade is orthogonal to T's
    // bytes (τ records WHEN the value was produced relative to the
    // current step, not WHAT it contains; mutating the value doesn't
    // violate the staleness measurement).
    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // ── Order discipline (chain order on staleness) ─────────────────
    //
    // Forwards to the semiring's lattice ops.  The chain order is
    // total: smaller τ = fresher = bottom-ier.  No is_concurrent
    // method (unlike TimeOrdered) because chain orders have no
    // concurrent pairs.

    // a.fresher_than(b) iff a's staleness < b's (strict).
    [[nodiscard]] constexpr bool fresher_than(Stale const& other) const noexcept {
        return semiring_type::leq(staleness(), other.staleness())
               && !(staleness() == other.staleness());
    }

    // a.no_staler_than(b) iff a's staleness ≤ b's (non-strict).
    [[nodiscard]] constexpr bool no_staler_than(Stale const& other) const noexcept {
        return semiring_type::leq(staleness(), other.staleness());
    }

    // ── Lattice join: pessimistic worst-case staleness ──────────────
    //
    // Combines two Stale views into ONE Stale whose staleness is the
    // MAX of both.  Used for watermarking — "across these N replicas,
    // the value with the worst observed staleness."  Payload is
    // taken from `*this`; the other only contributes its staleness.
    //
    // Two overloads (const& / &&) mirror TimeOrdered's advance_at
    // pattern — copy or move T as appropriate.
    [[nodiscard]] constexpr Stale combine_max(Stale const& other) const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Stale{this->peek(),
                     semiring_type::join(this->staleness(), other.staleness())};
    }

    [[nodiscard]] constexpr Stale combine_max(Stale const& other) &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        staleness_t maxed = semiring_type::join(this->staleness(), other.staleness());
        return Stale{std::move(impl_).consume(), maxed};
    }

    // ── Lattice meet: optimistic freshest-of-N replicas ─────────────
    //
    // Dual of combine_max: combines two Stale views into ONE Stale
    // whose staleness is the MIN of both.  Used for selection — "from
    // these N replicas of this value, take the one with smallest
    // staleness."  Payload taken from `*this`; the other only
    // contributes its staleness as the freshness comparand.
    //
    // Differs from combine_max in the lattice direction (meet vs
    // join) but preserves the same payload-from-*this convention,
    // mirroring the const&/&& overload pattern.  Models the §8 ASGD
    // admission gate's "pick the freshest available gradient
    // estimate" pattern when the aggregator has multiple replicas
    // to choose from.
    [[nodiscard]] constexpr Stale combine_min(Stale const& other) const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Stale{this->peek(),
                     semiring_type::meet(this->staleness(), other.staleness())};
    }

    [[nodiscard]] constexpr Stale combine_min(Stale const& other) &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        staleness_t minned = semiring_type::meet(this->staleness(), other.staleness());
        return Stale{std::move(impl_).consume(), minned};
    }

    // ── Semiring multiplication: chain-accumulated staleness ────────
    //
    // Combines two Stale views via SATURATING ADDITION of stalenesses.
    // Models the §8 chain-propagation semantic: "stage A produced at
    // τ=a; stage B consumed it AND added τ=b of its own work; the
    // composite stage's output is at τ=a+b (saturating to ∞ on
    // overflow)."
    //
    // Payload taken from `*this` (the consuming stage's output).
    // The other Stale contributes only its staleness as the
    // additive offset.
    //
    // ∞ absorbs (∞+x = ∞ via saturation) — semantically correct: a
    // chain that includes ANY ∞-stale stage has unbounded composite
    // staleness.
    [[nodiscard]] constexpr Stale compose_add(Stale const& other) const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Stale{this->peek(),
                     semiring_type::mul(this->staleness(), other.staleness())};
    }

    [[nodiscard]] constexpr Stale compose_add(Stale const& other) &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        staleness_t composed = semiring_type::mul(this->staleness(), other.staleness());
        return Stale{std::move(impl_).consume(), composed};
    }

    // ── Staleness advancement (returns NEW Stale) ───────────────────
    //
    // Bumps the staleness by `delta` ticks.  The §8 admission gate's
    // typical pattern: as a value sits in the queue, its staleness
    // increases; advance_by(1) per outer-step tick.
    //
    // Implemented via semiring_type::mul (tropical multiplication =
    // saturating add).  Three semantically-load-bearing edge cases:
    //   - delta = 0 is the multiplicative identity: advance_by(0)
    //     returns a structurally-equal Stale (τ unchanged).
    //   - this->is_infinite() is absorbing: ∞ + delta = ∞ for any
    //     delta, so advancing an already-infinite Stale stays infinite.
    //   - Overflow (this->staleness().value + delta > UINT64_MAX)
    //     saturates to ∞ via the underlying StalenessSemiring::mul's
    //     crucible::sat::add_sat polyfill.  Semantically correct: a
    //     staleness that exceeds uint64_t IS unbounded for any
    //     practical purpose.
    [[nodiscard]] constexpr Stale advance_by(std::uint64_t delta) const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Stale{this->peek(),
                     semiring_type::mul(this->staleness(),
                                        ::crucible::algebra::lattices::staleness::at(delta))};
    }

    [[nodiscard]] constexpr Stale advance_by(std::uint64_t delta) &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        staleness_t advanced = semiring_type::mul(
            this->staleness(),
            ::crucible::algebra::lattices::staleness::at(delta));
        return Stale{std::move(impl_).consume(), advanced};
    }
};

// ── CTAD: deduce T from the value argument ──────────────────────────
template <typename T>
Stale(T, ::crucible::algebra::lattices::StalenessSemiring::element_type)
    -> Stale<T>;

// ── Layout invariants ───────────────────────────────────────────────
//
// sizeof(Stale<T>) MUST be sizeof(T) + sizeof(uint64_t) + alignment
// padding.  Verified at three witnesses covering the typical T sizes
// production callers use (event payload-hash slot, pointer payload,
// 8-byte numeric).
namespace detail::stale_layout {

using S_int64 = Stale<std::int64_t>;
using S_voidp = Stale<void*>;
using S_dbl   = Stale<double>;

// Each carries 8 bytes of staleness; T is 8 bytes; total ≤ 16 bytes
// (no alignment padding needed — both fields are 8-byte aligned).
static_assert(sizeof(S_int64) <= sizeof(std::int64_t) + sizeof(std::uint64_t) + 8,
    "Stale<int64> exceeded sizeof(int64) + 8 + 8 bytes — the regime-#4 "
    "storage discipline drifted.  Investigate StalenessSemiring::"
    "element_type alignment OR Graded's grade_ field placement.");
static_assert(sizeof(S_voidp) <= sizeof(void*)        + sizeof(std::uint64_t) + 8);
static_assert(sizeof(S_dbl)   <= sizeof(double)       + sizeof(std::uint64_t) + 8);

}  // namespace detail::stale_layout

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::stale_self_test {

using SS  = ::crucible::algebra::lattices::StalenessSemiring;
using S_i = Stale<int>;

// Construction paths.
inline constexpr S_i s_default{};
static_assert(s_default.staleness() == SS::bottom());
static_assert(s_default.peek()      == 0);
static_assert(s_default.is_fresh());
static_assert(s_default.is_finite());

inline constexpr S_i s_fresh = S_i::fresh(42);
static_assert(s_fresh.staleness() == SS::bottom());
static_assert(s_fresh.peek()      == 42);

inline constexpr S_i s_inf = S_i::at_infinity(99);
static_assert(s_inf.is_infinite());
static_assert(!s_inf.is_finite());
static_assert(s_inf.peek() == 99);

inline constexpr S_i s_at7 = S_i::at(123, 7);
static_assert(s_at7.staleness().value == 7);
static_assert(s_at7.peek() == 123);

// Equality: same value AND staleness → equal.
static_assert(s_at7 == S_i{123, ::crucible::algebra::lattices::staleness::at(7)});

// Different value, same staleness → NOT equal.
static_assert(!(s_at7 == S_i{999, ::crucible::algebra::lattices::staleness::at(7)}));

// Same value, different staleness → NOT equal.
static_assert(!(s_at7 == S_i{123, ::crucible::algebra::lattices::staleness::at(3)}));

// Order discipline.
static_assert( s_fresh.fresher_than(s_at7));      // 0 < 7
static_assert( s_at7.fresher_than(s_inf));         // 7 < ∞
static_assert(!s_at7.fresher_than(s_fresh));       // 7 ⊏ 0 fails
static_assert( s_fresh.no_staler_than(s_fresh));   // reflexive
static_assert( s_fresh.no_staler_than(s_at7));     // 0 ≤ 7
static_assert(!s_at7.no_staler_than(s_fresh));     // 7 ≤ 0 fails

// combine_max: pessimistic watermark.
inline constexpr S_i s_a = S_i::at(10, 3);
inline constexpr S_i s_b = S_i::at(20, 8);
inline constexpr S_i s_combined = s_a.combine_max(s_b);
static_assert(s_combined.staleness().value == 8);   // max(3, 8) = 8
static_assert(s_combined.peek()            == 10);  // payload from *this

// combine_max with ∞: ∞ wins (max).
inline constexpr S_i s_combined_inf = s_a.combine_max(s_inf);
static_assert(s_combined_inf.is_infinite());

// combine_min: optimistic freshest selection.
inline constexpr S_i s_min_pair = s_a.combine_min(s_b);
static_assert(s_min_pair.staleness().value == 3);   // min(3, 8) = 3
static_assert(s_min_pair.peek()            == 10);  // payload from *this

// combine_min with ∞: the FINITE side wins (min).
inline constexpr S_i s_min_with_inf = s_a.combine_min(s_inf);
static_assert(s_min_with_inf.staleness().value == 3);
static_assert(s_min_with_inf.is_finite());

// combine_min with self: idempotent (min(τ, τ) = τ).
static_assert(s_a.combine_min(s_a).staleness() == s_a.staleness());

// compose_add: chain accumulation via saturating add.
inline constexpr S_i s_composed = s_a.compose_add(s_b);
static_assert(s_composed.staleness().value == 11);  // 3 + 8 = 11
static_assert(s_composed.peek()            == 10);  // payload from *this

// compose_add with ∞: ∞ absorbs.
inline constexpr S_i s_composed_inf = s_a.compose_add(s_inf);
static_assert(s_composed_inf.is_infinite());

// advance_by: tick staleness forward.
inline constexpr S_i s_advanced = s_a.advance_by(5);
static_assert(s_advanced.staleness().value == 8);  // 3 + 5 = 8
static_assert(s_advanced.peek()            == 10);

// advance_by(0) is identity on staleness (one is the multiplicative
// identity in the tropical semiring).
inline constexpr S_i s_advanced_zero = s_a.advance_by(0);
static_assert(s_advanced_zero.staleness().value == 3);

// Lattice/semiring identities preserved through the wrapper.
static_assert(SS::bottom() == s_fresh.staleness());
static_assert(SS::top()    == s_inf.staleness());

// value_type_name forwards to Graded's reflection-derived T name.
// Per the gcc16_c26_reflection_gotchas memory rule: use .ends_with()
// not == because display_string_of is TU-context-fragile.
static_assert(S_i::value_type_name().ends_with("int"));

// swap exchanges value AND staleness grade between two Stale.
[[nodiscard]] consteval bool swap_exchanges_both_components() noexcept {
    S_i a = S_i::at(10, 3);
    S_i b = S_i::at(20, 8);
    a.swap(b);
    return a.peek() == 20 && a.staleness().value == 8
        && b.peek() == 10 && b.staleness().value == 3;
}
static_assert(swap_exchanges_both_components());

// Free-function swap (ADL-friendly) reaches the same exchange.
[[nodiscard]] consteval bool free_swap_works() noexcept {
    S_i a = S_i::at(10, 3);
    S_i b = S_i::at(20, 8);
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Exercise every named operation at runtime per
// feedback_algebra_runtime_smoke_test_discipline.  Critical because
// Stale is the SECOND production wrapper using regime-#4 storage —
// any constexpr-vs-runtime divergence in StalenessSemiring's
// saturating-add path would surface here.
inline void runtime_smoke_test() {
    Stale<int> a = Stale<int>::at(10, 3);
    Stale<int> b = Stale<int>::at(20, 8);
    Stale<int> inf = Stale<int>::at_infinity(99);

    [[maybe_unused]] bool fa  = a.is_fresh();
    [[maybe_unused]] bool fi  = a.is_finite();
    [[maybe_unused]] bool ii  = inf.is_infinite();

    [[maybe_unused]] bool ord = a.fresher_than(b);
    [[maybe_unused]] bool nos = a.no_staler_than(b);

    Stale<int> watermark = a.combine_max(b);
    Stale<int> freshest  = a.combine_min(b);
    Stale<int> chain     = a.compose_add(b);
    Stale<int> ticked    = a.advance_by(5);

    [[maybe_unused]] auto v1 = watermark.peek();
    [[maybe_unused]] auto vf = freshest.peek();
    [[maybe_unused]] auto t1 = chain.staleness();
    [[maybe_unused]] auto t2 = ticked.staleness();

    // Move-based variants.
    Stale<int> moved = std::move(a).combine_max(b);
    [[maybe_unused]] auto mv = moved.peek();

    // Default + factories.
    Stale<int> def{};
    Stale<int> fr = Stale<int>::fresh(42);
    Stale<int> ai = Stale<int>::at(7, 100);
    [[maybe_unused]] auto def_t = def.staleness();
    [[maybe_unused]] auto fr_t  = fr.staleness();
    [[maybe_unused]] auto ai_t  = ai.staleness();

    // peek_mut on lvalue.
    chain.peek_mut() = 99;
}

}  // namespace detail::stale_self_test

}  // namespace crucible::safety

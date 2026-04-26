#pragma once

// ── crucible::safety::TimeOrdered<T, N, Tag> ────────────────────────
//
// MIGRATE-10: a value of type T paired with a vector clock recording
// the causal position at which it was produced.  Built directly on
// the algebra/HappensBefore.h ALGEBRA-13 lattice; the FIRST production
// wrapper exercising Graded's regime-#4 storage (genuine 2-field —
// the grade is a non-trivial std::array<uint64_t, N>, not an empty
// type-level singleton).
//
//   Substrate:  Graded<ModalityKind::Absolute,
//                       HappensBeforeLattice<N, Tag>,
//                       T>
//
//   Use case:   Cipher::ReplayLog<Ordering> per 25_04_2026.md §4.
//               Async-first training (Decoupled DiLoCo, INTELLECT-2,
//               HALoS) commits gradients out of step order; each
//               event carries a TimeOrdered<GradientShard, N> whose
//               vector clock pins the causal position so replay can
//               reproduce the same admission decisions deterministically.
//
//   Axiom coverage:
//     TypeSafe — N and Tag are template parameters; cross-N or
//                cross-Tag mixing is a compile error (inherited
//                from the underlying HappensBeforeLattice<N, Tag>).
//     DetSafe — every operation is constexpr where the underlying
//                lattice is constexpr; no runtime nondeterminism.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//   Runtime cost:
//     sizeof(TimeOrdered<T, N>) == sizeof(T) + sizeof(uint64_t) * N
//     + alignment padding.  For (T = ContentHash, N = 8) on x86_64,
//     ~88 bytes — load-bearing because every Cipher::ReplayLog event
//     pays this cost.  Verified by static_asserts at the end of this
//     header.
//
// ── Why not move-only ───────────────────────────────────────────────
//
// Linear<T> (MIGRATE-1) is move-only because its modality (QttGrade::
// One) encodes EXACTLY-ONCE ownership.  TimeOrdered<T> is COPYABLE
// because its modality encodes a value's causal position — a property
// of the value's identity, not an ownership claim.  Two TimeOrdered
// events with identical (value, clock) ARE the same event; copying
// represents replay, not duplication of ownership.
//
// ── Why no operator<=> on the wrapper ───────────────────────────────
//
// The underlying HappensBeforeLattice's element_type publishes
// operator<=> returning std::partial_ordering (concurrent clocks
// yield unordered).  Lifting that to TimeOrdered<T> would create a
// tension with `operator==`: two TimeOrdered values with the same
// clock but different payloads should be unequal, but their
// spaceship would return partial_ordering::equivalent (clocks are
// equal in the lattice's order, neither happens-before the other).
//
// We DELIBERATELY ship named methods (happens_before / is_concurrent /
// comparable) instead — the distributed-systems vocabulary stays
// explicit, the user reaches for the semantic verb, and there's no
// inconsistency between the spaceship's "equivalent" and `==`'s
// "not equal".
//
// Callers that want the partial-order spaceship can extract the
// clock via .vector_clock() and use the lattice's element_type::operator<=>
// directly — the lifting is one line and explicit.
//
// See ALGEBRA-13 (#458, HappensBefore.h) for the underlying lattice;
// 25_04_2026.md §4 for the Cipher::ReplayLog use case; MIGRATE-1
// (Linear.h) for the wrapper-pattern this mirrors.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/HappensBefore.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ════════════════════════════════════════════════════════════════════
// TERMINOLOGY BANNER — read before reading any method signature
// ════════════════════════════════════════════════════════════════════
//
// Throughout this file (and on every TimeOrdered<T,N,Tag> instance):
//
//   "clock", "vector_clock", "vector_clock_t" ALL refer to a
//   LOGICAL VECTOR CLOCK (Mattern 1988 / Fidge 1991) — a tuple of
//   N per-process integer counters tracking partial-order causal
//   precedence between distributed events.
//
//   These names DO NOT refer to:
//     ✗ std::chrono clocks (system_clock, steady_clock, etc.)
//     ✗ Wall-clock time / nanoseconds / microseconds
//     ✗ Monotonic time / CPU cycles / rdtsc counters
//     ✗ Hardware oscillators or NTP synchronized time
//
//   The vector clock IS:
//     ✓ A `std::array<uint64_t, N>`-wrapping struct (the
//       HappensBeforeLattice<N, Tag>::element_type).
//     ✓ Slot p ∈ [0, N) tracks "events from process p observed
//       so far" — Lamport's logical-time abstraction generalized
//       to N participants.
//     ✓ The substrate Cipher::ReplayLog<Ordering> uses to record
//       async-DiLoCo gradient-arrival ordering for replay-determinism.
//     ✓ Partially-ordered: comparing two clocks yields one of
//       {<, ==, >, ∥} where ∥ means "concurrent — could have
//       happened in either order in real time".
//
// All public methods (vector_clock, vector_clock_at, with_vector_clock,
// merge, advance_at, tick) operate on this LOGICAL clock — never on
// physical time.  See HappensBefore.h for the underlying lattice.
//
// ════════════════════════════════════════════════════════════════════
template <typename T, std::size_t N, typename Tag = void>
class [[nodiscard]] TimeOrdered {
    static_assert(N > 0,
        "TimeOrdered<T, 0> is forbidden — a zero-participant vector "
        "clock has no algebraic content.  Use N >= 1; N=1 reduces to "
        "a Lamport scalar clock.");

    // Substrate alias chain.  The HappensBeforeLattice's element_type
    // is a non-trivial 2-field struct (regime #4) — TimeOrdered is
    // the first production wrapper to exercise this regime.
    using lattice_type = ::crucible::algebra::lattices::HappensBeforeLattice<N, Tag>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;

    graded_type impl_;

public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type            = T;
    using lattice_t             = lattice_type;
    using vector_clock_t        = typename lattice_type::element_type;
    using process_id_t          = std::size_t;
    using tag_t                 = Tag;
    static constexpr std::size_t process_count = N;

    // ── Construction ────────────────────────────────────────────────
    //
    // Default-construct: value at T{}, clock at the zero vector
    // (HappensBeforeLattice's bottom()).  Useful for sentinel events.
    constexpr TimeOrdered() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, lattice_type::bottom()} {}

    // Explicit construction with both value and clock.  The most
    // common production pattern — caller has a value freshly produced
    // at a known causal position and binds them.
    constexpr TimeOrdered(T value, vector_clock_t vc) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), vc} {}

    // In-place construction of T inside TimeOrdered, paired with a
    // vector clock.  Avoids a move when T is expensive or non-movable;
    // mirrors Linear<T>'s std::in_place_t pattern.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr TimeOrdered(std::in_place_t, vector_clock_t vc, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), vc} {}

    // Convenience factory: value at the origin (clock == bottom).
    // Used when an event's first observation is at the start of a
    // session — no prior causal predecessors.
    [[nodiscard]] static constexpr TimeOrdered at_origin(T value)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return TimeOrdered{std::move(value), lattice_type::bottom()};
    }

    // Defaulted copy/move/destroy/equality — TimeOrdered is COPYABLE
    // (vs Linear<T> which deletes copy).  The Absolute modality with
    // a non-Linearity grade does not impose ownership constraints;
    // events can be replayed via copy.
    constexpr TimeOrdered(const TimeOrdered&)            = default;
    constexpr TimeOrdered(TimeOrdered&&)                 = default;
    constexpr TimeOrdered& operator=(const TimeOrdered&) = default;
    constexpr TimeOrdered& operator=(TimeOrdered&&)      = default;
    ~TimeOrdered()                                       = default;

    // Equality: compares BOTH value and clock.  Two events at the
    // same clock with different payloads are unequal events; two
    // events at different clocks with the same payload are also
    // unequal.
    //
    // We do NOT default `operator==` here because Graded itself does
    // not publish operator== (its grade-comparison semantics are
    // ambiguous — comparing grades alone, values alone, or both
    // depend on the modality).  TimeOrdered explicitly composes
    // T::operator== with vector_clock_t::operator==, picking the
    // "both must match" semantic that distributed-systems events
    // follow.
    [[nodiscard]] friend constexpr bool operator==(
        TimeOrdered const& a, TimeOrdered const& b) noexcept(
        noexcept(a.peek() == b.peek())
        && noexcept(a.vector_clock() == b.vector_clock()))
    {
        return a.peek() == b.peek() && a.vector_clock() == b.vector_clock();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    //
    // value_type_name(): T's display string via reflection (P2996R13).
    // Answers "what is this TimeOrdered wrapping?" without
    // dereferencing.
    //
    // lattice_name(): the underlying lattice's display name —
    // "HappensBeforeLattice<N>" here — used to disambiguate
    // diagnostics when multiple Graded wrappers are in scope (e.g.,
    // distinguishing a TimeOrdered<T, 4> grade-failure from a
    // Stale<T> grade-failure in the same logging output).  Forwards
    // to Graded::lattice_name which routes through the algebra-level
    // free function `lattice_name<L>()`.
    //
    // Pair these with a future protocol_name forwarder once
    // TimeOrdered gains a session-typed counterpart.  Audit-Tier-2
    // cross-wrapper parity sweep — Stale ships the same two
    // forwarders.
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    // ── Swap (forwarded from Graded substrate) ─────────────────────
    //
    // Standard exchange — swaps both value AND vector_clock between two
    // TimeOrdered instances.  Forwards to Graded::swap which is gated
    // on AbsoluteModality — sound here because the vector-clock grade
    // is orthogonal to T's identity (swapping the values doesn't
    // violate the per-event causal-position bookkeeping; it just
    // exchanges which event is at which storage cell).
    //
    // The friend overload enables ADL-style `swap(a, b)` calls that
    // generic algorithms (std::swap_ranges, std::iter_swap, etc.)
    // route through.
    constexpr void swap(TimeOrdered& other)
        noexcept(std::is_nothrow_swappable_v<T>
                 && std::is_nothrow_swappable_v<vector_clock_t>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(TimeOrdered& a, TimeOrdered& b)
        noexcept(std::is_nothrow_swappable_v<T>
                 && std::is_nothrow_swappable_v<vector_clock_t>)
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

    [[nodiscard]] constexpr vector_clock_t vector_clock() const
        noexcept(std::is_nothrow_copy_constructible_v<vector_clock_t>)
    {
        return impl_.grade();
    }

    // Slot accessor — read-only.  The vector_clock_t's bounds-checked
    // operator[] is forwarded.  Used in patterns like
    // `evt.vector_clock_at(my_process)` to ask "what's my view of the
    // sender's events at the moment THIS event was produced?".
    [[nodiscard]] constexpr std::uint64_t vector_clock_at(std::size_t p) const noexcept
        pre (p < N)
    {
        return impl_.grade()[p];
    }

    // ── Mutable T access ────────────────────────────────────────────
    //
    // peek_mut forwards to Graded::peek_mut, gated on AbsoluteModality
    // — sound here because the vector-clock grade is orthogonal to T's
    // bytes (the clock records WHEN the value was produced, not WHAT
    // it contains; mutation of the value doesn't violate the clock).
    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // ── Distributed-systems vocabulary (forwards to lattice) ────────
    //
    // Mirrors HappensBeforeLattice's named operations; the wrapper
    // surface lets callers reason in terms of EVENTS rather than
    // RAW CLOCKS.

    // a → b: strict causal precedence.  a's clock is leq b's AND
    // they are not equal.  Asymmetric, irreflexive, transitive.
    [[nodiscard]] constexpr bool happens_before(
        TimeOrdered const& other) const noexcept
    {
        return lattice_type::happens_before(this->vector_clock(), other.vector_clock());
    }

    // a ∥ b: causal independence.  Neither a → b nor b → a.  THE
    // distinctive vector-clock feature — events that could have
    // happened in either order.  For N=1 (degenerate Lamport) this
    // is vacuously false except for equal clocks.
    [[nodiscard]] constexpr bool is_concurrent(
        TimeOrdered const& other) const noexcept
    {
        return lattice_type::is_concurrent(this->vector_clock(), other.vector_clock());
    }

    // Comparable in EITHER direction (complement of is_concurrent
    // modulo equality).
    [[nodiscard]] constexpr bool comparable(
        TimeOrdered const& other) const noexcept
    {
        return lattice_type::comparable(this->vector_clock(), other.vector_clock());
    }

    // ── Causal advancement (returns NEW TimeOrdered) ────────────────
    //
    // Vector clocks are IMMUTABLE observations — advancing produces
    // a SUCCESSOR event, not a state mutation.  Two overloads:
    //   - const& copies T into the new event (replay-safe).
    //   - && moves T into the new event (single-owner forward).
    //
    // The resulting event always satisfies:
    //   this.happens_before(advanced_result)
    // — successor_at is monotone under leq.

    // Local-event advancement: process p observes a local event,
    // bumps its slot.  pre (p < N) inherited from successor_at.
    [[nodiscard]] constexpr TimeOrdered advance_at(std::size_t p) const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
        pre (p < N)
    {
        return TimeOrdered{this->peek(), lattice_type::successor_at(this->vector_clock(), p)};
    }

    [[nodiscard]] constexpr TimeOrdered advance_at(std::size_t p) &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
        pre (p < N)
    {
        vector_clock_t advanced = lattice_type::successor_at(this->vector_clock(), p);
        return TimeOrdered{std::move(impl_).consume(), advanced};
    }

    // tick(p) — synonym for advance_at(p) using the
    // distributed-systems-vocabulary verb.  "I observed a local
    // event; tick the clock at my slot."  Both names live; choose
    // by call-site readability.  No semantic difference.
    [[nodiscard]] constexpr TimeOrdered tick(std::size_t p) const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
        pre (p < N)
    {
        return advance_at(p);
    }

    [[nodiscard]] constexpr TimeOrdered tick(std::size_t p) &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
        pre (p < N)
    {
        return std::move(*this).advance_at(p);
    }

    // with_vector_clock(new_vector_clock) — produces a new TimeOrdered with the
    // SAME payload but a different clock.  Use case (§4 ReplayLog):
    // peer-state notification — "I learned that peer X is now at
    // clock C; update my view of X without changing my payload."
    //
    // Unlike advance_at / tick / merge, this does NOT enforce any
    // monotonicity relationship between the new clock and *this's
    // clock.  The caller is responsible for whatever protocol-
    // discipline applies (typically: only call this with a clock
    // that's known to be ≥ this->vector_clock() under the lattice's leq).
    [[nodiscard]] constexpr TimeOrdered with_vector_clock(vector_clock_t new_vector_clock) const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return TimeOrdered{this->peek(), new_vector_clock};
    }

    [[nodiscard]] constexpr TimeOrdered with_vector_clock(vector_clock_t new_vector_clock) &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return TimeOrdered{std::move(impl_).consume(), new_vector_clock};
    }

    // Receive-event advancement: process `me` receives an event with
    // the given clock; result clock = pointwise-max(this, received) +
    // bump slot me.  Composite of join + successor_at, encapsulated
    // here as the single canonical RECEIVE pattern.
    //
    // The `received_value` parameter is the new T payload (typically
    // either THIS's payload, or a merged transform of both — the
    // wrapper doesn't dictate the payload-merge policy because that
    // depends on T's domain).
    [[nodiscard]] constexpr TimeOrdered merge(
        T received_value, vector_clock_t received_vector_clock, std::size_t me) const noexcept(
        std::is_nothrow_move_constructible_v<T>)
        pre (me < N)
    {
        vector_clock_t merged = lattice_type::causal_merge(this->vector_clock(), received_vector_clock, me);
        return TimeOrdered{std::move(received_value), merged};
    }
};

// ── CTAD: deduce T from the value argument ──────────────────────────
template <typename T, std::size_t N, typename Tag = void>
TimeOrdered(T, typename ::crucible::algebra::lattices::HappensBeforeLattice<N, Tag>::element_type)
    -> TimeOrdered<T, N, Tag>;

// ── Layout invariants — load-bearing for §4 Cipher::ReplayLog cost ──
//
// sizeof(TimeOrdered<T, N>) MUST be sizeof(T) + sizeof(uint64_t)*N
// + minimal alignment padding.  Verified at three representative
// witnesses:
//
//   N=4 (small fleet)        — 32 bytes of clock, ≤8 bytes pad.
//   N=8 (typical deployment) — 64 bytes of clock.
//   N=16 (large fleet)       — 128 bytes of clock.
//
// If any of these fires, Graded's regime-#4 storage drifted (the
// inherited HappensBeforeLattice element_type's alignment grew, or
// the wrapper added a stray field).  Critical because Cipher's hot
// tier pays this cost per-event; bloat scales linearly with the
// log size.
namespace detail::time_ordered_layout {

// Witness T = std::int64_t (typical event payload-hash slot).
using TO_int64_n4  = TimeOrdered<std::int64_t, 4>;
using TO_int64_n8  = TimeOrdered<std::int64_t, 8>;
using TO_int64_n16 = TimeOrdered<std::int64_t, 16>;

// Each clock is N×8 bytes; T is 8 bytes; total is sizeof(T) +
// sizeof(clock) + alignment padding (≤ alignof(uint64_t) = 8).
static_assert(sizeof(TO_int64_n4)  <= sizeof(std::int64_t) + 4  * sizeof(std::uint64_t) + 8,
    "TimeOrdered<int64, 4> exceeded sizeof(int64) + 32 + 8 bytes — "
    "the regime-#4 storage discipline drifted.  Investigate "
    "HappensBeforeLattice<4>::element_type alignment OR Graded's "
    "grade_ field placement.");
static_assert(sizeof(TO_int64_n8)  <= sizeof(std::int64_t) + 8  * sizeof(std::uint64_t) + 8);
static_assert(sizeof(TO_int64_n16) <= sizeof(std::int64_t) + 16 * sizeof(std::uint64_t) + 8);

// Witness T = void* (typical pointer payload — a kernel result handle).
using TO_voidp_n4 = TimeOrdered<void*, 4>;
static_assert(sizeof(TO_voidp_n4) <= sizeof(void*) + 4 * sizeof(std::uint64_t) + 8);

}  // namespace detail::time_ordered_layout

// ── Self-test ───────────────────────────────────────────────────────
//
// Exercise the wrapper's full public API at compile time + runtime.
// The static_asserts pin every named operation against the underlying
// lattice's behavior; the runtime smoke ensures the constexpr-vs-
// runtime path doesn't diverge (per
// feedback_algebra_runtime_smoke_test_discipline).
namespace detail::time_ordered_self_test {

using TO4 = TimeOrdered<int, 4>;
using HB4 = ::crucible::algebra::lattices::HappensBeforeLattice<4>;

// Construction from explicit value+clock.
inline constexpr TO4 evt_a{10, HB4::element_type{{1, 0, 0, 0}}};
inline constexpr TO4 evt_b{20, HB4::element_type{{1, 1, 0, 0}}};
inline constexpr TO4 evt_c{30, HB4::element_type{{2, 2, 1, 0}}};

// Concurrent pair: x ∥ y.
inline constexpr TO4 evt_x{40, HB4::element_type{{2, 0, 1, 0}}};
inline constexpr TO4 evt_y{50, HB4::element_type{{0, 2, 0, 1}}};

// at_origin factory yields a TimeOrdered at clock = bottom.
inline constexpr TO4 evt_origin = TO4::at_origin(99);
static_assert(evt_origin.vector_clock() == HB4::element_type{{0, 0, 0, 0}});
static_assert(evt_origin.peek()  == 99);

// Default ctor: value = T{}, clock = bottom.
inline constexpr TO4 evt_default{};
static_assert(evt_default.vector_clock() == HB4::element_type{{0, 0, 0, 0}});
static_assert(evt_default.peek()  == 0);

// Strict causal chain: a → b → c.
static_assert( evt_a.happens_before(evt_b));
static_assert( evt_b.happens_before(evt_c));
static_assert( evt_a.happens_before(evt_c));   // transitive
static_assert(!evt_b.happens_before(evt_a));   // asymmetric
static_assert(!evt_a.happens_before(evt_a));   // irreflexive (strict)

// Concurrent pair: x ∥ y (neither happens-before the other).
static_assert( evt_x.is_concurrent(evt_y));
static_assert( evt_y.is_concurrent(evt_x));   // symmetric
static_assert(!evt_x.happens_before(evt_y));
static_assert(!evt_y.happens_before(evt_x));
static_assert(!evt_x.comparable(evt_y));

// Comparable forms — chain elements ARE comparable.
static_assert( evt_a.comparable(evt_b));
static_assert( evt_a.comparable(evt_c));

// Equality: same value AND clock → equal.
static_assert(evt_a == TO4{10, HB4::element_type{{1, 0, 0, 0}}});

// Different value, same clock → NOT equal (even though clocks are
// "equivalent" in the lattice order).  Pins the value-AND-clock
// equality discipline.
static_assert(!(evt_a == TO4{99, HB4::element_type{{1, 0, 0, 0}}}));

// Same value, different clock → NOT equal.  Two events with
// identical payloads but different causal positions are distinct.
static_assert(!(evt_a == TO4{10, HB4::element_type{{2, 0, 0, 0}}}));

// vector_clock_at: per-slot accessor.  Slot 0 of evt_a's vector clock is 1.
static_assert(evt_a.vector_clock_at(0) == 1);
static_assert(evt_a.vector_clock_at(1) == 0);
static_assert(evt_b.vector_clock_at(1) == 1);
static_assert(evt_c.vector_clock_at(2) == 1);

// advance_at on rvalue: produces a successor event.
inline constexpr TO4 evt_a_after = TO4{10, HB4::element_type{{1, 0, 0, 0}}}.advance_at(0);
static_assert(evt_a_after.vector_clock() == HB4::element_type{{2, 0, 0, 0}});
static_assert(evt_a_after.peek()  == 10);

// Successor strictly happens after predecessor.
static_assert(evt_a.happens_before(evt_a_after));

// tick(p) — synonym for advance_at(p), confirms semantic identity.
inline constexpr TO4 evt_a_ticked = evt_a.tick(0);
static_assert(evt_a_ticked.vector_clock() == evt_a_after.vector_clock());
static_assert(evt_a_ticked.peek()  == evt_a.peek());
static_assert(evt_a.happens_before(evt_a_ticked));

// with_vector_clock — same payload, different vector clock.  No monotonicity
// constraint — caller responsibility.  Used for peer-state
// notifications: "rebind my view of this value to a new clock."
inline constexpr TO4 evt_a_relocated = evt_a.with_vector_clock(HB4::element_type{{5, 5, 5, 5}});
static_assert(evt_a_relocated.peek()  == evt_a.peek());
static_assert(evt_a_relocated.vector_clock() == HB4::element_type{{5, 5, 5, 5}});

// merge: receive evt_y at process 0 — pointwise max of clocks + bump
// slot 0.  Result: max({1,0,0,0}, {0,2,0,1}) = {1,2,0,1}, then bump 0
// → {2,2,0,1}.  Payload = received_value (123 in this witness).
inline constexpr TO4 evt_merged = evt_a.merge(123, evt_y.vector_clock(), 0);
static_assert(evt_merged.vector_clock() == HB4::element_type{{2, 2, 0, 1}});
static_assert(evt_merged.peek()  == 123);

// Post-merge event observes BOTH input clocks (causal closure).
static_assert(evt_a.happens_before(evt_merged));
static_assert(evt_y.happens_before(evt_merged));

// Tag distinction at the wrapper level — different Tags produce
// different TimeOrdered specializations.
struct ReplayTag {};
struct KernelOrderTag {};
using TO_replay = TimeOrdered<int, 4, ReplayTag>;
using TO_kernel = TimeOrdered<int, 4, KernelOrderTag>;
static_assert(!std::is_same_v<TO_replay, TO_kernel>);
static_assert(!std::is_same_v<TO_replay::vector_clock_t, TO_kernel::vector_clock_t>);

// process_count exposed at the type level.
static_assert(TO4::process_count == 4);
static_assert(TimeOrdered<int, 8>::process_count == 8);

// value_type_name forwards to Graded's reflection-derived T name.
// Per the gcc16_c26_reflection_gotchas memory rule: use .ends_with()
// not == because display_string_of returns a TU-context-fragile name
// (could be "int" or "::int" depending on the including TU).
static_assert(TO4::value_type_name().ends_with("int"));

// lattice_name forwards to Graded::lattice_name → routes through the
// algebra-level free function lattice_name<L>().  HappensBeforeLattice
// returns the bare string "HappensBeforeLattice" (the N parameter is
// known structurally via process_count, not encoded in the name —
// matches the style of every other named lattice in the registry).
static_assert(TO4::lattice_name() == "HappensBeforeLattice");

// swap exchanges value AND vector_clock between two TimeOrdered.
[[nodiscard]] consteval bool swap_exchanges_both_components() noexcept {
    TO4 a{10, HB4::element_type{{1, 0, 0, 0}}};
    TO4 b{20, HB4::element_type{{2, 2, 1, 0}}};
    a.swap(b);
    return a.peek() == 20 && a.vector_clock() == HB4::element_type{{2, 2, 1, 0}}
        && b.peek() == 10 && b.vector_clock() == HB4::element_type{{1, 0, 0, 0}};
}
static_assert(swap_exchanges_both_components());

// Free-function swap (ADL-friendly) reaches the same exchange.
[[nodiscard]] consteval bool free_swap_works() noexcept {
    TO4 a{10, HB4::element_type{{1, 0, 0, 0}}};
    TO4 b{20, HB4::element_type{{2, 2, 1, 0}}};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline: exercise the
// wrapper's full operation set with non-constant arguments.  Critical
// because TimeOrdered is the FIRST production wrapper using regime-#4
// storage — any constexpr-vs-runtime divergence in HappensBefore's
// element_type initialization would surface here.
inline void runtime_smoke_test() {
    HB4::element_type c1{{1, 0, 0, 0}};
    HB4::element_type c2{{1, 1, 0, 0}};
    HB4::element_type cy{{0, 2, 0, 1}};

    TO4 a{10, c1};
    TO4 b{20, c2};
    TO4 y{50, cy};

    [[maybe_unused]] bool hb_ab = a.happens_before(b);
    [[maybe_unused]] bool conc  = a.is_concurrent(y);
    [[maybe_unused]] bool comp  = a.comparable(b);

    // Successor + merge.
    TO4 a_succ = a.advance_at(0);
    TO4 a_recv = a.merge(99, cy, 0);
    [[maybe_unused]] bool causal = a.happens_before(a_succ);

    // Move-based advance (consumes the rvalue's T).
    TO4 a_moved = std::move(a).advance_at(0);
    [[maybe_unused]] auto v = a_moved.peek();

    // Default + at_origin paths.
    TO4 d_evt{};
    TO4 o_evt = TO4::at_origin(7);
    [[maybe_unused]] auto d_clock = d_evt.vector_clock();
    [[maybe_unused]] auto o_clock = o_evt.vector_clock();

    // Slot accessor.
    [[maybe_unused]] auto slot0 = b.vector_clock_at(0);
    [[maybe_unused]] auto slot3 = b.vector_clock_at(3);

    // peek_mut on lvalue.
    a_recv.peek_mut() = 42;
}

}  // namespace detail::time_ordered_self_test

}  // namespace crucible::safety

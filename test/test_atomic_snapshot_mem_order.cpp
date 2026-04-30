// FOUND-G32 — AtomicSnapshot MemOrder-pinned production surface.
//
// Verifies the load_mo_pinned / try_load_mo_pinned / version_mo_pinned
// methods added to AtomicSnapshot.  These are the production call
// sites for the MemOrder wrapper (FOUND-G29 substrate, FOUND-G31
// negative-compile fixtures) at the seqlock-style SWMR snapshot
// reader path.
//
// API surface:
//   AtomicSnapshot<T>::load_mo_pinned     → MemOrder<Acquire, T>
//   AtomicSnapshot<T>::try_load_mo_pinned → optional<MemOrder<Acquire, T>>
//   AtomicSnapshot<T>::version_mo_pinned  → MemOrder<Acquire, uint64_t>
//
// Lattice rule (MemOrderLattice.h):
//   SeqCst(weakest) ⊑ AcqRel ⊑ Release ⊑ Acquire ⊑ Relaxed(strongest)
//   "stronger hardware-friendliness = HIGHER in the lattice"
//   satisfies<Required> = leq(Required, Self)
//
// Test surface coverage:
//   T01 — load_mo_pinned type-identity (Acquire)
//   T02 — try_load_mo_pinned type-identity (optional<Acquire>)
//   T03 — version_mo_pinned type-identity (Acquire on uint64_t)
//   T04 — round-trip: publish → load_mo_pinned → consume → equality
//   T05 — version_mo_pinned increments after publish
//   T06 — Acquire satisfies Acquire (self)
//   T07 — Acquire satisfies AcqRel, Release, SeqCst (weaker required)
//   T08 — Acquire does NOT satisfy Relaxed (Relaxed is stronger)
//   T09 — Relaxed satisfies all five (top of lattice)
//   T10 — SeqCst satisfies only SeqCst (bottom of lattice)
//   T11 — layout invariant (sizeof preservation across load + uint64 + T)
//   T12 — end-to-end consumer-fence simulation (Acquire-fence accepts Acquire)
//   T13 — SeqCst rejected at Acquire-fence (cannot-tighten matrix)
//   T14 — try_load_mo_pinned nullopt path (in-flight publish)
//   T15 — initial-value snapshot reads via load_mo_pinned

#include <crucible/concurrent/AtomicSnapshot.h>
#include <crucible/safety/IsMemOrder.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/Wait.h>
#include "test_assert.h"

#include <cstdio>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

using crucible::concurrent::AtomicSnapshot;
using crucible::safety::MemOrder;
using crucible::safety::MemOrderTag_v;

// SnapshotValue must be trivially-copyable + lock-free-atomic-eligible.
struct alignas(8) Pair {
    uint32_t a = 0;
    uint32_t b = 0;
    bool operator==(const Pair&) const = default;
};

// ── T01 — load_mo_pinned type-identity ────────────────────────
static void test_load_mo_pinned_type_identity() {
    AtomicSnapshot<uint64_t> snap{42ULL};

    using Got  = decltype(snap.load_mo_pinned());
    using Want = MemOrder<MemOrderTag_v::Acquire, uint64_t>;
    static_assert(std::is_same_v<Got, Want>,
        "load_mo_pinned must return MemOrder<Acquire, T>");
    static_assert(Got::tag == MemOrderTag_v::Acquire);

    auto pinned = snap.load_mo_pinned();
    uint64_t v = std::move(pinned).consume();
    assert(v == 42ULL);
}

// ── T02 — try_load_mo_pinned type-identity ────────────────────
static void test_try_load_mo_pinned_type_identity() {
    AtomicSnapshot<uint64_t> snap{99ULL};

    using Got  = decltype(snap.try_load_mo_pinned());
    using Want = std::optional<MemOrder<MemOrderTag_v::Acquire, uint64_t>>;
    static_assert(std::is_same_v<Got, Want>,
        "try_load_mo_pinned must return optional<MemOrder<Acquire, T>>");

    auto opt = snap.try_load_mo_pinned();
    assert(opt.has_value());
    uint64_t v = std::move(*opt).consume();
    assert(v == 99ULL);
}

// ── T03 — version_mo_pinned type-identity ─────────────────────
static void test_version_mo_pinned_type_identity() {
    AtomicSnapshot<uint64_t> snap{0ULL};

    using Got  = decltype(snap.version_mo_pinned());
    using Want = MemOrder<MemOrderTag_v::Acquire, uint64_t>;
    static_assert(std::is_same_v<Got, Want>,
        "version_mo_pinned must return MemOrder<Acquire, uint64_t>");
    static_assert(Got::tag == MemOrderTag_v::Acquire);

    auto v_pinned = snap.version_mo_pinned();
    (void)std::move(v_pinned).consume();
}

// ── T04 — Round-trip: publish → load_mo_pinned → consume ──────
static void test_round_trip_via_load_mo_pinned() {
    AtomicSnapshot<Pair> snap{Pair{1, 2}};

    auto first = snap.load_mo_pinned();
    Pair v0 = std::move(first).consume();
    assert((v0 == Pair{1, 2}));

    snap.publish(Pair{7, 11});

    auto second = snap.load_mo_pinned();
    Pair v1 = std::move(second).consume();
    assert((v1 == Pair{7, 11}));
}

// ── T05 — version_mo_pinned increments after publish ─────────
static void test_version_increments() {
    AtomicSnapshot<Pair> snap{Pair{0, 0}};

    auto v0 = snap.version_mo_pinned();
    uint64_t e0 = std::move(v0).consume();

    snap.publish(Pair{1, 1});
    snap.publish(Pair{2, 2});

    auto v2 = snap.version_mo_pinned();
    uint64_t e2 = std::move(v2).consume();
    assert(e2 > e0);
    assert(e2 - e0 == 2);
}

// ── T06 — Acquire satisfies Acquire (self) ────────────────────
static void test_acquire_satisfies_self() {
    using Acq = MemOrder<MemOrderTag_v::Acquire, int>;
    static_assert(Acq::satisfies<MemOrderTag_v::Acquire>);
}

// ── T07 — Acquire satisfies AcqRel, Release, SeqCst ──────────
static void test_acquire_satisfies_weaker() {
    using Acq = MemOrder<MemOrderTag_v::Acquire, int>;
    // Acquire is HIGHER in the hardware-friendliness lattice than
    // Release / AcqRel / SeqCst.  satisfies<Required> = leq(Required,
    // Self): a Release-tier consumer accepts an Acquire-tier value.
    static_assert(Acq::satisfies<MemOrderTag_v::Release>);
    static_assert(Acq::satisfies<MemOrderTag_v::AcqRel>);
    static_assert(Acq::satisfies<MemOrderTag_v::SeqCst>);
}

// ── T08 — Acquire does NOT satisfy Relaxed ────────────────────
static void test_acquire_does_not_satisfy_relaxed() {
    using Acq = MemOrder<MemOrderTag_v::Acquire, int>;
    // Relaxed is STRONGER (more hardware-friendly) — a Relaxed-only
    // consumer wants the cheapest-possible producer; Acquire emits a
    // fence, so it doesn't satisfy "I want NO fence at all".
    static_assert(!Acq::satisfies<MemOrderTag_v::Relaxed>,
        "Acquire is weaker than Relaxed; Relaxed-requiring consumers "
        "must reject Acquire-tier values (Acquire emits a fence)");
}

// ── T09 — Relaxed satisfies all five (top of lattice) ─────────
static void test_relaxed_satisfies_all() {
    using Rlx = MemOrder<MemOrderTag_v::Relaxed, int>;
    static_assert(Rlx::satisfies<MemOrderTag_v::Relaxed>);
    static_assert(Rlx::satisfies<MemOrderTag_v::Acquire>);
    static_assert(Rlx::satisfies<MemOrderTag_v::Release>);
    static_assert(Rlx::satisfies<MemOrderTag_v::AcqRel>);
    static_assert(Rlx::satisfies<MemOrderTag_v::SeqCst>);
}

// ── T10 — SeqCst satisfies only SeqCst (bottom) ───────────────
static void test_seqcst_satisfies_only_self() {
    using SC = MemOrder<MemOrderTag_v::SeqCst, int>;
    static_assert( SC::satisfies<MemOrderTag_v::SeqCst>);
    static_assert(!SC::satisfies<MemOrderTag_v::AcqRel>);
    static_assert(!SC::satisfies<MemOrderTag_v::Release>);
    static_assert(!SC::satisfies<MemOrderTag_v::Acquire>,
        "SeqCst is the weakest hardware-friendliness claim — it does "
        "NOT satisfy any stronger requirement.  This is THE load-"
        "bearing rejection: a SeqCst-emitting site cannot reach an "
        "Acquire-fence consumer.");
    static_assert(!SC::satisfies<MemOrderTag_v::Relaxed>);
}

// ── T11 — Layout invariant ────────────────────────────────────
static void test_layout_invariant() {
    static_assert(sizeof(MemOrder<MemOrderTag_v::Acquire, uint64_t>)
                  == sizeof(uint64_t));
    static_assert(sizeof(MemOrder<MemOrderTag_v::Acquire, Pair>)
                  == sizeof(Pair));
    static_assert(sizeof(MemOrder<MemOrderTag_v::Acquire, int>)
                  == sizeof(int));
}

// ── T12 — End-to-end consumer-fence (Acquire admits Acquire) ──
template <typename W>
    requires (W::template satisfies<MemOrderTag_v::Acquire>)
static uint64_t acquire_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_acquire_fence_admits_acquire() {
    AtomicSnapshot<uint64_t> snap{12345ULL};

    auto pinned = snap.load_mo_pinned();
    uint64_t v = acquire_consumer(std::move(pinned));
    assert(v == 12345ULL);
}

// ── T13 — Cannot-tighten matrix (SeqCst → Acquire) ────────────
template <typename W, MemOrderTag_v T_target>
concept can_tighten = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};

static void test_cannot_tighten_to_stronger() {
    using AcqT = MemOrder<MemOrderTag_v::Acquire, int>;
    using RlxT = MemOrder<MemOrderTag_v::Relaxed, int>;
    using SCT  = MemOrder<MemOrderTag_v::SeqCst,  int>;

    // Down (relax) — admissible.  Relaxed → Acquire → Release → AcqRel
    // → SeqCst is the relax-DOWN direction (toward weaker hardware-
    // friendliness).
    static_assert( can_tighten<RlxT, MemOrderTag_v::Acquire>);
    static_assert( can_tighten<AcqT, MemOrderTag_v::SeqCst>);

    // Self — admissible.
    static_assert( can_tighten<AcqT, MemOrderTag_v::Acquire>);
    static_assert( can_tighten<RlxT, MemOrderTag_v::Relaxed>);
    static_assert( can_tighten<SCT,  MemOrderTag_v::SeqCst>);

    // Up — REJECTED at every step (load-bearing).  Cannot tighten
    // SeqCst (least-friendly) into Acquire (more-friendly) — that
    // would CLAIM more hardware-friendliness than the source provides.
    static_assert(!can_tighten<SCT,  MemOrderTag_v::Acquire>);
    static_assert(!can_tighten<SCT,  MemOrderTag_v::Relaxed>);
    static_assert(!can_tighten<AcqT, MemOrderTag_v::Relaxed>);
}

// ── T14 — try_load_mo_pinned propagates nullopt ──────────────
//
// We can't reliably trigger the in-flight-publish branch single-
// threaded (the seqlock observes itself coherent), but we CAN
// witness the type identity of the optional carrying nullopt.
// The behavioral nullopt path is in test_atomic_snapshot.cpp.
static void test_try_load_mo_pinned_optional_shape() {
    AtomicSnapshot<uint64_t> snap{1ULL};
    auto opt = snap.try_load_mo_pinned();
    static_assert(std::is_same_v<
        decltype(opt),
        std::optional<MemOrder<MemOrderTag_v::Acquire, uint64_t>>>);
    assert(opt.has_value());
    (void)std::move(*opt).consume();
}

// ── T15 — Initial-value snapshot reads via load_mo_pinned ────
static void test_initial_value_via_load_mo_pinned() {
    AtomicSnapshot<Pair> snap{Pair{99, 1234}};
    auto pinned = snap.load_mo_pinned();
    Pair v = std::move(pinned).consume();
    assert((v == Pair{99, 1234}));
}

// ─────────────────────────────────────────────────────────────────
// FOUND-G32-AUDIT — extended positive coverage
// ─────────────────────────────────────────────────────────────────

// ── T16 — relax DOWN composes with consumer-fence ────────────
//
// A Relaxed-pinned source can be relaxed step-by-step through
// Acquire → AcqRel and still feed a Release-fence consumer.  Proves
// the relax<>() chain composes cleanly with the production consumer
// boundary at every step.
static void test_relax_down_chain() {
    using crucible::safety::MemOrder;

    // Lattice: SeqCst(weakest) ⊑ AcqRel ⊑ Release ⊑ Acquire ⊑ Relaxed(strongest).
    // Each step goes DOWN-the-lattice (toward weaker hardware-
    // friendliness).  Relaxed → Acquire → Release → AcqRel → SeqCst.
    MemOrder<MemOrderTag_v::Relaxed, int> rlx{42};
    auto acq    = std::move(rlx).relax<MemOrderTag_v::Acquire>();
    auto rel    = std::move(acq).relax<MemOrderTag_v::Release>();
    auto acqrel = std::move(rel).relax<MemOrderTag_v::AcqRel>();
    auto seqcst = std::move(acqrel).relax<MemOrderTag_v::SeqCst>();

    static_assert(std::is_same_v<decltype(acq),
        MemOrder<MemOrderTag_v::Acquire, int>>);
    static_assert(std::is_same_v<decltype(rel),
        MemOrder<MemOrderTag_v::Release, int>>);
    static_assert(std::is_same_v<decltype(acqrel),
        MemOrder<MemOrderTag_v::AcqRel, int>>);
    static_assert(std::is_same_v<decltype(seqcst),
        MemOrder<MemOrderTag_v::SeqCst, int>>);

    int v = std::move(seqcst).consume();
    assert(v == 42);
}

// ── T17 — Reflective trait agreement (mem_order_tag_v) ────────
//
// FOUND-D27 trait must agree with the wrapper's static `tag` member
// across cv-ref qualifiers.  Drift would silently corrupt diagnostic
// printing and row-hash folding.
static void test_reflective_trait_agreement() {
    using crucible::safety::extract::mem_order_tag_v;
    using crucible::safety::extract::is_mem_order_v;

    using Acq = MemOrder<MemOrderTag_v::Acquire, int>;
    using Rel = MemOrder<MemOrderTag_v::Release, int>;
    using SC  = MemOrder<MemOrderTag_v::SeqCst,  int>;

    static_assert(mem_order_tag_v<Acq>        == Acq::tag);
    static_assert(mem_order_tag_v<Rel>        == Rel::tag);
    static_assert(mem_order_tag_v<SC>         == SC::tag);
    static_assert(mem_order_tag_v<Acq&>       == Acq::tag);
    static_assert(mem_order_tag_v<Acq const&> == Acq::tag);
    static_assert(mem_order_tag_v<Acq&&>      == Acq::tag);

    // Concept gate — non-MemOrder rejected.
    static_assert(!is_mem_order_v<int>);
    static_assert(!is_mem_order_v<bool>);
    static_assert( is_mem_order_v<Acq>);
}

// ── T18 — Cross-axis composition with FOUND-G27 Wait ──────────
//
// load_pinned (FOUND-G27) returns Wait<SpinPause, T>; load_mo_pinned
// (FOUND-G32) returns MemOrder<Acquire, T>.  Both pin DIFFERENT
// axes (wait-strategy vs memory-ordering) on the SAME underlying
// load.  Confirm the two surfaces are type-distinct (no accidental
// aliasing) and BOTH preserve sizeof(T).
static void test_cross_axis_with_wait_pin() {
    using crucible::safety::Wait;
    using crucible::safety::WaitStrategy_v;

    AtomicSnapshot<uint64_t> snap{777ULL};

    using WaitPinT  = decltype(snap.load_pinned());
    using MemOrdPinT = decltype(snap.load_mo_pinned());

    static_assert(std::is_same_v<WaitPinT,
        Wait<WaitStrategy_v::SpinPause, uint64_t>>);
    static_assert(std::is_same_v<MemOrdPinT,
        MemOrder<MemOrderTag_v::Acquire, uint64_t>>);

    // Type-distinct — different axes shouldn't accidentally alias.
    static_assert(!std::is_same_v<WaitPinT, MemOrdPinT>);

    // Both preserve sizeof — orthogonal zero-cost wrappers.
    static_assert(sizeof(WaitPinT)  == sizeof(uint64_t));
    static_assert(sizeof(MemOrdPinT) == sizeof(uint64_t));

    // Both consume identically.
    auto w_pin = snap.load_pinned();
    auto m_pin = snap.load_mo_pinned();
    assert(std::move(w_pin).consume() == 777ULL);
    assert(std::move(m_pin).consume() == 777ULL);
}

// ── T19 — Full 5×5 satisfies<> truth table ────────────────────
//
// Exhaustive verification of the lattice direction across all
// (Self, Required) pairs.  Each cell = leq(Required, Self).
// Lattice: SeqCst(0) ⊑ AcqRel(1) ⊑ Release(2) ⊑ Acquire(3) ⊑ Relaxed(4)
// (per the implementation's INVERTED ordinal, see MemOrderLattice.h
// L85-87).  satisfies<R> on Self = R-position ≤ Self-position.
static void test_full_truth_table() {
    using crucible::safety::MemOrder;
    using T = MemOrderTag_v;

    // Row Self=Relaxed (top): satisfies all five.
    using Rlx = MemOrder<T::Relaxed, int>;
    static_assert( Rlx::satisfies<T::Relaxed>);
    static_assert( Rlx::satisfies<T::Acquire>);
    static_assert( Rlx::satisfies<T::Release>);
    static_assert( Rlx::satisfies<T::AcqRel>);
    static_assert( Rlx::satisfies<T::SeqCst>);

    // Row Self=Acquire: satisfies Acquire, Release, AcqRel, SeqCst.
    using Acq = MemOrder<T::Acquire, int>;
    static_assert(!Acq::satisfies<T::Relaxed>);
    static_assert( Acq::satisfies<T::Acquire>);
    static_assert( Acq::satisfies<T::Release>);
    static_assert( Acq::satisfies<T::AcqRel>);
    static_assert( Acq::satisfies<T::SeqCst>);

    // Row Self=Release: satisfies Release, AcqRel, SeqCst.
    using Rel = MemOrder<T::Release, int>;
    static_assert(!Rel::satisfies<T::Relaxed>);
    static_assert(!Rel::satisfies<T::Acquire>);
    static_assert( Rel::satisfies<T::Release>);
    static_assert( Rel::satisfies<T::AcqRel>);
    static_assert( Rel::satisfies<T::SeqCst>);

    // Row Self=AcqRel: satisfies AcqRel, SeqCst.
    using AR = MemOrder<T::AcqRel, int>;
    static_assert(!AR::satisfies<T::Relaxed>);
    static_assert(!AR::satisfies<T::Acquire>);
    static_assert(!AR::satisfies<T::Release>);
    static_assert( AR::satisfies<T::AcqRel>);
    static_assert( AR::satisfies<T::SeqCst>);

    // Row Self=SeqCst (bottom): satisfies only SeqCst.
    using SC = MemOrder<T::SeqCst, int>;
    static_assert(!SC::satisfies<T::Relaxed>);
    static_assert(!SC::satisfies<T::Acquire>);
    static_assert(!SC::satisfies<T::Release>);
    static_assert(!SC::satisfies<T::AcqRel>);
    static_assert( SC::satisfies<T::SeqCst>);
}

// ── T20 — Move-only enforcement (Graded-derived) ─────────────
static void test_move_only_witness() {
    using Acq = MemOrder<MemOrderTag_v::Acquire, uint64_t>;
    static_assert(std::is_move_constructible_v<Acq>);
    static_assert(std::is_trivially_move_constructible_v<Acq>);
    // The wrapper consumes via && rvalue-only consume() — verified at
    // T01 / T04 call sites; documented here at the type level.
}

int main() {
    test_load_mo_pinned_type_identity();
    test_try_load_mo_pinned_type_identity();
    test_version_mo_pinned_type_identity();
    test_round_trip_via_load_mo_pinned();
    test_version_increments();
    test_acquire_satisfies_self();
    test_acquire_satisfies_weaker();
    test_acquire_does_not_satisfy_relaxed();
    test_relaxed_satisfies_all();
    test_seqcst_satisfies_only_self();
    test_layout_invariant();
    test_acquire_fence_admits_acquire();
    test_cannot_tighten_to_stronger();
    test_try_load_mo_pinned_optional_shape();
    test_initial_value_via_load_mo_pinned();

    // ── FOUND-G32-AUDIT extended positive coverage ─────────────
    test_relax_down_chain();
    test_reflective_trait_agreement();
    test_cross_axis_with_wait_pin();
    test_full_truth_table();
    test_move_only_witness();

    std::puts("ok");
    return 0;
}

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
#include <crucible/safety/MemOrder.h>
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

    std::puts("ok");
    return 0;
}

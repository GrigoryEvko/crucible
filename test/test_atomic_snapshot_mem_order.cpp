// The lattice runs SeqCst, AcqRel, Release, Acquire, Relaxed, from
// bottom to top, where higher means more hardware-friendly.  That is
// the opposite of the usual reading, in which SeqCst is the strongest.
// satisfies<Required> is leq(Required, Self).

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

// The snapshot value must be trivially copyable and lock-free eligible.
struct alignas(8) Pair {
    uint32_t a = 0;
    uint32_t b = 0;
    bool operator==(const Pair&) const = default;
};

static void test_load_mo_pinned_type_identity() {
    AtomicSnapshot<uint64_t> snap{42ULL};

    using Got = decltype(snap.load_mo_pinned());
    using Want = MemOrder<MemOrderTag_v::Acquire, uint64_t>;
    static_assert(std::is_same_v<Got, Want>, "load_mo_pinned must return MemOrder<Acquire, T>");
    static_assert(Got::tag == MemOrderTag_v::Acquire);

    auto pinned = snap.load_mo_pinned();
    uint64_t v = std::move(pinned).consume();
    assert(v == 42ULL);
}

static void test_try_load_mo_pinned_type_identity() {
    AtomicSnapshot<uint64_t> snap{99ULL};

    using Got = decltype(snap.try_load_mo_pinned());
    using Want = std::optional<MemOrder<MemOrderTag_v::Acquire, uint64_t>>;
    static_assert(std::is_same_v<Got, Want>, "try_load_mo_pinned must return optional<MemOrder<Acquire, T>>");

    auto opt = snap.try_load_mo_pinned();
    assert(opt.has_value());
    uint64_t v = std::move(*opt).consume();
    assert(v == 99ULL);
}

static void test_version_mo_pinned_type_identity() {
    AtomicSnapshot<uint64_t> snap{0ULL};

    using Got = decltype(snap.version_mo_pinned());
    using Want = MemOrder<MemOrderTag_v::Acquire, uint64_t>;
    static_assert(std::is_same_v<Got, Want>, "version_mo_pinned must return MemOrder<Acquire, uint64_t>");
    static_assert(Got::tag == MemOrderTag_v::Acquire);

    auto v_pinned = snap.version_mo_pinned();
    (void)std::move(v_pinned).consume();
}

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

static void test_acquire_satisfies_self() {
    using Acq = MemOrder<MemOrderTag_v::Acquire, int>;
    static_assert(Acq::satisfies<MemOrderTag_v::Acquire>);
}

static void test_acquire_satisfies_weaker() {
    using Acq = MemOrder<MemOrderTag_v::Acquire, int>;
    // A Release-tier consumer accepts an Acquire-tier value, because
    // Acquire sits higher.
    static_assert(Acq::satisfies<MemOrderTag_v::Release>);
    static_assert(Acq::satisfies<MemOrderTag_v::AcqRel>);
    static_assert(Acq::satisfies<MemOrderTag_v::SeqCst>);
}

static void test_acquire_does_not_satisfy_relaxed() {
    using Acq = MemOrder<MemOrderTag_v::Acquire, int>;
    static_assert(!Acq::satisfies<MemOrderTag_v::Relaxed>,
                  "Acquire is weaker than Relaxed; Relaxed-requiring consumers "
                  "must reject Acquire-tier values (Acquire emits a fence)");
}

static void test_relaxed_satisfies_all() {
    using Rlx = MemOrder<MemOrderTag_v::Relaxed, int>;
    static_assert(Rlx::satisfies<MemOrderTag_v::Relaxed>);
    static_assert(Rlx::satisfies<MemOrderTag_v::Acquire>);
    static_assert(Rlx::satisfies<MemOrderTag_v::Release>);
    static_assert(Rlx::satisfies<MemOrderTag_v::AcqRel>);
    static_assert(Rlx::satisfies<MemOrderTag_v::SeqCst>);
}

static void test_seqcst_satisfies_only_self() {
    using SC = MemOrder<MemOrderTag_v::SeqCst, int>;
    static_assert(SC::satisfies<MemOrderTag_v::SeqCst>);
    static_assert(!SC::satisfies<MemOrderTag_v::AcqRel>);
    static_assert(!SC::satisfies<MemOrderTag_v::Release>);
    static_assert(!SC::satisfies<MemOrderTag_v::Acquire>, "SeqCst is the weakest hardware-friendliness claim — it does "
                                                          "NOT satisfy any stronger requirement.  This is THE load-"
                                                          "bearing rejection: a SeqCst-emitting site cannot reach an "
                                                          "Acquire-fence consumer.");
    static_assert(!SC::satisfies<MemOrderTag_v::Relaxed>);
}

static void test_layout_invariant() {
    static_assert(sizeof(MemOrder<MemOrderTag_v::Acquire, uint64_t>) == sizeof(uint64_t));
    static_assert(sizeof(MemOrder<MemOrderTag_v::Acquire, Pair>) == sizeof(Pair));
    static_assert(sizeof(MemOrder<MemOrderTag_v::Acquire, int>) == sizeof(int));
}

template <typename W>
    requires(W::template satisfies<MemOrderTag_v::Acquire>)
static uint64_t acquire_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_acquire_fence_admits_acquire() {
    AtomicSnapshot<uint64_t> snap{12345ULL};

    auto pinned = snap.load_mo_pinned();
    uint64_t v = acquire_consumer(std::move(pinned));
    assert(v == 12345ULL);
}

template <typename W, MemOrderTag_v T_target>
concept can_tighten = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};

static void test_cannot_tighten_to_stronger() {
    using AcqT = MemOrder<MemOrderTag_v::Acquire, int>;
    using RlxT = MemOrder<MemOrderTag_v::Relaxed, int>;
    using SCT = MemOrder<MemOrderTag_v::SeqCst, int>;

    // Relaxing downward, toward weaker hardware-friendliness.
    static_assert(can_tighten<RlxT, MemOrderTag_v::Acquire>);
    static_assert(can_tighten<AcqT, MemOrderTag_v::SeqCst>);

    static_assert(can_tighten<AcqT, MemOrderTag_v::Acquire>);
    static_assert(can_tighten<RlxT, MemOrderTag_v::Relaxed>);
    static_assert(can_tighten<SCT, MemOrderTag_v::SeqCst>);

    // Upward is rejected: it would claim more hardware-friendliness
    // than the source provides.
    static_assert(!can_tighten<SCT, MemOrderTag_v::Acquire>);
    static_assert(!can_tighten<SCT, MemOrderTag_v::Relaxed>);
    static_assert(!can_tighten<AcqT, MemOrderTag_v::Relaxed>);
}

// The in-flight-publish branch cannot be triggered from one thread: a
// seqlock always observes itself coherent.  Only the optional's type
// identity is witnessed here.
static void test_try_load_mo_pinned_optional_shape() {
    AtomicSnapshot<uint64_t> snap{1ULL};
    auto opt = snap.try_load_mo_pinned();
    static_assert(std::is_same_v<decltype(opt), std::optional<MemOrder<MemOrderTag_v::Acquire, uint64_t>>>);
    assert(opt.has_value());
    (void)std::move(*opt).consume();
}

static void test_initial_value_via_load_mo_pinned() {
    AtomicSnapshot<Pair> snap{Pair{99, 1234}};
    auto pinned = snap.load_mo_pinned();
    Pair v = std::move(pinned).consume();
    assert((v == Pair{99, 1234}));
}

static void test_relax_down_chain() {
    using crucible::safety::MemOrder;

    // Each step goes down the lattice.
    MemOrder<MemOrderTag_v::Relaxed, int> rlx{42};
    auto acq = std::move(rlx).relax<MemOrderTag_v::Acquire>();
    auto rel = std::move(acq).relax<MemOrderTag_v::Release>();
    auto acqrel = std::move(rel).relax<MemOrderTag_v::AcqRel>();
    auto seqcst = std::move(acqrel).relax<MemOrderTag_v::SeqCst>();

    static_assert(std::is_same_v<decltype(acq), MemOrder<MemOrderTag_v::Acquire, int>>);
    static_assert(std::is_same_v<decltype(rel), MemOrder<MemOrderTag_v::Release, int>>);
    static_assert(std::is_same_v<decltype(acqrel), MemOrder<MemOrderTag_v::AcqRel, int>>);
    static_assert(std::is_same_v<decltype(seqcst), MemOrder<MemOrderTag_v::SeqCst, int>>);

    int v = std::move(seqcst).consume();
    assert(v == 42);
}

// The trait must agree with the wrapper's own tag across cv-ref
// qualifiers.  Drift corrupts diagnostic printing and row-hash folding.
static void test_reflective_trait_agreement() {
    using crucible::safety::extract::mem_order_tag_v;
    using crucible::safety::extract::is_mem_order_v;

    using Acq = MemOrder<MemOrderTag_v::Acquire, int>;
    using Rel = MemOrder<MemOrderTag_v::Release, int>;
    using SC = MemOrder<MemOrderTag_v::SeqCst, int>;

    static_assert(mem_order_tag_v<Acq> == Acq::tag);
    static_assert(mem_order_tag_v<Rel> == Rel::tag);
    static_assert(mem_order_tag_v<SC> == SC::tag);
    static_assert(mem_order_tag_v<Acq&> == Acq::tag);
    static_assert(mem_order_tag_v<Acq const&> == Acq::tag);
    static_assert(mem_order_tag_v<Acq&&> == Acq::tag);

    static_assert(!is_mem_order_v<int>);
    static_assert(!is_mem_order_v<bool>);
    static_assert(is_mem_order_v<Acq>);
}

// The two pinned loads sit on different axes of the same underlying
// load: one pins the wait strategy, the other the memory ordering.
static void test_cross_axis_with_wait_pin() {
    using crucible::safety::Wait;
    using crucible::safety::WaitStrategy_v;

    AtomicSnapshot<uint64_t> snap{777ULL};

    using WaitPinT = decltype(snap.load_pinned());
    using MemOrdPinT = decltype(snap.load_mo_pinned());

    static_assert(std::is_same_v<WaitPinT, Wait<WaitStrategy_v::SpinPause, uint64_t>>);
    static_assert(std::is_same_v<MemOrdPinT, MemOrder<MemOrderTag_v::Acquire, uint64_t>>);

    static_assert(!std::is_same_v<WaitPinT, MemOrdPinT>);

    static_assert(sizeof(WaitPinT) == sizeof(uint64_t));
    static_assert(sizeof(MemOrdPinT) == sizeof(uint64_t));

    auto w_pin = snap.load_pinned();
    auto m_pin = snap.load_mo_pinned();
    assert(std::move(w_pin).consume() == 777ULL);
    assert(std::move(m_pin).consume() == 777ULL);
}

// Every (Self, Required) pair.  The ordinals run SeqCst 0, AcqRel 1,
// Release 2, Acquire 3, Relaxed 4, and satisfies<R> holds when R's
// ordinal is at most Self's.
static void test_full_truth_table() {
    using crucible::safety::MemOrder;
    using T = MemOrderTag_v;

    // Self = Relaxed, the top of the lattice.
    using Rlx = MemOrder<T::Relaxed, int>;
    static_assert(Rlx::satisfies<T::Relaxed>);
    static_assert(Rlx::satisfies<T::Acquire>);
    static_assert(Rlx::satisfies<T::Release>);
    static_assert(Rlx::satisfies<T::AcqRel>);
    static_assert(Rlx::satisfies<T::SeqCst>);

    // Self = Acquire.
    using Acq = MemOrder<T::Acquire, int>;
    static_assert(!Acq::satisfies<T::Relaxed>);
    static_assert(Acq::satisfies<T::Acquire>);
    static_assert(Acq::satisfies<T::Release>);
    static_assert(Acq::satisfies<T::AcqRel>);
    static_assert(Acq::satisfies<T::SeqCst>);

    // Self = Release.
    using Rel = MemOrder<T::Release, int>;
    static_assert(!Rel::satisfies<T::Relaxed>);
    static_assert(!Rel::satisfies<T::Acquire>);
    static_assert(Rel::satisfies<T::Release>);
    static_assert(Rel::satisfies<T::AcqRel>);
    static_assert(Rel::satisfies<T::SeqCst>);

    // Self = AcqRel.
    using AR = MemOrder<T::AcqRel, int>;
    static_assert(!AR::satisfies<T::Relaxed>);
    static_assert(!AR::satisfies<T::Acquire>);
    static_assert(!AR::satisfies<T::Release>);
    static_assert(AR::satisfies<T::AcqRel>);
    static_assert(AR::satisfies<T::SeqCst>);

    // Self = SeqCst, the bottom.
    using SC = MemOrder<T::SeqCst, int>;
    static_assert(!SC::satisfies<T::Relaxed>);
    static_assert(!SC::satisfies<T::Acquire>);
    static_assert(!SC::satisfies<T::Release>);
    static_assert(!SC::satisfies<T::AcqRel>);
    static_assert(SC::satisfies<T::SeqCst>);
}

static void test_move_only_witness() {
    using Acq = MemOrder<MemOrderTag_v::Acquire, uint64_t>;
    static_assert(std::is_move_constructible_v<Acq>);
    static_assert(std::is_trivially_move_constructible_v<Acq>);
    // The rvalue-only consume is exercised at the call sites above.
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

    test_relax_down_chain();
    test_reflective_trait_agreement();
    test_cross_axis_with_wait_pin();
    test_full_truth_table();
    test_move_only_witness();

    std::puts("ok");
    return 0;
}

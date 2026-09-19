// The seqlock read spins on the writer's release with a pause
// instruction.  load_pinned() states that wait classification in the
// return type so a consumer can constrain on it and admit only wait
// strategies its own call site can afford.

#include <crucible/concurrent/AtomicSnapshot.h>
#include <crucible/safety/_Wait.h>
#include "test_assert.h"

#include <cstdio>
#include <type_traits>
#include <utility>

using crucible::concurrent::AtomicSnapshot;
using crucible::safety::Wait;
using crucible::safety::WaitStrategy_v;

struct Payload {
    int a = 0;
    double b = 0.0;
    auto operator<=>(const Payload&) const = default;
};

static void test_load_pinned_bit_equality() {
    AtomicSnapshot<Payload> snap;

    Payload raw0 = snap.load();
    auto pinned0 = snap.load_pinned();
    Payload via_wrapper = std::move(pinned0).consume();
    assert(raw0 == via_wrapper);

    Payload p_expected{42, 3.14};
    snap.publish(p_expected);
    Payload raw1 = snap.load();
    auto pinned1 = snap.load_pinned();
    Payload via_wrapper1 = std::move(pinned1).consume();
    assert(raw1 == p_expected);
    assert(via_wrapper1 == p_expected);
}

static void test_load_pinned_type_identity() {
    AtomicSnapshot<int> snap;
    using Got = decltype(snap.load_pinned());
    using Want = Wait<WaitStrategy_v::SpinPause, int>;
    static_assert(std::is_same_v<Got, Want>, "load_pinned must return Wait<SpinPause, int>");
    static_assert(Got::strategy == WaitStrategy_v::SpinPause);
}

// These three concepts stand in for consumer call sites that can
// afford a progressively weaker wait.
template <typename W>
concept admissible_at_spin_pause_fence = W::strategy == WaitStrategy_v::SpinPause;

template <typename W>
concept admissible_at_park_fence =
    W::strategy == WaitStrategy_v::SpinPause || W::strategy == WaitStrategy_v::BoundedSpin
    || W::strategy == WaitStrategy_v::UmwaitC01 || W::strategy == WaitStrategy_v::AcquireWait
    || W::strategy == WaitStrategy_v::Park;

template <typename W>
concept admissible_at_block_fence =
    W::strategy == WaitStrategy_v::SpinPause || W::strategy == WaitStrategy_v::BoundedSpin
    || W::strategy == WaitStrategy_v::UmwaitC01 || W::strategy == WaitStrategy_v::AcquireWait
    || W::strategy == WaitStrategy_v::Park || W::strategy == WaitStrategy_v::Block;

static void test_fence_simulation() {
    using SP = Wait<WaitStrategy_v::SpinPause, int>;
    using BS = Wait<WaitStrategy_v::BoundedSpin, int>;
    using UM = Wait<WaitStrategy_v::UmwaitC01, int>;
    using AW = Wait<WaitStrategy_v::AcquireWait, int>;
    using PK = Wait<WaitStrategy_v::Park, int>;
    using BL = Wait<WaitStrategy_v::Block, int>;

    static_assert(admissible_at_spin_pause_fence<SP>);
    static_assert(admissible_at_park_fence<SP>);
    static_assert(admissible_at_block_fence<SP>);

    static_assert(!admissible_at_spin_pause_fence<BS>);
    static_assert(admissible_at_park_fence<BS>);
    static_assert(admissible_at_block_fence<BS>);

    static_assert(!admissible_at_spin_pause_fence<PK>);
    static_assert(admissible_at_park_fence<PK>);
    static_assert(admissible_at_block_fence<PK>);

    static_assert(!admissible_at_spin_pause_fence<BL>);
    static_assert(!admissible_at_park_fence<BL>);
    static_assert(admissible_at_block_fence<BL>);

    (void)sizeof(UM);
    (void)sizeof(AW);
}

static void test_negative_tier_witnesses() {
    using SP = Wait<WaitStrategy_v::SpinPause, int>;
    using BS = Wait<WaitStrategy_v::BoundedSpin, int>;
    using PK = Wait<WaitStrategy_v::Park, int>;
    using BL = Wait<WaitStrategy_v::Block, int>;

    // The lattice runs weakest to strongest as Block, Park,
    // AcquireWait, UmwaitC01, BoundedSpin, SpinPause.  satisfies<R>
    // holds when the strategy is R or stronger.

    static_assert(SP::satisfies<WaitStrategy_v::SpinPause>);
    static_assert(SP::satisfies<WaitStrategy_v::BoundedSpin>);
    static_assert(SP::satisfies<WaitStrategy_v::UmwaitC01>);
    static_assert(SP::satisfies<WaitStrategy_v::AcquireWait>);
    static_assert(SP::satisfies<WaitStrategy_v::Park>);
    static_assert(SP::satisfies<WaitStrategy_v::Block>);

    static_assert(!BS::satisfies<WaitStrategy_v::SpinPause>);
    static_assert(BS::satisfies<WaitStrategy_v::BoundedSpin>);
    static_assert(BS::satisfies<WaitStrategy_v::Park>);
    static_assert(BS::satisfies<WaitStrategy_v::Block>);

    static_assert(!PK::satisfies<WaitStrategy_v::SpinPause>);
    static_assert(!PK::satisfies<WaitStrategy_v::BoundedSpin>);
    static_assert(PK::satisfies<WaitStrategy_v::Park>);
    static_assert(PK::satisfies<WaitStrategy_v::Block>);

    static_assert(!BL::satisfies<WaitStrategy_v::SpinPause>);
    static_assert(!BL::satisfies<WaitStrategy_v::Park>);
    static_assert(BL::satisfies<WaitStrategy_v::Block>);
}

// The wait classification is type-level only, so it costs no storage.
static void test_layout_invariant() {
    static_assert(sizeof(Wait<WaitStrategy_v::SpinPause, int>) == sizeof(int));
    static_assert(sizeof(Wait<WaitStrategy_v::SpinPause, double>) == sizeof(double));
    static_assert(sizeof(Wait<WaitStrategy_v::SpinPause, Payload>) == sizeof(Payload));
}

// Relaxing runs down the lattice only.
static void test_relax_to_weaker() {
    AtomicSnapshot<int> snap;
    snap.publish(99);

    auto sp = snap.load_pinned();
    auto pk = std::move(sp).relax<WaitStrategy_v::Park>();
    static_assert(std::is_same_v<decltype(pk), Wait<WaitStrategy_v::Park, int>>);
    int via = std::move(pk).consume();
    assert(via == 99);
}

static void test_chain_composition() {
    using SP = Wait<WaitStrategy_v::SpinPause, int>;
    static_assert(SP::satisfies<WaitStrategy_v::SpinPause>);
    static_assert(SP::satisfies<WaitStrategy_v::BoundedSpin>);
    static_assert(SP::satisfies<WaitStrategy_v::UmwaitC01>);
    static_assert(SP::satisfies<WaitStrategy_v::AcquireWait>);
    static_assert(SP::satisfies<WaitStrategy_v::Park>);
    static_assert(SP::satisfies<WaitStrategy_v::Block>);
}

template <typename W>
    requires(W::template satisfies<WaitStrategy_v::SpinPause>)
static int spin_pause_hot_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_e2e_spin_pause_consumer() {
    AtomicSnapshot<int> snap;
    snap.publish(123);
    auto pinned = snap.load_pinned();
    int v = spin_pause_hot_consumer(std::move(pinned));
    assert(v == 123);
}

// A Park-gated consumer still admits a SpinPause value, by subsumption.
template <typename W>
    requires(W::template satisfies<WaitStrategy_v::Park>)
static int park_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_spin_pause_satisfies_park() {
    AtomicSnapshot<int> snap;
    snap.publish(456);
    auto pinned = snap.load_pinned();
    int v = park_consumer(std::move(pinned));
    assert(v == 456);
}

static void test_version_unchanged() {
    AtomicSnapshot<int> snap;
    auto v0 = snap.version();
    snap.publish(1);
    snap.publish(2);
    snap.publish(3);
    auto v1 = snap.version();
    assert(v1 - v0 == 3);

    (void)snap.load_pinned();
    auto v2 = snap.version();
    assert(v2 == v1);
}

static void test_load_pinned_many_publishes() {
    AtomicSnapshot<Payload> snap;
    for (int i = 0; i < 100; ++i) {
        Payload p{i, static_cast<double>(i) * 0.5};
        snap.publish(p);
        auto pinned = snap.load_pinned();
        Payload got = std::move(pinned).consume();
        assert(got == p);
    }
}

int main() {
    test_load_pinned_bit_equality();
    test_load_pinned_type_identity();
    test_fence_simulation();
    test_negative_tier_witnesses();
    test_layout_invariant();
    test_relax_to_weaker();
    test_chain_composition();
    test_e2e_spin_pause_consumer();
    test_spin_pause_satisfies_park();
    test_version_unchanged();
    test_load_pinned_many_publishes();
    std::puts("ok");
    return 0;
}

// Sentinel TU for fixy/Mutation.h: every wrapper lands in the storage
// regime its lattice selects, every constructor is behind its mint, the
// header's runtime smoke test runs under the test flags, and each
// contract that a violation can reach at runtime is shown to abort.

#include <fixy/Mutation.h>

#include <foundation/algebra/GradedTrait.h>

#include "../foundation/abort_probe.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <inplace_vector>
#include <latch>
#include <limits>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace fa = ::foundation::algebra;
using ::fixy::AppendOnly;
using ::fixy::AtomicMonotonic;
using ::fixy::BoundedMonotonic;
using ::fixy::MaxObserved;
using ::fixy::Monotonic;
using ::fixy::OrderedAppendOnly;
using ::fixy::WriteOnce;
using ::fixy::WriteOnceNonNull;
using ::foundation::test::aborts;

// ── Storage regimes ──────────────────────────────────────────────────
//
// Monotonic: the grade is the value (regime 2), one cell.
static_assert(sizeof(Monotonic<std::uint32_t>) == sizeof(std::uint32_t));
static_assert(sizeof(Monotonic<std::uint64_t>) == sizeof(std::uint64_t));
static_assert(sizeof(Monotonic<double>) == sizeof(double));
static_assert(std::is_same_v<Monotonic<std::uint64_t>::lattice_type::element_type, std::uint64_t>);

// AppendOnly: the grade derives from the container (regime 3), no
// second field.
static_assert(sizeof(AppendOnly<int>) == sizeof(std::vector<int>));
static_assert(sizeof(AppendOnly<char*>) == sizeof(std::vector<char*>));
static_assert(fa::LatticeDerivesGrade<AppendOnly<int>::lattice_type, std::vector<int>>);

// OrderedAppendOnly collapses its empty functors onto the AppendOnly.
static_assert(sizeof(OrderedAppendOnly<std::uint64_t>) == sizeof(AppendOnly<std::uint64_t>));

// BoundedMonotonic collapses onto its Monotonic.
static_assert(sizeof(BoundedMonotonic<std::uint32_t, 1024U>) == sizeof(std::uint32_t));
static_assert(sizeof(BoundedMonotonic<std::uint8_t, 7U>) == sizeof(std::uint8_t));

// WriteOnce pays the optional tag; WriteOnceNonNull pays nothing.
static_assert(sizeof(WriteOnce<int>) == sizeof(std::optional<int>));
static_assert(sizeof(WriteOnceNonNull<int*>) == sizeof(int*));
static_assert(sizeof(WriteOnceNonNull<double*>) == sizeof(double*));
static_assert(sizeof(WriteOnceNonNull<void*>) == sizeof(void*));

// AtomicMonotonic owns a cache line.
static_assert(alignof(AtomicMonotonic<std::uint64_t>) >= 64);
static_assert(sizeof(AtomicMonotonic<std::uint64_t>) >= 64);
static_assert(std::is_same_v<MaxObserved<std::uint32_t>, AtomicMonotonic<std::uint32_t, std::less<std::uint32_t>>>);

// ── Diagnostic surface ───────────────────────────────────────────────
static_assert(fa::GradedWrapper<Monotonic<std::uint64_t>>);
static_assert(fa::is_graded_wrapper_v<Monotonic<std::uint64_t>>);
// AppendOnly's element value_type differs from the graded container's;
// the value_type_decoupled specialization beside the wrapper admits it.
static_assert(fa::value_type_decoupled_v<AppendOnly<int>>);
static_assert(fa::GradedWrapper<AppendOnly<int>>);
static_assert(Monotonic<std::uint64_t>::lattice_name() == "MonotoneLattice");
static_assert(AppendOnly<int>::lattice_name() == "SeqPrefixLattice");
static_assert(Monotonic<std::uint64_t>::value_type_name() == Monotonic<std::uint64_t>::graded_type::value_type_name());
static_assert(AppendOnly<int>::value_type_name() == AppendOnly<int>::graded_type::value_type_name());
static_assert(std::is_same_v<AppendOnly<int>::value_type, int>);
static_assert(std::is_same_v<AppendOnly<int>::graded_type::value_type, std::vector<int>>);

// The substrate's bottom stays reachable through graded_type.
static_assert(Monotonic<std::uint64_t>::graded_type::at_bottom().grade() == 0u);
static_assert(Monotonic<std::uint64_t, std::greater<std::uint64_t>>::graded_type::at_bottom().grade()
              == std::numeric_limits<std::uint64_t>::max());
template <typename G>
concept CanAtBottomNoArg = requires { G::at_bottom(); };
static_assert(CanAtBottomNoArg<AppendOnly<int>::graded_type>);

// ── One door ─────────────────────────────────────────────────────────
static_assert(!std::is_default_constructible_v<AppendOnly<int>>);
static_assert(!std::is_default_constructible_v<OrderedAppendOnly<int>>);
static_assert(!std::is_constructible_v<Monotonic<std::uint32_t>, std::uint32_t>);
static_assert(!std::is_constructible_v<BoundedMonotonic<std::uint32_t, 8U>, std::uint32_t>);
static_assert(!std::is_default_constructible_v<WriteOnce<int>>);
static_assert(!std::is_default_constructible_v<WriteOnceNonNull<int*>>);
static_assert(!std::is_constructible_v<AtomicMonotonic<std::uint64_t>, std::uint64_t>);

// The gates on the doors.
static_assert(::fixy::AppendOnlyStorage<int, std::vector>);
static_assert(::fixy::MonotoneCarrier<std::uint32_t, std::less<std::uint32_t>>);
static_assert(::fixy::MonotoneCarrier<double, std::greater<double>>);
static_assert(!::fixy::MonotoneCarrier<std::uint32_t, int>);
static_assert(::fixy::BoundedMonotoneCarrier<std::uint32_t, 8U, std::less<std::uint32_t>>);
static_assert(::fixy::SlotPointer<int*>);
static_assert(::fixy::SlotPointer<void*>);
static_assert(!::fixy::SlotPointer<int>);

// Monotonic is neither assignable from nor convertible to the raw T.
static_assert(!std::is_assignable_v<Monotonic<std::uint32_t>&, std::uint32_t>);
static_assert(!std::is_convertible_v<Monotonic<std::uint32_t>, std::uint32_t>);
static_assert(std::is_copy_constructible_v<Monotonic<std::uint32_t>>);
static_assert(std::is_copy_constructible_v<BoundedMonotonic<std::uint32_t, 8U>>);
static_assert(std::is_copy_constructible_v<WriteOnce<int>>);
static_assert(std::is_copy_constructible_v<WriteOnceNonNull<int*>>);

// AtomicMonotonic is Pinned.
static_assert(!std::is_copy_constructible_v<AtomicMonotonic<std::uint64_t>>);
static_assert(!std::is_move_constructible_v<AtomicMonotonic<std::uint64_t>>);
static_assert(!std::is_copy_assignable_v<AtomicMonotonic<std::uint64_t>>);
static_assert(!std::is_move_assignable_v<AtomicMonotonic<std::uint64_t>>);

// The redundancy traits AppendOnly reads.
static_assert(::fixy::is_writeonce_v<WriteOnce<int>>);
static_assert(::fixy::is_writeonce_v<WriteOnce<int> const&>);
static_assert(!::fixy::is_writeonce_v<int>);
static_assert(::fixy::is_writeoncenonnull_v<WriteOnceNonNull<int*>>);
static_assert(!::fixy::is_writeoncenonnull_v<int*>);
static_assert(!::fixy::is_writeoncenonnull_v<WriteOnce<int*>>);

// The mints are usable in a constant expression.
static_assert(::fixy::mint_monotonic<std::uint32_t>(3u).get() == 3u);
static_assert(::fixy::mint_bounded_monotonic<std::uint32_t, 8U>(8u).get() == 8u);
static_assert(BoundedMonotonic<std::uint32_t, 10U>::max() == 10U);
static_assert(!::fixy::mint_write_once<int>().has_value());
static_assert(!::fixy::mint_write_once_non_null<int*>().has_value());

// ── Behaviour ────────────────────────────────────────────────────────

int check_append_only() {
    AppendOnly<int> log = ::fixy::mint_append_only<int>();
    log.append(1);
    log.append(2);
    log.append(3);
    // No erase is exposed, so the only way out of the log is reading it.
    if (log.size() != 3) return 10;
    if (log[2] != 3) return 11;
    if (log.front() != 1 || log.back() != 3) return 12;

    int sum = 0;
    for (int item : log)
        sum += item;
    if (sum != 6) return 13;

    std::vector<int> drained = std::move(log).drain();
    if (drained.size() != 3 || drained[0] != 1) return 14;
    return 0;
}

int check_monotonic() {
    Monotonic<std::uint64_t> epoch = ::fixy::mint_monotonic<std::uint64_t>(0);
    epoch.advance(1);
    epoch.advance(5);
    // A backward advance would fire the monotonicity contract.
    if (epoch.get() != 5) return 20;
    if (epoch.try_advance(2)) return 21;
    if (!epoch.try_advance(10)) return 22;
    if (epoch.get() != 10) return 23;
    if (epoch.current() != 10) return 24;
    epoch.bump();
    if (epoch.get() != 11) return 25;

    // The back door moves the value backward.
    epoch.reset_under_quiescence();
    if (epoch.get() != 0) return 26;
    epoch.reset_under_quiescence(3);
    if (epoch.get() != 3) return 27;

    // Under std::greater the direction inverts.
    Monotonic<std::uint64_t, std::greater<std::uint64_t>> floor =
        ::fixy::mint_monotonic<std::uint64_t, std::greater<std::uint64_t>>(100);
    if (!floor.try_advance(50)) return 28;
    if (floor.try_advance(60)) return 29;
    if (floor.get() != 50) return 30;
    return 0;
}

int check_ordered_append_only() {
    // The default key function is the identity and the default comparison is
    // less, so the appended values themselves must not decrease.
    {
        OrderedAppendOnly<std::uint64_t> timeline = ::fixy::mint_ordered_append_only<std::uint64_t>();
        timeline.append(0ULL);
        timeline.append(1ULL);
        timeline.append(1ULL);  // equal keys are non-decreasing
        timeline.append(2ULL);
        timeline.emplace(5ULL);  // emplace must take the ordering path too
        // Appending 3 here would violate the ordering contract.
        if (timeline.size() != 5) return 40;
        if (timeline.back() != 5) return 41;
        if (timeline[0] != 0) return 42;
        if (timeline.front() != 0) return 43;
        if (timeline.empty()) return 44;

        std::uint64_t sum = 0;
        for (std::uint64_t item : timeline)
            sum += item;
        if (sum != 9) return 45;

        std::vector<std::uint64_t> drained = std::move(timeline).drain();
        if (drained.size() != 5) return 46;
    }
    // With a projected key the ordering runs on one field, so the payload is
    // free to move in any direction.
    {
        struct Entry {
            std::uint64_t step;
            int payload;
        };
        struct ByStep {
            constexpr std::uint64_t operator()(const Entry& e) const noexcept { return e.step; }
        };
        // OrderedAppendOnly rejects a KeyFn or Cmp that carries state:
        // append compares each item only against the back element, so a
        // functor that answers differently at two appends would admit a
        // sequence ordered under no single comparator.  Emptiness is also
        // what lets the layout assertion above hold.
        static_assert(std::is_empty_v<ByStep> && std::is_default_constructible_v<ByStep>);
        static_assert(sizeof(OrderedAppendOnly<Entry, ByStep>) == sizeof(AppendOnly<Entry>),
                      "a projected key must still collapse to zero layout cost");
        OrderedAppendOnly<Entry, ByStep> log_by_step = ::fixy::mint_ordered_append_only<Entry, ByStep>();
        log_by_step.append({.step = 10, .payload = 100});
        log_by_step.append({.step = 10, .payload = 101});  // duplicate step OK
        log_by_step.append({.step = 20, .payload = 200});
        // A step of 15 appended here would violate the ordering contract.
        if (log_by_step.size() != 3) return 47;
        if (log_by_step.back().step != 20) return 48;
    }
    return 0;
}

int check_bounded_monotonic() {
    BoundedMonotonic<std::uint32_t, 10U> counter = ::fixy::mint_bounded_monotonic<std::uint32_t, 10U>(0U);
    if (counter.get() != 0) return 50;
    counter.advance(1U);
    counter.advance(5U);
    counter.advance(10U);  // the bound itself is admissible
    // 11 would exceed the bound; 5 would go backwards. Either fires a
    // precondition.
    if (counter.get() != 10) return 51;
    if (counter.current() != 10) return 52;

    BoundedMonotonic<std::uint32_t, 3U> bumper = ::fixy::mint_bounded_monotonic<std::uint32_t, 3U>(0U);
    bumper.bump();
    bumper.bump();
    bumper.bump();
    // A fourth bump would fire the bound precondition.
    if (bumper.get() != 3U) return 53;

    // try_advance rejects rather than firing a contract. The two
    // rejections below have different causes.
    BoundedMonotonic<std::uint32_t, 5U> cnt = ::fixy::mint_bounded_monotonic<std::uint32_t, 5U>(2U);
    if (cnt.try_advance(6U)) return 54;  // over the bound
    if (cnt.get() != 2U) return 55;
    if (cnt.try_advance(1U)) return 56;  // backwards
    if (!cnt.try_advance(4U)) return 57;
    if (cnt.get() != 4U) return 58;
    return 0;
}

int check_write_once() {
    WriteOnce<int> slot = ::fixy::mint_write_once<int>();
    if (slot.has_value()) return 60;
    if (static_cast<bool>(slot)) return 61;
    if (!slot.try_set(33)) return 62;
    if (!slot.has_value()) return 63;
    if (slot.get() != 33) return 64;
    if (slot.get_assuming_set() != 33) return 65;
    if (slot.try_set(99)) return 66;
    if (slot.get() != 33) return 67;

    WriteOnce<int> direct = ::fixy::mint_write_once<int>();
    direct.set(7);
    if (direct.get() != 7) return 68;
    return 0;
}

int check_write_once_non_null() {
    // nullptr is the unset sentinel here, which costs nothing. Wrapping the
    // pointer in an optional instead would add a tag byte and its padding.
    int payload = 7;

    WriteOnceNonNull<int*> slot = ::fixy::mint_write_once_non_null<int*>();
    if (slot.has_value()) return 70;
    if (static_cast<bool>(slot)) return 71;

    // Setting nullptr writes the sentinel, so it is refused and the slot
    // stays unset.
    if (slot.try_set(nullptr)) return 72;
    if (slot.has_value()) return 73;

    if (!slot.try_set(&payload)) return 74;
    if (!slot.has_value()) return 75;
    if (!static_cast<bool>(slot)) return 76;
    if (slot.get() != &payload) return 77;
    if (*slot != 7) return 78;

    // A second try_set is refused even with a valid pointer.
    int other = 13;
    if (slot.try_set(&other)) return 79;
    if (slot.get() != &payload) return 80;

    struct Thing {
        int x;
    };
    Thing t{42};
    WriteOnceNonNull<Thing*> thing_slot = ::fixy::mint_write_once_non_null<Thing*>();
    thing_slot.set(&t);
    if (thing_slot->x != 42) return 81;

    // A void pointee has no dereference operators, so this path only
    // checks that the rest of the interface still compiles.
    int raw = 99;
    WriteOnceNonNull<void*> vslot = ::fixy::mint_write_once_non_null<void*>();
    vslot.set(&raw);
    if (!vslot.has_value()) return 82;
    if (vslot.get() != &raw) return 83;
    return 0;
}

int check_atomic_monotonic() {
    // Single-threaded behaviour must match the non-atomic Monotonic.
    {
        AtomicMonotonic<std::uint64_t> step = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);
        if (step.get() != 0ULL) return 90;
        if (!step.try_advance(1ULL)) return 91;
        if (step.get() != 1ULL) return 92;
        if (step.try_advance(1ULL)) return 93;  // equal
        if (step.try_advance(0ULL)) return 94;  // backwards
        if (!step.try_advance(7ULL)) return 95;
        if (step.get() != 7ULL) return 96;
        if (step.try_advance(5ULL)) return 97;  // backwards
        if (step.get() != 7ULL) return 98;

        step.advance(8ULL);
        if (step.get() != 8ULL) return 99;
    }

    // Under std::greater the whole direction inverts: advancing walks the
    // value down, and a larger value counts as backwards.
    {
        AtomicMonotonic<std::uint64_t, std::greater<std::uint64_t>> floor =
            ::fixy::mint_atomic_monotonic<std::uint64_t, std::greater<std::uint64_t>>(100ULL);
        if (floor.get() != 100ULL) return 100;
        if (!floor.try_advance(50ULL)) return 101;
        if (floor.get() != 50ULL) return 102;
        if (floor.try_advance(50ULL)) return 103;  // equal
        if (floor.try_advance(60ULL)) return 104;  // backwards
        if (!floor.try_advance(0ULL)) return 105;
        if (floor.get() != 0ULL) return 106;
    }

    {
        MaxObserved<std::uint32_t> high_water = ::fixy::mint_atomic_monotonic<std::uint32_t>(0U);
        if (!high_water.try_advance(50U)) return 107;
        if (!high_water.try_advance(100U)) return 108;
        if (high_water.try_advance(75U)) return 109;
        if (high_water.get() != 100U) return 110;
    }

    // bump returns the value it replaced, which is the slot index the caller
    // has just claimed. A producer reserves ring slots this way.
    {
        AtomicMonotonic<std::uint64_t> ring_head = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);
        if (ring_head.bump() != 0ULL) return 111;
        if (ring_head.get() != 1ULL) return 112;
        if (ring_head.bump() != 1ULL) return 113;
        if (ring_head.bump_by(5ULL) != 2ULL) return 114;  // claims slots 2 through 6
        if (ring_head.get() != 7ULL) return 115;
        if (ring_head.bump_by(0ULL) != 7ULL) return 116;
        if (ring_head.get() != 7ULL) return 117;
    }

    // Under std::greater, bump walks the counter down instead.
    {
        AtomicMonotonic<std::uint64_t, std::greater<std::uint64_t>> ttl =
            ::fixy::mint_atomic_monotonic<std::uint64_t, std::greater<std::uint64_t>>(1000ULL);
        if (ttl.bump() != 1000ULL) return 118;
        if (ttl.get() != 999ULL) return 119;
        if (ttl.bump_by(99ULL) != 999ULL) return 120;
        if (ttl.get() != 900ULL) return 121;
    }

    // A single producer owns the head, so it may read its own value relaxed
    // and publish the successor with a release store. get() is the acquire
    // load a reader on another thread would use.
    {
        AtomicMonotonic<std::uint64_t> spsc_head = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);
        const std::uint64_t h0 = spsc_head.peek_relaxed();
        if (h0 != 0ULL) return 122;
        spsc_head.advance(h0 + 1);
        if (spsc_head.peek_relaxed() != 1ULL) return 123;
        if (spsc_head.get() != 1ULL) return 124;
        if (spsc_head.load_relaxed() != 1ULL) return 125;

        for (std::uint64_t i = 1; i < 16; ++i) {
            const std::uint64_t h = spsc_head.peek_relaxed();
            if (h != i) return 126;
            spsc_head.advance(h + 1);
        }
        if (spsc_head.get() != 16ULL) return 127;
    }

    // Reset moves the counter backwards, which advance forbids. The caller
    // carries the obligation that no other thread is writing at the time.
    {
        AtomicMonotonic<std::uint64_t> ring_head = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);
        ring_head.advance(1);
        ring_head.advance(2);
        ring_head.advance(100);
        if (ring_head.get() != 100ULL) return 128;

        ring_head.reset_under_quiescence();
        if (ring_head.get() != 0ULL) return 129;

        ring_head.advance(1);  // monotonic from the new baseline
        if (ring_head.get() != 1ULL) return 130;

        ring_head.reset_under_quiescence(42ULL);
        if (ring_head.get() != 42ULL) return 131;
    }

    // The default orderings are acquire on load and release on store. The
    // explicit-order overloads exist for callers that need seq_cst, and they
    // keep the monotonicity contract on the store side.
    {
        AtomicMonotonic<std::uint64_t> counter = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);

        if (counter.load() != 0ULL) return 132;

        if (counter.load(std::memory_order_seq_cst) != 0ULL) return 133;
        if (counter.load(std::memory_order_relaxed) != 0ULL) return 134;

        counter.store(7ULL);
        if (counter.get() != 7ULL) return 135;

        counter.store(42ULL, std::memory_order_seq_cst);
        if (counter.get() != 42ULL) return 136;
    }

    // A failed compare-exchange writes the value it actually observed back
    // through `expected`, which is how the owner and a thief resolve their
    // race over the last element in a work-stealing deque.
    {
        AtomicMonotonic<std::uint64_t> top = ::fixy::mint_atomic_monotonic<std::uint64_t>(5);

        std::uint64_t observed = 5ULL;
        bool ok = top.compare_exchange_advance(observed, 6ULL);
        if (!ok) return 137;
        if (top.get() != 6ULL) return 138;

        observed = 5ULL;  // deliberately stale guess
        ok = top.compare_exchange_advance(observed, 7ULL);
        if (ok) return 139;
        if (observed != 6ULL) return 140;

        // Retrying with the value just reported back succeeds.
        ok = top.compare_exchange_advance(observed, 7ULL);
        if (!ok) return 141;
        if (top.get() != 7ULL) return 142;

        // The explicit-order form with seq_cst on success is what a
        // work-stealing pop needs.
        observed = 7ULL;
        ok = top.compare_exchange_advance(observed, 8ULL, std::memory_order_seq_cst, std::memory_order_relaxed);
        if (!ok) return 143;
        if (top.get() != 8ULL) return 144;
    }

    // The weak form may fail spuriously, so the caller must loop on the
    // return value rather than treat one failure as contention.
    {
        AtomicMonotonic<std::uint64_t> seq = ::fixy::mint_atomic_monotonic<std::uint64_t>(100);

        std::uint64_t observed = seq.get();
        std::uint64_t target = 0;
        bool advanced = false;
        for (int retries = 0; retries < 16 && !advanced; ++retries) {
            target = observed + 1;
            advanced = seq.compare_exchange_advance_weak(observed, target);
        }
        if (!advanced) return 145;
        if (seq.get() != 101ULL) return 146;
    }

    // A work-stealing pop needs a bare seq_cst fence between the owner's
    // bottom store and its load of top. The fence sits on the counter type so
    // that it reads next to the two calls it separates.
    {
        AtomicMonotonic<std::uint64_t> bottom = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);
        AtomicMonotonic<std::uint64_t> top = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);

        // One thread only. No race is exercised here, just the API shape.
        bottom.store(5ULL);
        AtomicMonotonic<std::uint64_t>::fence_seq_cst();
        const auto t = top.load(std::memory_order_relaxed);
        if (t != 0ULL) return 147;
        if (bottom.get() != 5ULL) return 148;
    }

    // Compare-exchange follows the same inverted direction under greater.
    {
        AtomicMonotonic<std::uint64_t, std::greater<std::uint64_t>> ttl =
            ::fixy::mint_atomic_monotonic<std::uint64_t, std::greater<std::uint64_t>>(1000ULL);

        std::uint64_t observed = 1000ULL;
        bool ok = ttl.compare_exchange_advance(observed, 999ULL);
        if (!ok) return 149;
        if (ttl.get() != 999ULL) return 150;

        observed = 1000ULL;  // deliberately stale guess
        ok = ttl.compare_exchange_advance(observed, 998ULL);
        if (ok) return 151;
        if (observed != 999ULL) return 152;
    }

    // Each thread pushes its own exclusive range of values, so the final
    // value is the global maximum.
    //
    // The latch is a starting line. Without it the threads constructed early
    // in the loop finish thousands of calls before the later ones start, and
    // the contention this test exists to exercise never happens.
    {
        constexpr int kThreads = 4;
        constexpr std::uint64_t kPerThread = 4096;

        AtomicMonotonic<std::uint64_t> shared_high = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);
        std::atomic<std::uint64_t> total_advances{0};

        std::latch start_gate{kThreads + 1};

        {
            std::inplace_vector<std::jthread, kThreads> workers;
            for (int tid = 0; tid < kThreads; ++tid) {
                workers.emplace_back([tid, &shared_high, &total_advances, &start_gate] {
                    start_gate.arrive_and_wait();
                    std::uint64_t local_advances = 0;
                    const std::uint64_t base = static_cast<std::uint64_t>(tid) * kPerThread;
                    for (std::uint64_t v = 1; v <= kPerThread; ++v) {
                        if (shared_high.try_advance(base + v)) ++local_advances;
                    }
                    total_advances.fetch_add(local_advances, std::memory_order_relaxed);
                });
            }
            // The main thread counts for the extra slot in the latch and
            // arrives last, releasing every worker at once.
            start_gate.arrive_and_wait();
        }  // the jthreads join here

        const std::uint64_t expected_max = static_cast<std::uint64_t>(kThreads - 1) * kPerThread + kPerThread;
        if (shared_high.get() != expected_max) return 153;
        // The thread holding the top range advances on every one of its own
        // steps, which sets the lower bound. No advance can happen twice, so
        // the total count is the upper bound.
        const std::uint64_t advances = total_advances.load(std::memory_order_relaxed);
        if (advances < kPerThread) return 154;
        if (advances > kThreads * kPerThread) return 155;
    }
    return 0;
}

// ── Contracts fire at runtime ────────────────────────────────────────
//
// The wrapper under test lives outside the probed body, so the jump out
// of the failing call skips no destructor of its own.

int check_contracts_abort() {
    Monotonic<std::uint32_t> mono = ::fixy::mint_monotonic<std::uint32_t>(10u);
    if (!aborts([&] { mono.advance(5u); })) return 160;
    if (mono.get() != 10u) return 161;

    Monotonic<std::uint8_t> at_max = ::fixy::mint_monotonic<std::uint8_t>(255);
    if (!aborts([&] { at_max.bump(); })) return 162;

    BoundedMonotonic<std::uint32_t, 8U> bounded = ::fixy::mint_bounded_monotonic<std::uint32_t, 8U>(8U);
    if (!aborts([&] { bounded.advance(9U); })) return 163;
    if (!aborts([&] { bounded.bump(); })) return 164;
    if (!aborts([&] { bounded.advance(7U); })) return 165;
    if (bounded.get() != 8U) return 166;

    std::uint32_t over = 9U;
    if (!aborts([&] { (void)::fixy::mint_bounded_monotonic<std::uint32_t, 8U>(over); })) return 167;

    WriteOnce<int> once = ::fixy::mint_write_once<int>();
    if (!aborts([&] { (void)once.get(); })) return 168;
    once.set(1);
    if (!aborts([&] { once.set(2); })) return 169;
    if (once.get() != 1) return 170;

    int target = 0;
    WriteOnceNonNull<int*> slot = ::fixy::mint_write_once_non_null<int*>();
    if (!aborts([&] { slot.set(nullptr); })) return 171;
    if (!aborts([&] { (void)slot.get(); })) return 172;
    slot.set(&target);
    if (!aborts([&] { slot.set(&target); })) return 173;

    OrderedAppendOnly<int> ordered = ::fixy::mint_ordered_append_only<int>();
    ordered.append(5);
    if (!aborts([&] { ordered.append(3); })) return 174;
    if (ordered.size() != 1) return 175;

    AtomicMonotonic<std::uint64_t> atomic = ::fixy::mint_atomic_monotonic<std::uint64_t>(10);
    if (!aborts([&] { atomic.advance(9); })) return 176;
    if (!aborts([&] { atomic.store(9); })) return 177;
    if (atomic.get() != 10) return 178;

    AtomicMonotonic<std::int64_t> signed_counter = ::fixy::mint_atomic_monotonic<std::int64_t>(0);
    if (!aborts([&] { (void)signed_counter.bump_by(-1); })) return 179;

    std::uint64_t expected = 10;
    if (!aborts([&] { (void)atomic.compare_exchange_advance(expected, 9); })) return 180;

    // A satisfied contract is silent.
    if (aborts([&] { mono.advance(11u); })) return 181;
    if (mono.get() != 11u) return 182;
    return 0;
}

}  // namespace

int main() {
    ::fixy::detail::mutation_self_test::runtime_smoke_test();

    if (int rc = check_append_only(); rc != 0) return rc;
    if (int rc = check_monotonic(); rc != 0) return rc;
    if (int rc = check_ordered_append_only(); rc != 0) return rc;
    if (int rc = check_bounded_monotonic(); rc != 0) return rc;
    if (int rc = check_write_once(); rc != 0) return rc;
    if (int rc = check_write_once_non_null(); rc != 0) return rc;
    if (int rc = check_atomic_monotonic(); rc != 0) return rc;
    if (int rc = check_contracts_abort(); rc != 0) return rc;

    return 0;
}

// Sentinel TU for fixy/Mutation.h: every wrapper lands in the storage
// regime its lattice selects, every constructor is behind its mint, and
// each contract that a violation can reach at runtime is shown to abort.

#include <fixy/Mutation.h>

#include <foundation/algebra/GradedTrait.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

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
using ::fixy::check::expect_dies;

// The reports of the refusals of the counters that two or more cases share.
constexpr ::fixy::TextView kBumpAtTheEnd{
    "foundation: contract violation: impl_.peek() != ((AscendingOrder<T, Cmp>) ? ::foundation::core::max_of<T> : "
    "::foundation::core::min_of<T>)"};
constexpr ::fixy::TextView kAdvanceBack{"foundation: contract violation: lattice_type::leq(impl_.peek(), new_value)"};
constexpr ::fixy::TextView kAtomicAdvanceBack{"foundation: contract violation: replaced < new_value"};
constexpr ::fixy::TextView kSoleWriterStepBack{"foundation: contract violation: Cmp{}(current, new_value)"};
constexpr ::fixy::TextView kBumpOverflow{
    "foundation: contract violation: ::foundation::decide::no_overflow_sum(observed, delta)"};

// The owner of the proof of the one writer of the proven publish.  The test
// holds one counter and its copy, so the owner makes the proof of the writer.
struct ProofOwner final {
    struct Role final {};
    using Proof = ::fixy::SoleWriterProof<ProofOwner, Role>;

    [[nodiscard]] static constexpr Proof proof() noexcept { return Proof{}; }
};

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
static_assert(std::is_same_v<MaxObserved<std::uint32_t>,
                             AtomicMonotonic<std::uint32_t, ::fixy::CounterWriters::many, std::less<std::uint32_t>>>);

// Only the cell of one writer reads its own value with relaxed order.  A
// cell of many writers has no such read, and its writers seed a
// compare-exchange with load_relaxed.
template <class Cell>
concept HasPeekRelaxed = requires(Cell const& cell) { cell.peek_relaxed(); };
template <class Cell>
concept HasLoadRelaxed = requires(Cell const& cell) { cell.load_relaxed(); };
static_assert(HasPeekRelaxed<AtomicMonotonic<std::uint64_t, ::fixy::CounterWriters::sole>>);
static_assert(!HasPeekRelaxed<AtomicMonotonic<std::uint64_t>>);
static_assert(HasLoadRelaxed<AtomicMonotonic<std::uint64_t>>);

// ── Diagnostic surface ───────────────────────────────────────────────
static_assert(fa::GradedWrapper<Monotonic<std::uint64_t>>);
static_assert(fa::is_graded_wrapper_v<Monotonic<std::uint64_t>>);
// AppendOnly's element value_type differs from the graded container's;
// the value_type_decoupled member the wrapper declares admits it.
static_assert(fa::DeclaresValueTypeDecoupled<AppendOnly<int>>);
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
static_assert(::fixy::BoundedMonotoneCarrier<std::uint8_t, 255, std::less<std::uint8_t>>);
static_assert(::fixy::BoundedMonotoneCarrier<std::int32_t, 10U, std::less<std::int32_t>>,
              "an unsigned bound that a signed carrier holds is admitted");
static_assert(!::fixy::BoundedMonotoneCarrier<std::uint8_t, 256, std::less<std::uint8_t>>,
              "a bound the carrier cannot hold would be truncated");
static_assert(!::fixy::BoundedMonotoneCarrier<std::uint32_t, -1, std::less<std::uint32_t>>,
              "a negative bound would become the maximum of an unsigned carrier");
static_assert(!::fixy::BoundedMonotoneCarrier<double, 5, std::less<double>>, "the bound is compared as an integer");
static_assert(!::fixy::BoundedMonotoneCarrier<bool, true, std::less<bool>>);
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

// No wrapper is assignable.  An assignment moves a counter backward,
// empties a set slot, or drops every element of a log, and the move
// constructor, which starts a new object, stays.
template <typename W>
inline constexpr bool is_movable_never_assignable =
    std::is_move_constructible_v<W> && !std::is_copy_assignable_v<W> && !std::is_move_assignable_v<W>;
static_assert(is_movable_never_assignable<AppendOnly<int>>);
static_assert(is_movable_never_assignable<OrderedAppendOnly<int>>);
static_assert(is_movable_never_assignable<Monotonic<std::uint32_t>>);
static_assert(is_movable_never_assignable<BoundedMonotonic<std::uint32_t, 8U>>);
static_assert(is_movable_never_assignable<WriteOnce<int>>);
static_assert(is_movable_never_assignable<WriteOnceNonNull<int*>>);

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
    if (!log.empty()) return 9;
    // emplace constructs in place and append copies; both grow the log
    // by one and neither offers a way to shrink it.
    log.emplace(1);
    log.append(2);
    log.append(3);
    // No erase is exposed, so the only way out of the log is reading it.
    if (log.empty()) return 15;
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
    auto floor = ::fixy::mint_monotonic<std::uint64_t, std::greater<std::uint64_t>>(100);
    if (!floor.try_advance(50)) return 28;
    if (floor.try_advance(60)) return 29;
    if (floor.get() != 50) return 30;

    // bump steps in the direction of the comparator, so under std::greater
    // it takes one away.  check_bounded_monotonic bumps a cell at the floor
    // of its carrier, which aborts in the bump of this cell.
    floor.bump();
    if (floor.get() != 49) return 31;
    return 0;
}

// Bumps a cell one time and gives its value after the step.
template <class Cell>
[[nodiscard]] auto value_after_bump(Cell cell) noexcept {
    cell.bump();
    return cell.get();
}

// A bounded counter of at most 10 that moves down.
[[nodiscard]] auto falling_counter_at(std::uint32_t start) noexcept {
    return ::fixy::mint_bounded_monotonic<std::uint32_t, 10U, std::greater<std::uint32_t>>(start);
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

    // Under std::greater, bump steps down, and the bound holds for each step.
    // A step past the floor of the carrier aborts.
    if (value_after_bump(falling_counter_at(10U)) != 9U) return 210;
    expect_dies(
        ::fixy::TestRunnerCtx{::foundation::effects::testing::test()},
        [] noexcept { (void)value_after_bump(falling_counter_at(0U)); }, kBumpAtTheEnd);

    // try_advance rejects rather than firing a contract. The two
    // rejections below have different causes.
    BoundedMonotonic<std::uint32_t, 5U> cnt = ::fixy::mint_bounded_monotonic<std::uint32_t, 5U>(2U);
    if (cnt.try_advance(6U)) return 54;  // over the bound
    if (cnt.get() != 2U) return 55;
    if (cnt.try_advance(1U)) return 56;  // backwards
    if (!cnt.try_advance(4U)) return 57;
    if (cnt.get() != 4U) return 58;

    // The back door moves the value backward, inside the bound.
    counter.reset_under_quiescence(4U);
    if (counter.get() != 4U) return 59;
    counter.reset_under_quiescence();
    if (counter.get() != 0U) return 49;
    counter.advance(10U);
    if (counter.get() != 10U) return 199;
    return 0;
}

int check_write_once() {
    WriteOnce<int> slot = ::fixy::mint_write_once<int>();
    if (slot.has_value()) return 60;
    if (static_cast<bool>(slot)) return 61;
    if (!slot.try_set(33)) return 62;
    if (!slot.has_value()) return 63;
    if (slot.get() != 33) return 64;
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
        AtomicMonotonic<std::uint64_t, ::fixy::CounterWriters::many, std::greater<std::uint64_t>> floor =
            ::fixy::mint_atomic_monotonic<std::uint64_t, ::fixy::CounterWriters::many, std::greater<std::uint64_t>>(
                100ULL);
        if (floor.get() != 100ULL) return 100;
        if (!floor.try_advance(50ULL)) return 101;
        if (floor.get() != 50ULL) return 102;
        if (floor.try_advance(50ULL)) return 103;  // equal
        if (floor.try_advance(60ULL)) return 104;  // backwards
        if (!floor.try_advance(0ULL)) return 105;
        if (floor.get() != 0ULL) return 106;

        // Under std::greater, bump walks the counter down instead.
        floor.reset_under_quiescence(1000ULL);
        if (floor.bump() != 1000ULL) return 118;
        if (floor.get() != 999ULL) return 119;
        if (floor.bump_by(99ULL) != 999ULL) return 120;
        if (floor.get() != 900ULL) return 121;
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

    // A single producer owns the head, so it may read its own value relaxed
    // and publish the successor with a release store. get() is the acquire
    // load a reader on another thread would use.
    {
        AtomicMonotonic<std::uint64_t, ::fixy::CounterWriters::sole> spsc_head =
            ::fixy::mint_atomic_monotonic<std::uint64_t, ::fixy::CounterWriters::sole>(0);
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

    // The order of each read is in its name: get() reads with acquire
    // order, and peek_relaxed() is the read of the one writer.  The one
    // writer of a counter publishes with a release store and no locked
    // instruction.
    {
        AtomicMonotonic<std::uint64_t, ::fixy::CounterWriters::sole> counter =
            ::fixy::mint_atomic_monotonic<std::uint64_t, ::fixy::CounterWriters::sole>(0);

        if (counter.get() != 0ULL) return 132;
        if (counter.peek_relaxed() != 0ULL) return 133;
        if (counter.load_relaxed() != 0ULL) return 134;

        counter.advance_sole_writer_from(0ULL, 7ULL);
        if (counter.get() != 7ULL) return 135;

        counter.advance_sole_writer_from(7ULL, 42ULL);
        if (counter.get() != 42ULL) return 136;

        counter.advance_sole_writer_from(42ULL, 43ULL);
        if (counter.get() != 43ULL) return 193;
    }

    // The checked publish of a writer that keeps a copy of the counter steps
    // the counter and the copy to the same value.
    {
        AtomicMonotonic<std::uint64_t, ::fixy::CounterWriters::sole> counter =
            ::fixy::mint_atomic_monotonic<std::uint64_t, ::fixy::CounterWriters::sole>(0);
        Monotonic<std::uint64_t> copy = ::fixy::mint_monotonic<std::uint64_t>(0);

        counter.advance_sole_writer_from(0ULL, 7ULL, copy);
        if (counter.get() != 7ULL || copy.get() != 7ULL) return 211;

        counter.advance_sole_writer_from(7ULL, 42ULL, copy);
        if (counter.get() != 42ULL || copy.get() != 42ULL) return 212;
    }

    // A failed compare-exchange writes the value it actually observed back
    // through `expected`, which is how the owner and a thief resolve their
    // race over the last element in a work-stealing deque.
    {
        AtomicMonotonic<std::uint64_t> top = ::fixy::mint_atomic_monotonic<std::uint64_t>(5);

        std::uint64_t observed = 5ULL;
        bool ok = top.compare_exchange_advance_acq_rel(observed, 6ULL);
        if (!ok) return 137;
        if (top.get() != 6ULL) return 138;

        observed = 5ULL;  // deliberately stale guess
        ok = top.compare_exchange_advance_acq_rel(observed, 7ULL);
        if (ok) return 139;
        if (observed != 6ULL) return 140;

        // Retrying with the value just reported back succeeds.
        ok = top.compare_exchange_advance_acq_rel(observed, 7ULL);
        if (!ok) return 141;
        if (top.get() != 7ULL) return 142;

        // The seq_cst form is what a work-stealing pop needs.  A stale
        // guess fails and reports the value too.
        observed = 7ULL;
        ok = top.compare_exchange_advance_seq_cst(observed, 8ULL);
        if (!ok) return 143;
        if (top.get() != 8ULL) return 144;
        observed = 7ULL;
        ok = top.compare_exchange_advance_seq_cst(observed, 9ULL);
        if (ok) return 194;
        if (observed != 8ULL) return 195;
    }

    // The relaxed weak form claims a ticket, as a producer of the MPSC ring
    // does: a loop on the reported value takes the next ticket.
    {
        AtomicMonotonic<std::uint64_t> head = ::fixy::mint_atomic_monotonic<std::uint64_t>(10);
        std::uint64_t observed = 9ULL;  // deliberately stale guess
        bool claimed = false;
        for (int retries = 0; retries < 16 && !claimed; ++retries) {
            claimed = head.compare_exchange_advance_weak_relaxed(observed, observed + 1);
        }
        if (!claimed) return 196;
        if (head.get() != 11ULL) return 197;
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
            advanced = seq.compare_exchange_advance_weak_acq_rel(observed, target);
        }
        if (!advanced) return 145;
        if (seq.get() != 101ULL) return 146;
    }

    // A work-stealing pop needs a bare seq_cst fence between the owner's
    // bottom store and its load of top. The fence sits on the counter type so
    // that it reads next to the two calls it separates.
    {
        AtomicMonotonic<std::uint64_t, ::fixy::CounterWriters::sole> bottom =
            ::fixy::mint_atomic_monotonic<std::uint64_t, ::fixy::CounterWriters::sole>(0);
        AtomicMonotonic<std::uint64_t> top = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);

        // One thread only. No race is exercised here, just the API shape.
        bottom.advance_sole_writer_from(0ULL, 5ULL);
        AtomicMonotonic<std::uint64_t>::fence_seq_cst();
        const auto t = top.load_relaxed();
        if (t != 0ULL) return 147;
        if (bottom.get() != 5ULL) return 148;
    }

    // Compare-exchange follows the same inverted direction under greater.
    {
        AtomicMonotonic<std::uint64_t, ::fixy::CounterWriters::many, std::greater<std::uint64_t>> ttl =
            ::fixy::mint_atomic_monotonic<std::uint64_t, ::fixy::CounterWriters::many, std::greater<std::uint64_t>>(
                1000ULL);

        std::uint64_t observed = 1000ULL;
        bool ok = ttl.compare_exchange_advance_acq_rel(observed, 999ULL);
        if (!ok) return 149;
        if (ttl.get() != 999ULL) return 150;

        observed = 1000ULL;  // deliberately stale guess
        ok = ttl.compare_exchange_advance_acq_rel(observed, 998ULL);
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

    // Many writers bump one counter.  Every step is one compare-exchange,
    // so no step is lost and each bump returns a slot no other bump
    // returned.
    {
        constexpr int kThreads = 4;
        constexpr std::uint64_t kPerThread = 4096;

        AtomicMonotonic<std::uint64_t> shared_count = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);
        std::atomic<std::uint64_t> slot_sum{0};
        std::latch start_gate{kThreads + 1};
        {
            std::inplace_vector<std::jthread, kThreads> workers;
            for (int tid = 0; tid < kThreads; ++tid) {
                workers.emplace_back([&shared_count, &slot_sum, &start_gate] {
                    start_gate.arrive_and_wait();
                    std::uint64_t local_sum = 0;
                    for (std::uint64_t i = 0; i < kPerThread; ++i)
                        local_sum += shared_count.bump();
                    slot_sum.fetch_add(local_sum, std::memory_order_relaxed);
                });
            }
            start_gate.arrive_and_wait();
        }
        constexpr std::uint64_t kTotal = kThreads * kPerThread;
        if (shared_count.get() != kTotal) return 156;
        // The slots handed out are 0 through kTotal - 1, each one time.
        if (slot_sum.load(std::memory_order_relaxed) != kTotal * (kTotal - 1) / 2) return 157;
    }
    return 0;
}

// ── Contracts fire at runtime ────────────────────────────────────────
//
// Each refused call runs in a child through expect_dies of the Check family,
// so the wrapper in the test process keeps its value.

int check_contracts_abort() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    Monotonic<std::uint32_t> mono = ::fixy::mint_monotonic<std::uint32_t>(10u);
    expect_dies(test, [&mono] noexcept { mono.advance(5u); }, kAdvanceBack);
    if (mono.get() != 10u) return 161;

    Monotonic<std::uint8_t> at_max = ::fixy::mint_monotonic<std::uint8_t>(255);
    expect_dies(test, [&at_max] noexcept { at_max.bump(); }, kBumpAtTheEnd);

    BoundedMonotonic<std::uint32_t, 8U> bounded = ::fixy::mint_bounded_monotonic<std::uint32_t, 8U>(8U);
    expect_dies(
        test, [&bounded] noexcept { bounded.advance(9U); },
        ::fixy::TextView{"foundation: contract violation: std::cmp_less_equal(new_value, Max)"});
    expect_dies(
        test, [&bounded] noexcept { bounded.bump(); },
        ::fixy::TextView{
            "foundation: contract violation: (DescendingOrder<T, Cmp>) || std::cmp_less(inner_.get(), Max)"});
    expect_dies(test, [&bounded] noexcept { bounded.advance(7U); }, kAdvanceBack);
    if (bounded.get() != 8U) return 166;

    std::uint32_t over = 9U;
    expect_dies(
        test, [&over] noexcept { (void)::fixy::mint_bounded_monotonic<std::uint32_t, 8U>(over); },
        ::fixy::TextView{"foundation: contract violation: std::cmp_less_equal(initial, Max)"});
    // A reset over the bound aborts.
    expect_dies(
        test, [&bounded, &over] noexcept { bounded.reset_under_quiescence(over); },
        ::fixy::TextView{"foundation: contract violation: std::cmp_less_equal(value, Max)"});
    if (bounded.get() != 8U) return 201;

    WriteOnce<int> once = ::fixy::mint_write_once<int>();
    expect_dies(
        test, [&once] noexcept { (void)once.get(); },
        ::fixy::TextView{"foundation: contract violation: value_.has_value()"});
    once.set(1);
    expect_dies(
        test, [&once] noexcept { once.set(2); },
        ::fixy::TextView{"foundation: contract violation: !value_.has_value()"});
    if (once.get() != 1) return 170;

    int target = 0;
    WriteOnceNonNull<int*> slot = ::fixy::mint_write_once_non_null<int*>();
    expect_dies(
        test, [&slot] noexcept { slot.set(nullptr); },
        ::fixy::TextView{"foundation: contract violation: p != nullptr"});
    expect_dies(
        test, [&slot] noexcept { (void)slot.get(); },
        ::fixy::TextView{"foundation: contract violation: ptr_ != nullptr"});
    slot.set(&target);
    expect_dies(
        test, [&slot, &target] noexcept { slot.set(&target); },
        ::fixy::TextView{"foundation: contract violation: ptr_ == nullptr"});

    OrderedAppendOnly<int> ordered = ::fixy::mint_ordered_append_only<int>();
    ordered.append(5);
    expect_dies(
        test, [&ordered] noexcept { ordered.append(3); },
        ::fixy::TextView{"foundation: contract violation: inner_.empty() || !cmp_(key_(item), key_(inner_.back()))"});
    if (ordered.size() != 1) return 175;

    AtomicMonotonic<std::uint64_t> atomic = ::fixy::mint_atomic_monotonic<std::uint64_t>(10);
    expect_dies(test, [&atomic] noexcept { atomic.advance(9); }, kAtomicAdvanceBack);
    expect_dies(test, [&atomic] noexcept { atomic.advance(10); }, kAtomicAdvanceBack);
    if (atomic.get() != 10) return 178;

    // The step from a held value refuses a step back, a step that stands
    // still, and a step that wraps past the end of the carrier.  A held
    // value that is not the stored one is refused where the Debug check is
    // armed, and every test binary arms it.
    AtomicMonotonic<std::uint64_t, ::fixy::CounterWriters::sole> sole =
        ::fixy::mint_atomic_monotonic<std::uint64_t, ::fixy::CounterWriters::sole>(10);
    expect_dies(test, [&sole] noexcept { sole.advance_sole_writer_from(10, 9); }, kSoleWriterStepBack);
    expect_dies(test, [&sole] noexcept { sole.advance_sole_writer_from(10, 10); }, kSoleWriterStepBack);
    AtomicMonotonic<std::uint8_t, ::fixy::CounterWriters::sole> at_top =
        ::fixy::mint_atomic_monotonic<std::uint8_t, ::fixy::CounterWriters::sole>(std::uint8_t{255});
    expect_dies(
        test, [&at_top] noexcept { at_top.advance_sole_writer_from(std::uint8_t{255}, std::uint8_t{0}); },
        kSoleWriterStepBack);
    expect_dies(
        test, [&sole] noexcept { sole.advance_sole_writer_from(7, 11); },
        ::fixy::TextView{"foundation: fatal invariant failed: !Cmp{}(current, stored) && !Cmp{}(stored, current)"});
    if (sole.get() != 10 || at_top.get() != 255) return 198;

    // The publish that steps a copy refuses the same steps, before it moves
    // the counter or the copy.
    Monotonic<std::uint64_t> sole_copy = ::fixy::mint_monotonic<std::uint64_t>(10);
    expect_dies(
        test, [&sole, &sole_copy] noexcept { sole.advance_sole_writer_from(10, 9, sole_copy); }, kSoleWriterStepBack);
    expect_dies(
        test, [&sole, &sole_copy] noexcept { sole.advance_sole_writer_from(7, 11, sole_copy); },
        ::fixy::TextView{"foundation: fatal invariant failed: !Cmp{}(current, stored) && !Cmp{}(stored, current)"});
    if (sole.get() != 10 || sole_copy.get() != 10) return 213;

    // The proven publish refuses a step back and a step that stands still,
    // before it moves the counter or the copy.  The step of the copy folds
    // into this one check.
    constexpr ::fixy::TextView kProvenStepBack{"foundation: fatal invariant failed: Cmp{}(current, new_value)"};
    expect_dies(
        test, [&sole, &sole_copy] noexcept { sole.advance_sole_writer_by_proof(ProofOwner::proof(), sole_copy, 9); },
        kProvenStepBack);
    expect_dies(
        test, [&sole, &sole_copy] noexcept { sole.advance_sole_writer_by_proof(ProofOwner::proof(), sole_copy, 10); },
        kProvenStepBack);
    if (sole.get() != 10 || sole_copy.get() != 10) return 214;
    sole.advance_sole_writer_by_proof(ProofOwner::proof(), sole_copy, 11);
    if (sole.get() != 11 || sole_copy.get() != 11) return 215;

    AtomicMonotonic<std::int64_t> signed_counter = ::fixy::mint_atomic_monotonic<std::int64_t>(0);
    expect_dies(
        test, [&signed_counter] noexcept { (void)signed_counter.bump_by(-1); },
        ::fixy::TextView{"foundation: contract violation: ::foundation::decide::non_negative(delta)"});

    // A bump past the end of the carrier would wrap to the other end, so
    // it aborts.  The same holds for a decreasing counter at its floor.
    AtomicMonotonic<std::uint8_t> near_top = ::fixy::mint_atomic_monotonic<std::uint8_t>(std::uint8_t{250});
    expect_dies(test, [&near_top] noexcept { (void)near_top.bump_by(std::uint8_t{6}); }, kBumpOverflow);
    if (near_top.get() != 250) return 187;
    if (near_top.bump_by(std::uint8_t{5}) != 250 || near_top.get() != 255) return 188;
    expect_dies(test, [&near_top] noexcept { (void)near_top.bump(); }, kBumpOverflow);
    AtomicMonotonic<std::uint8_t, ::fixy::CounterWriters::many, std::greater<std::uint8_t>> near_floor =
        ::fixy::mint_atomic_monotonic<std::uint8_t, ::fixy::CounterWriters::many, std::greater<std::uint8_t>>(
            std::uint8_t{3});
    expect_dies(
        test, [&near_floor] noexcept { (void)near_floor.bump_by(std::uint8_t{4}); },
        ::fixy::TextView{
            "foundation: contract violation: observed >= static_cast<T>(std::numeric_limits<T>::min() + delta)"});
    if (near_floor.get() != 3) return 191;
    if (near_floor.bump_by(std::uint8_t{3}) != 3 || near_floor.get() != 0) return 192;

    std::uint64_t expected = 10;
    expect_dies(
        test, [&atomic, &expected] noexcept { (void)atomic.compare_exchange_advance_acq_rel(expected, 9); },
        ::fixy::TextView{"foundation: contract violation: Cmp{}(expected, desired)"});

    // A satisfied contract is silent.
    mono.advance(11u);
    if (mono.get() != 11u) return 182;
    return 0;
}

}  // namespace

int main() {
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

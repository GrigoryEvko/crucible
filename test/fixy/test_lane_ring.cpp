// What fixy/concurrent/LaneRing.h claims, checked.
//
// The walks on one thread check the order of the header: the turn over the
// lanes, the wait on an open lane while other lanes hold items, a full lane,
// the end of a lane, the end of the stream and the drain.  A walk checks the
// room: the publish of the tail of a lane at each tail_interval pops, and of
// each lane before an Empty answer.  The threaded case runs four producers and
// the consumer on threads of their own, under several schedules: a delay of a
// Philox stream on each thread, of a seed of its own in each schedule.  The
// lanes have different lengths, one lane holds no item, and a lane holds 16
// items, so the producers wait for room, and the consumer defers the publish
// of each tail for four pops.  Each merged stream must equal the oracle of the
// order, item for item.  The oracle reads only the count of the items of each
// lane.
//
// Controls
//   The same producers push into an MPSC ring under the same schedules.  Its
//   stream follows the schedule, so the streams of the schedules must differ.
//   The schedules then differ, and the equal streams of the lanes are no
//   accident of one schedule.  A planted swap of two items must make the
//   compare with the oracle fail.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/EventCount.h>
#include <fixy/concurrent/LaneRing.h>
#include <fixy/concurrent/MpscRing.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>

#include "../foundation/philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace cc = ::fixy::concurrent;
namespace eff = ::foundation::effects;

using Item = std::uint64_t;
using WaitPolicy = cc::SpinThenPark<2000>;

// The tag of a snapshot of a ring and of a pop of the MPSC ring.  The test
// context holds Nondet.
constexpr eff::cap::Nondet kNondet = eff::testing::test().nondet;

// The owner of each ring of lanes of the test.  It mints the proof of a lane
// and the proof of the consumer (the writers of fixy/concurrent/LaneRing.h).
// A walk holds the producer of each lane and the consumer on one thread, and
// a threaded run gives each lane to the thread of its producer.
struct LanesOwner final {
    template <class Ring>
    [[nodiscard]] static typename Ring::LaneProof lane(Ring const& /*ring*/, typename Ring::lane_index lane) noexcept {
        return typename Ring::LaneProof{lane};
    }

    template <class Ring>
    [[nodiscard]] static typename Ring::ConsumerProof consumer(Ring const& /*ring*/) noexcept {
        return typename Ring::ConsumerProof{};
    }
};

// The codes of a receive that gives no item.  No push gives either code.
constexpr Item kEmpty = ~Item{0};
constexpr Item kClosed = ~Item{0} - 1;

// The item of a receive, or the code of its end.
template <class Ring>
[[nodiscard]] Item receive_code(Ring& ring) noexcept {
    return ring.try_receive(LanesOwner::consumer(ring))
        .match([](Item item) noexcept { return item; },
               [](cc::NoItem why) noexcept { return why == cc::NoItem::Closed ? kClosed : kEmpty; });
}

// Takes one receive for each code of expected, in order, and ends the test at
// the first receive that gives another code.
template <class Ring, std::size_t N>
void expect_receives(Ring& ring, ::fixy::FixedArray<Item, N> const& expected) noexcept {
    for (Item const code : expected) {
        Item const got = receive_code(ring);
        if (got != code) ::fixy::fatal("a receive gave {} where the order of the lanes owes {}", got, code);
    }
}

// ── The walks on one thread ─────────────────────────────────────────────

using Small = cc::LaneRing<Item, 4, cc::LaneCount{3}, LanesOwner>;
constexpr Small::lane_index kFirst{::fixy::index<0>()};
constexpr Small::lane_index kSecond{::fixy::index<1>()};
constexpr Small::lane_index kThird{::fixy::index<2>()};

// The pushes of a walk.  A walk pushes below the capacity of each lane, and a
// push that the ring refuses ends the test.
template <std::size_t N>
void push_each(Small& ring, Small::lane_index lane, ::fixy::FixedArray<Item, N> const& items) noexcept {
    for (Item const item : items) {
        if (!ring.try_push(LanesOwner::lane(ring, lane), item))
            ::fixy::fatal("a lane refused item {} below its capacity", item);
    }
}

// The turn starts at lane 0 and takes one item of each lane in turn.  While
// the lane of the turn is empty and open, a receive gives Empty, although the
// other lanes hold items.  A full lane refuses a push.
void walk_turn_wait_and_full_lane(::fixy::TestRunnerCtx const& test) noexcept {
    Small ring{};
    CRUCIBLE_TEST_ASSERT(test, ring.empty_approx(kNondet) && ring.size_approx(kNondet) == 0);
    push_each(ring, kThird, ::fixy::FixedArray<Item, 1>{301});
    push_each(ring, kSecond, ::fixy::FixedArray<Item, 1>{201});
    expect_receives(ring, ::fixy::FixedArray<Item, 2>{kEmpty, kEmpty});
    push_each(ring, kFirst, ::fixy::FixedArray<Item, 2>{101, 102});
    CRUCIBLE_TEST_ASSERT(test, ring.size_approx(kNondet) == 4);
    expect_receives(ring, ::fixy::FixedArray<Item, 5>{101, 201, 301, 102, kEmpty});

    push_each(ring, kSecond, ::fixy::FixedArray<Item, 4>{202, 203, 204, 205});
    CRUCIBLE_TEST_ASSERT(test, !ring.try_push(LanesOwner::lane(ring, kSecond), 206));
    expect_receives(ring, ::fixy::FixedArray<Item, 2>{202, kEmpty});
}

// An ended lane leaves the turn after its last item, and the turn goes on
// over the other lanes in the order of their index.  When each lane ended and
// gave its last item, the stream ended.
void walk_end_of_lanes(::fixy::TestRunnerCtx const& test) noexcept {
    Small ring{};
    push_each(ring, kFirst, ::fixy::FixedArray<Item, 1>{101});
    push_each(ring, kSecond, ::fixy::FixedArray<Item, 3>{201, 202, 203});
    push_each(ring, kThird, ::fixy::FixedArray<Item, 1>{301});
    ring.end_lane(LanesOwner::lane(ring, kThird));
    expect_receives(ring, ::fixy::FixedArray<Item, 4>{101, 201, 301, kEmpty});

    // The third lane gave its last item, so after it leaves the turn, the turn
    // goes from the second lane to the first.
    push_each(ring, kFirst, ::fixy::FixedArray<Item, 2>{102, 103});
    expect_receives(ring, ::fixy::FixedArray<Item, 5>{102, 202, 103, 203, kEmpty});
    ring.end_lane(LanesOwner::lane(ring, kFirst));
    push_each(ring, kSecond, ::fixy::FixedArray<Item, 1>{204});
    expect_receives(ring, ::fixy::FixedArray<Item, 2>{204, kEmpty});
    ring.end_lane(LanesOwner::lane(ring, kSecond));
    expect_receives(ring, ::fixy::FixedArray<Item, 2>{kClosed, kClosed});
    CRUCIBLE_TEST_ASSERT(test, ring.try_pop(LanesOwner::consumer(ring)).is_none());
}

// The drain gives the items in the order of the turn, with each empty lane
// taken as ended, and then opens each lane again with the turn at lane 0.
void walk_drain(::fixy::TestRunnerCtx const& test) noexcept {
    Small ring{};
    push_each(ring, kSecond, ::fixy::FixedArray<Item, 1>{201});
    push_each(ring, kFirst, ::fixy::FixedArray<Item, 1>{101});
    expect_receives(ring, ::fixy::FixedArray<Item, 3>{101, 201, kEmpty});

    push_each(ring, kThird, ::fixy::FixedArray<Item, 1>{301});
    push_each(ring, kFirst, ::fixy::FixedArray<Item, 2>{102, 103});
    push_each(ring, kSecond, ::fixy::FixedArray<Item, 1>{202});
    ::fixy::FixedList<Item, 8> drained{};
    ring.drain(LanesOwner::consumer(ring), [&drained](Item item) noexcept {
        static_cast<void>(drained.append(item).expect("the walk drains at most eight items"));
    });
    ::fixy::FixedArray<Item, 4> const order{301, 102, 202, 103};
    CRUCIBLE_TEST_ASSERT(test, drained.size() == order.size());
    for (::fixy::Index<4> const position : order.indices()) {
        CRUCIBLE_TEST_ASSERT(test,
                             *drained.get(position.raw()).expect("the drain gave four items") == order.at(position));
    }
    CRUCIBLE_TEST_ASSERT(test, ring.empty_approx(kNondet));

    // Each lane is open again, and the turn starts at lane 0.
    expect_receives(ring, ::fixy::FixedArray<Item, 1>{kEmpty});
    push_each(ring, kThird, ::fixy::FixedArray<Item, 1>{302});
    push_each(ring, kFirst, ::fixy::FixedArray<Item, 1>{104});
    push_each(ring, kSecond, ::fixy::FixedArray<Item, 1>{203});
    expect_receives(ring, ::fixy::FixedArray<Item, 4>{104, 203, 302, kEmpty});
}

// A ring of two lanes of 16 cells, which publishes the tail of a lane at each
// four deferred pops (the room of the header).
using Wide = cc::LaneRing<Item, 16, cc::LaneCount{2}, LanesOwner>;
static_assert(Wide::tail_interval == 4);
static_assert(Small::tail_interval == 1);
constexpr Wide::lane_index kWideFirst{::fixy::index<0>()};
constexpr Wide::lane_index kWideSecond{::fixy::index<1>()};

// Fills lane of a wide ring: items from first, until the lane refuses one.
// Gives the count of the items that went in.
[[nodiscard]] std::uint64_t fill_lane(Wide& ring, Wide::lane_index lane, Item first) noexcept {
    std::uint64_t count = 0;
    while (ring.try_push(LanesOwner::lane(ring, lane), first + count))
        ++count;
    return count;
}

// The producer of a full lane sees the cells of the deferred pops of its lane
// at each tail_interval pops of the lane, and at the next Empty answer, also
// when the turn waits at another lane.  A snapshot after an Empty answer counts
// only the items that the lanes hold.
void walk_room_after_empty(::fixy::TestRunnerCtx const& test) noexcept {
    Wide ring{};
    CRUCIBLE_TEST_ASSERT(test, fill_lane(ring, kWideFirst, 100) == 16);
    // One pop of the first lane, and the second lane is empty and open, so
    // the turn waits there.  The Empty answer publishes the tail of the first
    // lane, and its producer then takes the free cell.
    expect_receives(ring, ::fixy::FixedArray<Item, 2>{100, kEmpty});
    CRUCIBLE_TEST_ASSERT(test, ring.size_approx(kNondet) == 15);
    CRUCIBLE_TEST_ASSERT(test, fill_lane(ring, kWideFirst, 116) == 1);

    // The turn stays at the second lane.  With both lanes full, no receive
    // gives an Empty answer.  Three pops of a lane publish no tail, and the
    // fourth publishes four cells.
    CRUCIBLE_TEST_ASSERT(test, fill_lane(ring, kWideSecond, 200) == 16);
    expect_receives(ring, ::fixy::FixedArray<Item, 6>{200, 101, 201, 102, 202, 103});
    CRUCIBLE_TEST_ASSERT(test, fill_lane(ring, kWideFirst, 117) == 0);
    CRUCIBLE_TEST_ASSERT(test, fill_lane(ring, kWideSecond, 216) == 0);
    expect_receives(ring, ::fixy::FixedArray<Item, 2>{203, 104});
    CRUCIBLE_TEST_ASSERT(test, fill_lane(ring, kWideFirst, 117) == 4);
    CRUCIBLE_TEST_ASSERT(test, fill_lane(ring, kWideSecond, 216) == 4);

    // The end of each lane: the ring gives the 32 items that the lanes hold,
    // and each lane publishes its tail when it leaves the turn.
    ring.end_lane(LanesOwner::lane(ring, kWideFirst));
    ring.end_lane(LanesOwner::lane(ring, kWideSecond));
    std::uint64_t received = 0;
    Item code = receive_code(ring);
    while (code != kClosed && code != kEmpty) {
        ++received;
        code = receive_code(ring);
    }
    CRUCIBLE_TEST_ASSERT(test, code == kClosed && received == 32);
    CRUCIBLE_TEST_ASSERT(test, ring.empty_approx(kNondet) && ring.size_approx(kNondet) == 0);
}

// A lane ends one time in a stream, and a second end ends the process.
void second_end() noexcept {
    Small ring{};
    ring.end_lane(LanesOwner::lane(ring, kSecond));
    ring.end_lane(LanesOwner::lane(ring, kSecond));
}

// ── The threaded case ───────────────────────────────────────────────────

constexpr cc::LaneCount kLanes{4};
constexpr std::size_t kLaneCapacity = 16;
using Lanes = cc::LaneRing<Item, kLaneCapacity, kLanes, LanesOwner>;
using LaneIndex = Lanes::lane_index;
// The consumer publishes the tail of a lane at each four pops of the lane, so
// the runs take the deferred pops of the room.
static_assert(Lanes::tail_interval == 4);

// The count of the items of each lane: lanes of different lengths, a lane
// with no item, and a lane that ends early.
constexpr ::fixy::FixedArray<std::uint64_t, 4> kLengths{3000, 1000, 0, 2500};
constexpr std::size_t kTotal = 6500;
using Stream = ::fixy::FixedArray<Item, kTotal>;

// The item of a lane at a place: the lane in the high word, the place in the
// low word.
[[nodiscard]] constexpr Item item_at(LaneIndex lane, std::uint64_t place) noexcept {
    return (Item{lane.raw()} << 32) | place;
}

// The stream that the order gives for kLengths: in each round, the next item
// of each lane that holds one, in the order of the lanes.  Complexity: linear
// in the rounds times the lanes.
[[nodiscard]] Stream oracle_stream() noexcept {
    Stream stream{};
    std::size_t count = 0;
    std::uint64_t rounds = 0;
    for (std::uint64_t const length : kLengths)
        rounds = length > rounds ? length : rounds;
    for (std::uint64_t round = 0; round < rounds; ++round) {
        for (LaneIndex const lane : ::fixy::IndexRange<4>{}) {
            if (round >= kLengths.at(lane)) continue;
            Item const item = item_at(lane, round);
            stream.with_element(::fixy::Index<kTotal>::check(count).expect("the lanes hold kTotal items"),
                                [item](Item& held) noexcept { held = item; });
            ++count;
        }
    }
    return stream;
}

// The first place where two streams differ, or no value when they are equal.
// Complexity: linear in kTotal.
[[nodiscard]] ::fixy::Option<std::size_t> first_difference(Stream const& got, Stream const& expected) noexcept {
    for (::fixy::Index<kTotal> const place : got.indices()) {
        if (got.at(place) != expected.at(place)) return ::fixy::some(place.raw());
    }
    return ::fixy::none;
}

// A fold of the bits of a stream, so a test compares streams by one word.
// Complexity: linear in kTotal.
[[nodiscard]] std::uint64_t fold_of(Stream const& stream) noexcept {
    std::uint64_t fold = 14695981039346656037ull;
    for (Item const item : stream)
        fold = (fold ^ item) * 1099511628211ull;
    return fold;
}

// How the threads of a run wait between their ops.
enum class DelayShape : std::uint8_t {
    none,
    random,
};

// One schedule: the seed of the delays of its threads, and their shape.
struct Schedule {
    std::uint64_t seed = 0;
    DelayShape shape = DelayShape::none;
};

// The delay of a thread before an op: short for most ops and long for some,
// so the other threads run ahead, and the lanes fill or drain.
void delay(::crucible::test::PhiloxStream& delays, DelayShape shape) noexcept {
    if (shape == DelayShape::none) return;
    std::uint64_t const draw = delays.below(100);
    std::uint64_t const turns = draw < 90 ? delays.below(200) : 2000 + delays.below(10000);
    for (std::uint64_t turn = 0; turn < turns; ++turn) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// The state of one threaded run: the ring, the two event counts, and the
// stream that the consumer received.  The consumer is the one writer of the
// stream and of its count, and the join orders them before the reads of main.
template <class Ring>
struct Run {
    Ring ring{};
    cc::EventCount items{};
    cc::EventCount room{};
    ::fixy::Atomic<std::uint32_t> finished{0};
    Stream got{};
    std::size_t count = 0;
    bool has_overflow = false;

    // The consumer keeps an item.  A ring that gives more than kTotal items
    // is wrong, and the run records that.
    void keep(Item item) noexcept {
        for (auto const slot : ::fixy::Index<kTotal>::check(count)) {
            got.with_element(slot, [item](Item& held) noexcept { held = item; });
            ++count;
            return;
        }
        has_overflow = true;
    }
};

// The owner of the MPSC ring of the control.  It makes the proof of the one
// consumer (the writers of fixy/concurrent/MpscRing.h), which pops on one
// thread.
struct MergeOwner final {
    [[nodiscard]] static constexpr ::fixy::SoleWriterProof<MergeOwner, cc::MpscConsumerRole> consumer() noexcept {
        return {};
    }
};

using LaneRun = Run<Lanes>;
using MergeRun = Run<cc::MpscRing<Item, 16, MergeOwner>>;

// True for the ring of lanes, and false for the MPSC ring of the control.
template <class Ring>
inline constexpr bool kIsLanes = false;
template <>
inline constexpr bool kIsLanes<Lanes> = true;

// The body of the producer of a lane.  It pushes the items of its lane, waits
// for room on a full lane, and wakes the consumer after each push.  A
// producer of the ring of lanes then ends its lane.  A producer of the MPSC
// ring counts its end.
template <class Ring>
[[nodiscard]] auto make_producer(Run<Ring>& run, LaneIndex lane, Schedule schedule) noexcept {
    return [&run, lane, schedule](::fixy::BgLoadCtx const& ctx) noexcept {
        ::crucible::test::PhiloxStream delays{schedule.seed * 16 + lane.raw()};
        for (std::uint64_t place = 0; place < kLengths.at(lane); ++place) {
            delay(delays, schedule.shape);
            Item const item = item_at(lane, place);
            cc::wait_for<WaitPolicy>(ctx, run.room, [&run, lane, item]() noexcept {
                if constexpr (kIsLanes<Ring>) {
                    return run.ring.try_push(LanesOwner::lane(run.ring, lane), item);
                } else {
                    return run.ring.try_push(item);
                }
            });
            run.items.notify_one(ctx);
        }
        if constexpr (kIsLanes<Ring>) run.ring.end_lane(LanesOwner::lane(run.ring, lane));
        static_cast<void>(run.finished.fetch_add_acq_rel(1));
        run.items.notify_one(ctx);
    };
}

// The next code of the consumer: an item, or kClosed at the end of the stream.
// The ring of lanes tells its end.  The MPSC ring ends when each producer
// ended and the ring is empty.
template <class Ring>
[[nodiscard]] Item next_code(Run<Ring>& run) noexcept {
    if constexpr (kIsLanes<Ring>) {
        return receive_code(run.ring);
    } else {
        bool const has_ended = run.finished.load_acquire() == 4;
        ::fixy::Option<Item> item = run.ring.try_pop(MergeOwner::consumer(), kNondet);
        if (item.is_some()) return std::move(item).expect("the test above found an item");
        return has_ended ? kClosed : kEmpty;
    }
}

// The body of the consumer.  It receives until the end of the stream, waits
// while it gets no item, and wakes each producer after each receive.  The
// first Empty answer after an item publishes the tail of each lane (the room
// of fixy/concurrent/LaneRing.h), so it wakes each producer too.
template <class Ring>
[[nodiscard]] auto make_consumer(Run<Ring>& run, Schedule schedule) noexcept {
    return [&run, schedule](::fixy::BgLoadCtx const& ctx) noexcept {
        ::crucible::test::PhiloxStream delays{schedule.seed * 16 + 15};
        for (;;) {
            Item code = kEmpty;
            bool is_first_empty = true;
            cc::wait_for<WaitPolicy>(ctx, run.items, [&run, &code, &ctx, &is_first_empty]() noexcept {
                code = next_code(run);
                if (code != kEmpty) return true;
                if (is_first_empty) run.room.notify_all(ctx);
                is_first_empty = false;
                return false;
            });
            run.room.notify_all(ctx);
            if (code == kClosed) return;
            run.keep(code);
            delay(delays, schedule.shape);
        }
    };
}

// One threaded run: a producer for each lane and the consumer, each on a
// thread of its own.
template <class Ring>
void run_threads(::fixy::BgLoadCtx const& ctx, Run<Ring>& run, Schedule schedule) noexcept {
    auto first = make_producer(run, LaneIndex{::fixy::index<0>()}, schedule);
    auto second = make_producer(run, LaneIndex{::fixy::index<1>()}, schedule);
    auto third = make_producer(run, LaneIndex{::fixy::index<2>()}, schedule);
    auto fourth = make_producer(run, LaneIndex{::fixy::index<3>()}, schedule);
    auto consumer = make_consumer(run, schedule);
    ::fixy::thread::run_joined_raw_probe(ctx, ::fixy::TestRunnerCtx{eff::testing::test()},
                                         "the bare ring under test is the state of the run",
                                         ::fixy::thread::ThreadOptions{}, std::move(first), std::move(second),
                                         std::move(third), std::move(fourth), std::move(consumer))
        .expect("the thread door starts the producers and the consumer");
}

// True when the items of each lane come in the order of their places in a
// stream, and each lane gives each of its items.  Complexity: linear in
// kTotal.
[[nodiscard]] bool keeps_each_lane_in_order(Stream const& stream) noexcept {
    ::fixy::FixedArray<std::uint64_t, 4> next{};
    for (Item const item : stream) {
        for (LaneIndex const lane : LaneIndex::check(item >> 32)) {
            if ((item & 0xFFFFFFFFu) != next.at(lane)) return false;
            next.with_element(lane, [](std::uint64_t& held) noexcept { ++held; });
        }
    }
    for (LaneIndex const lane : ::fixy::IndexRange<4>{}) {
        if (next.at(lane) != kLengths.at(lane)) return false;
    }
    return true;
}

constexpr ::fixy::FixedArray<Schedule, 5> kSchedules{
    Schedule{.seed = 1, .shape = DelayShape::none},   Schedule{.seed = 2, .shape = DelayShape::random},
    Schedule{.seed = 3, .shape = DelayShape::random}, Schedule{.seed = 4, .shape = DelayShape::random},
    Schedule{.seed = 5, .shape = DelayShape::random},
};

// Each schedule gives the stream of the oracle through the ring of lanes, and
// the MPSC ring gives at least two different streams over the schedules.
void threaded_streams_equal_the_oracle(::fixy::TestRunnerCtx const& test, ::fixy::BgLoadCtx const& ctx) noexcept {
    Stream const oracle = oracle_stream();
    CRUCIBLE_TEST_ASSERT(test, keeps_each_lane_in_order(oracle));

    // The control of the compare: a planted swap of two items of two lanes.
    Stream swapped = oracle;
    Item const kept = swapped.at(::fixy::index<100>());
    Item const moved = swapped.at(::fixy::index<101>());
    swapped.with_element(::fixy::index<100>(), [moved](Item& held) noexcept { held = moved; });
    swapped.with_element(::fixy::index<101>(), [kept](Item& held) noexcept { held = kept; });
    CRUCIBLE_TEST_ASSERT(test, first_difference(swapped, oracle).value_or(kTotal) == 100);

    ::fixy::FixedArray<std::uint64_t, 5> merge_folds{};
    for (::fixy::Index<5> const index : kSchedules.indices()) {
        Schedule const schedule = kSchedules.at(index);
        {
            LaneRun run{};
            run_threads(ctx, run, schedule);
            CRUCIBLE_TEST_ASSERT(test, !run.has_overflow && run.count == kTotal);
            for (std::size_t const place : first_difference(run.got, oracle)) {
                ::fixy::fatal("the ring of lanes under schedule {} gave {} at place {}, where the order owes {}",
                              schedule.seed, *run.got.get(place).expect("a place of the stream"), place,
                              *oracle.get(place).expect("a place of the stream"));
            }
        }
        {
            MergeRun run{};
            run_threads(ctx, run, schedule);
            CRUCIBLE_TEST_ASSERT(test, !run.has_overflow && run.count == kTotal);
            CRUCIBLE_TEST_ASSERT(test, keeps_each_lane_in_order(run.got));
            std::uint64_t const fold = fold_of(run.got);
            merge_folds.with_element(index, [fold](std::uint64_t& held) noexcept { held = fold; });
        }
    }
    std::size_t distinct = 0;
    for (::fixy::Index<5> const index : merge_folds.indices()) {
        bool is_new = true;
        for (::fixy::Index<5> const before : merge_folds.indices()) {
            if (before < index && merge_folds.at(before) == merge_folds.at(index)) is_new = false;
        }
        distinct += is_new ? 1 : 0;
    }
    CRUCIBLE_TEST_ASSERT(test, distinct >= 2);
    ::fixy::check::pass(test, "  the MPSC ring gave {} different streams over the {} schedules\n", distinct,
                        kSchedules.size());
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{eff::testing::test()};
    ::fixy::BgLoadCtx const ctx{eff::testing::bg()};
    walk_turn_wait_and_full_lane(test);
    walk_end_of_lanes(test);
    walk_drain(test);
    walk_room_after_empty(test);
    ::fixy::check::expect_dies(test, second_end, ::fixy::TextView{"end.load_acquire() == LaneEnd::open"});
    threaded_streams_equal_the_oracle(test, ctx);
    ::fixy::check::pass(test, "test_lane_ring: PASSED\n");
    return 0;
}

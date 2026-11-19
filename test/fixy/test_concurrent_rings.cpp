// What fixy/concurrent/SpscRing.h and fixy/concurrent/MpscRing.h claim,
// checked.
//
// Both rings are lock-free and single-consumer, and both make their
// correctness argument out of memory ordering rather than exclusion, so
// a single-threaded walk can only check the index arithmetic: the gates,
// the wrap, the batch clamps.  The threaded half is what checks the
// ordering claim, and it is the half that means anything under TSan.

#include <fixy/Core.h>
#include <fixy/concurrent/MpscRing.h>
#include <fixy/concurrent/SpscRing.h>
#include <fixy/os/OwnedThread.h>
#include <foundation/effects/Ctx.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace c = fixy::concurrent;

namespace {

// The tag of a snapshot of a ring and of a pop of an MPSC ring.  The test
// context holds Nondet.
constexpr ::foundation::effects::cap::Nondet kNondet = ::foundation::effects::testing::test().nondet;

// ── What a cell may hold ─────────────────────────────────────────────

struct Plain {
    int value = 0;
    std::uint32_t producer = 0;
};

struct OwnsSomething {
    OwnsSomething() = default;
    ~OwnsSomething() { delete owned; }
    OwnsSomething(const OwnsSomething&) = delete;
    OwnsSomething& operator=(const OwnsSomething&) = delete;
    int* owned = nullptr;
};

static_assert(c::RingValue<int>);
static_assert(c::RingValue<Plain>);
static_assert(!c::RingValue<OwnsSomething>);

// The cells that a batch call of a ring reads or writes.  The batch calls
// take a View of the base.
template <class T, std::size_t N>
class Run final {
    ::fixy::FixedArray<T, N> cells_{};

public:
    [[nodiscard]] T& at(std::size_t position) noexcept {
        return *cells_.get(position).expect("a position of a run is below the count of the run");
    }

    // The first count cells, for a push.
    [[nodiscard]] ::fixy::View<T const> first(std::size_t count) const noexcept {
        return cells_.view().window(0, count).expect("a run holds the count of a push");
    }

    // Each cell, for a pop.
    [[nodiscard]] ::fixy::View<T> each() noexcept { return cells_.view(); }
};

// ── Shape ────────────────────────────────────────────────────────────

using Spsc = c::SpscRing<Plain, 8>;
using Mpsc = c::MpscRing<Plain, 8>;

// The address is the identity: both rings hold atomics other threads
// reach through a pointer taken once.
static_assert(!std::is_copy_constructible_v<Spsc>);
static_assert(!std::is_move_constructible_v<Spsc>);
static_assert(!std::is_copy_constructible_v<Mpsc>);
static_assert(!std::is_move_constructible_v<Mpsc>);

static_assert(Spsc::capacity() == 8);
static_assert(Spsc::channel_capacity == 8);
static_assert(Mpsc::capacity() == 8);
static_assert(std::is_same_v<typename Spsc::value_type, Plain>);

// The head and tail counters are on separate cache lines, so a push
// does not invalidate the consumer's line and a pop does not invalidate
// the producer's.  AtomicMonotonic carries that, and the ring does not
// restate it, so this line records the ring's dependence on it.
static_assert(alignof(Spsc) >= 64);
static_assert(alignof(Mpsc) >= 64);
static_assert(alignof(::fixy::AtomicMonotonic<std::uint64_t>) >= 64);
static_assert(sizeof(::fixy::AtomicMonotonic<std::uint64_t>) >= 64);

// The MPSC capacity gate over the two positions a producer read.  A head
// that trails the tail is stale, and the gate lets the compare-exchange
// refuse it.  An unsigned difference wraps there and reads a full ring.
static_assert(Mpsc::has_room_for(9, 2, 1), "seven items and room for one more");
static_assert(!Mpsc::has_room_for(10, 2, 1), "eight items fill a ring of eight");
static_assert(Mpsc::has_room_for(5, 7, 1), "a stale head that trails the tail is not a full ring");
static_assert(Mpsc::has_room_for(3, 10, 4), "a stale head is not a full ring for a batch either");
static_assert(!Mpsc::has_room_for(7, 2, 4), "a batch of four does not fit beside five items");

// The SPSC count of a head and a tail that a reader loaded in that order.
// A reader on a third thread can load a tail that passed the head, and the
// unsigned difference then wraps to a value near 2^64.
static_assert(Spsc::count_between(9, 2) == 7, "seven items");
static_assert(Spsc::count_between(10, 2) == 8, "a full ring of eight");
static_assert(Spsc::count_between(7, 7) == 0, "an empty ring");
static_assert(Spsc::count_between(5, 7) == 0, "a tail that passed the head is an empty ring");
static_assert(Spsc::count_between(20, 2) == 8, "the count never passes the capacity");

// A ring whose capacity is below one bitmap word still gets one word,
// and one that spans several gets several.  Both are exercised below.
using MpscSmall = c::MpscRing<int, 4>;
using MpscWide = c::MpscRing<int, 128>;

[[nodiscard]] int spsc_single_threaded() {
    Spsc ring{};
    if (!ring.empty_approx(kNondet) || ring.size_approx(kNondet) != 0) {
        std::fprintf(stderr, "a fresh SPSC ring did not read as empty\n");
        return 1;
    }
    if (ring.try_pop().is_some()) {
        std::fprintf(stderr, "an empty SPSC ring returned an item\n");
        return 1;
    }

    // Filling to capacity and one past it: the gate is h - t >= Capacity.
    for (int i = 0; i < 8; ++i) {
        if (!ring.try_push(Plain{i, 0})) {
            std::fprintf(stderr, "SPSC push %d was refused below capacity\n", i);
            return 1;
        }
    }
    if (ring.try_push(Plain{8, 0})) {
        std::fprintf(stderr, "SPSC accepted a push past capacity\n");
        return 1;
    }
    if (ring.size_approx(kNondet) != 8) {
        std::fprintf(stderr, "SPSC size_approx disagreed with the pushes\n");
        return 1;
    }

    // FIFO, and the ring wraps: after draining and refilling, the
    // positions are past the buffer end and the mask brings them back.
    // An empty pop gives the value -1, which no push gave.
    for (int i = 0; i < 8; ++i) {
        const Plain item = ring.try_pop().value_or(Plain{-1, 0});
        if (item.value != i) {
            std::fprintf(stderr, "SPSC pop %d came back out of order\n", i);
            return 1;
        }
    }

    // A batch that does not fit fills what it can, unlike the MPSC
    // batch, which is all or nothing.
    Run<Plain, 6> six{};
    for (std::size_t i = 0; i < 6; ++i)
        six.at(i) = Plain{100 + static_cast<int>(i), 0};
    if (ring.try_push_batch(six.first(6)) != 6) {
        std::fprintf(stderr, "SPSC batch push did not take all six\n");
        return 1;
    }
    if (ring.try_push_batch(six.first(6)) != 2) {
        std::fprintf(stderr, "SPSC batch push did not fill the remaining two\n");
        return 1;
    }

    Run<Plain, 8> drained{};
    if (ring.try_pop_batch(drained.each()) != 8) {
        std::fprintf(stderr, "SPSC batch pop did not drain the ring\n");
        return 1;
    }
    if (drained.at(0).value != 100 || drained.at(5).value != 105 || drained.at(6).value != 100
        || drained.at(7).value != 101) {
        std::fprintf(stderr, "SPSC batch pop returned the wrong sequence across the wrap\n");
        return 1;
    }
    if (!ring.empty_approx(kNondet)) {
        std::fprintf(stderr, "SPSC ring did not read as empty after the drain\n");
        return 1;
    }
    return 0;
}

// Each side of the SPSC ring keeps a cache of the index of the other side
// (THE CACHES of fixy/concurrent/SpscRing.h).  The walk below makes each
// cache stale in each direction, and each call must still give the answer of
// a fresh read: a full ring refuses, a pop frees room for the next push, an
// empty ring gives no item, and a push gives an item to the next pop.  The
// batch calls must take the count of a fresh read too.
[[nodiscard]] int spsc_caches_follow_the_other_side() {
    c::SpscRing<int, 4> ring{};
    const auto fail = [](fixy::TextView what) {
        fixy::report(fixy::Sink::Err, "SPSC cache: {}\n", what);
        return 1;
    };
    // The cached head is zero, and a push moves the head to one.  The pop
    // must read the head again and find the item.
    if (!ring.try_push(10)) return fail(fixy::TextView{"a push into an empty ring was refused"});
    if (ring.try_pop().value_or(-1) != 10) return fail(fixy::TextView{"a pop missed an item behind its cache"});
    if (ring.try_pop().is_some()) return fail(fixy::TextView{"a pop of an empty ring gave an item"});

    // Fill the ring.  The cached tail is behind, so the fifth push must read
    // the tail, find no room, and refuse.
    for (int i = 0; i < 4; ++i) {
        if (!ring.try_push(20 + i)) return fail(fixy::TextView{"a push below capacity was refused"});
    }
    if (ring.try_push(99)) return fail(fixy::TextView{"a push into a full ring was accepted"});

    // One pop frees one cell.  The cached tail still says full, so the push
    // must read the tail again and take the cell.
    if (ring.try_pop().value_or(-1) != 20) return fail(fixy::TextView{"the first item of a full ring"});
    if (!ring.try_push(24)) return fail(fixy::TextView{"a push after a pop did not read the tail again"});
    if (ring.try_push(99)) return fail(fixy::TextView{"a push into a ring full again was accepted"});

    // The cached head of the consumer is behind the head by the pushes above.
    // The consumer takes each item in order across the wrap.
    for (int i = 21; i <= 24; ++i) {
        if (ring.try_pop().value_or(-1) != i) return fail(fixy::TextView{"an item came back out of order"});
    }
    if (ring.try_pop().is_some()) return fail(fixy::TextView{"a drained ring gave an item"});

    // A batch push of six into four free cells takes four, and a batch pop
    // with a cached head behind them takes all four.
    Run<int, 6> six{};
    for (std::size_t i = 0; i < 6; ++i)
        six.at(i) = 30 + static_cast<int>(i);
    if (ring.try_push_batch(six.first(6)) != 4) return fail(fixy::TextView{"a batch push took the wrong count"});
    if (ring.try_push_batch(six.first(1)) != 0) return fail(fixy::TextView{"a batch push into a full ring"});
    Run<int, 3> three{};
    if (ring.try_pop_batch(three.each()) != 3 || three.at(0) != 30 || three.at(2) != 32) {
        return fail(fixy::TextView{"a batch pop of three"});
    }
    // The cached tail says no free cell, and three are free.  A batch push
    // of three must read the tail again and take all three.
    if (ring.try_push_batch(six.first(3)) != 3) return fail(fixy::TextView{"a batch push behind a stale cache"});
    Run<int, 8> rest{};
    if (ring.try_pop_batch(rest.each()) != 4 || rest.at(0) != 33 || rest.at(1) != 30 || rest.at(3) != 32) {
        return fail(fixy::TextView{"the batch pop of the rest"});
    }
    if (!ring.empty_approx(kNondet) || ring.try_pop().is_some()) return fail(fixy::TextView{"the ring is not empty"});
    return 0;
}

// Each role of the SPSC ring moves to a new thread after the last owner of
// the role ended.  The new owner reads the caches that the last owner wrote,
// and the join of the last owner orders those writes before the reads.  The
// stream keeps its order and loses no item.  Under TSan, a cache that no
// synchronization covers is a data race.
[[nodiscard]] int spsc_roles_move_between_threads() {
    constexpr int kPerTurn = 2000;
    constexpr int kTurns = 4;
    using Starter = ::foundation::effects::ExecCtx<
        ::foundation::effects::Bg,
        ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Alloc>>;
    c::SpscRing<int, 64> ring{};
    int next_expected = 0;
    for (int turn = 0; turn < kTurns; ++turn) {
        const int first = turn * kPerTurn;
        ::fixy::thread::OwnedThread producer =
            ::fixy::thread::mint_owned_thread(Starter{::foundation::effects::testing::bg()},
                                              ::fixy::thread::ThreadOptions{},
                                              [&ring, first]() noexcept {
                                                  for (int i = first; i < first + kPerTurn;) {
                                                      if (ring.try_push(i)) {
                                                          ++i;
                                                      } else {
                                                          CRUCIBLE_SPIN_PAUSE;
                                                      }
                                                  }
                                              })
                .expect("the thread door starts the producer of a turn");
        ::fixy::thread::OwnedThread consumer =
            ::fixy::thread::mint_owned_thread(
                Starter{::foundation::effects::testing::bg()}, ::fixy::thread::ThreadOptions{},
                [&ring, &next_expected, first]() noexcept {
                    while (next_expected < first + kPerTurn) {
                        ::fixy::Option<int> item = ring.try_pop();
                        if (item.is_none()) {
                            CRUCIBLE_SPIN_PAUSE;
                            continue;
                        }
                        if (std::move(item).expect("the test above found an item") != next_expected) return;
                        ++next_expected;
                    }
                })
                .expect("the thread door starts the consumer of a turn");
        std::move(producer).join();
        std::move(consumer).join();
        if (next_expected != first + kPerTurn) {
            fixy::report(fixy::Sink::Err, "SPSC gave item {} out of order after a move of the roles\n", next_expected);
            return 1;
        }
    }
    return ring.empty_approx(kNondet) ? 0 : 1;
}

// The owner of the rings of the proven tests (THE PROVEN FORM of
// fixy/concurrent/SpscRing.h).  Each test runs one producer and one consumer,
// so the owner makes the proof of each side.
struct ProvenOwner final {
    using Ring = c::SpscRing<int, 8, ProvenOwner>;

    [[nodiscard]] static constexpr auto producer() noexcept { return Ring::producer_proof{}; }
    [[nodiscard]] static constexpr auto consumer() noexcept { return Ring::consumer_proof{}; }
};

// The checked calls and the proven calls keep the same copies of each side,
// so one ring takes each form on each call.  The walk crosses the wrap, fills
// the ring and drains it in each form.
[[nodiscard]] int spsc_proven_and_checked_calls_share_one_ring() {
    ProvenOwner::Ring ring{};
    const auto fail = [](fixy::TextView what) {
        fixy::report(fixy::Sink::Err, "SPSC proven form: {}\n", what);
        return 1;
    };
    int next_in = 0;
    int next_out = 0;
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 8; ++i) {
            const bool is_pushed = (i % 2 == 0) ? ring.try_push_proven(ProvenOwner::producer(), next_in)
                                                : ring.try_push(next_in);
            if (!is_pushed) return fail(fixy::TextView{"a push below capacity was refused"});
            ++next_in;
        }
        if (ring.try_push_proven(ProvenOwner::producer(), -1) || ring.try_push(-1)) {
            return fail(fixy::TextView{"a push into a full ring was accepted"});
        }
        for (int i = 0; i < 8; ++i) {
            fixy::Option<int> item = (i % 3 == 0) ? ring.try_pop() : ring.try_pop_proven(ProvenOwner::consumer());
            if (std::move(item).value_or(-1) != next_out) {
                return fail(fixy::TextView{"an item came back out of order"});
            }
            ++next_out;
        }
        if (ring.try_pop_proven(ProvenOwner::consumer()).is_some() || ring.try_pop().is_some()) {
            return fail(fixy::TextView{"a drained ring gave an item"});
        }
        // A batch between the single calls keeps the copies too.
        Run<int, 3> three{};
        for (std::size_t i = 0; i < 3; ++i)
            three.at(i) = next_in + static_cast<int>(i);
        if (ring.try_push_batch(three.first(3)) != 3) return fail(fixy::TextView{"a batch push"});
        next_in += 3;
        for (int i = 0; i < 3; ++i) {
            if (ring.try_pop_proven(ProvenOwner::consumer()).value_or(-1) != next_out) {
                return fail(fixy::TextView{"a proven pop after a batch push"});
            }
            ++next_out;
        }
    }
    return ring.empty_approx(kNondet) ? 0 : fail(fixy::TextView{"the ring is not empty at the end"});
}

// The proven calls on two threads: one producer and one consumer, each with
// the proof of its side.  The stream keeps its order and loses no item.
// Under TSan, a publish that does not release the cell is a data race.
[[nodiscard]] int spsc_proven_two_threads() {
    constexpr int kItems = 5000;
    using Starter = ::foundation::effects::ExecCtx<
        ::foundation::effects::Bg,
        ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Alloc>>;
    ProvenOwner::Ring proven{};
    ::fixy::thread::OwnedThread producer =
        ::fixy::thread::mint_owned_thread(Starter{::foundation::effects::testing::bg()},
                                          ::fixy::thread::ThreadOptions{},
                                          [&proven]() noexcept {
                                              for (int i = 0; i < kItems;) {
                                                  if (proven.try_push_proven(ProvenOwner::producer(), i)) {
                                                      ++i;
                                                  } else {
                                                      CRUCIBLE_SPIN_PAUSE;
                                                  }
                                              }
                                          })
            .expect("the thread door starts the producer");
    int next_expected = 0;
    while (next_expected < kItems) {
        fixy::Option<int> item = proven.try_pop_proven(ProvenOwner::consumer());
        if (item.is_none()) {
            CRUCIBLE_SPIN_PAUSE;
            continue;
        }
        if (std::move(item).value_or(-1) != next_expected) break;
        ++next_expected;
    }
    std::move(producer).join();
    if (next_expected != kItems) {
        fixy::report(fixy::Sink::Err, "SPSC proven form gave item {} out of order\n", next_expected);
        return 1;
    }
    return 0;
}

[[nodiscard]] int mpsc_single_threaded() {
    Mpsc ring{};
    if (!ring.empty_approx(kNondet) || ring.try_pop(kNondet).is_some()) {
        std::fprintf(stderr, "a fresh MPSC ring did not read as empty\n");
        return 1;
    }

    for (int i = 0; i < 8; ++i) {
        if (!ring.try_push(Plain{i, 0})) {
            std::fprintf(stderr, "MPSC push %d was refused below capacity\n", i);
            return 1;
        }
    }
    if (ring.try_push(Plain{8, 0})) {
        std::fprintf(stderr, "MPSC accepted a push past capacity\n");
        return 1;
    }

    for (int i = 0; i < 8; ++i) {
        const Plain item = ring.try_pop(kNondet).value_or(Plain{-1, 0});
        if (item.value != i) {
            std::fprintf(stderr, "MPSC pop %d came back out of order\n", i);
            return 1;
        }
    }

    // All or nothing: a batch larger than the free space pushes nothing.
    Run<Plain, 6> six{};
    for (std::size_t i = 0; i < 6; ++i)
        six.at(i) = Plain{200 + static_cast<int>(i), 0};
    if (ring.try_push_batch(six.first(6)) != 6) {
        std::fprintf(stderr, "MPSC batch push did not take all six\n");
        return 1;
    }
    if (ring.try_push_batch(six.first(6)) != 0) {
        std::fprintf(stderr, "MPSC batch push partially filled where it must refuse\n");
        return 1;
    }

    // try_pop_batch clamps the request to Capacity.  Unclamped, the
    // prefix scan wraps past the buffer end, counts cells it already
    // took, and returns more items than the ring holds.  The count and
    // the empty ring below show the clamp.
    Run<Plain, 64> oversized{};
    const std::size_t taken = ring.try_pop_batch(kNondet, oversized.each());
    if (taken != 6) {
        std::fprintf(stderr, "MPSC oversized batch pop returned %zu, not the six available\n", taken);
        return 1;
    }
    if (oversized.at(0).value != 200 || oversized.at(5).value != 205) {
        std::fprintf(stderr, "MPSC batch pop returned the wrong sequence\n");
        return 1;
    }
    if (!ring.empty_approx(kNondet) || ring.size_approx(kNondet) != 0) {
        std::fprintf(stderr, "MPSC ring did not read as empty after the clamped drain\n");
        return 1;
    }

    // The ring still accepts a push after the clamped drain.
    if (!ring.try_push(Plain{7, 0})) {
        ::fixy::report(::fixy::Sink::Err, "MPSC ring refused a push after the oversized drain\n");
        return 1;
    }
    (void)ring.try_pop(kNondet);
    return 0;
}

// The bitmap is a word per 64 cells.  A capacity below one word leaves
// high bits permanently zero; a capacity spanning words makes the
// prefix scan cross a word boundary.  Both paths run here.
[[nodiscard]] int mpsc_bitmap_widths() {
    MpscSmall small{};
    for (int i = 0; i < 4; ++i) {
        if (!small.try_push(i)) return 1;
    }
    if (small.try_push(4)) return 1;
    Run<int, 4> out{};
    if (small.try_pop_batch(kNondet, out.each()) != 4 || out.at(3) != 3) return 1;

    MpscWide wide{};
    Run<int, 100> items{};
    for (std::size_t i = 0; i < 100; ++i)
        items.at(i) = static_cast<int>(i);
    if (wide.try_push_batch(items.first(100)) != 100) return 1;

    // The drain crosses the boundary between bitmap words 0 and 1.
    Run<int, 100> drained{};
    if (wide.try_pop_batch(kNondet, drained.each()) != 100) return 1;
    for (int i = 0; i < 100; ++i) {
        if (drained.at(static_cast<std::size_t>(i)) != i) {
            std::fprintf(stderr, "the wide MPSC drain broke at index %d\n", i);
            return 1;
        }
    }
    return 0;
}

// ── Each batch path at each capacity ─────────────────────────────────

// One case of the walk below.  The ring starts with both positions at
// offset.  A batch of count items goes in, the first singles items come
// out one at a time, and one batch pop drains the rest.  Then a full batch
// fills the ring again.  A tail past the head admits one more push, and a
// stale ready bit gives one more item to a batch pop.
template <std::size_t Capacity>
[[nodiscard]] int mpsc_batch_case(std::size_t offset, std::size_t count, std::size_t singles) {
    c::MpscRing<int, Capacity> ring{};
    Run<int, Capacity> items{};
    Run<int, 2 * Capacity> drained{};
    const auto fail = [&](fixy::TextView what) {
        fixy::report(fixy::Sink::Err, "MPSC ring of {}: {} (offset {}, batch {}, single pops {})\n", Capacity, what,
                     offset, count, singles);
        return 1;
    };

    if (offset > 0) {
        if (ring.try_push_batch(items.first(offset)) != offset) return fail(fixy::TextView{"offset push"});
        if (ring.try_pop_batch(kNondet, drained.each()) != offset) return fail(fixy::TextView{"offset pop"});
    }

    for (std::size_t i = 0; i < Capacity; ++i)
        items.at(i) = static_cast<int>(i);
    if (ring.try_push_batch(items.first(count)) != count) return fail(fixy::TextView{"batch push"});
    for (std::size_t i = 0; i < singles; ++i) {
        if (ring.try_pop(kNondet).value_or(-1) != static_cast<int>(i)) return fail(fixy::TextView{"single pop"});
    }
    const std::size_t rest = ring.try_pop_batch(kNondet, drained.each());
    if (rest != count - singles) return fail(fixy::TextView{"the batch pop count"});
    for (std::size_t i = 0; i < rest; ++i) {
        if (drained.at(i) != static_cast<int>(singles + i)) return fail(fixy::TextView{"the batch pop order"});
    }
    if (ring.try_pop(kNondet).is_some() || ring.size_approx(kNondet) != 0 || !ring.empty_approx(kNondet)) {
        return fail(fixy::TextView{"the drained ring"});
    }

    for (std::size_t i = 0; i < Capacity; ++i)
        items.at(i) = static_cast<int>(Capacity + i);
    if (ring.try_push_batch(items.first(Capacity)) != Capacity) return fail(fixy::TextView{"the refill"});
    if (ring.try_push(-1)) return fail(fixy::TextView{"a push past capacity"});
    if (ring.try_pop_batch(kNondet, drained.each()) != Capacity) {
        return fail(fixy::TextView{"the refill drain count"});
    }
    for (std::size_t i = 0; i < Capacity; ++i) {
        if (drained.at(i) != static_cast<int>(Capacity + i)) return fail(fixy::TextView{"the refill drain order"});
    }
    return 0;
}

// A full batch sets the ready bits of the whole ring.  A ring below 64
// cells has one bitmap word, and only its low Capacity bits belong to
// cells.  The prefix scan of try_pop_batch reads a set bit past them as
// one more ready cell.  The walk starts a batch at each offset and pushes
// each batch size, so each batch path meets each wrap and each word edge.
// Each batch then loses no item, one item, or each item but one to single
// pops before the batch pop.
template <std::size_t Capacity>
[[nodiscard]] int mpsc_batch_paths_at() {
    for (std::size_t offset = 0; offset < Capacity; ++offset) {
        for (std::size_t count = 1; count <= Capacity; ++count) {
            for (std::size_t form = 0; form < 3; ++form) {
                const std::size_t singles = form == 0 ? 0 : form == 1 ? 1 : count - 1;
                if (const int rc = mpsc_batch_case<Capacity>(offset, count, singles); rc != 0) return rc;
            }
        }
    }
    return 0;
}

[[nodiscard]] int mpsc_batch_paths_each_capacity() {
    if (const int rc = mpsc_batch_paths_at<1>(); rc != 0) return rc;
    if (const int rc = mpsc_batch_paths_at<2>(); rc != 0) return rc;
    if (const int rc = mpsc_batch_paths_at<4>(); rc != 0) return rc;
    if (const int rc = mpsc_batch_paths_at<8>(); rc != 0) return rc;
    if (const int rc = mpsc_batch_paths_at<16>(); rc != 0) return rc;
    if (const int rc = mpsc_batch_paths_at<32>(); rc != 0) return rc;
    if (const int rc = mpsc_batch_paths_at<64>(); rc != 0) return rc;
    return mpsc_batch_paths_at<128>();
}

// ── Threaded: the ordering claim ─────────────────────────────────────

[[nodiscard]] int spsc_two_threads() {
    constexpr int kItems = 20000;
    c::SpscRing<int, 1024> ring{};
    std::atomic<bool> producer_done{false};

    int next_expected = 0;
    int received = 0;

    {
        std::jthread producer([&ring, &producer_done] {
            for (int i = 0; i < kItems;) {
                if (ring.try_push(i)) ++i;
            }
            producer_done.store(true, std::memory_order_release);
        });

        while (received < kItems) {
            auto item = ring.try_pop();
            if (item.is_some()) {
                // FIFO is a property of the ring, not of the schedule:
                // one producer means the order is the push order.
                const int value = std::move(item).expect("the test above found an item");
                if (value != next_expected) {
                    std::fprintf(stderr, "SPSC delivered %d where %d was owed\n", value, next_expected);
                    return 1;
                }
                ++next_expected;
                ++received;
            } else if (producer_done.load(std::memory_order_acquire) && ring.empty_approx(kNondet)) {
                break;
            }
        }
    }

    if (received != kItems) {
        std::fprintf(stderr, "SPSC lost %d items\n", kItems - received);
        return 1;
    }
    return 0;
}

[[nodiscard]] int mpsc_many_producers() {
    constexpr std::uint32_t kProducers = 4;
    constexpr int kPerProducer = 4000;
    constexpr int kTotal = static_cast<int>(kProducers) * kPerProducer;

    c::MpscRing<Plain, 1024> ring{};
    std::atomic<std::uint32_t> finished{0};

    // Per producer, the ring must preserve that producer's own order;
    // across producers it says nothing, and this test claims nothing.
    std::array<int, kProducers> last_seen{};
    for (auto& slot : last_seen)
        slot = -1;
    int received = 0;

    {
        std::vector<std::jthread> producers;
        for (std::uint32_t p = 0; p < kProducers; ++p) {
            producers.emplace_back([&ring, &finished, p] {
                for (int i = 0; i < kPerProducer;) {
                    if (ring.try_push(Plain{i, p})) ++i;
                }
                finished.fetch_add(1, std::memory_order_release);
            });
        }

        while (received < kTotal) {
            auto popped = ring.try_pop(kNondet);
            if (popped.is_some()) {
                const Plain item = std::move(popped).expect("the test above found an item");
                const std::size_t who = item.producer;
                if (who >= kProducers) {
                    std::fprintf(stderr, "MPSC delivered an item from producer %zu\n", who);
                    return 1;
                }
                if (item.value != last_seen[who] + 1) {
                    std::fprintf(stderr, "MPSC reordered producer %zu: saw %d after %d\n", who, item.value,
                                 last_seen[who]);
                    return 1;
                }
                last_seen[who] = item.value;
                ++received;
            } else if (finished.load(std::memory_order_acquire) == kProducers && ring.empty_approx(kNondet)) {
                break;
            }
        }
    }

    if (received != kTotal) {
        std::fprintf(stderr, "MPSC lost %d items\n", kTotal - received);
        return 1;
    }
    for (std::uint32_t p = 0; p < kProducers; ++p) {
        if (last_seen[p] != kPerProducer - 1) {
            std::fprintf(stderr, "MPSC dropped the tail of producer %u\n", p);
            return 1;
        }
    }
    return 0;
}

// Many producers push full batches and partial batches into a ring of
// eight cells, and the consumer takes one item and then a batch, in turn.
// So a batch pop often follows a full batch and one single pop.  Each item
// carries its producer and its place in the order of that producer.  A
// stale ready bit makes the batch pop give a cell a second time, and a
// later producer then writes that cell while the bit is still set.
[[nodiscard]] int mpsc_full_batches_many_producers() {
    constexpr std::size_t kCapacity = 8;
    constexpr std::size_t kPartial = 3;
    constexpr std::uint32_t kProducers = 8;
    constexpr int kRounds = 400;
    constexpr int kPerProducer = kRounds * static_cast<int>(kCapacity + kPartial);
    constexpr int kTotal = static_cast<int>(kProducers) * kPerProducer;

    c::MpscRing<Plain, kCapacity> ring{};
    fixy::Atomic<std::uint32_t> finished{0};
    Run<int, kProducers> last_seen{};
    for (std::size_t p = 0; p < kProducers; ++p)
        last_seen.at(p) = -1;
    int received = 0;
    Run<Plain, kCapacity> drained{};

    const auto take = [&](const Plain item) {
        if (item.producer >= kProducers || item.value != last_seen.at(item.producer) + 1) {
            fixy::report(fixy::Sink::Err, "MPSC gave item {} of producer {} after item {}\n", item.value, item.producer,
                         item.producer < kProducers ? last_seen.at(item.producer) : -1);
            return false;
        }
        last_seen.at(item.producer) = item.value;
        ++received;
        return true;
    };

    {
        std::vector<std::jthread> producers;
        for (std::uint32_t p = 0; p < kProducers; ++p) {
            producers.emplace_back([&ring, &finished, p] {
                Run<Plain, kCapacity> batch{};
                int next = 0;
                for (int round = 0; round < kRounds; ++round) {
                    for (std::size_t form = 0; form < 2; ++form) {
                        const std::size_t size = form == 0 ? kCapacity : kPartial;
                        for (std::size_t i = 0; i < size; ++i)
                            batch.at(i) = Plain{next + static_cast<int>(i), p};
                        while (ring.try_push_batch(batch.first(size)) != size) {}
                        next += static_cast<int>(size);
                    }
                }
                static_cast<void>(finished.fetch_add_acq_rel(1));
            });
        }

        bool is_ordered = true;
        while (is_ordered && received < kTotal) {
            auto single = ring.try_pop(kNondet);
            const bool has_single = single.is_some();
            if (has_single) {
                is_ordered = take(std::move(single).expect("the test above found an item"));
            }
            const std::size_t got = ring.try_pop_batch(kNondet, drained.each());
            for (std::size_t i = 0; is_ordered && i < got; ++i)
                is_ordered = take(drained.at(i));
            if (got == 0 && !has_single && finished.load_acquire() == kProducers && ring.empty_approx(kNondet)) {
                break;
            }
        }
        if (!is_ordered) {
            // The producers wait for room, so the drain goes on until they end.
            while (finished.load_acquire() != kProducers) {
                static_cast<void>(ring.try_pop_batch(kNondet, drained.each()));
            }
            return 1;
        }
    }

    if (received != kTotal) {
        fixy::report(fixy::Sink::Err, "MPSC delivered {} of the {} items of the full batches\n", received, kTotal);
        return 1;
    }
    return 0;
}

// A producer reads the head and then the tail.  Between the two loads,
// other producers can push and the consumer can pop past the head that
// was read, so the head is stale and trails the tail.  The ring below
// holds more cells than a round pushes, so it is never full, and each
// refusal counts one stale head that the capacity gate read as a full
// ring.  Half the producers push single items and half push batches.
// The stale head needs many producers on the head line.  On the
// development host, a gate that read a stale head as a full ring refused
// pushes in three rounds of four with 64 producers, and almost never
// with 8.
namespace never_full {

constexpr std::uint32_t kProducers = 64;
constexpr std::uint64_t kPushesPerProducer = 4000;
constexpr std::size_t kBatch = 4;
constexpr std::size_t kCapacity = std::size_t{1} << 18;
constexpr int kRounds = 4;
static_assert(kCapacity >= kProducers * kPushesPerProducer, "the ring must hold every push of a round");

}  // namespace never_full

[[nodiscard]] int mpsc_never_full_round() {
    using namespace never_full;
    auto ring = std::make_unique<c::MpscRing<std::uint64_t, kCapacity>>();
    std::atomic<std::uint64_t> refused{0};
    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint32_t> finished{0};
    std::uint64_t received = 0;

    {
        std::array<std::jthread, kProducers> producers{};
        for (std::uint32_t p = 0; p < kProducers; ++p) {
            const bool pushes_batches = (p % 2) == 1;
            producers[p] = std::jthread([&ring, &refused, &accepted, &finished, pushes_batches] {
                std::uint64_t taken = 0;
                std::uint64_t refusals = 0;
                if (pushes_batches) {
                    Run<std::uint64_t, kBatch> batch{};
                    for (std::size_t i = 0; i < kBatch; ++i)
                        batch.at(i) = i + 1;
                    for (std::uint64_t i = 0; i < kPushesPerProducer / kBatch; ++i) {
                        const std::size_t pushed = ring->try_push_batch(batch.first(kBatch));
                        taken += pushed;
                        refusals += (pushed == 0) ? 1 : 0;
                    }
                } else {
                    for (std::uint64_t i = 0; i < kPushesPerProducer; ++i) {
                        if (ring->try_push(i)) {
                            ++taken;
                        } else {
                            ++refusals;
                        }
                    }
                }
                accepted.fetch_add(taken, std::memory_order_relaxed);
                refused.fetch_add(refusals, std::memory_order_relaxed);
                finished.fetch_add(1, std::memory_order_release);
            });
        }

        for (;;) {
            if (ring->try_pop(kNondet).is_some()) {
                ++received;
            } else if (finished.load(std::memory_order_acquire) == kProducers && ring->empty_approx(kNondet)) {
                break;
            }
        }
    }

    if (const std::uint64_t spurious = refused.load(std::memory_order_relaxed); spurious != 0) {
        std::fprintf(stderr, "a never-full MPSC ring refused %llu pushes: a stale head passed for a full ring\n",
                     static_cast<unsigned long long>(spurious));
        return 1;
    }
    if (received != accepted.load(std::memory_order_relaxed)) {
        std::fprintf(stderr, "MPSC delivered %llu items of the %llu it accepted\n",
                     static_cast<unsigned long long>(received),
                     static_cast<unsigned long long>(accepted.load(std::memory_order_relaxed)));
        return 1;
    }
    return 0;
}

[[nodiscard]] int mpsc_never_full_ring_refuses_nothing() {
    for (int round = 0; round < never_full::kRounds; ++round) {
        if (const int rc = mpsc_never_full_round(); rc != 0) return rc;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = spsc_single_threaded(); rc != 0) return rc;
    if (const int rc = spsc_caches_follow_the_other_side(); rc != 0) return rc;
    if (const int rc = spsc_roles_move_between_threads(); rc != 0) return rc;
    if (const int rc = spsc_proven_and_checked_calls_share_one_ring(); rc != 0) return rc;
    if (const int rc = spsc_proven_two_threads(); rc != 0) return rc;
    if (const int rc = mpsc_single_threaded(); rc != 0) return rc;
    if (const int rc = mpsc_bitmap_widths(); rc != 0) return rc;
    if (const int rc = mpsc_batch_paths_each_capacity(); rc != 0) return rc;
    if (const int rc = spsc_two_threads(); rc != 0) return rc;
    if (const int rc = mpsc_many_producers(); rc != 0) return rc;
    if (const int rc = mpsc_full_batches_many_producers(); rc != 0) return rc;
    if (const int rc = mpsc_never_full_ring_refuses_nothing(); rc != 0) return rc;
    return 0;
}

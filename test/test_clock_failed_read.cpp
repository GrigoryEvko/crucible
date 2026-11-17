// A clock read that fails, or that gives a value that the stamp cannot
// hold, gives an error and no reading.  The transaction log reads no clock,
// so a clock that fails changes none of its stamps.  The watch of a session
// wait reads no clock either, so a clock that jumps or fails changes no point
// at which a wait traces.
//
// A working clock does not fail, so this executable has its own
// clock_gettime.  The linker connects each call of clock_gettime in this
// executable to that function, and not to the function in the C library.
// The readers and the sleeper read the clock through the door of
// fixy/os/MonotonicClock.h.  The door is a member of the static
// library libfixy.a, so its call of clock_gettime is a call in this
// executable too.  The function asks the kernel through the raw system call,
// unless a case gives it a plan: fail with an errno, or give a specified
// timespec.  Each case sets each value, so the test knows each result.
//
// The door does not read errno.  After a failed call of the C library, it
// asks the kernel again through the raw system call primitive, and it gives
// the word of the kernel.  So a failed plan gives the reading of the kernel
// for a working clock, and a clock that the kernel does not have gives the
// error of the kernel.

#include <crucible/MerkleDag.h>
#include <crucible/Transaction.h>
#include <fixy/Ctx.h>
#include <fixy/os/ClockSource.h>
#include <fixy/os/Time.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Watch.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <limits>
#include <utility>

#include <sys/syscall.h>
#include <unistd.h>
#include "test_assert.h"

namespace {

// What a call of clock_gettime in this executable does.
enum class ClockPlan : std::uint8_t {
    Kernel,  // the kernel answers
    Fail,  // the call fails with failure_errno
    Fixed,  // the call gives fixed_reading
};

struct FakeClock {
    ClockPlan plan = ClockPlan::Kernel;
    int failure_errno = 0;
    std::timespec fixed_reading{};
};

// Only the thread of main() reads and writes this state.
FakeClock g_fake_clock{};

}  // namespace

extern "C" int clock_gettime(::clockid_t clock, ::timespec* reading) noexcept {
    switch (g_fake_clock.plan) {
        case ClockPlan::Fail:
            errno = g_fake_clock.failure_errno;
            return -1;
        case ClockPlan::Fixed:
            *reading = g_fake_clock.fixed_reading;
            return 0;
        case ClockPlan::Kernel:
        default:
            return static_cast<int>(::syscall(SYS_clock_gettime, clock, reading));
    }
}

namespace {

namespace eff = ::foundation::effects;

void use_the_kernel_clock() noexcept { g_fake_clock = FakeClock{}; }

void fail_each_read(int failure_errno) noexcept {
    g_fake_clock = FakeClock{ClockPlan::Fail, failure_errno, std::timespec{}};
}

void give_each_read(std::time_t seconds, long nanos) noexcept {
    g_fake_clock = FakeClock{ClockPlan::Fixed, 0, std::timespec{seconds, nanos}};
}

constexpr std::time_t max_seconds = std::numeric_limits<std::time_t>::max();

// A reading that each reader refuses: a second field of -1.
void refuse_each_read() noexcept { give_each_read(-1, 0); }

// The number of failed checks.  Each case does all of its checks, so one
// run shows each failure.
struct Tally {
    int failures = 0;

    void expect(bool is_held) noexcept {
        if (!is_held) ++failures;
    }
};

using foundation::os::Errno;

// Says whether a read gave the error `expected` and no reading.  It prints
// the case when the read did not.
template <typename Result>
[[nodiscard]] bool is_refused_with(Result const& result, Errno expected, char const* what) {
    return result.match(
        [what](auto const& reading) {
            std::fprintf(stderr, "%s: the read gave the reading %llu, and it must give an error\n", what,
                         static_cast<unsigned long long>(reading.peek()));
            return false;
        },
        [what, expected](Errno const& error) {
            if (error != expected) {
                std::fprintf(stderr, "%s: the read gave the error number %d, and it must give %d\n", what,
                             foundation::os::error_number(error), foundation::os::error_number(expected));
                return false;
            }
            return true;
        });
}

// Says whether a read gave the reading `expected_nanos`.  It prints the case
// when the read did not.
template <typename Result>
[[nodiscard]] bool is_reading_of(Result const& result, std::uint64_t expected_nanos, char const* what) {
    return result.match(
        [what, expected_nanos](auto const& reading) {
            if (reading.peek() != expected_nanos) {
                std::fprintf(stderr, "%s: the read gave the reading %llu, and it must give %llu\n", what,
                             static_cast<unsigned long long>(reading.peek()),
                             static_cast<unsigned long long>(expected_nanos));
                return false;
            }
            return true;
        },
        [what, expected_nanos](Errno const& error) {
            std::fprintf(stderr, "%s: the read gave the error number %d, and it must give the reading %llu\n", what,
                         foundation::os::error_number(error), static_cast<unsigned long long>(expected_nanos));
            return false;
        });
}

// Says whether a read gave a reading above `floor_nanos`.
template <typename Result>
[[nodiscard]] bool is_reading_above(Result const& result, std::uint64_t floor_nanos) noexcept {
    return result.match([floor_nanos](auto const& reading) noexcept { return reading.peek() > floor_nanos; },
                        [](Errno const&) noexcept { return false; });
}

// An unclamped reader.  The reader refuses a negative field, a nanosecond
// field of a full second, and a count of more than 64 bits.  The first read
// gets a specified value, and the reader must give that value with no change.
// This shows that the reader reads the clock of this executable.  A failed
// call of the C library makes the door ask the kernel, and the reader gives
// the reading of the kernel, which is far above the specified value.
void realtime_reader_refuses_what_it_cannot_stamp(Tally& tally) {
    const fixy::TestRunnerCtx ctx{eff::testing::test()};
    const auto realtime = fixy::time::mint_clock_reader<fixy::ClockSource_v::Realtime>(ctx);
    const eff::cap::Nondet nondet = ctx.cap().nondet;

    give_each_read(2, 5);
    const auto fixed = realtime.read(nondet);
    fail_each_read(EINVAL);
    const auto asked_the_kernel = realtime.read(nondet);
    give_each_read(-5, 0);
    const auto before_epoch = realtime.read(nondet);
    give_each_read(0, -1);
    const auto negative_nanos = realtime.read(nondet);
    give_each_read(0, 1000000000);
    const auto full_second = realtime.read(nondet);
    give_each_read(max_seconds, 999999999);
    const auto past_64_bits = realtime.read(nondet);
    use_the_kernel_clock();

    tally.expect(is_reading_of(fixed, 2000000005, "Realtime, a reading of 2 s and 5 ns"));
    tally.expect(is_reading_above(asked_the_kernel, 2000000005));
    tally.expect(is_refused_with(before_epoch, Errno::erange, "Realtime, a second field of -5"));
    tally.expect(is_refused_with(negative_nanos, Errno::erange, "Realtime, a nanosecond field of -1"));
    tally.expect(is_refused_with(full_second, Errno::erange, "Realtime, a nanosecond field of one second"));
    tally.expect(is_refused_with(past_64_bits, Errno::eoverflow, "Realtime, a count past 64 bits"));
}

// A clamped reader.  A refused value after a good read gives an error, and
// not the floor that the good read set.  A refused value does not move the
// floor, so a subsequent good read gives its own value.
void monotonic_reader_gives_no_floor_for_a_failed_read(Tally& tally) {
    const fixy::TestRunnerCtx ctx{eff::testing::test()};
    const auto monotonic = fixy::time::mint_clock_reader<fixy::ClockSource_v::Monotonic>(ctx);
    const eff::cap::Nondet nondet = ctx.cap().nondet;

    give_each_read(100, 0);
    const auto first = monotonic.read(nondet);
    give_each_read(50, 0);
    const auto regressed = monotonic.read(nondet);
    give_each_read(-5, 0);
    const auto negative = monotonic.read(nondet);
    give_each_read(max_seconds, 999999999);
    const auto past_64_bits = monotonic.read(nondet);
    give_each_read(150, 0);
    const auto later = monotonic.read(nondet);
    use_the_kernel_clock();

    tally.expect(is_reading_of(first, 100000000000, "Monotonic, a reading of 100 s"));
    tally.expect(is_reading_of(regressed, 100000000000, "Monotonic, a reading of 50 s below a floor of 100 s"));
    tally.expect(is_refused_with(negative, Errno::erange, "Monotonic, a second field of -5 after a reading of 100 s"));
    tally.expect(is_refused_with(past_64_bits, Errno::eoverflow, "Monotonic, a count past 64 bits"));
    tally.expect(is_reading_of(later, 150000000000, "Monotonic, a reading of 150 s after two refused values"));
}

// The sleeper reads the monotonic clock for its deadline.  A deadline with a
// negative second field makes the kernel refuse clock_nanosleep with EINVAL,
// and the sleeper gives that error.  A reading with a nanosecond field of a
// full second gives EINVAL, the answer of the kernel for such a deadline.  A
// reading that gives no deadline in 64 bits gives EOVERFLOW.  A failed call
// of the C library makes the door ask the kernel, and the sleep then has a
// deadline of the kernel clock.
void sleeper_gives_the_error_of_a_failed_call(Tally& tally) {
    const fixy::TestRunnerCtx ctx{eff::testing::test()};
    const auto sleeper = fixy::time::mint_bounded_sleep<1000>(ctx);

    give_each_read(-5, 0);
    const auto refused_deadline = sleeper.sleep_for(0);
    give_each_read(0, 1000000000);
    const auto full_second = sleeper.sleep_for(0);
    give_each_read(max_seconds, 999999999);
    const auto past_64_bits = sleeper.sleep_for(1000);
    fail_each_read(EFAULT);
    const auto asked_the_kernel = sleeper.sleep_for(0);
    use_the_kernel_clock();
    const auto slept = sleeper.sleep_for(0);

    tally.expect(refused_deadline.err().value_or(Errno::eio) == Errno::einval);
    tally.expect(full_second.err().value_or(Errno::eio) == Errno::einval);
    tally.expect(past_64_bits.err().value_or(Errno::eio) == Errno::eoverflow);
    tally.expect(asked_the_kernel.is_ok());
    tally.expect(slept.is_ok());
}

// A clock that the kernel does not have.  The call of the C library fails,
// the door asks the kernel, and the read gives the error of the kernel.  The
// door does not read errno, so a failed call with a different errno gives the
// error of the kernel too.
void read_of_a_clock_the_kernel_refuses_gives_its_error(Tally& tally) {
    constexpr fixy::time::ClockId no_clock = static_cast<fixy::time::ClockId>(99);
    use_the_kernel_clock();
    const auto refused = fixy::time::detail::read_clock_nanos(no_clock);
    fail_each_read(EPERM);
    const auto refused_again = fixy::time::detail::read_clock_nanos(no_clock);
    use_the_kernel_clock();

    tally.expect(refused.err().value_or(Errno::eio) == Errno::einval);
    tally.expect(refused_again.err().value_or(Errno::eio) == Errno::einval);
}

// The owner proof of a log that this one thread operates.
class SoloOwner {
public:
    [[nodiscard]] static SoloOwner claim() noexcept { return SoloOwner{}; }
    SoloOwner(const SoloOwner&) = delete("the owner proof stays with its thread");
    SoloOwner(SoloOwner&&) = delete("the owner proof stays with its thread");
    SoloOwner& operator=(const SoloOwner&) = delete("the owner proof stays with its thread");
    SoloOwner& operator=(SoloOwner&&) = delete("the owner proof stays with its thread");
    ~SoloOwner() {}

private:
    SoloOwner() noexcept {}
};

// Says whether a transaction holds the place `expected` in the order of the
// events of its log.
[[nodiscard]] bool holds_place(crucible::Transaction const* tx, std::uint64_t expected, char const* what) {
    if (tx == nullptr) {
        std::fprintf(stderr, "%s: the log gave no transaction\n", what);
        return false;
    }
    if (tx->event_order.get() != expected) {
        std::fprintf(stderr, "%s: the transaction holds the place %llu, and it must hold %llu\n", what,
                     static_cast<unsigned long long>(tx->event_order.get()), static_cast<unsigned long long>(expected));
        return false;
    }
    return true;
}

// Makes a region of a copy of one operation and calculates its merkle root.
// The copy and the region are in the arena.
[[nodiscard]] crucible::RegionNode* committed_region(eff::Test const& test, ::fixy::BumpArena& arena,
                                                     crucible::TraceEntry& op) {
    const ::fixy::Ref<crucible::RegionNode> region =
        crucible::make_region(test.alloc, arena, arena.array<crucible::TraceEntry>(test.alloc, 1, op));
    crucible::recompute_merkle(*region);
    return &*region;
}

// The log stamps the transaction at each change of state with the next place
// of its own count, and it reads no clock.  So a clock that refuses each read
// or gives a fixed reading changes no stamp.  The case does a test of each
// stamp site of the log: begin_tx, commit, activate (the displaced
// transaction and the new one) and rollback (the two again).
void transaction_stamps_read_no_clock(Tally& tally) {
    const auto test = eff::testing::test();
    const SoloOwner owner = SoloOwner::claim();
    crucible::TransactionLog<16, SoloOwner> log{};
    ::fixy::BumpArena arena(1 << 12);

    crucible::TraceEntry ops[3]{};
    ops[0].schema_hash = crucible::SchemaHash{0xFEED};
    ops[1].schema_hash = crucible::SchemaHash{0xBEEF};
    ops[2].schema_hash = crucible::SchemaHash{0xCAFE};
    crucible::RegionNode* regions[3] = {committed_region(test, arena, ops[0]), committed_region(test, arena, ops[1]),
                                        committed_region(test, arena, ops[2])};
    for (crucible::RegionNode const* region : regions) {
        if (region == nullptr || region->merkle_hash.raw() == 0) {
            std::fprintf(stderr, "the test could not build a region with a merkle root\n");
            tally.expect(false);
            return;
        }
    }
    const auto commit = [&log, &owner](crucible::Transaction* tx, crucible::RegionNode* region) {
        return log.commit(
            owner, *tx,
            ::fixy::mint_tagged<::fixy::tags::source::Arena>(::fixy::Ref<crucible::RegionNode>{::fixy::to(*region)}),
            region->content_hash, region->merkle_hash);
    };

    give_each_read(7, 0);
    auto* first = &log.begin_tx(owner, 1);
    tally.expect(holds_place(first, 1, "begin_tx of the first transaction, with the clock at 7 s"));

    refuse_each_read();
    tally.expect(commit(first, regions[0]));
    tally.expect(holds_place(first, 2, "commit of the first transaction, with a clock that fails"));

    give_each_read(8, 0);
    tally.expect(log.activate(owner, *first).try_get() == nullptr);
    tally.expect(holds_place(first, 3, "activate of the first transaction, with the clock at 8 s"));

    refuse_each_read();
    auto* second = &log.begin_tx(owner, 2);
    tally.expect(holds_place(second, 4, "begin_tx of the second transaction, with a clock that fails"));

    give_each_read(9, 0);
    tally.expect(commit(second, regions[1]));
    tally.expect(holds_place(second, 5, "commit of the second transaction, with the clock at 9 s"));

    refuse_each_read();
    tally.expect(log.activate(owner, *second).try_get() == first);
    tally.expect(holds_place(first, 6, "the first transaction, displaced with a clock that fails"));
    tally.expect(holds_place(second, 7, "activate of the second transaction, with a clock that fails"));

    give_each_read(10, 0);
    auto* third = &log.begin_tx(owner, 3);
    give_each_read(11, 0);
    tally.expect(commit(third, regions[2]));
    give_each_read(12, 0);
    tally.expect(log.activate(owner, *third).try_get() == second);
    tally.expect(holds_place(second, 10, "the second transaction, displaced with the clock at 12 s"));
    tally.expect(holds_place(third, 11, "activate of the third transaction, with the clock at 12 s"));

    refuse_each_read();
    tally.expect(log.rollback(owner));
    tally.expect(holds_place(third, 12, "the third transaction, rolled back with a clock that fails"));
    tally.expect(holds_place(second, 13, "the second transaction, restored with a clock that fails"));
    use_the_kernel_clock();
}

// What the clock of this executable does while a session wait polls.
enum class ClockDuringWait : std::uint8_t {
    Kernel,  // the kernel answers each read
    Jumps,  // each trace point moves the reading one hour on
    Fails,  // each read fails
};

// The trace points at which a wait traced its chain, the first two at most.
struct TracePoints {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
};

// The resource of a session that only waits.
struct WaitWire {
    int value = 0;
};

// Opens a wait on the endpoint of a session, and polls it for twice the
// trace points of its first trace, under the clock plan.  It notes each
// trace point at which the wait traced.
[[nodiscard]] TracePoints trace_points_of_one_wait(ClockDuringWait plan) {
    namespace session = ::fixy::session;
    const fixy::TestRunnerCtx ctx{eff::testing::test()};
    auto end = session::mint_session<session::Recv<int, session::End>, session::check::Enforced>(ctx, WaitWire{});
    TracePoints traced{};
    {
        session::watch::wait_scope wait{end.watch_endpoint()};
        constexpr std::uint64_t kTracePoints =
            session::watch::suspect_after_trace_points + session::watch::confirm_after_trace_points;
        for (std::uint64_t point = 1; point <= kTracePoints; ++point) {
            if (plan == ClockDuringWait::Jumps) give_each_read(static_cast<std::time_t>(point * 3600), 0);
            if (plan == ClockDuringWait::Fails) fail_each_read(EINVAL);
            for (std::uint32_t poll = 0; poll < session::watch::polls_per_trace_point; ++poll)
                wait.poll();
            if (wait.last_trace_point() != point) continue;
            if (traced.first == 0) {
                traced.first = point;
            } else if (traced.second == 0) {
                traced.second = point;
            }
        }
    }
    use_the_kernel_clock();
    std::move(end).detach(session::detach_reason::TestInstrumentation{});
    return traced;
}

// The watch of a session wait reads no clock.  The count of the polls of a
// wait decides when it traces, so two waits with the same polls trace at the
// same trace points, and a clock that jumps or fails changes none of them.
void expect_traces_at_the_poll_count(Tally& tally, ClockDuringWait plan) {
    constexpr std::uint64_t kFirst = ::fixy::session::watch::suspect_after_trace_points;
    constexpr std::uint64_t kSecond = kFirst + ::fixy::session::watch::confirm_after_trace_points;
    const TracePoints traced = trace_points_of_one_wait(plan);
    if (traced.first == kFirst && traced.second == kSecond) return;
    ::fixy::report(::fixy::Sink::Err,
                   "the session watch, clock plan {}: the wait traced at the trace points {} and {}, and it must "
                   "trace at {} and {}\n",
                   static_cast<int>(plan), traced.first, traced.second, kFirst, kSecond);
    tally.expect(false);
}

void session_watch_traces_at_its_poll_count(Tally& tally) {
    expect_traces_at_the_poll_count(tally, ClockDuringWait::Kernel);
    expect_traces_at_the_poll_count(tally, ClockDuringWait::Kernel);
    expect_traces_at_the_poll_count(tally, ClockDuringWait::Jumps);
    expect_traces_at_the_poll_count(tally, ClockDuringWait::Fails);
}

}  // namespace

int main() {
    Tally tally;
    realtime_reader_refuses_what_it_cannot_stamp(tally);
    monotonic_reader_gives_no_floor_for_a_failed_read(tally);
    sleeper_gives_the_error_of_a_failed_call(tally);
    read_of_a_clock_the_kernel_refuses_gives_its_error(tally);
    transaction_stamps_read_no_clock(tally);
    session_watch_traces_at_its_poll_count(tally);
    if (tally.failures != 0) {
        std::fprintf(stderr, "test_clock_failed_read: %d check(s) failed\n", tally.failures);
        return 1;
    }
    crucible::test::pass("test_clock_failed_read: all cases passed\n");
    return 0;
}

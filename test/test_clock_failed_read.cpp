// A clock read that fails, or that gives a value that the stamp cannot
// hold, gives an error and no reading.  A transaction keeps no reading
// after a failed read.
//
// A working clock does not fail, so this executable has its own
// clock_gettime.  The linker connects each call of clock_gettime in this
// executable to that function, and not to the function in the C library.
// The readers and the transaction log are inline in their headers, so their
// reads are calls in this executable.  The function asks the kernel through
// the raw system call, unless a case gives it a plan: fail with an errno, or
// give a specified timespec.  Each case sets each value, so the test knows
// each result.

#include <crucible/Arena.h>
#include <crucible/MerkleDag.h>
#include <crucible/Transaction.h>
#include <fixy/Ctx.h>
#include <fixy/os/ClockSource.h>
#include <fixy/os/Time.h>
#include <foundation/effects/Effect.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <limits>
#include <system_error>

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

// The number of failed checks.  Each case does all of its checks, so one
// run shows each failure.
struct Tally {
    int failures = 0;

    void expect(bool is_held) noexcept {
        if (!is_held) ++failures;
    }
};

[[nodiscard]] std::error_code errno_code(int error_number) noexcept {
    return std::error_code{error_number, std::system_category()};
}

// Says whether a read gave the error `expected` and no reading.  It prints
// the case when the read did not.
template <typename Result>
[[nodiscard]] bool is_refused_with(Result const& result, std::error_code const& expected, char const* what) {
    if (result.has_value()) {
        std::fprintf(stderr, "%s: the read gave the reading %llu, and it must give an error\n", what,
                     static_cast<unsigned long long>(result->peek()));
        return false;
    }
    if (result.error() != expected) {
        std::fprintf(stderr, "%s: the read gave the error '%s', and it must give '%s'\n", what,
                     result.error().message().c_str(), expected.message().c_str());
        return false;
    }
    return true;
}

// Says whether a read gave the reading `expected_nanos`.  It prints the case
// when the read did not.
template <typename Result>
[[nodiscard]] bool is_reading_of(Result const& result, std::uint64_t expected_nanos, char const* what) {
    if (!result.has_value()) {
        std::fprintf(stderr, "%s: the read gave the error '%s', and it must give the reading %llu\n", what,
                     result.error().message().c_str(), static_cast<unsigned long long>(expected_nanos));
        return false;
    }
    if (result->peek() != expected_nanos) {
        std::fprintf(stderr, "%s: the read gave the reading %llu, and it must give %llu\n", what,
                     static_cast<unsigned long long>(result->peek()), static_cast<unsigned long long>(expected_nanos));
        return false;
    }
    return true;
}

// An unclamped reader.  A failed call gives its errno.  The reader refuses a
// negative field, a nanosecond field of a full second, and a count of more
// than 64 bits.  The first read gets a specified value, and the reader must
// give that value with no change.  This shows that the reader reads the
// clock of this executable.
void realtime_reader_refuses_what_it_cannot_stamp(Tally& tally) {
    const fixy::TestRunnerCtx ctx{eff::testing::test()};
    const auto realtime = fixy::time::mint_clock_reader<fixy::ClockSource_v::Realtime>(ctx);

    give_each_read(2, 5);
    const auto fixed = realtime.read();
    fail_each_read(EINVAL);
    const auto failed = realtime.read();
    give_each_read(-5, 0);
    const auto before_epoch = realtime.read();
    give_each_read(0, -1);
    const auto negative_nanos = realtime.read();
    give_each_read(0, 1000000000);
    const auto full_second = realtime.read();
    give_each_read(max_seconds, 999999999);
    const auto past_64_bits = realtime.read();
    use_the_kernel_clock();

    const auto out_of_range = std::make_error_code(std::errc::result_out_of_range);
    tally.expect(is_reading_of(fixed, 2000000005, "Realtime, a reading of 2 s and 5 ns"));
    tally.expect(is_refused_with(failed, errno_code(EINVAL), "Realtime, a failed call"));
    tally.expect(is_refused_with(before_epoch, out_of_range, "Realtime, a second field of -5"));
    tally.expect(is_refused_with(negative_nanos, out_of_range, "Realtime, a nanosecond field of -1"));
    tally.expect(is_refused_with(full_second, out_of_range, "Realtime, a nanosecond field of one second"));
    tally.expect(is_refused_with(past_64_bits, std::make_error_code(std::errc::value_too_large),
                                 "Realtime, a count past 64 bits"));
}

// A clamped reader.  A failed call after a good read gives an error, and not
// the floor that the good read set.  A refused value does not move the
// floor, so a subsequent good read gives its own value.
void monotonic_reader_gives_no_floor_for_a_failed_read(Tally& tally) {
    const fixy::TestRunnerCtx ctx{eff::testing::test()};
    const auto monotonic = fixy::time::mint_clock_reader<fixy::ClockSource_v::Monotonic>(ctx);

    give_each_read(100, 0);
    const auto first = monotonic.read();
    fail_each_read(EPERM);
    const auto failed = monotonic.read();
    give_each_read(50, 0);
    const auto regressed = monotonic.read();
    give_each_read(-5, 0);
    const auto negative = monotonic.read();
    give_each_read(max_seconds, 999999999);
    const auto past_64_bits = monotonic.read();
    give_each_read(150, 0);
    const auto later = monotonic.read();
    use_the_kernel_clock();

    tally.expect(is_reading_of(first, 100000000000, "Monotonic, a reading of 100 s"));
    tally.expect(is_refused_with(failed, errno_code(EPERM), "Monotonic, a failed call after a reading of 100 s"));
    tally.expect(is_reading_of(regressed, 100000000000, "Monotonic, a reading of 50 s below a floor of 100 s"));
    tally.expect(is_refused_with(negative, std::make_error_code(std::errc::result_out_of_range),
                                 "Monotonic, a second field of -5"));
    tally.expect(is_refused_with(past_64_bits, std::make_error_code(std::errc::value_too_large),
                                 "Monotonic, a count past 64 bits"));
    tally.expect(is_reading_of(later, 150000000000, "Monotonic, a reading of 150 s after two refused values"));
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

// Says whether a transaction holds the reading `expected_nanos`.
[[nodiscard]] bool holds_reading(crucible::Transaction const* tx, std::uint64_t expected_nanos, char const* what) {
    if (tx == nullptr) {
        std::fprintf(stderr, "%s: the log gave no transaction\n", what);
        return false;
    }
    if (!tx->ts_ns.has_value()) {
        std::fprintf(stderr, "%s: the transaction holds no reading, and it must hold %llu\n", what,
                     static_cast<unsigned long long>(expected_nanos));
        return false;
    }
    if (tx->ts_ns->peek() != expected_nanos) {
        std::fprintf(stderr, "%s: the transaction holds %llu, and it must hold %llu\n", what,
                     static_cast<unsigned long long>(tx->ts_ns->peek()),
                     static_cast<unsigned long long>(expected_nanos));
        return false;
    }
    return true;
}

// Says whether a transaction holds no reading.
[[nodiscard]] bool holds_no_reading(crucible::Transaction const* tx, char const* what) {
    if (tx == nullptr) {
        std::fprintf(stderr, "%s: the log gave no transaction\n", what);
        return false;
    }
    if (tx->ts_ns.has_value()) {
        std::fprintf(stderr, "%s: the transaction holds the reading %llu after a failed read\n", what,
                     static_cast<unsigned long long>(tx->ts_ns->peek()));
        return false;
    }
    return true;
}

// Makes a region of one operation and calculates its merkle root.  It gives
// null when the arena has no space.
[[nodiscard]] crucible::RegionNode* committed_region(eff::Test const& test, crucible::Arena& arena,
                                                     crucible::TraceEntry& op) {
    auto* region = crucible::make_region(test.alloc, arena, &op, 1);
    if (region != nullptr) crucible::recompute_merkle(region);
    return region;
}

// The log stamps the transaction at each change of state.  After a stamp
// under a failed read, the transaction has no reading, and the reading of a
// previous change does not stay.  The case does a test of each stamp site of
// the log: begin_tx, commit, activate (the displaced transaction and the new
// one) and rollback (the two again).
void transaction_keeps_no_reading_after_a_failed_read(Tally& tally) {
    const auto test = eff::testing::test();
    const fixy::TestRunnerCtx ctx{eff::testing::test()};
    const SoloOwner owner = SoloOwner::claim();
    crucible::TransactionLog<16, SoloOwner> log{ctx};
    crucible::Arena arena(1 << 12);

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
        return log.commit(owner, tx, ::fixy::mint_tagged<::fixy::tags::source::Arena>(region), region->content_hash,
                          region->merkle_hash);
    };

    give_each_read(7, 0);
    auto* first = log.begin_tx(owner, 1);
    tally.expect(holds_reading(first, 7000000000, "begin_tx of the first transaction at 7 s"));

    fail_each_read(EIO);
    tally.expect(commit(first, regions[0]));
    tally.expect(holds_no_reading(first, "commit of the first transaction under a failed read"));

    give_each_read(8, 0);
    tally.expect(log.activate(owner, first) == nullptr);
    tally.expect(holds_reading(first, 8000000000, "activate of the first transaction at 8 s"));

    fail_each_read(EIO);
    auto* second = log.begin_tx(owner, 2);
    tally.expect(holds_no_reading(second, "begin_tx of the second transaction under a failed read"));

    give_each_read(9, 0);
    tally.expect(commit(second, regions[1]));
    tally.expect(holds_reading(second, 9000000000, "commit of the second transaction at 9 s"));

    fail_each_read(EIO);
    tally.expect(log.activate(owner, second) == first);
    tally.expect(holds_no_reading(first, "the first transaction, displaced under a failed read"));
    tally.expect(holds_no_reading(second, "activate of the second transaction under a failed read"));

    give_each_read(10, 0);
    auto* third = log.begin_tx(owner, 3);
    give_each_read(11, 0);
    tally.expect(commit(third, regions[2]));
    give_each_read(12, 0);
    tally.expect(log.activate(owner, third) == second);
    tally.expect(holds_reading(second, 12000000000, "the second transaction, displaced at 12 s"));
    tally.expect(holds_reading(third, 12000000000, "activate of the third transaction at 12 s"));

    fail_each_read(EIO);
    tally.expect(log.rollback(owner));
    tally.expect(holds_no_reading(third, "the third transaction, rolled back under a failed read"));
    tally.expect(holds_no_reading(second, "the second transaction, restored under a failed read"));
    use_the_kernel_clock();
}

}  // namespace

int main() {
    Tally tally;
    realtime_reader_refuses_what_it_cannot_stamp(tally);
    monotonic_reader_gives_no_floor_for_a_failed_read(tally);
    transaction_keeps_no_reading_after_a_failed_read(tally);
    if (tally.failures != 0) {
        std::fprintf(stderr, "test_clock_failed_read: %d check(s) failed\n", tally.failures);
        return 1;
    }
    crucible::test::pass("test_clock_failed_read: all cases passed\n");
    return 0;
}

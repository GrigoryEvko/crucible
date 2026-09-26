// The clock-source band and the readers minted over it, exercised at
// run time.
//
// These cases used to be `runtime_smoke_test` functions inside
// fixy/os/ClockSource.h and fixy/os/Time.h, compiled into every
// translation unit that included either header.  What they check is
// behaviour under a constructed sequence of calls, not a property of
// the types as shipped, so they belong here.  The properties of the
// types — which tier a source declares, which source subsumes which,
// that two sources are distinct types — stay in the headers, where they
// fire wherever the type is used.
//
// The arguments below are non-constant on purpose.  A suite made only
// of static_asserts masks the bugs that appear when a body is
// instantiated for runtime evaluation rather than folded.

#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <fixy/os/ClockSource.h>
#include <fixy/os/Sched.h>
#include <fixy/os/Time.h>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <limits>
#include <system_error>
#include <type_traits>
#include <utility>

namespace eff = foundation::effects;
namespace ml = foundation::algebra::lattices;

namespace {

using BootU64 = fixy::BootClockBytes<std::uint64_t>;
using MonoU64 = fixy::MonotonicClockBytes<std::uint64_t>;

// The named contexts these mints are meant to take belong to a header
// the tree does not have yet.  These two stand in, in the shape
// foundation's own context self-test uses.  Each is handed the
// capability it claims: a context is not evidence of a capability, it
// carries one.
using InitWitness = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::Alloc, eff::Effect::IO>>;
using BlockWitness =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;
using BgWitness = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

// Every reading here comes from a clock.  The constructor that stamped a
// literal with a source is private now, and the reader is the one door,
// so the round trip starts from two reads rather than from two numbers.
[[nodiscard]] int clock_source_values_round_trip() {
    InitWitness init{eff::testing::init()};
    auto boot_reader = fixy::time::mint_clock_reader<fixy::ClockSource_v::Boot>(init);

    const BootU64 earlier = boot_reader.read();
    const BootU64 later = boot_reader.read();
    if (later.peek() < earlier.peek()) {
        std::fprintf(stderr, "two reads through one clamped boot reader went backwards\n");
        return 1;
    }

    // A copy of a reading is a reading of the same clock, and compares
    // equal to the original.
    BootU64 copied{earlier};
    if (!(copied == earlier) || copied.peek() != earlier.peek()) {
        std::fprintf(stderr, "a copied reading lost its value\n");
        return 1;
    }

    BootU64 first{earlier};
    BootU64 second{later};
    swap(first, second);
    if (first.peek() != later.peek() || second.peek() != earlier.peek()) {
        std::fprintf(stderr, "swap did not exchange two same-source readings\n");
        return 1;
    }

    const std::uint64_t expected = later.peek();
    if (BootU64{later}.consume() != expected) {
        std::fprintf(stderr, "consume did not move the value out\n");
        return 1;
    }

    // The subsumption answers, read at run time rather than folded.  A
    // boot reading covers a boot requirement; a monotonic one does not,
    // because it pauses on suspend.
    const bool boot_covers = BootU64::satisfies<fixy::ClockSource_v::Boot>;
    const bool mono_covers = MonoU64::satisfies<fixy::ClockSource_v::Boot>;
    if (!boot_covers || mono_covers) {
        std::fprintf(stderr, "the suspend-behaviour subsumption answered wrongly at run time\n");
        return 1;
    }

    // A second source, read through its own reader.  The PMU source has no
    // reader in this tree, and nothing can build its readings.  Its type
    // stays nameable for the gates that read it.  The PTP source has a
    // reader of its own, and a case below exercises it.
    auto realtime_reader = fixy::time::mint_clock_reader<fixy::ClockSource_v::Realtime>(init);
    const fixy::RealtimeClockBytes<std::uint64_t> realtime = realtime_reader.read();
    if (realtime.peek() == 0) {
        std::fprintf(stderr, "the realtime clock read zero, which a running system never does\n");
        return 1;
    }
    static_assert(!std::is_constructible_v<fixy::PmuBytes<std::uint64_t>, std::uint64_t>);
    return 0;
}

[[nodiscard]] int readers_and_sleeper_run() {
    InitWitness init{eff::testing::init()};
    BlockWitness blocking{eff::testing::test()};

    auto boot_reader = fixy::time::mint_clock_reader<fixy::ClockSource_v::Boot>(init);
    const auto now = boot_reader.read();
    if (now.peek() == 0) {
        std::fprintf(stderr, "the boot clock read zero, which a running system never does\n");
        return 1;
    }

    auto sleeper = fixy::time::mint_bounded_sleep<1000>(blocking);
    sleeper.sleep_for(0);
    return 0;
}

// The leg the header deliberately withheld.
//
// fixy/os/Time.h says a TSC read needs a pin from fixy::mint_affinity,
// which lives in fixy/os/Sched.h, so the case belongs in a translation
// unit that includes both — this one.  The pin the old tree's smoke
// test used was minted for itself with no sched_setaffinity on the
// path, which is the forgery the proof exists to prevent, written by
// the tree the proof protects.  Here the pin is earned: mint_affinity
// calls sched_setaffinity and only hands one back if it succeeded.
[[nodiscard]] int tsc_read_through_an_earned_pin() {
    BgWitness bg{eff::testing::bg()};
    InitWitness init{eff::testing::init()};

    // The default posture is PinnedExplicit, which is the one a TSC
    // reader admits: an auto pin can still migrate.
    auto pin = fixy::sched::mint_affinity<ml::AffinityMask::single(0)>(bg);
    if (!pin) {
        // A restricted cpuset returns EINVAL and CPU 0 may be outside
        // it.  That is the environment, not a defect, so the leg is
        // skipped rather than failed.
        std::fprintf(stderr, "[skipped] no pin to CPU 0 available in this cpuset (errno %d)\n", pin.error());
        return 0;
    }

    auto reader = fixy::time::mint_tsc_reader<fixy::time::TscMode::Raw>(init, std::move(*pin));
    const auto first = reader.read();
    const auto second = reader.read();
    if (second.peek() < first.peek()) {
        std::fprintf(stderr, "the timestamp counter ran backwards on one pinned core\n");
        return 1;
    }
    return 0;
}

// The gate and the clamp of the monotonic reader, exercised.  A reader
// minted off the replay path never returns a value lower than the last
// one it returned, and the clamp holds when handed the regression no
// real clock produces.
// The gate's refusal of a foreground context is a compile-time fact and
// lives in test/fixy/neg/neg_os_clock_reader_foreground_ctx.cpp; here
// the gate admits a background and an init context.
[[nodiscard]] int monotonic_reader_never_regresses() {
    BgWitness bg{eff::testing::bg()};
    InitWitness init{eff::testing::init()};

    auto monotonic = fixy::time::mint_clock_reader<fixy::ClockSource_v::Monotonic>(bg);
    auto realtime = fixy::time::mint_clock_reader<fixy::ClockSource_v::Realtime>(init);

    const auto first = monotonic.read();
    const auto second = monotonic.read();
    if (second.peek() < first.peek()) {
        std::fprintf(stderr, "the monotonic reader returned a lower value on its second read\n");
        return 1;
    }
    if (realtime.read().peek() == 0) {
        std::fprintf(stderr, "the realtime clock read zero, which a running system never does\n");
        return 1;
    }

    // Two consecutive reads of a working clock cannot show the clamp
    // doing anything, so it is driven on its own with the sequence the
    // kernel promises never to send: a value below the floor.  The floor
    // wins, and a later value above it advances it.
    std::uint64_t high = 1000;
    std::uint64_t low = 500;
    auto last_returned = fixy::mint_atomic_monotonic<std::uint64_t>(0);
    if (fixy::time::clamp_non_decreasing(last_returned, high) != high) {
        std::fprintf(stderr, "the clamp did not pass a value above the floor through\n");
        return 1;
    }
    if (fixy::time::clamp_non_decreasing(last_returned, low) != high) {
        std::fprintf(stderr, "the clamp let a regressing value through\n");
        return 1;
    }
    if (fixy::time::clamp_non_decreasing(last_returned, high + low) != high + low) {
        std::fprintf(stderr, "the clamp did not advance past a stale floor\n");
        return 1;
    }
    if (last_returned.get() != high + low) {
        std::fprintf(stderr, "the floor did not record the highest value returned\n");
        return 1;
    }
    return 0;
}

// The PTP reader.  A build host usually has no /dev/ptpN.  This case then
// takes the device-missing path: the mint refuses with the errno of the
// failed open and gives no reader.  If the device exists, one read must
// give a reading or an error, and never a clamped value.
//
// The case also drives the two pure steps of a read with values that no
// working clock returns.  The first step gives the clock id of a
// descriptor, and the case decodes that id back to the descriptor.  The
// second step converts a timespec.  It refuses a negative field, an
// out-of-range nanosecond field and a count that does not fit in 64 bits.
[[nodiscard]] int ptp_reader_refuses_what_it_cannot_stamp() {
    using BgFsWitness = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO, eff::Effect::Block>>;
    BgFsWitness bg{eff::testing::bg()};

    std::uint16_t absent_index = 254;
    const fixy::time::PtpDeviceIndex index = fixy::mint_refined<fixy::bounded_above<std::uint16_t{255}>>(absent_index);
    auto reader = fixy::time::mint_ptp_clock_reader(bg, index);
    if (reader) {
        const auto reading = reader->read();
        if (!reading && reading.error() == std::errc::result_out_of_range) {
            std::fprintf(stderr, "the PTP clock returned a negative or out-of-range timespec\n");
            return 1;
        }
        const auto caps = reader->caps();
        if (!caps && caps.error().value() == 0) {
            std::fprintf(stderr, "the PTP capability query failed with no errno\n");
            return 1;
        }
    } else if (reader.error() != std::errc::no_such_file_or_directory && reader.error() != std::errc::permission_denied) {
        std::fprintf(stderr, "the PTP mint failed with an unexpected error: %s\n", reader.error().message().c_str());
        return 1;
    }

    for (int descriptor : {0, 3, 17, 1023}) {
        const ::clockid_t id = fixy::time::detail::ptp_clockid_from_fd(descriptor);
        const unsigned int decoded = ~static_cast<unsigned int>(id) >> 3u;
        if (decoded != static_cast<unsigned int>(descriptor) || (static_cast<unsigned int>(id) & 7u) != 3u) {
            std::fprintf(stderr, "the PTP clock id of descriptor %d does not decode back to it\n", descriptor);
            return 1;
        }
    }

    long seconds = 2;
    long nanos = 5;
    const auto good = fixy::time::ptp_nanos_from_timespec(std::timespec{seconds, nanos});
    if (!good || *good != 2000000005ULL) {
        std::fprintf(stderr, "the PTP conversion changed a valid reading\n");
        return 1;
    }
    for (std::timespec refused : {std::timespec{-seconds, 0}, std::timespec{0, -nanos},
                                  std::timespec{0, 1000000000L}}) {
        const auto result = fixy::time::ptp_nanos_from_timespec(refused);
        if (result || result.error() != std::errc::result_out_of_range) {
            std::fprintf(stderr, "the PTP conversion accepted a negative or out-of-range timespec\n");
            return 1;
        }
    }
    const auto overflow = fixy::time::ptp_nanos_from_timespec(
        std::timespec{std::numeric_limits<std::time_t>::max(), 999999999L});
    if (overflow || overflow.error() != std::errc::value_too_large) {
        std::fprintf(stderr, "the PTP conversion did not refuse a count past 64 bits\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = clock_source_values_round_trip(); rc != 0) return rc;
    if (const int rc = readers_and_sleeper_run(); rc != 0) return rc;
    if (const int rc = tsc_read_through_an_earned_pin(); rc != 0) return rc;
    if (const int rc = monotonic_reader_never_regresses(); rc != 0) return rc;
    if (const int rc = ptp_reader_refuses_what_it_cannot_stamp(); rc != 0) return rc;
    return 0;
}

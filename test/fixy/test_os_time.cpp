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

#include <fixy/os/ClockSource.h>
#include <fixy/os/Sched.h>
#include <fixy/os/Time.h>

#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace eff = foundation::effects;
namespace ml = foundation::algebra::lattices;

namespace {

using BootU64 = fixy::BootClockBytes<unsigned long long>;
using MonoU64 = fixy::MonotonicClockBytes<unsigned long long>;

// The named contexts these mints are meant to take belong to a header
// the tree does not have yet.  These two stand in, in the shape
// foundation's own context self-test uses.  Each is handed the
// capability it claims: a context is not evidence of a capability, it
// carries one.
using InitWitness = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::Alloc, eff::Effect::IO>>;
using BlockWitness =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;
using BgWitness = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

[[nodiscard]] int clock_source_values_round_trip() {
    unsigned long long seed = 21;

    BootU64 boot{seed * 2};
    if (boot.peek() != 42) {
        std::fprintf(stderr, "a clock-source band did not carry its value\n");
        return 1;
    }
    boot.peek_mut() = 9;
    if (boot.peek() != 9) {
        std::fprintf(stderr, "peek_mut did not write through\n");
        return 1;
    }

    auto minted = fixy::mint_clock_source<fixy::ClockSource_v::TscRaw, unsigned long long>(seed);
    if (std::move(minted).consume() != 21) {
        std::fprintf(stderr, "consume did not move the value out\n");
        return 1;
    }

    BootU64 first{1};
    BootU64 second{2};
    swap(first, second);
    if (first.peek() != 2 || second.peek() != 1) {
        std::fprintf(stderr, "swap did not exchange two same-source readings\n");
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

    fixy::RealtimeClockBytes<unsigned long long> realtime{123};
    fixy::PmuBytes<unsigned long long> pmu{456};
    if (realtime.peek() != 123 || pmu.peek() != 456) {
        std::fprintf(stderr, "a non-boot source did not carry its value\n");
        return 1;
    }
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

}  // namespace

int main() {
    if (const int rc = clock_source_values_round_trip(); rc != 0) return rc;
    if (const int rc = readers_and_sleeper_run(); rc != 0) return rc;
    if (const int rc = tsc_read_through_an_earned_pin(); rc != 0) return rc;
    return 0;
}

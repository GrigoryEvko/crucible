// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The kernel reads the deadline of a futex wait as a time of
// CLOCK_MONOTONIC, so Deadline::after takes only a reader of that clock.  A
// reader of CLOCK_MONOTONIC_RAW gives the time of a different clock, so the
// door refuses it and names the concept of the reader.
//
// Expected diagnostic: no candidate of after, with a note on the concept of
// the reader.

#include <fixy/os/Futex.h>
#include <fixy/os/Time.h>
#include <foundation/effects/Ctx.h>

namespace eff = ::foundation::effects;

int main() {
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    auto const clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::MonotonicRaw>(ctx);
    [[maybe_unused]] auto deadline = ::fixy::futex::Deadline::after(clock, 1000);
    return 0;
}

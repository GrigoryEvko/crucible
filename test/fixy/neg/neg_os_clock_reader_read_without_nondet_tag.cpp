// The read of a clock reader takes the bare tag of Nondet.  A read with no
// tag has no overload, so a body that holds a reader but no tag reads
// nothing through it.
//
// Expected diagnostic: no candidate of read with no argument.

#include <fixy/Ctx.h>
#include <fixy/os/Time.h>
#include <foundation/effects/Effect.h>

int main() {
    const fixy::TestRunnerCtx ctx{foundation::effects::testing::test()};
    const auto reader = fixy::time::mint_clock_reader<fixy::ClockSource_v::Monotonic>(ctx);
    [[maybe_unused]] auto reading = reader.read();
    return 0;
}

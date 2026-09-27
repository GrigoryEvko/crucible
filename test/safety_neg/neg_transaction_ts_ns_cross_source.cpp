// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Transaction::ts_ns must hold a reading of the monotonic clock.  A boot
// clock reading counts time spent in suspend, and a wall clock reading
// jumps when the system time is corrected, so either would break the
// ordering that the log relies on.  Each clock source is a distinct
// wrapper type, and no conversion joins two of them.
//
// The fixture takes a real reading of the boot clock through a reader,
// which is the only way to hold one.
//
// Companion: neg_transaction_ts_ns_bare_u64_assign.cpp refuses a bare
// integer.

#include <crucible/Transaction.h>
#include <fixy/Ctx.h>
#include <fixy/os/Time.h>
#include <foundation/effects/Effect.h>

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    const auto boot_reader = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Boot>(ctx);

    crucible::Transaction tx{};
    tx.ts_ns = *boot_reader.read();
    return 0;
}

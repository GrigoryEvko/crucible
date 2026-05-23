// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Transaction-3 #1062, mismatch class #2 of 2:
// CROSS-SOURCE CLOCK ASSIGNMENT IS REJECTED.
//
// Transaction::ts_ns must specifically carry the Monotonic source.
// CLOCK_BOOTTIME (BootClockBytes) ticks across system suspend;
// CLOCK_REALTIME (WallClockBytes) is NTP-jumpy and can move
// backwards.  Either would corrupt the per-transaction monotonicity
// invariant the log relies on for ordering and replay determinism.
// Each ClockSource_v enumerator pins a distinct NTTP so the wrapper
// types are unrelated — assignment between them is a compile-time
// reject by the type system, not by a runtime check.
//
// Distinct from the bare-u64 fixture which fails because the source
// has NO wrap at all; here both sides ARE wrapped, but the source
// lattice value disagrees.
//
// Expected diagnostic: no match for 'operator=' / cannot convert /
// no viable / conversion from.

#include <crucible/Transaction.h>
#include <crucible/safety/ClockSource.h>

#include <cstdint>

int main() {
    crucible::Transaction tx{};

    // Mint a BootClockBytes<u64> witness (CLOCK_BOOTTIME provenance).
    auto boot_bytes = ::crucible::safety::mint_clock_source<
        ::crucible::safety::ClockSource_v::Boot,
        std::uint64_t>(42);

    // Should FAIL: BootClockBytes<u64> and MonotonicClockBytes<u64>
    // are unrelated wrapper types (distinct ClockSource_v NTTPs); no
    // implicit conversion exists between them.
    tx.ts_ns = boot_bytes;

    return 0;
}

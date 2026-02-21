#pragma once

// Shared test utilities for Crucible integration tests.
//
// All synchronization is pure spin on atomic::load(acquire).
// MESI delivers cache-line invalidations in 10-40ns.
// No sleep_for, no yield, no futex, no condition_variable — ever.

#include <crucible/Vigil.h>
#include <cassert>
#include <cstdint>

namespace crucible::test {

// Wait until Vigil reaches COMPILED mode.
// Pure spin — after flush(), mode_ is already visible (release/acquire
// on total_processed guarantees it). This loop exists as a safety net.
inline void wait_mode_compiled(Vigil& vigil) {
    uint64_t spins = 0;
    while (!vigil.is_compiled()) {
        assert(++spins < 100'000'000
               && "Vigil did not reach COMPILED mode");
        CRUCIBLE_SPIN_PAUSE;
    }
}

// Flush all pending ring entries AND wait for COMPILED mode.
//
// flush() spins on total_processed — returns only when bg thread
// has fully processed all entries (drain + IterationDetector +
// build_trace + make_region + callback). After flush(), is_compiled()
// should be true on the first check (release/acquire guarantees
// visibility of mode_). The spin loop is a structural safety net.
inline void flush_and_wait_compiled(Vigil& vigil) {
    vigil.flush();
    assert(vigil.flush_complete()
           && "flush() returned but bg thread did not finish processing");
    wait_mode_compiled(vigil);
}

} // namespace crucible::test

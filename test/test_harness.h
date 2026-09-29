#pragma once

// Synchronization in this header is a pure spin on an acquire load.  Do not
// add sleep_for, yield, futex or condition_variable.

#include <crucible/Vigil.h>
#include <fixy/Tags.h>
#include "test_assert.h"
#include <cstdint>

namespace crucible::test {

// Waits for the background thread to publish a region.  Replay has not
// started at that point: the foreground aligns to the region on its next
// dispatches, and Vigil::is_compiled turns true only when it activates.
//
// After flush() the publication is already visible, because the release and
// acquire on total_processed order it.  This loop is a safety net.
inline void wait_region_published(Vigil& vigil) {
    uint64_t spins = 0;
    while (!vigil.has_pending_region()) {
        assert(++spins < 100000000 && "the background thread did not publish a region");
        CRUCIBLE_SPIN_PAUSE;
    }
}

inline void flush_and_wait_region_published(Vigil& vigil) {
    vigil.flush();
    assert(vigil.flush_complete() && "flush() returned but bg thread did not finish processing");
    wait_region_published(vigil);
}

// Certifies an Entry a test built field by field, so it can reach
// record_op and dispatch_op, which take the second trust tag.
//
// This lives in the harness rather than beside TraceRing because it runs
// no check: it states that the fields are whatever the test wrote, which
// is true of a literal in a test and is not true of anything an adapter
// fills from a foreign runtime. An adapter reaching a recording entry
// point runs the checks in vessel_api_typed.h and crosses the retag
// edge, and it has nothing shorter to reach for, because this is not on
// its include path.
//
// A test that wants the adapter's checks exercised should call the
// adapter. This is for the tests that drive the Vigil directly.
[[nodiscard]] inline TraceRing::ValidatedEntryPtr certify_synthetic_entry(const TraceRing::Entry& entry
                                                                          CRUCIBLE_LIFETIMEBOUND) noexcept {
    return mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>();
}

}  // namespace crucible::test

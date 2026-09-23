#pragma once

// Synchronization in this header is a pure spin on an acquire load.  Do not
// add sleep_for, yield, futex or condition_variable.

#include <crucible/Vigil.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/fixy/Sched.h>
#include "test_assert.h"
#include <cstdint>
#include <expected>
#include <type_traits>

#ifdef __linux__
#include <sched.h>
#endif

namespace crucible::test {

inline void elevate_priority() {
#ifdef __linux__
    // An elevation that fails with EPERM under a non-root runner is absorbed.
    // The tests still run, with more scheduler noise.
    //
    // The affinity probe stays a raw sched_setaffinity because it pins to the
    // calling thread's current cpu, which is a runtime choice.  The mint path
    // takes the mask as a template argument and cannot express it.
    auto p = ::crucible::fixy::sched::mint_priority<-10>(::crucible::effects::TestRunnerCtx{});
    (void)p;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(static_cast<size_t>(sched_getcpu()), &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);
#endif
}

// Any drift in the nice value or in the mint return shape trips here, in every
// TU that includes this header.
#ifdef __linux__
static_assert(
    std::is_same_v<decltype(::crucible::fixy::sched::mint_priority<-10>(::crucible::effects::TestRunnerCtx{})),
                   std::expected<::crucible::fixy::sched::SchedPriority<-10>, int>>,
    "elevate_priority must mint SchedPriority<-10> via "
    "fixy::sched::mint_priority<-10>(TestRunnerCtx).");
#endif

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
    return TraceRing::ValidatedEntryPtr{&entry};
}

}  // namespace crucible::test

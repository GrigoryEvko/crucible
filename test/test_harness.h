#pragma once

// Shared test utilities for Crucible integration tests.
//
// All synchronization is pure spin on atomic::load(acquire).
// MESI delivers cache-line invalidations in 10-40ns.
// No sleep_for, no yield, no futex, no condition_variable — ever.

#include <crucible/Vigil.h>
#include <crucible/effects/ExecCtx.h>   // FIXY-V-200: TestRunnerCtx
#include <crucible/fixy/Sched.h>        // FIXY-V-200: mint_priority<-10>
#include "test_assert.h"
#include <cstdint>
#include <expected>                     // FIXY-V-200: mint_priority return
#include <type_traits>                  // FIXY-V-200: sentinel static_assert

#ifdef __linux__
#include <sched.h>
#endif

namespace crucible::test {

inline void elevate_priority() {
#ifdef __linux__
    // FIXY-V-200: setpriority(PRIO_PROCESS, 0, -10) → mint_priority<-10>
    // through the test-runner ExecCtx.  TestRunnerCtx is the freely-
    // constructible alias whose row owns Test+Alloc+IO+Block — it
    // satisfies CtxFitsPriorityMint (which only requires IsExecCtx +
    // Nice ∈ [-20, 19]) and documents at the type level that this is
    // test-harness elevation, not production scheduling.  EPERM under
    // non-root is silently absorbed (matches the prior unconditional
    // `setpriority(...)` discipline — tests still run, just with more
    // scheduler noise).  Affinity probe stays a raw sched_setaffinity:
    // the test harness picks the calling thread's *current* cpu via
    // sched_getcpu and pins to it; that's a runtime selection by
    // construction and cannot use the static-NTTP fixy::sched::
    // mint_affinity<Mask> path.  apply_affinity_to_cpu(ctx, cpu) (the
    // runtime sibling) requires a Bg/Init row, which TestRunnerCtx
    // lacks by design; raw sched_setaffinity is the documented escape
    // hatch for tests that mirror prod thread-pinning behavior.
    auto p = ::crucible::fixy::sched::mint_priority<-10>(
        ::crucible::effects::TestRunnerCtx{});
    (void)p;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(static_cast<size_t>(sched_getcpu()), &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);
#endif
}

// FIXY-V-200 sentinel: mirror V-197's bench_harness pin.  The
// elevate_priority() body mints a SchedPriority<-10>; any drift in
// the nice value or return shape trips here at every TU including
// test_harness.h.  TestRunnerCtx is the test-side dual of V-197's
// ColdInitCtx — both ExecCtx aliases satisfy CtxFitsPriorityMint.
#ifdef __linux__
static_assert(
    std::is_same_v<
        decltype(::crucible::fixy::sched::mint_priority<-10>(
            ::crucible::effects::TestRunnerCtx{})),
        std::expected<::crucible::fixy::sched::SchedPriority<-10>, int>>,
    "FIXY-V-200: test elevate_priority must mint SchedPriority<-10> "
    "via fixy::sched::mint_priority<-10>(TestRunnerCtx).");
#endif

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

#pragma once

// Synchronization in this header is a pure spin on an acquire load.  Do not
// add sleep_for, yield, futex or condition_variable.

#include <crucible/Vigil.h>
#include <crucible/effects/ExecCtx.h>
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

// After flush() the mode is already visible, because the release and acquire on
// total_processed order it.  This loop is a safety net.
inline void wait_mode_compiled(Vigil& vigil) {
    uint64_t spins = 0;
    while (!vigil.is_compiled()) {
        assert(++spins < 100000000 && "Vigil did not reach COMPILED mode");
        CRUCIBLE_SPIN_PAUSE;
    }
}

inline void flush_and_wait_compiled(Vigil& vigil) {
    vigil.flush();
    assert(vigil.flush_complete() && "flush() returned but bg thread did not finish processing");
    wait_mode_compiled(vigil);
}

}  // namespace crucible::test

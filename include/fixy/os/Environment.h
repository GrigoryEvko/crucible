#pragma once

// The environment door of fixy: the switches of the environment of the
// process, each with its parsed type.
//
// A start-up hook of src/fixy/os/ExitHooks.cpp, the one includer of
// <stdlib.h>, reads each switch through getenv of the C library one time.
// The C library owns the environment (decision D12 of the plan).  The hook
// runs before main, or in the dlopen of the shared library that holds the
// door, and no code writes the record after it.  So no later code reads the
// environment, a setenv after the start changes no switch, and each read of
// the record gives the same value for the life of the process.
//
// The record names each switch that a program of the tree reads.  A new
// switch adds one field here and one read to the hook.

#include <foundation/core/Choice.h>

#include <cstdint>

namespace fixy::env {

// The switches of the environment at the start of the process.  The record
// holds parsed values only, and no text of the environment.  A switch whose
// variable is absent has the value of its field initializer.
struct Switches {
    // CRUCIBLE_PERF_QUIET: the perf layer reports no failed load.  The
    // switch is on when its value starts with the character 1.
    bool is_perf_quiet = false;
    // CRUCIBLE_PERF_VERBOSE: the perf layer reports each step of a load and
    // the log of libbpf.  The switch is on when its value starts with the
    // character 1.
    bool is_perf_verbose = false;
    // CRUCIBLE_WARDEN_QUIET: the warden reports no knob that it skips.  The
    // switch is on when its value is the text 1.
    bool is_warden_quiet = false;
    // CRUCIBLE_WARDEN_FORCE: the warden sets a real-time class on a CPU that
    // is not isolated.  The switch is on when its value is the text 1.
    bool is_warden_forced = false;
    std::uint32_t pad_after_flags = 0;
    // The sample periods of the PMU events of the perf layer, each a decimal
    // number above zero.  No value stands for an absent variable, a text
    // that is no decimal number, a sign, or zero, and the caller then takes
    // the period of its event.
    //   CRUCIBLE_PERF_PMU_PERIOD_HW    the cache, branch and TLB events
    //   CRUCIBLE_PERF_PMU_PERIOD_IBS   the two events of the dynamic AMD PMU
    //   CRUCIBLE_PERF_PMU_PERIOD_SW    the software events
    ::foundation::core::Option<std::uint64_t> pmu_period_hardware{};
    ::foundation::core::Option<std::uint64_t> pmu_period_dynamic{};
    ::foundation::core::Option<std::uint64_t> pmu_period_software{};
};

// The switches that the start-up hook read.  The call copies a record that
// no code writes after the start, so it takes no lock and no atomic, and
// each thread can call it.
[[nodiscard]] Switches switches() noexcept;

}  // namespace fixy::env

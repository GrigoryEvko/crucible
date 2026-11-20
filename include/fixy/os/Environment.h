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

#include <fixy/os/Path.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>
#include <foundation/os/PathText.h>

#include <cstddef>
#include <cstdint>

namespace fixy::env {

// The largest count of the characters of the path of TMPDIR.  A program makes
// paths under the temporary directory, so the bound keeps most of the
// kPathBytes characters of a path for them.
inline constexpr std::size_t kTemporaryDirectoryBytes = 1024;

// The checked path of TMPDIR.
using TemporaryDirectoryPath = ::foundation::os::PathText<kTemporaryDirectoryBytes>;

// The checked path of a value of TMPDIR: an absolute path with no ".." part,
// of at most kTemporaryDirectoryBytes characters.  A value that fails a check
// gives no value.  Complexity: linear in the size of the value.
[[nodiscard]] constexpr ::foundation::core::Option<TemporaryDirectoryPath>
check_temporary_directory(::foundation::core::TextView value) noexcept {
    if (!::foundation::core::starts_with(value, "/")) return ::foundation::core::none;
    if (::fixy::sanitize::path_traversal::check_no_dotdot(value).is_err()) return ::foundation::core::none;
    TemporaryDirectoryPath path{};
    if (path.append(value).is_err()) return ::foundation::core::none;
    return ::foundation::core::Option<TemporaryDirectoryPath>::some(path);
}

// The DEBUGINFOD_URLS switch of a value of the variable: on when the value
// holds one character or more, and off for an empty value and for no
// variable.
[[nodiscard]] constexpr bool
has_debuginfod_server(::foundation::core::Option<::foundation::core::TextView> value) noexcept {
    return value.match([](::foundation::core::TextView text) noexcept { return text.size() != 0; },
                       [] noexcept { return false; });
}

// The switches of the environment at the start of the process.  The record
// holds parsed values only.  A path of the record is a parsed value too: the
// hook checks the text of its variable, and the record holds the checked path
// or no value.  A switch whose variable is absent has the value of its field
// initializer.
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
    // DEBUGINFOD_URLS: the servers that valgrind asks for the debug
    // information of each loaded object at its start.  The switch is on when
    // the value holds one character or more (has_debuginfod_server).
    bool has_debuginfod_server = false;
    std::uint8_t pad_after_flags = 0;
    std::uint16_t pad_before_periods = 0;
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
    // TMPDIR: the temporary directory of the host, which the temporary
    // directory door of fixy/os/Fs.h reads.  No value stands for an absent
    // variable and for a value that check_temporary_directory refuses, and
    // the door then takes /tmp.
    ::foundation::core::Option<TemporaryDirectoryPath> temporary_directory{};
};

// The switches that the start-up hook read.  The call gives a reference to
// the one record of the process, which has static storage and which no code
// writes after the start.  So the call copies no byte of the record, takes no
// lock and no atomic, and each thread can call it.  A caller reads a field of
// the record in place: an Option field takes the borrow match, which keeps
// the field as it is.
[[nodiscard]] Switches const& switches() noexcept;

}  // namespace fixy::env

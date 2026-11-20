// The door of fixy to the process state that the C library keeps: the exit
// hooks (fixy/os/ExitHooks.h) and the environment (fixy/os/Environment.h).
// It is the one includer of <stdlib.h>.

#include <fixy/os/Environment.h>
#include <fixy/os/ExitHooks.h>

#include <foundation/core/Algorithms.h>
#include <foundation/core/Parse.h>
#include <foundation/core/Text.h>

#include <stdlib.h>

#include <cstddef>
#include <cstdint>

namespace fixy::exit::detail {

::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> run_at_exit_(ExitHook hook) noexcept {
    if (::atexit(hook) != 0) return ::foundation::core::err(::foundation::os::Errno::enomem);
    if (::at_quick_exit(hook) != 0) return ::foundation::core::err(::foundation::os::Errno::enomem);
    return ::foundation::core::Unit{};
}

}  // namespace fixy::exit::detail

namespace fixy::env {

namespace {

namespace core = ::foundation::core;

// The largest count of the characters of a TextView.  The count loop below
// stops there too, and the second exit keeps the optimizer from a call of
// strlen of the C library, which the rule of the symbols of the base refuses.
constexpr std::size_t kLongestValue = static_cast<std::size_t>(__PTRDIFF_MAX__);

// The characters of the value of the variable name, or no value when the
// environment holds no such variable.  The TextView borrows the storage of
// the environment of the C library, so it lives only in the start-up hook.
// Complexity: the scan of the C library over each variable, and one more
// pass over the characters of the value to count them.
[[nodiscard]] core::Option<core::TextView> read_value(core::CText name) noexcept {
    char const* const value = ::getenv(core::detail::c_text_first_(name));
    if (value == nullptr) return core::none;
    std::size_t count = 0;
    while (count < kLongestValue && value[count] != '\0') {
        ++count;
    }
    return core::Option<core::TextView>::some(core::detail::text_over_(value, count));
}

// True when the variable name holds a value that starts with the character 1.
[[nodiscard]] bool has_leading_one(core::CText name) noexcept {
    return read_value(name).match([](core::TextView value) noexcept { return core::starts_with(value, "1"); },
                                  [] noexcept { return false; });
}

// True when the variable name holds the text 1 and no other character.
[[nodiscard]] bool has_sole_one(core::CText name) noexcept {
    return read_value(name).match([](core::TextView value) noexcept { return value == "1"; },
                                  [] noexcept { return false; });
}

// The decimal number above zero that the variable name holds.  The grammar
// of parse refuses a sign, so "-5" does not become a value near the maximum
// of the type.  A zero period would turn the sampling off, so zero gives no
// value too.
[[nodiscard]] core::Option<std::uint64_t> read_period(core::CText name) noexcept {
    using Period = core::Option<std::uint64_t>;
    return read_value(name).match(
        [](core::TextView value) noexcept -> Period {
            return core::parse<std::uint64_t>(value).match(
                [](std::uint64_t period) noexcept -> Period {
                    if (period == 0) return core::none;
                    return Period::some(period);
                },
                [](core::ParseError) noexcept -> Period { return core::none; });
        },
        [] noexcept -> Period { return core::none; });
}

// The checked path that the variable name holds (check_temporary_directory).
[[nodiscard]] core::Option<TemporaryDirectoryPath> read_temporary_directory(core::CText name) noexcept {
    return read_value(name).match([](core::TextView value) noexcept { return check_temporary_directory(value); },
                                  [] noexcept -> core::Option<TemporaryDirectoryPath> { return core::none; });
}

// The switches of the process.  The hook below writes the record one time,
// before main, or in the dlopen of the shared library that holds the door.
// No code writes it after that, and each read comes after the write: the
// thread of the read starts after the hook, or it reaches the door through
// the dlopen that ran the hook.  So the read needs no atomic.  Each shared
// library that holds the door has its own copy, and only the code of that
// library reads it.
constinit Switches switches_at_start{};

// The read of the switches.  The priority 103 comes after the start-up check
// of the kernel floor (101) and the count of the CPUs of the process (102),
// and before each constructor with no priority, so a static object of a
// program reads the switches of the environment.
[[gnu::constructor(103)]] void read_switches_at_start() noexcept {
    switches_at_start = Switches{
        .is_perf_quiet = has_leading_one("CRUCIBLE_PERF_QUIET"),
        .is_perf_verbose = has_leading_one("CRUCIBLE_PERF_VERBOSE"),
        .is_warden_quiet = has_sole_one("CRUCIBLE_WARDEN_QUIET"),
        .is_warden_forced = has_sole_one("CRUCIBLE_WARDEN_FORCE"),
        .has_debuginfod_server = has_debuginfod_server(read_value("DEBUGINFOD_URLS")),
        .pmu_period_hardware = read_period("CRUCIBLE_PERF_PMU_PERIOD_HW"),
        .pmu_period_dynamic = read_period("CRUCIBLE_PERF_PMU_PERIOD_IBS"),
        .pmu_period_software = read_period("CRUCIBLE_PERF_PMU_PERIOD_SW"),
        .temporary_directory = read_temporary_directory("TMPDIR"),
    };
}

}  // namespace

// The call gives a reference, which is never null.  GCC 16 at -O3 suggests
// the attribute returns_nonnull for a function that gives a reference, and it
// refuses that attribute on such a function, so the suggestion is off for this
// one body.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsuggest-attribute=returns_nonnull"
Switches const& switches() noexcept { return switches_at_start; }
#pragma GCC diagnostic pop

}  // namespace fixy::env

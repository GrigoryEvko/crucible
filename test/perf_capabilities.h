#pragma once

// The decision of a perf smoke test: a load that must succeed, or a skip.
//
// A process with CAP_BPF, CAP_PERFMON and CAP_DAC_READ_SEARCH in its
// effective set can load each BPF facade: the first two let it load the
// programs and open the perf events, and the third lets it read the
// tracepoint files of tracefs.  A load that gives none then is a defect, and
// the test fails.  A process without one of them cannot load, and the test
// prints one skip line with the reason and passes.
//
// The test reads the set from the line CapEff of /proc/self/status through
// the file door of the base.  An environment variable cannot carry the
// decision, because the test launcher drops the variables that it does not
// know.

#include <fixy/Core.h>
#include <foundation/core/Parse.h>
#include <foundation/os/File.h>

#include <cstddef>
#include <cstdint>

namespace crucible::test {

namespace perf_capabilities_detail {

// The bit of each capability in the set, from <linux/capability.h>.
inline constexpr std::uint64_t kDacReadSearch = std::uint64_t{1} << 2U;
inline constexpr std::uint64_t kPerfmon = std::uint64_t{1} << 38U;
inline constexpr std::uint64_t kBpf = std::uint64_t{1} << 39U;
inline constexpr std::uint64_t kLoadSet = kDacReadSearch | kPerfmon | kBpf;

// The hexadecimal digits after the key of the line that starts with key, or
// no value when no line starts with it.  Complexity: one pass over text.
[[nodiscard]] inline ::fixy::Option<::fixy::TextView> value_of_line(::fixy::TextView text,
                                                                    ::fixy::TextView key) noexcept {
    ::fixy::TextView rest = text;
    while (rest.size() != 0) {
        std::size_t const newline = ::fixy::find(rest, '\n').value_or(rest.size());
        ::fixy::TextView const line = rest.window(0, newline).value_or(::fixy::TextView{});
        if (::fixy::starts_with(line, key)) {
            return line.window(key.size(), line.size() - key.size());
        }
        std::size_t const next = newline < rest.size() ? newline + 1 : rest.size();
        rest = rest.window(next, rest.size() - next).value_or(::fixy::TextView{});
    }
    return ::fixy::none;
}

}  // namespace perf_capabilities_detail

// The effective capability set of the calling process, or zero when the
// status file cannot be read.
[[nodiscard]] inline std::uint64_t effective_capabilities() noexcept {
    ::fixy::FixedText<8192> status{};
    if (::foundation::os::read_file("/proc/self/status", status).is_err()) return 0;
    return perf_capabilities_detail::value_of_line(status.view(), "CapEff:\t")
        .match(
            [](::fixy::TextView digits) noexcept {
                return ::foundation::core::parse_hex<std::uint64_t>(digits).value_or(std::uint64_t{0});
            },
            [] noexcept { return std::uint64_t{0}; });
}

// True when the process holds each capability that a BPF load needs.
[[nodiscard]] inline bool can_load_bpf() noexcept {
    return (effective_capabilities() & perf_capabilities_detail::kLoadSet) == perf_capabilities_detail::kLoadSet;
}

// The verdict of a load that gave no facade: 1, a failure, for a process
// that can load, and 0, a skip with one line, for a process that cannot.
[[nodiscard]] inline int verdict_of_no_load(::fixy::TextView facade) noexcept {
    if (can_load_bpf()) {
        ::fixy::report(::fixy::Sink::Err,
                       "{}: the load gave none, and the process holds CAP_BPF, CAP_PERFMON and "
                       "CAP_DAC_READ_SEARCH\n",
                       facade);
        return 1;
    }
    ::fixy::report(::fixy::Sink::Out,
                   "[skipped] {}: the process lacks CAP_BPF, CAP_PERFMON or CAP_DAC_READ_SEARCH, so no load can "
                   "succeed\n",
                   facade);
    return 0;
}

}  // namespace crucible::test

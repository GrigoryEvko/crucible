// The cold reports of the Check family (fixy/check/Check.h).

#include <fixy/check/Check.h>

#include <fixy/os/Process.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
#include <foundation/os/ChildProcess.h>
#include <foundation/os/Errno.h>

#include <cstddef>

namespace fixy::check::detail {

namespace {

namespace core = ::foundation::core;

// The most characters of a name of a Site that a report writes.  The bound
// keeps the walk below finite in its own text, and a bounded walk is no call
// of strlen of the C library at -O3.
constexpr std::size_t kSiteNameBound = 4096;

// The view of a name of a Site, without its terminating zero, and of at most
// kSiteNameBound characters.  The consteval constructor of Site walked to
// that zero, so the walk ends there or at the bound.  Complexity: O(n) in the
// characters of the name.
[[nodiscard]] core::TextView view_of_site_name(char const* name) noexcept {
    std::size_t size = 0;
    while (size < kSiteNameBound && name[size] != '\0') {
        ++size;
    }
    return core::detail::text_over_(name, size);
}

[[noreturn]] void end_at(core::Site site) noexcept {
    core::detail::report_fatal_("expect_dies: the body did not end as the test expects", core::detail::site_file_(site),
                                core::detail::site_line_(site), core::detail::site_function_(site));
}

}  // namespace

void report_unexpected_end_(::fixy::process::ChildReport const& report, core::TextView expected,
                            core::Site site) noexcept {
    ::foundation::os::ChildEnd const end = report.end();
    core::TextView const how = end.kind == ::foundation::os::ChildEndKind::signaled ? core::TextView{"the signal"}
                                                                                    : core::TextView{"the exit code"};
    core::report(core::Sink::Err,
                 "expect_dies: the test expects the end by the signal {} with the text \"{}\" on standard error.\n"
                 "expect_dies: the child ended by {} {}.  Its standard error ({} earlier bytes dropped):\n{}\n",
                 static_cast<unsigned>(::foundation::os::kAbortSignal), expected, how,
                 static_cast<unsigned>(end.number), report.dropped_bytes(), report.output());
    end_at(site);
}

void report_unexpected_signal_(::fixy::process::ChildReport const& report, ::fixy::signal::FatalSignal expected,
                               core::Site site) noexcept {
    ::foundation::os::ChildEnd const end = report.end();
    core::TextView const how = end.kind == ::foundation::os::ChildEndKind::signaled ? core::TextView{"the signal"}
                                                                                    : core::TextView{"the exit code"};
    core::report(core::Sink::Err,
                 "expect_dies_by: the test expects the end by the signal {}.\n"
                 "expect_dies_by: the child ended by {} {}.  Its standard error ({} earlier bytes dropped):\n{}\n",
                 static_cast<unsigned>(::fixy::signal::number_of(expected)), how, static_cast<unsigned>(end.number),
                 report.dropped_bytes(), report.output());
    end_at(site);
}

void report_child_error_(::foundation::os::Errno error, core::Site site) noexcept {
    core::report(core::Sink::Err, "expect_dies: the child door failed with the error number {}.\n",
                 ::foundation::os::error_number(error));
    end_at(site);
}

void report_failed_check_(core::TextView test_name, core::TextView condition, core::Site site) noexcept {
    core::report(core::Sink::Err, "{}: check failed: {} at {}:{} in {}\n", test_name, condition,
                 view_of_site_name(core::detail::site_file_(site)), core::detail::site_line_(site),
                 view_of_site_name(core::detail::site_function_(site)));
}

void report_failed_tally_(Tally const& tally) noexcept {
    core::report(core::Sink::Err, "{}: {} of {} checks failed\n", tally.name_, tally.failures_, tally.checks_);
    core::fatal("a check of the Tally failed, so the test fails at its close");
}

void report_unclosed_tally_(core::TextView test_name) noexcept {
    core::report(core::Sink::Err, "{}: the Tally of the test ended with no close\n", test_name);
    core::fatal("the Tally of the test {} ended with no close, so its failures would be lost", test_name);
}

}  // namespace fixy::check::detail

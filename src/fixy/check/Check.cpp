// The cold reports of the Check family (fixy/check/Check.h).

#include <fixy/check/Check.h>

#include <fixy/os/Process.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
#include <foundation/os/ChildProcess.h>
#include <foundation/os/Errno.h>

namespace fixy::check::detail {

namespace {

namespace core = ::foundation::core;

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

void report_child_error_(::foundation::os::Errno error, core::Site site) noexcept {
    core::report(core::Sink::Err, "expect_dies: the child door failed with the error number {}.\n",
                 ::foundation::os::error_number(error));
    end_at(site);
}

}  // namespace fixy::check::detail

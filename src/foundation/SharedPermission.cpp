#include <foundation/permissions/SharedPermission.h>

#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
#include <foundation/diag/tags/Aborts.h>
#include <foundation/os/Abort.h>

namespace foundation::permissions {

namespace {

// The text of the abort of a saturated pool.  The consteval constructor of
// Fmt makes sure that each placeholder has an argument of the given type.
constexpr core::Fmt<core::TextView, core::TextView, core::TextView> saturated_text{
    "crucible: fatal contract violation: {}\n"
    "  description: {}\n"
    "  remediation: {}\n"};

}  // namespace

// The cold path of a lend that found the share count at its maximum.  It
// is in a source file, so a translation unit that includes
// SharedPermission.h does not read the diagnostic tag.  The text goes through
// the sink write of the Report family, and the process ends in the abort
// door.
void shared_permission_pool_saturated_abort_() noexcept {
    using Tag = ::foundation::diag::SharedPermissionPoolSaturated;
    core::detail::FmtArg const arguments[] = {
        core::detail::FmtArg{core::detail::FmtArgKind::text, 0, Tag::name.data(), Tag::name.size()},
        core::detail::FmtArg{core::detail::FmtArgKind::text, 0, Tag::description.data(), Tag::description.size()},
        core::detail::FmtArg{core::detail::FmtArgKind::text, 0, Tag::remediation.data(), Tag::remediation.size()},
    };
    core::detail::report_(core::Sink::Err, core::detail::fmt_text_(saturated_text), arguments,
                          sizeof arguments / sizeof arguments[0]);
    os::abort_process();
}

// The cold path of a pool that ends while a share is out.  It is in a source
// file, and no includer of SharedPermission.h compiles the report.
void shared_permission_pool_outlived_abort_() noexcept {
    core::report(core::Sink::Err,
                 "crucible: fatal contract violation: a SharedPermissionPool ended while shares were out.  Each "
                 "guard points into its pool, so the release of that guard would write freed memory.  End every "
                 "share before the pool ends.\n");
    os::abort_process();
}

}  // namespace foundation::permissions

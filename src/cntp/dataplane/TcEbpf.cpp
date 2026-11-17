#include <crucible/cntp/dataplane/TcEbpf.h>

namespace crucible::cntp::dataplane {

// The attach of a TC program needs CAP_NET_ADMIN and the netlink calls of
// clsact, and no door of the base gives them.  So the plan stays a plan.
::fixy::Result<::fixy::Unit, TcError> attach_tc_program(DeclaredTcProgram const& program) noexcept {
    static_cast<void>(program);
    return ::fixy::err(TcError::PrivilegedAttachDeferred);
}

}  // namespace crucible::cntp::dataplane

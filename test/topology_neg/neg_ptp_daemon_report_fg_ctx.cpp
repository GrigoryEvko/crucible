// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Admitting a report from the PTP daemons is background work.  The
// foreground hot-path context is refused.  A foreground context is built
// only from the producer claim, so the refused call names it in an
// unevaluated operand.

#include <crucible/topology/Ptp.h>

#include <utility>

int main() {
    return sizeof(crucible::topology::admit_ptp_daemon_report(std::declval<::fixy::HotFgCtx const&>(),
                                                              crucible::topology::PtpDaemonReport{})) == 0
               ? 1
               : 0;
}

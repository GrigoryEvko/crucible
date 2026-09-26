// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A status comes from a daemon report that a background context admitted.
// A raw report does not convert to the tagged one, so a report nobody
// admitted cannot become a status.

#include <crucible/topology/Ptp.h>

int main() {
    crucible::topology::PtpDaemonReport raw{};
    auto status = crucible::topology::ptp_status_from_daemon_report(raw);
    (void)status;
    return 0;
}

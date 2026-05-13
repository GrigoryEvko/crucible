#include <crucible/topology/Ptp.h>

int main() {
    auto report = crucible::topology::admit_ptp_daemon_report(
        crucible::effects::HotFgCtx{},
        crucible::topology::PtpDaemonReport{});
    (void)report;
    return 0;
}

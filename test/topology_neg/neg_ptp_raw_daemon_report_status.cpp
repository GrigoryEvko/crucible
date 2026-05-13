#include <crucible/topology/Ptp.h>

int main() {
    crucible::topology::PtpDaemonReport raw{};
    auto status = crucible::topology::ptp_status_from_daemon_report(raw);
    (void)status;
    return 0;
}

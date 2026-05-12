// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// GAPS-129 boundary fixture. Opening /dev/ptpN requires an admitted
// PtpDeviceIndex, not an unbounded raw integer.

#include <crucible/topology/Ptp.h>

int main() {
    auto clock = crucible::topology::open_ptp_clock(0);
    (void)clock;
    return 0;
}

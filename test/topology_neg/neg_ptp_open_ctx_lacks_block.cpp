// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Opening /dev/ptpN is a file system call that can hold the caller, so the
// context must admit IO and Block.  The background drain context admits
// neither and is refused.

#include <crucible/topology/Ptp.h>

int main() {
    ::fixy::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto index = crucible::topology::admit_ptp_device_index(0).value();
    auto clock = crucible::topology::open_ptp_clock(bg, index);
    (void)clock;
    return 0;
}

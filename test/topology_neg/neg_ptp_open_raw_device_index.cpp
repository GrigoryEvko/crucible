// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Opening /dev/ptpN takes an admitted PtpDeviceIndex.  A raw int does not
// convert to the bounded index, so an unchecked number cannot name a
// device.

#include <crucible/topology/Ptp.h>

int main() {
    ::foundation::effects::ExecCtx<
        ::foundation::effects::Bg,
        ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::IO,
                                   ::foundation::effects::Effect::Block>>
        ctx{::foundation::effects::testing::bg()};
    auto clock = crucible::topology::open_ptp_clock(ctx, 0);
    (void)clock;
    return 0;
}

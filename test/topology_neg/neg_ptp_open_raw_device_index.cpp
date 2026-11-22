// Negative-compile test.  This file must fail to compile.
//
// Opening /dev/ptpN takes an admitted PtpDeviceIndex.  A raw int does not
// convert to the bounded index, so an unchecked number cannot name a
// device.  The context owns each capability of the door, so the conversion
// is the one reason of the refusal.

#include <crucible/topology/Ptp.h>

int main() {
    ::foundation::effects::ExecCtx<
        ::foundation::effects::Bg,
        ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::IO,
                                   ::foundation::effects::Effect::Block, ::foundation::effects::Effect::Nondet>>
        ctx{::foundation::effects::testing::bg()};
    auto clock = crucible::topology::open_ptp_clock(ctx, 0);
    (void)clock;
    return 0;
}

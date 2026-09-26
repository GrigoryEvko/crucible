// An admission controller is built at initialization.  A background
// context owns no Init, so it cannot mint one, even a context that may
// block.

#include <crucible/cntp/BackpressureRuntime.h>
#include <fixy/Ctx.h>

int main() {
    namespace cntp = crucible::cntp;
    ::fixy::BgLoadCtx load{::foundation::effects::testing::bg()};
    auto controller = cntp::mint_admission_controller<2, 2>(load);
    return static_cast<int>(controller.live_connections());
}

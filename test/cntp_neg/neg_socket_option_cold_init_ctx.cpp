// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A socket option call takes the socket lock and can load a congestion
// control module, so its gate asks for IO and Block.  The cold-init context
// carries IO but no Block, and the gate refuses it.  The choice and the
// descriptor are well formed, so the context is the one thing refused.

#include <crucible/cntp/CongestionControl.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace cntp = crucible::cntp;

    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto fd = cntp::admit_socket_fd(0).value();
    auto choice = cntp::mint_cc_choice<cntp::CcAlgorithm::Cubic, cntp::LinkClass::CrossDatacenter>();
    auto result = cntp::set_cc_for_socket(init, fd, choice);
    (void)result;
    return 0;
}

// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A socket option call enters the kernel, so its gate asks for IO and
// Block.  The background drain context carries neither, and the gate
// refuses it.  The rate and the descriptor are well formed, so the context
// is the one thing refused.

#include <crucible/cntp/Pacing.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace cntp = crucible::cntp;

    ::fixy::BgDrainCtx drain{::foundation::effects::testing::bg()};
    auto fd = cntp::admit_socket_fd(0).value();
    auto rate = cntp::admit_pacing_rate(1000000).value();
    auto result = cntp::set_socket_pacing_rate(drain, fd, rate);
    (void)result;
    return 0;
}

// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Socket timestamping takes an admitted SocketFd.  A raw int does not
// convert to it, so an unchecked descriptor cannot reach setsockopt, also
// under a context that admits the socket calls.

#include <crucible/topology/Ptp.h>

using IoBlockCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Bg,
    ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::IO,
                               ::foundation::effects::Effect::Block>>;

int main() {
    IoBlockCtx const ctx{::foundation::effects::testing::bg()};
    auto enabled = crucible::topology::enable_socket_timestamping(ctx, 3);
    (void)enabled;
    return 0;
}

// Negative-compile test.  This file must fail to compile.
//
// Socket timestamping takes the ref of a socket that the doors of the base
// gave.  A raw int does not convert to it, so an unchecked descriptor cannot
// reach setsockopt, also under a context that admits the socket calls.

#include <crucible/topology/Ptp.h>

using IoBlockCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Bg,
    ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::IO,
                               ::foundation::effects::Effect::Block, ::foundation::effects::Effect::Nondet>>;

int main() {
    IoBlockCtx const ctx{::foundation::effects::testing::bg()};
    auto enabled = crucible::topology::enable_socket_timestamping(ctx, 3);
    (void)enabled;
    return 0;
}

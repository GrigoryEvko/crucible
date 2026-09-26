// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A kTLS socket is minted at initialization.  The background drain context
// owns no Init effect, so the mint refuses it.

#include <crucible/cntp/_wip/KtlsOffload.h>
#include <fixy/Ctx.h>

int main() {
    using namespace crucible::cntp::_wip;
    auto fd = admit_socket_fd(3).value();
    auto iface = NicInterfaceName::from("eth0").value();
    ::fixy::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto socket = mint_ktls_socket(bg, fd, iface);
    return socket.is_offload_active() ? 0 : 1;
}

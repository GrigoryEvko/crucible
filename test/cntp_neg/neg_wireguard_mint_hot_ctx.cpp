// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A WireGuard tunnel is minted at initialization.  The foreground context
// claims no effect, so the mint refuses it.

#include <crucible/cntp/_wip/Wireguard.h>
#include <fixy/Ctx.h>

#include <utility>

namespace wg = crucible::cntp::_wip;

int mint_on_hot_path(wg::DeclaredWireguardConfig config);

int mint_on_hot_path(wg::DeclaredWireguardConfig config) {
    const ::fixy::HotFgCtx hot = ::foundation::effects::testing::foreground();
    auto tunnel = wg::mint_wireguard_tunnel(hot, std::move(config));
    return tunnel.has_value() ? 0 : 1;
}

int main() { return 0; }

// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A WireGuard tunnel is minted at initialization.  The background drain
// context owns no Init effect, so the mint refuses it.

#include <crucible/cntp/_wip/Wireguard.h>
#include <fixy/Ctx.h>

#include <utility>

namespace wg = crucible::cntp::_wip;

int mint_from_background(wg::DeclaredWireguardConfig config);

int mint_from_background(wg::DeclaredWireguardConfig config) {
    ::fixy::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto tunnel = wg::mint_wireguard_tunnel(bg, std::move(config));
    return tunnel.has_value() ? 0 : 1;
}

int main() { return 0; }

// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A NIC configuration is startup work. The foreground row is empty, so
// the mint refuses the dispatch thread. A foreground context comes only
// from the producer claim, so the fixture takes one as a parameter.

#include <crucible/cog/NicConfig.h>

namespace cog = crucible::cog;
namespace nic = crucible::cog::nic;
namespace cntp = crucible::cntp;

[[maybe_unused]] static int mint_from_foreground(::fixy::HotFgCtx const& fg, cntp::NicInterfaceName iface) {
    auto config = nic::mint_nic_config(fg, cog::CogIdentity{}, iface);
    return config.has_value() ? 0 : 1;
}

int main() { return 0; }

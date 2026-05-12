#include <crucible/cntp/dataplane/Xdp.h>

int main() {
    crucible::cog::CogIdentity id{};
    id.kind = crucible::cog::CogKind::NicPort;
    crucible::cog::NicPortTargetCaps caps{};
    crucible::cntp::dataplane::XdpProgramSpec raw{};
    auto result = crucible::cntp::dataplane::xdp_admit_nic(id, caps, raw);
    return result.has_value() ? 0 : 1;
}

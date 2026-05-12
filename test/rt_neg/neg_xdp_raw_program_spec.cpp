#include <crucible/rt/Xdp.h>

int main() {
    crucible::cog::CogIdentity id{};
    id.kind = crucible::cog::CogKind::NicPort;
    crucible::cog::NicPortTargetCaps caps{};
    crucible::rt::XdpProgramSpec raw{};
    auto result = crucible::rt::xdp_admit_nic(id, caps, raw);
    return result.has_value() ? 0 : 1;
}

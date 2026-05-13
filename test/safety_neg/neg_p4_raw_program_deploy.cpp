#include <crucible/cntp/_wip/P4.h>

int main() {
    namespace cog = crucible::cog;
    namespace p4 = crucible::cntp::_wip::p4;

    cog::CogIdentity sw{};
    cog::NvSwitchTargetCaps caps{};
    p4::P4ProgramSpec raw{};
    auto deployed = p4::deploy_p4_program(sw, caps, raw);
    (void)deployed;
    return 0;
}

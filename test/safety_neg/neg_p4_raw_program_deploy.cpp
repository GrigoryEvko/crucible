#include <crucible/cntp/P4.h>

int main() {
    namespace cog = crucible::cog;
    namespace p4 = crucible::cntp::p4;

    cog::CogIdentity sw{};
    cog::NvSwitchTargetCaps caps{};
    p4::P4ProgramSpec raw{};
    auto deployed = p4::deploy_p4_program(sw, caps, raw);
    (void)deployed;
    return 0;
}

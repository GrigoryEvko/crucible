// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// deploy_p4_program takes a declared program, and mint_p4_program is the
// one function that makes one.  A spec built by hand does not convert.

#include <crucible/cntp/_wip/P4.h>

namespace cog = crucible::cog;
namespace p4 = crucible::cntp::_wip::p4;

int main() {
    const cog::CogIdentity sw{};
    const cog::NvSwitchTargetCaps caps{};
    const p4::P4ProgramSpec raw{
        .program_id = *p4::admit_p4_program_id(1),
        .source_bytes = *p4::admit_p4_source_bytes(1),
        .budget = *p4::admit_p4_resource_budget(1, 1, 1),
    };
    auto deployed = p4::deploy_p4_program(sw, caps, raw);
    return deployed.has_value() ? 0 : 1;
}

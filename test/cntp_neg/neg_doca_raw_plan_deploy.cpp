// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// deploy_doca_offload takes a declared plan, and mint_doca_deploy_plan is
// the one function that makes one.  A plan built by hand does not convert.

#include <crucible/cntp/_wip/Doca.h>

namespace doca = crucible::cntp::_wip::doca;

int main() {
    const doca::DocaDeployPlan raw{
        .dpu = {},
        .spec =
            {
                .program_id = *doca::admit_doca_program_id(1),
                .image_bytes = *doca::admit_doca_image_bytes(1),
                .queue_depth = *doca::admit_doca_queue_depth(1),
            },
    };
    auto deployed = doca::deploy_doca_offload(raw);
    return deployed.has_value() ? 0 : 1;
}

#include <crucible/cntp/_wip/Doca.h>

int main() {
    crucible::cntp::_wip::doca::DocaDeployPlan raw{};
    auto deployed = crucible::cntp::_wip::doca::deploy_doca_offload(raw);
    (void)deployed;
    return 0;
}

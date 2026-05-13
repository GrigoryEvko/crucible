#include <crucible/cntp/Doca.h>

int main() {
    crucible::cntp::doca::DocaDeployPlan raw{};
    auto deployed = crucible::cntp::doca::deploy_doca_offload(raw);
    (void)deployed;
    return 0;
}

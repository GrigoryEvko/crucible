#include <crucible/topology/Health.h>

int main() {
    auto scorer = crucible::topology::mint_topology_health<
        crucible::effects::BgDrainCtx, 2>(
            crucible::effects::BgDrainCtx{});
    (void)scorer;
    return 0;
}

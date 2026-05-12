#include <crucible/rt/Quarantine.h>

int main() {
    auto policy = crucible::rt::mint_quarantine_policy<
        crucible::effects::BgDrainCtx, 2>(crucible::effects::BgDrainCtx{});
    (void)policy;
    return 0;
}

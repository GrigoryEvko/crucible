#include <crucible/warden/Quarantine.h>

int main() {
    auto policy =
        crucible::warden::mint_quarantine_policy<crucible::effects::BgDrainCtx, 2>(crucible::effects::BgDrainCtx{::crucible::effects::testing::bg()});
    (void)policy;
    return 0;
}

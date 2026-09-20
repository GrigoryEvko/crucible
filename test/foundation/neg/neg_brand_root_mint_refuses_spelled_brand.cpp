// The fresh brand is the root mint's last template parameter, after
// the argument pack, so a caller can name the tag and nothing else.  A
// second explicit argument lands in the pack, where it is read as a
// context, and the one root template's fit concept refuses it.  That
// is what stops a caller minting a second token for a brand it holds.

#include <foundation/permissions/Permission.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct HeldBrand {};
}  // namespace

int main() {
    [[maybe_unused]] auto forged = ::foundation::permissions::mint_permission_root<Region, HeldBrand>();
    return 0;
}

// A split is only as sound as its manifest.  No splits_into<Whole,
// Left, Right> is declared here, so the token split refuses to mint
// two subregion tokens out of the parent.

#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct Whole {};
struct Left {};
struct Right {};
}  // namespace

int main() {
    auto whole = ::foundation::permissions::mint_permission_root<Whole>();
    [[maybe_unused]] auto halves = ::foundation::permissions::mint_permission_split<Left, Right>(std::move(whole));
    return 0;
}

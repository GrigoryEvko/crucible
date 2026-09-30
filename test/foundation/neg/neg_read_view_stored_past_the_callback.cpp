// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A view that a body stores by value in an object outside the frame of
// the door outlives its source.  std::optional::emplace builds the stored
// view in place, so a move is not needed, only a copy.  The copy
// constructor of ReadView is deleted, so the store is refused.
//
// Expected diagnostic: use of the deleted copy constructor of ReadView.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <optional>
#include <utility>

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    std::optional<::foundation::permissions::ReadView<Region>> kept;
    auto back = ::foundation::permissions::with_read_view(
        ::foundation::permissions::permission_erase_brand(::foundation::permissions::mint_permission_root<Region>()),
        [&kept](::foundation::permissions::ReadView<Region> const& view) noexcept { kept.emplace(view); });
    (void)back;
    return 0;
}

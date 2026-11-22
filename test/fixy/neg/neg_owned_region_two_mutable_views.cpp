// The caller takes two Views of one region and writes the same element
// through both.  The two Views would name the same elements, so no proof that
// two runs do not overlap would hold for them.  mint_borrowed gives a View of
// the const elements, and a write takes the frame door with_mut.

#include <fixy/OwnedRegion.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct RegionWhole {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    auto region =
        ::fixy::mint_owned_region<int>(nullptr, 0uz, ::foundation::permissions::mint_permission_root<RegionWhole>());
    ::foundation::core::View<int> const first = ::fixy::mint_borrowed(region), second = ::fixy::mint_borrowed(region);
    *first.get(0).expect("a write through the first borrow") = 1;
    *second.get(0).expect("a write through the second borrow") = 2;
    ::foundation::linear::drop(std::move(region).release());
    return 0;
}

// A borrow proof carries the brand of the permission it was minted
// from, so presenting it beside a permission for another region of the
// same tag fails deduction.  This is the identity refusal for the
// borrow proof: the sibling fixture
// neg_brand_read_view_from_temporary_permission.cpp is the refusal by
// value category, and the two are different mechanisms.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

template <class Brand>
constexpr void read_under(::foundation::permissions::Permission<Region, Brand> const&,
                          ::foundation::permissions::ReadView<Region, Brand>) noexcept {}
}  // namespace

int main() {
    auto owned = ::foundation::permissions::mint_permission_root<Region>();
    auto other = ::foundation::permissions::mint_permission_root<Region>();
    auto proof_of_owned = ::foundation::permissions::mint_read_view(owned);
    read_under(other, proof_of_owned);
    return 0;
}

// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A borrow proof is about the region it was borrowed from.  Two roots of
// one tag are two regions with two brands, so a view lent from the first
// cannot be presented beside the second.  The callee names one Brand
// parameter for the permission and the view, and deduction refuses the
// pair: the refusal is by identity, not by value category.
//
// Expected diagnostic: no matching function for read_under, with two
// brands deduced for one parameter.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

template <class Brand>
constexpr void read_under(::foundation::permissions::Permission<Region, Brand> const&,
                          ::foundation::permissions::ReadView<Region, Brand> const&) noexcept {}

}  // namespace

int main() {
    auto owned = ::foundation::permissions::mint_permission_root<Region>();
    auto other = ::foundation::permissions::mint_permission_root<Region>();
    auto back = ::foundation::permissions::with_read_view(
        std::move(owned), [&other](auto const& proof_of_owned) noexcept { read_under(other, proof_of_owned); });
    (void)back;
    return 0;
}

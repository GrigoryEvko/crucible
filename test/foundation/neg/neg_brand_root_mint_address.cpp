// The root mint as a function with a target function type that names a
// brand the caller already holds.  Deduction from the target type would
// set the brand parameter to the held brand, and a call through the
// reference would give a second exclusive token of that brand.  An address
// with a function pointer target deduces in the same way.  The result type
// holds the brand in undeducible_brand_t, so the target type sets no brand,
// and the binding is refused.

#include <foundation/permissions/Permission.h>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    auto first = ::foundation::permissions::mint_permission_root<Region>();
    using HeldBrand = decltype(first)::brand_type;
    using Target = ::foundation::permissions::Permission<Region, HeldBrand>() noexcept;
    Target& mint = ::foundation::permissions::mint_permission_root<Region>;
    auto second = mint();
    ::foundation::linear::drop(static_cast<decltype(first)&&>(first));
    ::foundation::linear::drop(static_cast<decltype(second)&&>(second));
    return 0;
}

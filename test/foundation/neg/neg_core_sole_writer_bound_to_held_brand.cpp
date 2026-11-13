// mint_sole_writer as a function with a target function type that names
// the brand of a writer that the caller holds.  Deduction from the target
// type would set the brand parameter to that brand, and a call through the
// reference would give a second writer of one cell.  The result type holds
// the brand in undeducible_brand_t, so the target type sets no brand, and
// the binding is refused.

#include <foundation/core/Atomic.h>

namespace {
struct HeadRole {};
}  // namespace

int main() {
    auto first = foundation::core::mint_sole_writer<HeadRole>();
    using HeldBrand = decltype(first)::brand_type;
    using Target = foundation::core::SoleWriter<HeadRole, HeldBrand>() noexcept;
    Target& mint = foundation::core::mint_sole_writer<HeadRole>;
    auto second = mint();
    ::foundation::linear::drop(static_cast<decltype(first)&&>(first));
    ::foundation::linear::drop(static_cast<decltype(second)&&>(second));
    return 0;
}

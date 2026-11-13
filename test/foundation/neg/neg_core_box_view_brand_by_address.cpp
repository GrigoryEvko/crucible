// The address of Box::view with a target type that names a brand.  If
// deduction took the brand from the target type, a call through the address
// would give a Ref with a brand that the caller chose.  The result type of
// view holds the brand in undeducible_brand_t, so the target type sets no
// brand, and the conversion is refused.

#include <foundation/core/Ref.h>

struct HeldBrand {};

int main() {
    static_cast<void>(static_cast<foundation::core::Ref<int, HeldBrand> (foundation::core::Box<int>::*)() & noexcept>(
        &foundation::core::Box<int>::view));
    return 0;
}

// A maker gives an array of Unkeyed, an owner with no key brand.  The key of
// the mint would open no door of that array, and the array would take no key
// at its doors, so the gate of the mint refuses each maker whose owner does
// not carry the brand of the mint.

#include <foundation/core/Array.h>

namespace {

struct UnkeyedArrayOwner final {
    template <class Brand>
    [[nodiscard]] constexpr ::foundation::core::FixedArray<int, 4>
    operator()(::foundation::core::MintedBrand<Brand> /*proof*/) const noexcept {
        return ::foundation::core::FixedArray<int, 4>{};
    }
};

}  // namespace

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(UnkeyedArrayOwner{});
    static_cast<void>(key);
    return array.at(::foundation::core::index<0>());
}
